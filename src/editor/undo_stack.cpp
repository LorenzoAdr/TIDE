#include "editor/undo_stack.hpp"

namespace tgdb {

namespace {

constexpr std::size_t kMaxUndo = 100;

}  // namespace

void push_undo(EditorBuffer* buffer) {
  buffer->undo_stack.push_back({buffer->lines, buffer->cursors});
  if (buffer->undo_stack.size() > kMaxUndo) {
    buffer->undo_stack.erase(buffer->undo_stack.begin());
  }
}

bool undo(EditorBuffer* buffer) {
  if (buffer->undo_stack.empty()) {
    return false;
  }
  const EditorSnapshot snapshot = std::move(buffer->undo_stack.back());
  buffer->undo_stack.pop_back();
  buffer->lines = snapshot.lines;
  buffer->cursors = snapshot.cursors;
  buffer->ensure_cursors();
  buffer->dirty = true;
  return true;
}

void clear_undo(EditorBuffer* buffer) { buffer->undo_stack.clear(); }

}  // namespace tgdb
