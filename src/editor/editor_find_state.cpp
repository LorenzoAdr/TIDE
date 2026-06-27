#include "editor/editor_find_state.hpp"

#include "editor/clipboard.hpp"
#include "editor/text_ops.hpp"

namespace tgdb {

void EditorFindState::refresh_matches(const EditorBuffer& buffer) {
  matches.clear();
  if (query.empty()) {
    return;
  }
  matches = find_all_matches(buffer, query);
}

bool EditorFindState::jump_to_next_match(EditorBuffer* buffer, int visible_lines) {
  if (query.empty()) {
    return false;
  }
  refresh_matches(*buffer);
  if (matches.empty()) {
    return false;
  }

  CursorPos from = buffer->primary().head;
  ++from.col;

  TextMatch next;
  if (!find_next_match(*buffer, query, from, &next, nullptr)) {
    if (!find_next_match(*buffer, query, {0, 0}, &next, nullptr)) {
      return false;
    }
  }

  buffer->reset_to_single_cursor(next.line, next.col);
  buffer->primary().anchor = {next.line, next.col};
  buffer->primary().head = {next.line, next.col + next.length};
  ensure_scroll_visible(buffer, visible_lines);
  return true;
}

void open_find_bar(EditorFindState* find, EditorBuffer* buffer) {
  if (find->query.empty()) {
    find->query = extract_selection_text(*buffer, buffer->primary());
  }
  find->open = true;
  find->refresh_matches(*buffer);
}

void close_find_bar(EditorFindState* find) {
  find->open = false;
  find->matches.clear();
}

}  // namespace tgdb
