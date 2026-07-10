#include "editor/text_ops.hpp"

#include <algorithm>
#include <cctype>

#include "editor/bracket_match.hpp"
#include "editor/editor_buffer_source.hpp"
#include "editor/editor_folds.hpp"
#include "editor/indent_guides.hpp"
#include "editor/line_comment.hpp"
#include "editor/text_search.hpp"
#include "editor/undo_stack.hpp"
#include "symbols/completion_snippet.hpp"
#include "ui/cursor_blink.hpp"

#include "util/clang_format_config.hpp"

namespace tgdb {

namespace {

int max_scroll(int total, int visible) { return std::max(0, total - visible); }

void mark_dirty(EditorBuffer* buffer) {
  buffer->dirty = true;
  cursor_blink::show();
}

bool is_identifier_char(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) || c == '_';
}

void identifier_bounds_at(const EditorBuffer& buffer, int line, int col, int* start_col,
                          int* end_col) {
  if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    *start_col = 0;
    *end_col = 0;
    return;
  }
  const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
  const int len = static_cast<int>(text.size());
  col = std::max(0, std::min(col, len));

  int pos = col;
  if (pos < len && !is_identifier_char(text[static_cast<std::size_t>(pos)])) {
    int left = pos;
    while (left > 0 && !is_identifier_char(text[static_cast<std::size_t>(left - 1)])) {
      --left;
    }
    if (left > 0 && is_identifier_char(text[static_cast<std::size_t>(left - 1)])) {
      pos = left;
    } else {
      int right = pos;
      while (right < len && !is_identifier_char(text[static_cast<std::size_t>(right)])) {
        ++right;
      }
      pos = right;
    }
  }

  int start = pos;
  while (start > 0 && is_identifier_char(text[static_cast<std::size_t>(start - 1)])) {
    --start;
  }
  int end = pos;
  while (end < len && is_identifier_char(text[static_cast<std::size_t>(end)])) {
    ++end;
  }
  *start_col = start;
  *end_col = end;
}

int word_left_col(const EditorBuffer& buffer, int line, int col) {
  if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    return 0;
  }
  const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
  col = std::max(0, std::min(col, static_cast<int>(text.size())));

  int pos = col;
  if (pos > 0 && is_identifier_char(text[static_cast<std::size_t>(pos - 1)])) {
    while (pos > 0 && is_identifier_char(text[static_cast<std::size_t>(pos - 1)])) {
      --pos;
    }
    return pos;
  }

  pos = col;
  while (pos > 0 && !is_identifier_char(text[static_cast<std::size_t>(pos - 1)])) {
    --pos;
  }
  while (pos > 0 && is_identifier_char(text[static_cast<std::size_t>(pos - 1)])) {
    --pos;
  }
  return pos;
}

int word_right_col(const EditorBuffer& buffer, int line, int col) {
  if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    return 0;
  }
  const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
  const int len = static_cast<int>(text.size());
  col = std::max(0, std::min(col, len));

  int pos = col;
  if (pos < len && is_identifier_char(text[static_cast<std::size_t>(pos)])) {
    while (pos < len && is_identifier_char(text[static_cast<std::size_t>(pos)])) {
      ++pos;
    }
    return pos;
  }

  while (pos < len && !is_identifier_char(text[static_cast<std::size_t>(pos)])) {
    ++pos;
  }
  return pos;
}

void adjust_cursors_after_col_delete(EditorBuffer* buffer, int line, int start_col, int end_col) {
  const int delta = end_col - start_col;
  for (auto& cursor : buffer->cursors) {
    auto fix = [&](CursorPos* pos) {
      if (pos->line != line) {
        return;
      }
      if (pos->col >= end_col) {
        pos->col -= delta;
      } else if (pos->col > start_col) {
        pos->col = start_col;
      }
    };
    fix(&cursor.head);
    fix(&cursor.anchor);
  }
}

void adjust_cursors_after_multiline_delete(EditorBuffer* buffer, int start_line, int start_col,
                                           int end_line, int end_col) {
  const int removed_lines = end_line - start_line;
  for (auto& cursor : buffer->cursors) {
    auto fix = [&](CursorPos* pos) {
      if (pos->line < start_line || (pos->line == start_line && pos->col <= start_col)) {
        return;
      }
      if (pos->line > end_line || (pos->line == end_line && pos->col >= end_col)) {
        if (pos->line == end_line) {
          pos->line = start_line;
          pos->col = start_col + (pos->col - end_col);
        } else {
          pos->line -= removed_lines;
        }
        return;
      }
      pos->line = start_line;
      pos->col = start_col;
    };
    fix(&cursor.head);
    fix(&cursor.anchor);
  }
}

void delete_range(EditorBuffer* buffer, int start_line, int start_col, int end_line, int end_col) {
  if (buffer->lines.empty()) {
    return;
  }
  start_line = std::max(0, std::min(start_line, static_cast<int>(buffer->lines.size()) - 1));
  end_line = std::max(0, std::min(end_line, static_cast<int>(buffer->lines.size()) - 1));
  if (start_line > end_line || (start_line == end_line && start_col >= end_col)) {
    return;
  }

  if (start_line == end_line) {
    auto& line = buffer->lines[static_cast<std::size_t>(start_line)];
    start_col = std::max(0, std::min(start_col, static_cast<int>(line.size())));
    end_col = std::max(start_col, std::min(end_col, static_cast<int>(line.size())));
    const int removed = end_col - start_col;
    line.erase(static_cast<std::size_t>(start_col),
               static_cast<std::size_t>(removed));
    if (removed > 0) {
      editor_buffer_note_char_removed(buffer, start_line, start_col,
                                      static_cast<std::size_t>(removed));
    }
    adjust_cursors_after_col_delete(buffer, start_line, start_col, end_col);
    return;
  }

  auto& first = buffer->lines[static_cast<std::size_t>(start_line)];
  const auto& last = buffer->lines[static_cast<std::size_t>(end_line)];
  start_col = std::max(0, std::min(start_col, static_cast<int>(first.size())));
  end_col = std::max(0, std::min(end_col, static_cast<int>(last.size())));

  first.erase(static_cast<std::size_t>(start_col));
  first += last.substr(static_cast<std::size_t>(end_col));
  buffer->lines.erase(buffer->lines.begin() + start_line + 1,
                      buffer->lines.begin() + end_line + 1);
  adjust_cursors_after_multiline_delete(buffer, start_line, start_col, end_line, end_col);
  editor_buffer_rebuild_joined(buffer);
  buffer->semantic_layout_dirty = true;
}

bool any_cursor_has_selection(const EditorBuffer& buffer) {
  for (const auto& cursor : buffer.cursors) {
    if (cursor.has_selection()) {
      return true;
    }
  }
  return false;
}

char closing_for_open_char(char open) {
  switch (open) {
    case '(':
      return ')';
    case '[':
      return ']';
    case '{':
      return '}';
    case '"':
      return '"';
    case '\'':
      return '\'';
    default:
      return '\0';
  }
}

bool is_auto_pair_closer(char c) {
  return c == ')' || c == ']' || c == '}' || c == '"' || c == '\'';
}

void advance_cursors_at(EditorBuffer* buffer, int line, int col) {
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line && cursor.head.col == col) {
      ++cursor.head.col;
    }
    if (cursor.anchor.line == line && cursor.anchor.col == col) {
      ++cursor.anchor.col;
    }
  }
}

void place_cursors_between_pair(EditorBuffer* buffer, int line, int col, int pair_len) {
  const int after_pair = col + pair_len;
  const int between = col + pair_len / 2;
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line && cursor.head.col == after_pair) {
      cursor.head.col = between;
    }
    if (cursor.anchor.line == line && cursor.anchor.col == after_pair) {
      cursor.anchor.col = between;
    }
  }
}

void insert_char_at(EditorBuffer* buffer, int line, int col, char c);
void insert_string_at(EditorBuffer* buffer, int line, int col, const std::string& text);
void newline_at(EditorBuffer* buffer, int line, int col, bool smart_indent);

bool wrap_selection_with_pair(EditorBuffer* buffer, MultiCursor* cursor, char open) {
  if (cursor == nullptr || !cursor->has_selection()) {
    return false;
  }

  const char close = closing_for_open_char(open);
  if (close == '\0') {
    return false;
  }

  int start_line = 0;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
  cursor->normalized_range(&start_line, &start_col, &end_line, &end_col);
  if (!cursor_in_code(*buffer, start_line, start_col)) {
    return false;
  }

  insert_char_at(buffer, start_line, start_col, open);

  int close_line = end_line;
  int close_col = end_col;
  if (start_line == end_line) {
    close_col += 1;
  }

  insert_char_at(buffer, close_line, close_col, close);
  cursor->set_pos(close_line, close_col + 1);
  return true;
}

void insert_char_at_with_pairs(EditorBuffer* buffer, int line, int col, char c) {
  if (!cursor_in_code(*buffer, line, col)) {
    insert_char_at(buffer, line, col, c);
    return;
  }

  const std::string& line_text = buffer->lines[static_cast<std::size_t>(line)];
  const char next =
      col < static_cast<int>(line_text.size()) ? line_text[static_cast<std::size_t>(col)] : '\0';

  if (is_auto_pair_closer(c) && next == c) {
    advance_cursors_at(buffer, line, col);
    return;
  }

  const char close = closing_for_open_char(c);
  if (close != '\0') {
    if (next == close) {
      insert_char_at(buffer, line, col, c);
      return;
    }
    const std::string pair = std::string(1, c) + close;
    insert_string_at(buffer, line, col, pair);
    place_cursors_between_pair(buffer, line, col, static_cast<int>(pair.size()));
    return;
  }

  insert_char_at(buffer, line, col, c);
}

void insert_char_at(EditorBuffer* buffer, int line, int col, char c) {
  auto& text = buffer->lines[static_cast<std::size_t>(line)];
  text.insert(static_cast<std::size_t>(col), 1, c);
  editor_buffer_note_char_inserted(buffer, line, col, std::string_view(&c, 1));
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line && cursor.head.col >= col) {
      ++cursor.head.col;
    }
    if (cursor.anchor.line == line && cursor.anchor.col >= col) {
      ++cursor.anchor.col;
    }
  }
}

void insert_string_at(EditorBuffer* buffer, int line, int col, const std::string& text) {
  if (text.empty()) {
    return;
  }
  auto& line_text = buffer->lines[static_cast<std::size_t>(line)];
  line_text.insert(static_cast<std::size_t>(col), text);
  editor_buffer_note_char_inserted(buffer, line, col, text);
  const int delta = static_cast<int>(text.size());
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line && cursor.head.col >= col) {
      cursor.head.col += delta;
    }
    if (cursor.anchor.line == line && cursor.anchor.col >= col) {
      cursor.anchor.col += delta;
    }
  }
}

void insert_multiline_text_at(EditorBuffer* buffer, int line, int col, const std::string& text) {
  if (text.empty()) {
    return;
  }
  if (text.find('\n') == std::string::npos) {
    insert_string_at(buffer, line, col, text);
    return;
  }
  editor_buffer_invalidate_joined(buffer);
  buffer->semantic_layout_dirty = true;

  int cur_line = line;
  int cur_col = col;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t next = text.find('\n', pos);
    const std::string part =
        next == std::string::npos ? text.substr(pos) : text.substr(pos, next - pos);
    if (!part.empty()) {
      insert_string_at(buffer, cur_line, cur_col, part);
      cur_col += static_cast<int>(part.size());
    }
    if (next == std::string::npos) {
      break;
    }
    newline_at(buffer, cur_line, cur_col, false);
    cur_line++;
    cur_col = 0;
    pos = next + 1;
  }
}

void backspace_at(EditorBuffer* buffer, int line, int col) {
  if (col > 0) {
    buffer->lines[static_cast<std::size_t>(line)].erase(static_cast<std::size_t>(col - 1), 1);
    editor_buffer_note_char_removed(buffer, line, col - 1, 1);
    for (auto& cursor : buffer->cursors) {
      if (cursor.head.line == line && cursor.head.col >= col) {
        --cursor.head.col;
      }
      if (cursor.anchor.line == line && cursor.anchor.col >= col) {
        --cursor.anchor.col;
      }
    }
    return;
  }
  if (line <= 0) {
    return;
  }
  const int join_col =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(line - 1)].size());
  const std::string tail = buffer->lines[static_cast<std::size_t>(line)];
  buffer->lines.erase(buffer->lines.begin() + line);
  buffer->lines[static_cast<std::size_t>(line - 1)] += tail;
  editor_buffer_note_line_joined(buffer, line);
  if (!buffer->joined_source_cache.valid) {
    editor_buffer_rebuild_joined(buffer);
  }
  buffer->semantic_layout_dirty = true;
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line) {
      cursor.head.line = line - 1;
      cursor.head.col += join_col;
    } else if (cursor.head.line > line) {
      --cursor.head.line;
    }
    if (cursor.anchor.line == line) {
      cursor.anchor.line = line - 1;
      cursor.anchor.col += join_col;
    } else if (cursor.anchor.line > line) {
      --cursor.anchor.line;
    }
  }
}

void delete_at(EditorBuffer* buffer, int line, int col) {
  auto& text = buffer->lines[static_cast<std::size_t>(line)];
  if (col < static_cast<int>(text.size())) {
    text.erase(static_cast<std::size_t>(col), 1);
    editor_buffer_note_char_removed(buffer, line, col, 1);
    for (auto& cursor : buffer->cursors) {
      if (cursor.head.line == line && cursor.head.col > col) {
        --cursor.head.col;
      }
      if (cursor.anchor.line == line && cursor.anchor.col > col) {
        --cursor.anchor.col;
      }
    }
    return;
  }
  if (line + 1 >= static_cast<int>(buffer->lines.size())) {
    return;
  }
  const int old_len = static_cast<int>(text.size());
  text += buffer->lines[static_cast<std::size_t>(line + 1)];
  buffer->lines.erase(buffer->lines.begin() + line + 1);
  editor_buffer_rebuild_joined(buffer);
  buffer->semantic_layout_dirty = true;
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line > line + 1) {
      --cursor.head.line;
    } else if (cursor.head.line == line + 1) {
      cursor.head.line = line;
      cursor.head.col = old_len + cursor.head.col;
    }
    if (cursor.anchor.line > line + 1) {
      --cursor.anchor.line;
    } else if (cursor.anchor.line == line + 1) {
      cursor.anchor.line = line;
      cursor.anchor.col = old_len + cursor.anchor.col;
    }
  }
}

void delete_word_backward_at(EditorBuffer* buffer, int line, int col) {
  if (buffer->lines.empty()) {
    return;
  }
  line = std::max(0, std::min(line, static_cast<int>(buffer->lines.size()) - 1));
  const int line_len = static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
  col = std::max(0, std::min(col, line_len));

  int start_line = line;
  int start_col = word_left_col(*buffer, line, col);
  if (start_col == col && col == 0 && line > 0) {
    start_line = line - 1;
    start_col = word_left_col(
        *buffer, start_line,
        static_cast<int>(buffer->lines[static_cast<std::size_t>(start_line)].size()));
  }
  if (start_line == line && start_col == col) {
    return;
  }
  delete_range(buffer, start_line, start_col, line, col);
}

void delete_word_forward_at(EditorBuffer* buffer, int line, int col) {
  if (buffer->lines.empty()) {
    return;
  }
  line = std::max(0, std::min(line, static_cast<int>(buffer->lines.size()) - 1));
  const int line_len = static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
  col = std::max(0, std::min(col, line_len));

  int end_line = line;
  int end_col = word_right_col(*buffer, line, col);
  if (end_col == col && col >= line_len && line + 1 < static_cast<int>(buffer->lines.size())) {
    end_line = line + 1;
    end_col = word_right_col(*buffer, end_line, 0);
  }
  if (end_line == line && end_col == col) {
    return;
  }
  delete_range(buffer, line, col, end_line, end_col);
}

int smart_indent_size() { return std::max(1, editor_indent::width()); }

std::string leading_whitespace(const std::string& line) {
  std::size_t end = 0;
  while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) {
    ++end;
  }
  return line.substr(0, end);
}

bool open_brace_before_col(const std::string& line, int col) {
  const int limit = std::min(col, static_cast<int>(line.size()));
  int i = limit - 1;
  while (i >= 0 && std::isspace(static_cast<unsigned char>(line[static_cast<std::size_t>(i)]))) {
    --i;
  }
  return i >= 0 && line[static_cast<std::size_t>(i)] == '{';
}

std::string smart_newline_indent(const std::string& line, int col) {
  std::string indent = leading_whitespace(line);
  if (open_brace_before_col(line, col)) {
    indent.append(static_cast<std::size_t>(smart_indent_size()), ' ');
  }
  return indent;
}

void newline_at(EditorBuffer* buffer, int line, int col, bool smart_indent) {
  if (buffer != nullptr) {
    buffer->semantic_layout_dirty = true;
  }
  auto& text = buffer->lines[static_cast<std::size_t>(line)];
  const std::string tail = text.substr(static_cast<std::size_t>(col));
  text.erase(static_cast<std::size_t>(col));
  const std::string indent = smart_indent ? smart_newline_indent(text, col) : std::string{};
  const int indent_len = static_cast<int>(indent.size());
  buffer->lines.insert(buffer->lines.begin() + line + 1, indent + tail);
  editor_buffer_rebuild_joined(buffer);

  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line) {
      if (cursor.head.col > col) {
        cursor.head.line = line + 1;
        cursor.head.col = indent_len + (cursor.head.col - col);
      } else if (cursor.head.col == col) {
        cursor.head.line = line + 1;
        cursor.head.col = indent_len;
      }
    } else if (cursor.head.line > line) {
      ++cursor.head.line;
    }

    if (cursor.anchor.line == line) {
      if (cursor.anchor.col > col) {
        cursor.anchor.line = line + 1;
        cursor.anchor.col = indent_len + (cursor.anchor.col - col);
      } else if (cursor.anchor.col == col) {
        cursor.anchor.line = line + 1;
        cursor.anchor.col = indent_len;
      }
    } else if (cursor.anchor.line > line) {
      ++cursor.anchor.line;
    }
  }
}

void newline_at(EditorBuffer* buffer, int line, int col) {
  newline_at(buffer, line, col, true);
}

void apply_to_all_cursors(EditorBuffer* buffer,
                            void (*op)(EditorBuffer*, int, int)) {
  clamp_all_cursors(buffer);
  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }

  std::vector<CursorPos> positions;
  positions.reserve(buffer->cursors.size());
  for (const auto& cursor : buffer->cursors) {
    positions.push_back(cursor.head);
  }
  std::sort(positions.begin(), positions.end(), [](const CursorPos& a, const CursorPos& b) {
    if (a.line != b.line) {
      return a.line > b.line;
    }
    return a.col > b.col;
  });

  for (const auto& pos : positions) {
    op(buffer, pos.line, pos.col);
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
}

void clamp_head_col(EditorBuffer* buffer) {
  if (buffer->lines.empty()) {
    return;
  }
  for (auto& cursor : buffer->cursors) {
    auto& head = cursor.head;
    head.line = std::max(0, std::min(head.line, static_cast<int>(buffer->lines.size()) - 1));
    const int len = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
    head.col = std::max(0, std::min(head.col, len));
  }
}

void finish_move(EditorBuffer* buffer, bool extend_selection) {
  clamp_head_col(buffer);
  if (!extend_selection) {
    for (auto& cursor : buffer->cursors) {
      cursor.anchor = cursor.head;
    }
  }
  clamp_all_cursors(buffer);
  cursor_blink::show();
}

void paste_string_at(EditorBuffer* buffer, int line, int col, const std::string& text) {
  int cur_line = line;
  int cur_col = col;
  for (char c : text) {
    if (c == '\n') {
      newline_at(buffer, cur_line, cur_col, false);
      ++cur_line;
      cur_col = 0;
    } else {
      insert_char_at(buffer, cur_line, cur_col, c);
      ++cur_col;
    }
  }
}

}  // namespace

void delete_all_selections(EditorBuffer* buffer) {
  clamp_all_cursors(buffer);
  std::vector<std::size_t> indices;
  indices.reserve(buffer->cursors.size());
  for (std::size_t i = 0; i < buffer->cursors.size(); ++i) {
    if (buffer->cursors[i].has_selection()) {
      indices.push_back(i);
    }
  }
  if (indices.empty()) {
    return;
  }

  std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
    const auto& ca = buffer->cursors[a];
    const auto& cb = buffer->cursors[b];
    if (ca.head.line != cb.head.line) {
      return ca.head.line > cb.head.line;
    }
    return ca.head.col > cb.head.col;
  });

  for (std::size_t idx : indices) {
    auto& cursor = buffer->cursors[idx];
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
    delete_range(buffer, start_line, start_col, end_line, end_col);
    cursor.set_pos(start_line, start_col);
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
}

void ensure_scroll_visible(EditorBuffer* buffer, int visible_lines, int code_width) {
  if (!buffer->collapsed_folds.empty()) {
    ensure_scroll_visible_fold_aware(buffer, buffer->fold_regions, visible_lines, code_width);
    return;
  }
  const int primary = buffer->primary_line();
  if (primary < buffer->scroll) {
    buffer->scroll = primary;
  } else if (primary >= buffer->scroll + visible_lines) {
    buffer->scroll = std::max(0, primary - visible_lines + 1);
  }
  buffer->scroll = std::max(
      0, std::min(buffer->scroll,
                  max_scroll(static_cast<int>(buffer->lines.size()), visible_lines)));

  if (code_width <= 0) {
    return;
  }
  const int col = buffer->primary_col();
  constexpr int kMargin = 8;
  if (col < buffer->scroll_col + kMargin) {
    buffer->scroll_col = std::max(0, col - kMargin);
  } else if (col >= buffer->scroll_col + code_width - kMargin) {
    buffer->scroll_col = std::max(0, col - code_width + kMargin + 1);
  }
  const int line_len =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(primary)].size());
  const int max_scroll_col = std::max(0, line_len - code_width + 1);
  buffer->scroll_col = std::min(buffer->scroll_col, max_scroll_col);
}

void ensure_scroll_centered(EditorBuffer* buffer, int visible_lines, int code_width) {
  if (!buffer->collapsed_folds.empty()) {
    const int primary = buffer->primary_line();
    const int half = std::max(0, visible_lines / 2);
    const std::vector<int> visible = visible_buffer_lines(
        static_cast<int>(buffer->lines.size()), buffer->fold_regions, buffer->collapsed_folds);
    const int primary_index = visible_line_index(visible, primary);
    if (primary_index >= 0) {
      const int scroll_index = std::max(0, primary_index - half);
      buffer->scroll = visible[static_cast<std::size_t>(scroll_index)];
    }
    ensure_scroll_visible_fold_aware(buffer, buffer->fold_regions, visible_lines, code_width);
    return;
  }
  const int primary = buffer->primary_line();
  const int half = std::max(0, visible_lines / 2);
  buffer->scroll = std::max(0, primary - half);
  buffer->scroll = std::max(
      0, std::min(buffer->scroll,
                  max_scroll(static_cast<int>(buffer->lines.size()), visible_lines)));

  if (code_width <= 0) {
    return;
  }
  const int col = buffer->primary_col();
  constexpr int kMargin = 8;
  if (col < buffer->scroll_col + kMargin) {
    buffer->scroll_col = std::max(0, col - kMargin);
  } else if (col >= buffer->scroll_col + code_width - kMargin) {
    buffer->scroll_col = std::max(0, col - code_width + kMargin + 1);
  }
  const int line_len =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(primary)].size());
  const int max_scroll_col = std::max(0, line_len - code_width + 1);
  buffer->scroll_col = std::min(buffer->scroll_col, max_scroll_col);
}

void scroll_view_by_columns(EditorBuffer* buffer, int delta_columns, int code_width) {
  if (code_width <= 0) {
    buffer->scroll_col = std::max(0, buffer->scroll_col + delta_columns);
    return;
  }
  int max_len = 0;
  for (const auto& line : buffer->lines) {
    max_len = std::max(max_len, static_cast<int>(line.size()));
  }
  const int max_scroll_col = std::max(0, max_len - code_width + 1);
  buffer->scroll_col =
      std::max(0, std::min(buffer->scroll_col + delta_columns, max_scroll_col));
}

void scroll_view_by_lines(EditorBuffer* buffer, int delta_lines, int visible_lines) {
  if (!buffer->collapsed_folds.empty()) {
    scroll_view_by_lines_fold_aware(buffer, delta_lines, buffer->fold_regions, visible_lines);
    return;
  }
  const int total = static_cast<int>(buffer->lines.size());
  buffer->scroll = std::max(
      0, std::min(buffer->scroll + delta_lines, max_scroll(total, visible_lines)));
}

void move_primary_half_page_up(EditorBuffer* buffer, int visible_lines, bool extend_selection) {
  commit_undo_group(buffer);
  const int delta = std::max(1, visible_lines / 2);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    cursor.head.line = std::max(0, cursor.head.line - delta);
  }
  finish_move(buffer, extend_selection);
}

void move_primary_half_page_down(EditorBuffer* buffer, int visible_lines, bool extend_selection) {
  commit_undo_group(buffer);
  const int delta = std::max(1, visible_lines / 2);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    cursor.head.line =
        std::min(cursor.head.line + delta, static_cast<int>(buffer->lines.size()) - 1);
  }
  finish_move(buffer, extend_selection);
}

void replace_word_at_cursor(EditorBuffer* buffer, const std::string& replacement) {
  if (replacement.empty()) {
    return;
  }
  const MultiCursor& primary = buffer->primary();
  int start_col = 0;
  int end_col = 0;
  const int line = primary.head.line;
  ident_range_at_cursor(*buffer, primary, &start_col, &end_col);
  replace_text_range(buffer, line, start_col, end_col, replacement);
}

void replace_text_range(EditorBuffer* buffer, int line, int start_col, int end_col,
                        const std::string& replacement) {
  if (replacement.empty()) {
    return;
  }
  commit_undo_group(buffer);
  push_undo(buffer);
  exit_multi_cursor_mode(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  }

  delete_range(buffer, line, start_col, line, end_col);

  auto& text = buffer->lines[static_cast<std::size_t>(line)];
  start_col = std::max(0, std::min(start_col, static_cast<int>(text.size())));
  text.insert(static_cast<std::size_t>(start_col), replacement);

  const int delta = static_cast<int>(replacement.size());
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line && cursor.head.col >= start_col) {
      cursor.head.col += delta;
    }
    if (cursor.anchor.line == line && cursor.anchor.col >= start_col) {
      cursor.anchor.col += delta;
    }
  }
  buffer->reset_to_single_cursor(line, start_col + delta);
  clamp_all_cursors(buffer);
  mark_dirty(buffer);
}

void replace_text_range_with_caret(EditorBuffer* buffer, int line, int start_col, int end_col,
                                   const std::string& replacement, int caret_line_offset,
                                   int caret_col, int sel_start_col, int sel_end_col) {
  if (replacement.empty()) {
    return;
  }
  commit_undo_group(buffer);
  push_undo(buffer);
  exit_multi_cursor_mode(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  }

  delete_range(buffer, line, start_col, line, end_col);

  auto& text = buffer->lines[static_cast<std::size_t>(line)];
  start_col = std::max(0, std::min(start_col, static_cast<int>(text.size())));
  if (replacement.find('\n') != std::string::npos) {
    insert_multiline_text_at(buffer, line, start_col, replacement);
  } else {
    text.insert(static_cast<std::size_t>(start_col), replacement);
  }

  const int caret_line = line + caret_line_offset;
  const int caret_col_base = caret_line_offset == 0 ? start_col : 0;
  const int abs_caret_col = caret_col_base + caret_col;

  buffer->reset_to_single_cursor(caret_line, abs_caret_col);
  if (sel_start_col >= 0 && sel_end_col > sel_start_col) {
    buffer->primary().anchor = {caret_line, caret_col_base + sel_start_col};
    buffer->primary().head = {caret_line, caret_col_base + sel_end_col};
  }
  clamp_all_cursors(buffer);
  mark_dirty(buffer);
}

void apply_completion_at_all_cursors(EditorBuffer* buffer, const SnippetResult& snippet) {
  if (snippet.text.empty()) {
    return;
  }
  commit_undo_group(buffer);
  push_undo(buffer);
  clamp_all_cursors(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  }
  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }

  struct Site {
    int line = 0;
    int start_col = 0;
    int end_col = 0;
    std::size_t cursor_index = 0;
  };
  std::vector<Site> sites;
  sites.reserve(buffer->cursors.size());
  for (std::size_t i = 0; i < buffer->cursors.size(); ++i) {
    Site site;
    site.line = buffer->cursors[i].head.line;
    completion_replace_range_at_cursor(*buffer, buffer->cursors[i], &site.start_col,
                                       &site.end_col);
    site.cursor_index = i;
    sites.push_back(site);
  }
  std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) {
    if (a.line != b.line) {
      return a.line > b.line;
    }
    return a.start_col > b.start_col;
  });

  for (const auto& site : sites) {
    delete_range(buffer, site.line, site.start_col, site.line, site.end_col);
    int col = site.start_col;
    if (site.line >= 0 && site.line < static_cast<int>(buffer->lines.size())) {
      col = std::max(0, std::min(col, static_cast<int>(buffer->lines[site.line].size())));
    }
    insert_multiline_text_at(buffer, site.line, col, snippet.text);
    const int caret_line = site.line + snippet.caret_line_offset;
    const int caret_base = snippet.caret_line_offset == 0 ? col : 0;
    buffer->cursors[site.cursor_index].head = {caret_line, caret_base + snippet.caret_col};
    buffer->cursors[site.cursor_index].anchor = buffer->cursors[site.cursor_index].head;
  }

  if (snippet.sel_start_col >= 0 && snippet.sel_end_col > snippet.sel_start_col) {
    MultiCursor& primary = buffer->primary();
    const int caret_base =
        snippet.caret_line_offset == 0 ? primary.head.col - snippet.caret_col : 0;
    primary.anchor = {primary.head.line, caret_base + snippet.sel_start_col};
    primary.head = {primary.head.line, caret_base + snippet.sel_end_col};
  }

  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
  mark_dirty(buffer);
}

void insert_char(EditorBuffer* buffer, char c) {
  push_undo(buffer);
  clamp_all_cursors(buffer);

  if (closing_for_open_char(c) != '\0' && any_cursor_has_selection(*buffer)) {
    std::vector<std::size_t> indices;
    indices.reserve(buffer->cursors.size());
    for (std::size_t i = 0; i < buffer->cursors.size(); ++i) {
      if (buffer->cursors[i].has_selection()) {
        indices.push_back(i);
      }
    }

    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
      const auto& ca = buffer->cursors[a];
      const auto& cb = buffer->cursors[b];
      int end_line_a = 0;
      int end_col_a = 0;
      int end_line_b = 0;
      int end_col_b = 0;
      int start_line_a = 0;
      int start_col_a = 0;
      int start_line_b = 0;
      int start_col_b = 0;
      ca.normalized_range(&start_line_a, &start_col_a, &end_line_a, &end_col_a);
      cb.normalized_range(&start_line_b, &start_col_b, &end_line_b, &end_col_b);
      if (end_line_a != end_line_b) {
        return end_line_a > end_line_b;
      }
      return end_col_a > end_col_b;
    });

    for (std::size_t idx : indices) {
      auto& cursor = buffer->cursors[idx];
      if (wrap_selection_with_pair(buffer, &cursor, c)) {
        continue;
      }
      int start_line = 0;
      int start_col = 0;
      int end_line = 0;
      int end_col = 0;
      cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
      delete_range(buffer, start_line, start_col, end_line, end_col);
      cursor.set_pos(start_line, start_col);
      insert_char_at_with_pairs(buffer, start_line, start_col, c);
    }

    for (auto& cursor : buffer->cursors) {
      cursor.collapse_to_head();
    }
    clamp_all_cursors(buffer);
    merge_overlapping_cursors(buffer);
    mark_dirty(buffer);
    return;
  }

  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  } else {
    for (auto& cursor : buffer->cursors) {
      cursor.collapse_to_head();
    }
  }

  std::vector<CursorPos> positions;
  positions.reserve(buffer->cursors.size());
  for (const auto& cursor : buffer->cursors) {
    positions.push_back(cursor.head);
  }
  std::sort(positions.begin(), positions.end(), [](const CursorPos& a, const CursorPos& b) {
    if (a.line != b.line) {
      return a.line > b.line;
    }
    return a.col > b.col;
  });

  for (const auto& pos : positions) {
    insert_char_at_with_pairs(buffer, pos.line, pos.col, c);
  }
  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
  mark_dirty(buffer);
}

void insert_tab_stop(EditorBuffer* buffer, int tab_size) {
  const int display_tab = std::max(1, tab_size > 0 ? tab_size : editor_indent::tab_display_width());
  push_undo(buffer);
  clamp_all_cursors(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  } else {
    for (auto& cursor : buffer->cursors) {
      cursor.collapse_to_head();
    }
  }

  std::vector<CursorPos> positions;
  positions.reserve(buffer->cursors.size());
  for (const auto& cursor : buffer->cursors) {
    positions.push_back(cursor.head);
  }
  std::sort(positions.begin(), positions.end(), [](const CursorPos& a, const CursorPos& b) {
    if (a.line != b.line) {
      return a.line > b.line;
    }
    return a.col > b.col;
  });

  for (const auto& pos : positions) {
    if (pos.line < 0 || pos.line >= static_cast<int>(buffer->lines.size())) {
      continue;
    }
    const std::string& line_text = buffer->lines[static_cast<std::size_t>(pos.line)];
    if (editor_indent::use_tab_char()) {
      insert_string_at(buffer, pos.line, pos.col, "\t");
      continue;
    }
    const int visual_col = byte_index_to_visual_column(line_text, pos.col, display_tab);
    int count = display_tab;
    if (visual_col > 0) {
      count = display_tab - (visual_col % display_tab);
      if (count <= 0) {
        count = display_tab;
      }
    }
    insert_string_at(buffer, pos.line, pos.col,
                     std::string(static_cast<std::size_t>(count), ' '));
  }
  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
  mark_dirty(buffer);
}

void backspace(EditorBuffer* buffer) {
  push_undo(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  } else {
    apply_to_all_cursors(buffer, backspace_at);
  }
  mark_dirty(buffer);
}

void delete_char(EditorBuffer* buffer) {
  push_undo(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  } else {
    apply_to_all_cursors(buffer, delete_at);
  }
  mark_dirty(buffer);
}

void delete_word_backward(EditorBuffer* buffer) {
  commit_undo_group(buffer);
  push_undo(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  } else {
    apply_to_all_cursors(buffer, delete_word_backward_at);
  }
  mark_dirty(buffer);
}

void delete_word_forward(EditorBuffer* buffer) {
  commit_undo_group(buffer);
  push_undo(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  } else {
    apply_to_all_cursors(buffer, delete_word_forward_at);
  }
  mark_dirty(buffer);
}

void newline(EditorBuffer* buffer) {
  commit_undo_group(buffer);
  push_undo(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  }
  apply_to_all_cursors(buffer, newline_at);
  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }
  mark_dirty(buffer);
}

void paste_at_primary(EditorBuffer* buffer, const std::string& text) {
  if (text.empty()) {
    return;
  }
  commit_undo_group(buffer);
  push_undo(buffer);
  clamp_all_cursors(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  } else {
    for (auto& cursor : buffer->cursors) {
      cursor.collapse_to_head();
    }
  }

  std::vector<CursorPos> positions;
  positions.reserve(buffer->cursors.size());
  for (const auto& cursor : buffer->cursors) {
    positions.push_back(cursor.head);
  }
  std::sort(positions.begin(), positions.end(), [](const CursorPos& a, const CursorPos& b) {
    if (a.line != b.line) {
      return a.line > b.line;
    }
    return a.col > b.col;
  });

  for (const auto& pos : positions) {
    paste_string_at(buffer, pos.line, pos.col, text);
  }
  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
  mark_dirty(buffer);
}

bool undo_edit(EditorBuffer* buffer) { return undo(buffer); }

bool redo_edit(EditorBuffer* buffer) { return redo(buffer); }

void clear_primary_selection(EditorBuffer* buffer) {
  buffer->primary().collapse_to_head();
}

void select_word_at(EditorBuffer* buffer, int line, int col) {
  int start = 0;
  int end = 0;
  identifier_bounds_at(*buffer, line, col, &start, &end);
  buffer->reset_to_single_cursor(line, start);
  buffer->primary().anchor = {line, start};
  buffer->primary().head = {line, end};
  clamp_all_cursors(buffer);
}

void select_words_range(EditorBuffer* buffer, int anchor_line, int anchor_col, int head_line,
                        int head_col) {
  int anchor_start = 0;
  int anchor_end = 0;
  int head_start = 0;
  int head_end = 0;
  identifier_bounds_at(*buffer, anchor_line, anchor_col, &anchor_start, &anchor_end);
  identifier_bounds_at(*buffer, head_line, head_col, &head_start, &head_end);

  CursorPos start{anchor_line, anchor_start};
  CursorPos end{head_line, head_end};
  if (head_line < anchor_line || (head_line == anchor_line && head_col < anchor_col)) {
    start = {head_line, head_start};
    end = {anchor_line, anchor_end};
  }

  buffer->reset_to_single_cursor(start.line, start.col);
  buffer->primary().anchor = start;
  buffer->primary().head = end;
  clamp_all_cursors(buffer);
}

void select_line_at(EditorBuffer* buffer, int line) {
  if (buffer->lines.empty()) {
    return;
  }
  line = std::max(0, std::min(line, static_cast<int>(buffer->lines.size()) - 1));
  const int len = static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
  buffer->reset_to_single_cursor(line, 0);
  buffer->primary().anchor = {line, 0};
  buffer->primary().head = {line, len};
  clamp_all_cursors(buffer);
}

void select_lines_range(EditorBuffer* buffer, int anchor_line, int head_line) {
  if (buffer->lines.empty()) {
    return;
  }
  const int max_line = static_cast<int>(buffer->lines.size()) - 1;
  anchor_line = std::max(0, std::min(anchor_line, max_line));
  head_line = std::max(0, std::min(head_line, max_line));
  const int lo = std::min(anchor_line, head_line);
  const int hi = std::max(anchor_line, head_line);
  const int head_col =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(hi)].size());
  buffer->reset_to_single_cursor(lo, 0);
  buffer->primary().anchor = {lo, 0};
  buffer->primary().head = {hi, head_col};
  clamp_all_cursors(buffer);
}

void extend_line_below(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }
  buffer->ensure_cursors();
  auto& cursor = buffer->primary();
  const int max_line = static_cast<int>(buffer->lines.size()) - 1;
  const int head_line = std::max(0, std::min(cursor.head.line, max_line));

  auto line_end_col = [&](int line) {
    return static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
  };

  if (cursor.has_selection()) {
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
    const bool line_wise =
        start_col == 0 && end_col == line_end_col(end_line);
    const bool head_at_range_end =
        cursor.head.line == end_line && cursor.head.col == end_col;
    if (line_wise && head_at_range_end && end_line < max_line) {
      const int next_line = end_line + 1;
      cursor.anchor = {start_line, 0};
      cursor.head = {next_line, line_end_col(next_line)};
      clamp_cursor(&cursor, *buffer);
      return;
    }
  }

  select_line_at(buffer, head_line);
}

void split_selection_on_newlines(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }
  buffer->ensure_cursors();

  const MultiCursor& primary = buffer->primary();
  int start_line = primary.head.line;
  int end_line = start_line;
  if (primary.has_selection()) {
    int start_col = 0;
    int end_col = 0;
    primary.normalized_range(&start_line, &start_col, &end_line, &end_col);
  }

  const int max_line = static_cast<int>(buffer->lines.size()) - 1;
  start_line = std::max(0, std::min(start_line, max_line));
  end_line = std::max(0, std::min(end_line, max_line));
  if (start_line > end_line) {
    std::swap(start_line, end_line);
  }

  buffer->cursors.clear();
  for (int line = start_line; line <= end_line; ++line) {
    MultiCursor cursor;
    cursor.set_pos(line, 0);
    buffer->cursors.push_back(cursor);
  }
  merge_overlapping_cursors(buffer);
  cursor_blink::show();
}

namespace {

int char_find_destination_col(const std::string& line, int from_col, CharFindKind kind, char target) {
  const int len = static_cast<int>(line.size());
  if (target == '\0') {
    return -1;
  }

  const auto matches = [&](int col) {
    return col >= 0 && col < len &&
           static_cast<unsigned char>(line[static_cast<std::size_t>(col)]) ==
               static_cast<unsigned char>(target);
  };

  switch (kind) {
    case CharFindKind::kFind:
      for (int col = std::max(0, from_col); col < len; ++col) {
        if (matches(col)) {
          return col;
        }
      }
      return -1;
    case CharFindKind::kTill:
      for (int col = std::max(0, from_col); col < len; ++col) {
        if (matches(col)) {
          return std::max(0, col - 1);
        }
      }
      return -1;
    case CharFindKind::kFindBack:
      for (int col = std::min(from_col, len > 0 ? len - 1 : 0); col >= 0; --col) {
        if (matches(col)) {
          return col;
        }
      }
      return -1;
    case CharFindKind::kTillBack:
      for (int col = std::min(from_col, len > 0 ? len - 1 : 0); col >= 0; --col) {
        if (matches(col)) {
          return std::min(len, col + 1);
        }
      }
      return -1;
  }
  return -1;
}

int char_find_search_from_col(int head_col, CharFindKind kind) {
  switch (kind) {
    case CharFindKind::kFind:
    case CharFindKind::kTill:
      return head_col + 1;
    case CharFindKind::kFindBack:
    case CharFindKind::kTillBack:
      return head_col - 1;
  }
  return head_col;
}

}  // namespace

bool char_find_on_line(EditorBuffer* buffer, CharFindKind kind, char target, bool extend_selection) {
  if (buffer == nullptr || buffer->lines.empty() || target == '\0') {
    return false;
  }
  buffer->ensure_cursors();
  bool moved = false;

  for (auto& cursor : buffer->cursors) {
    const int line = std::max(0, std::min(cursor.head.line, static_cast<int>(buffer->lines.size()) - 1));
    const std::string& text = buffer->lines[static_cast<std::size_t>(line)];
    const int from_col = char_find_search_from_col(cursor.head.col, kind);
    const int dest_col = char_find_destination_col(text, from_col, kind, target);
    if (dest_col < 0) {
      continue;
    }
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    cursor.head.line = line;
    cursor.head.col = dest_col;
    if (!extend_selection) {
      cursor.anchor = cursor.head;
    }
    moved = true;
  }

  if (!moved) {
    return false;
  }
  clamp_all_cursors(buffer);
  cursor_blink::show();
  return true;
}

void move_primary_left(EditorBuffer* buffer, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    auto& head = cursor.head;
    if (head.col > 0) {
      --head.col;
    } else if (head.line > 0) {
      --head.line;
      head.col = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
    }
  }
  finish_move(buffer, extend_selection);
}

void move_primary_right(EditorBuffer* buffer, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    auto& head = cursor.head;
    const int len = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
    if (head.col < len) {
      ++head.col;
    } else if (head.line + 1 < static_cast<int>(buffer->lines.size())) {
      ++head.line;
      head.col = 0;
    }
  }
  finish_move(buffer, extend_selection);
}

void move_primary_vertical(EditorBuffer* buffer, int line_delta, bool extend_selection) {
  if (buffer->lines.empty()) {
    return;
  }
  commit_undo_group(buffer);
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const int last_line = static_cast<int>(buffer->lines.size()) - 1;
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    auto& head = cursor.head;
    const std::string& from_line = buffer->lines[static_cast<std::size_t>(head.line)];
    const int desired_vis = byte_index_to_visual_column(from_line, head.col, tab_size);

    head.line = std::max(0, std::min(head.line + line_delta, last_line));

    const std::string& to_line = buffer->lines[static_cast<std::size_t>(head.line)];
    const int line_bytes = static_cast<int>(to_line.size());
    const int to_vis_len = byte_index_to_visual_column(to_line, line_bytes, tab_size);
    const int max_vis_on_line = line_bytes > 0 ? std::max(0, to_vis_len - 1) : 0;
    const int target_vis = std::min(desired_vis, max_vis_on_line);
    head.col = visual_column_to_byte_index(to_line, target_vis, tab_size);
    head.col = std::max(0, std::min(head.col, line_bytes));
  }
  finish_move(buffer, extend_selection);
}

void move_primary_up(EditorBuffer* buffer, bool extend_selection) {
  move_primary_vertical(buffer, -1, extend_selection);
}

void move_primary_down(EditorBuffer* buffer, bool extend_selection) {
  move_primary_vertical(buffer, 1, extend_selection);
}

void move_primary_home(EditorBuffer* buffer, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    cursor.head.col = 0;
  }
  finish_move(buffer, extend_selection);
}

void move_primary_end(EditorBuffer* buffer, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    const int line = cursor.head.line;
    cursor.head.col = static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
  }
  finish_move(buffer, extend_selection);
}

void move_primary_page_up(EditorBuffer* buffer, int visible_lines, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    cursor.head.line = std::max(0, cursor.head.line - visible_lines);
  }
  finish_move(buffer, extend_selection);
}

void move_primary_page_down(EditorBuffer* buffer, int visible_lines, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    cursor.head.line =
        std::min(cursor.head.line + visible_lines,
                 static_cast<int>(buffer->lines.size()) - 1);
  }
  finish_move(buffer, extend_selection);
}

void move_primary_word_left(EditorBuffer* buffer, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    auto& head = cursor.head;
    if (head.col > 0 || head.line > 0) {
      if (head.col == 0) {
        --head.line;
        head.col = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
      }
      head.col = word_left_col(*buffer, head.line, head.col);
    }
  }
  finish_move(buffer, extend_selection);
}

void move_primary_word_right(EditorBuffer* buffer, bool extend_selection) {
  commit_undo_group(buffer);
  for (auto& cursor : buffer->cursors) {
    if (!extend_selection) {
      cursor.collapse_to_head();
    }
    auto& head = cursor.head;
    const int line_len =
        static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
    if (head.col < line_len || head.line + 1 < static_cast<int>(buffer->lines.size())) {
      if (head.col >= line_len) {
        ++head.line;
        head.col = 0;
      }
      head.col = word_right_col(*buffer, head.line, head.col);
    }
  }
  finish_move(buffer, extend_selection);
}

void extend_block_selection_vertical(EditorBuffer* buffer, int direction) {
  if (buffer->lines.empty() || direction == 0) {
    return;
  }

  int col_start = buffer->primary_col();
  int col_end = col_start;
  int top_line = buffer->primary_line();
  int bottom_line = top_line;

  if (buffer->multi_cursor_active() || buffer->primary().has_selection()) {
    col_start = static_cast<int>(buffer->lines[0].size());
    col_end = 0;
    top_line = static_cast<int>(buffer->lines.size()) - 1;
    bottom_line = 0;
    for (const auto& cursor : buffer->cursors) {
      int start_line = 0;
      int start_col = 0;
      int end_line = 0;
      int end_col = 0;
      cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
      top_line = std::min(top_line, start_line);
      bottom_line = std::max(bottom_line, end_line);
      col_start = std::min(col_start, start_col);
      col_end = std::max(col_end, end_col);
    }
  }

  if (direction > 0) {
    if (bottom_line + 1 >= static_cast<int>(buffer->lines.size())) {
      return;
    }
    ++bottom_line;
  } else {
    if (top_line <= 0) {
      return;
    }
    --top_line;
  }

  buffer->cursors.clear();
  for (int line = top_line; line <= bottom_line; ++line) {
    MultiCursor cursor;
    if (col_start == col_end) {
      cursor.set_pos(line, col_start);
    } else {
      cursor.anchor = {line, col_start};
      cursor.head = {line, col_end};
    }
    buffer->cursors.push_back(cursor);
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
  cursor_blink::show();
}

void goto_buffer_line(EditorBuffer* buffer, int line_one_based, int visible_lines) {
  if (buffer->lines.empty()) {
    return;
  }
  const int max_line = static_cast<int>(buffer->lines.size());
  const int line = std::max(1, std::min(line_one_based, max_line)) - 1;
  buffer->reset_to_single_cursor(line, 0);
  if (visible_lines > 0) {
    buffer->scroll = std::max(0, line - visible_lines / 2);
  }
  ensure_scroll_visible(buffer, visible_lines);
}

void adjust_cursors_after_line_edit(EditorBuffer* buffer, int line, int col, int delta) {
  if (delta == 0) {
    return;
  }
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line && cursor.head.col >= col) {
      cursor.head.col += delta;
    }
    if (cursor.anchor.line == line && cursor.anchor.col >= col) {
      cursor.anchor.col += delta;
    }
  }
}

void collect_affected_lines(const EditorBuffer& buffer, std::vector<int>* lines) {
  if (lines == nullptr) {
    return;
  }
  lines->clear();
  std::vector<int> unique;
  unique.reserve(buffer.cursors.size());
  for (const auto& cursor : buffer.cursors) {
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    if (cursor.has_selection()) {
      cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
    } else {
      start_line = cursor.head.line;
      end_line = cursor.head.line;
    }
    for (int line = start_line; line <= end_line; ++line) {
      unique.push_back(line);
    }
  }
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  *lines = std::move(unique);
}

std::size_t comment_insert_column(const std::string& line) {
  std::size_t index = 0;
  while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index]))) {
    ++index;
  }
  return index;
}

void comment_lines(EditorBuffer* buffer, const LineCommentStyle& style) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }

  std::vector<int> lines;
  collect_affected_lines(*buffer, &lines);
  if (lines.empty()) {
    return;
  }

  commit_undo_group(buffer);
  push_undo(buffer);
  for (int line_index : lines) {
    if (line_index < 0 || line_index >= static_cast<int>(buffer->lines.size())) {
      continue;
    }
    std::string& line = buffer->lines[static_cast<std::size_t>(line_index)];
    const std::size_t insert_col = comment_insert_column(line);
    const std::string before = line;
    comment_line_text(&line, style);
    if (line != before) {
      const int delta = static_cast<int>(line.size()) - static_cast<int>(before.size());
      adjust_cursors_after_line_edit(buffer, line_index, static_cast<int>(insert_col), delta);
    }
  }

  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
  mark_dirty(buffer);
}

void uncomment_lines(EditorBuffer* buffer, const LineCommentStyle& style) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }

  std::vector<int> lines;
  collect_affected_lines(*buffer, &lines);
  if (lines.empty()) {
    return;
  }

  commit_undo_group(buffer);
  push_undo(buffer);
  for (int line_index : lines) {
    if (line_index < 0 || line_index >= static_cast<int>(buffer->lines.size())) {
      continue;
    }
    std::string& line = buffer->lines[static_cast<std::size_t>(line_index)];
    const std::size_t insert_col = comment_insert_column(line);
    const std::string before = line;
    if (uncomment_line_text(&line, style)) {
      const int delta = static_cast<int>(line.size()) - static_cast<int>(before.size());
      adjust_cursors_after_line_edit(buffer, line_index, static_cast<int>(insert_col), delta);
    }
  }

  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }
  clamp_all_cursors(buffer);
  merge_overlapping_cursors(buffer);
  mark_dirty(buffer);
}

namespace {

bool line_has_content(const std::string& line) {
  return line.find_first_not_of(" \t\r\n") != std::string::npos;
}

}  // namespace

void toggle_comment_lines(EditorBuffer* buffer, const LineCommentStyle& style) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }

  std::vector<int> lines;
  collect_affected_lines(*buffer, &lines);
  if (lines.empty()) {
    return;
  }

  bool all_commented = true;
  bool any_content = false;
  for (int line_index : lines) {
    if (line_index < 0 || line_index >= static_cast<int>(buffer->lines.size())) {
      continue;
    }
    const std::string& line = buffer->lines[static_cast<std::size_t>(line_index)];
    if (!line_has_content(line)) {
      continue;
    }
    any_content = true;
    if (!line_is_commented(line, style)) {
      all_commented = false;
      break;
    }
  }
  if (!any_content) {
    return;
  }

  if (all_commented) {
    uncomment_lines(buffer, style);
  } else {
    comment_lines(buffer, style);
  }
}

}  // namespace tgdb
