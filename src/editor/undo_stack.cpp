#include "editor/undo_stack.hpp"

#include "editor/editor_buffer_source.hpp"
#include "ui/cursor_blink.hpp"

namespace tgdb {

namespace {

constexpr std::size_t kMaxUndo = 100;

void trim_history(std::vector<EditorSnapshot>* stack) {
  if (stack == nullptr) {
    return;
  }
  if (stack->size() > kMaxUndo) {
    stack->erase(stack->begin());
  }
}

// Snapshotting a buffer used to mean copying the whole vector<string> (one
// allocation per line) on every push_undo/undo/redo -- O(document size),
// paid on the first keystroke of every edit group. With the rope backend,
// EditorText::clone() is O(1): TextRope's nodes are immutable and shared via
// shared_ptr, so cloning is just bumping the root's refcount, and any
// subsequent mutation only reallocates the O(log n) nodes on the path it
// touches (see text_rope.hpp). Calling .clone() explicitly here (rather than
// relying on EditorText's copy constructor doing the same thing implicitly)
// documents that this is the operation this migration phase specifically
// targets -- see the "Fase 4" text storage migration plan.
EditorSnapshot make_snapshot(const EditorBuffer& buffer) {
  return EditorSnapshot{buffer.lines.clone(), buffer.cursors};
}

}  // namespace

void commit_undo_group(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  buffer->undo_coalesce_open = false;
}

void push_undo(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  if (buffer->undo_coalesce_open) {
    return;
  }
  buffer->redo_stack.clear();
  buffer->undo_stack.push_back(make_snapshot(*buffer));
  trim_history(&buffer->undo_stack);
  buffer->undo_coalesce_open = true;
}

bool undo(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->undo_stack.empty()) {
    return false;
  }
  const std::size_t prev_line_count = buffer->lines.size();
  buffer->redo_stack.push_back(make_snapshot(*buffer));
  trim_history(&buffer->redo_stack);
  EditorSnapshot snapshot = std::move(buffer->undo_stack.back());
  buffer->undo_stack.pop_back();
  buffer->lines = std::move(snapshot.lines);
  buffer->cursors = std::move(snapshot.cursors);
  buffer->ensure_cursors();
  editor_buffer_rebuild_joined(buffer);
  buffer->semantic_layout_dirty = buffer->lines.size() != prev_line_count;
  if (buffer->semantic_layout_dirty) {
    // Undo can rewrite any part of the buffer, so conservatively treat the
    // whole document as shifted rather than guessing a safe boundary line.
    buffer->semantic_layout_dirty_from_line = 0;
  }
  buffer->dirty = true;
  buffer->undo_coalesce_open = false;
  cursor_blink::show();
  return true;
}

bool redo(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->redo_stack.empty()) {
    return false;
  }
  commit_undo_group(buffer);
  const std::size_t prev_line_count = buffer->lines.size();
  buffer->undo_stack.push_back(make_snapshot(*buffer));
  trim_history(&buffer->undo_stack);
  EditorSnapshot snapshot = std::move(buffer->redo_stack.back());
  buffer->redo_stack.pop_back();
  buffer->lines = std::move(snapshot.lines);
  buffer->cursors = std::move(snapshot.cursors);
  buffer->ensure_cursors();
  editor_buffer_rebuild_joined(buffer);
  buffer->semantic_layout_dirty = buffer->lines.size() != prev_line_count;
  if (buffer->semantic_layout_dirty) {
    // Redo can rewrite any part of the buffer, so conservatively treat the
    // whole document as shifted rather than guessing a safe boundary line.
    buffer->semantic_layout_dirty_from_line = 0;
  }
  buffer->dirty = true;
  buffer->undo_coalesce_open = false;
  cursor_blink::show();
  return true;
}

void clear_undo(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  buffer->undo_stack.clear();
  buffer->redo_stack.clear();
  buffer->undo_coalesce_open = false;
}

}  // namespace tgdb
