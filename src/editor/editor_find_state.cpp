#include "editor/editor_find_state.hpp"

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
  if (buffer->primary().has_selection()) {
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    buffer->primary().normalized_range(&start_line, &start_col, &end_line, &end_col);
    from = {end_line, end_col};
  }

  for (const TextMatch& match : matches) {
    if (match.line > from.line || (match.line == from.line && match.col >= from.col)) {
      buffer->reset_to_single_cursor(match.line, match.col);
      buffer->primary().anchor = {match.line, match.col};
      buffer->primary().head = {match.line, match.col + match.length};
      ensure_scroll_visible(buffer, visible_lines);
      return true;
    }
  }

  const TextMatch& first = matches.front();
  buffer->reset_to_single_cursor(first.line, first.col);
  buffer->primary().anchor = {first.line, first.col};
  buffer->primary().head = {first.line, first.col + first.length};
  ensure_scroll_visible(buffer, visible_lines);
  return true;
}

void open_find_bar(EditorFindState* find, EditorBuffer* buffer) {
  find->cursor_pos = 0;
  find->open = true;
  find->refresh_matches(*buffer);
}

void close_find_bar(EditorFindState* find) {
  find->open = false;
  find->matches.clear();
}

}  // namespace tgdb
