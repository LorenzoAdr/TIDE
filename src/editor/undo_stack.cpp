#include "editor/undo_stack.hpp"

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

}  // namespace

void push_undo(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  buffer->redo_stack.clear();
  buffer->undo_stack.push_back({buffer->lines, buffer->cursors});
  trim_history(&buffer->undo_stack);
}

bool undo(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->undo_stack.empty()) {
    return false;
  }
  buffer->redo_stack.push_back({buffer->lines, buffer->cursors});
  trim_history(&buffer->redo_stack);
  const EditorSnapshot snapshot = std::move(buffer->undo_stack.back());
  buffer->undo_stack.pop_back();
  buffer->lines = snapshot.lines;
  buffer->cursors = snapshot.cursors;
  buffer->ensure_cursors();
  buffer->dirty = true;
  cursor_blink::show();
  return true;
}

bool redo(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->redo_stack.empty()) {
    return false;
  }
  buffer->undo_stack.push_back({buffer->lines, buffer->cursors});
  trim_history(&buffer->undo_stack);
  const EditorSnapshot snapshot = std::move(buffer->redo_stack.back());
  buffer->redo_stack.pop_back();
  buffer->lines = snapshot.lines;
  buffer->cursors = snapshot.cursors;
  buffer->ensure_cursors();
  buffer->dirty = true;
  cursor_blink::show();
  return true;
}

void clear_undo(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  buffer->undo_stack.clear();
  buffer->redo_stack.clear();
}

}  // namespace tgdb
