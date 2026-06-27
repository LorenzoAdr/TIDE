#include "editor/text_ops.hpp"

#include <algorithm>
#include <cctype>

#include "editor/text_search.hpp"
#include "editor/undo_stack.hpp"

namespace tgdb {

namespace {

int max_scroll(int total, int visible) { return std::max(0, total - visible); }

void mark_dirty(EditorBuffer* buffer) { buffer->dirty = true; }

bool is_word_char(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) || c == '_';
}

int word_left_col(const EditorBuffer& buffer, int line, int col) {
  if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    return 0;
  }
  const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
  col = std::max(0, std::min(col, static_cast<int>(text.size())));
  int pos = col;
  while (pos > 0 && !is_word_char(text[static_cast<std::size_t>(pos - 1)])) {
    --pos;
  }
  while (pos > 0 && is_word_char(text[static_cast<std::size_t>(pos - 1)])) {
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
  while (pos < len && is_word_char(text[static_cast<std::size_t>(pos)])) {
    ++pos;
  }
  while (pos < len && !is_word_char(text[static_cast<std::size_t>(pos)])) {
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
    line.erase(static_cast<std::size_t>(start_col),
               static_cast<std::size_t>(end_col - start_col));
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
}

bool any_cursor_has_selection(const EditorBuffer& buffer) {
  for (const auto& cursor : buffer.cursors) {
    if (cursor.has_selection()) {
      return true;
    }
  }
  return false;
}

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

void insert_char_at(EditorBuffer* buffer, int line, int col, char c) {
  auto& text = buffer->lines[static_cast<std::size_t>(line)];
  text.insert(static_cast<std::size_t>(col), 1, c);
  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line && cursor.head.col >= col) {
      ++cursor.head.col;
    }
    if (cursor.anchor.line == line && cursor.anchor.col >= col) {
      ++cursor.anchor.col;
    }
  }
}

void backspace_at(EditorBuffer* buffer, int line, int col) {
  if (col > 0) {
    buffer->lines[static_cast<std::size_t>(line)].erase(static_cast<std::size_t>(col - 1), 1);
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

void newline_at(EditorBuffer* buffer, int line, int col) {
  auto& text = buffer->lines[static_cast<std::size_t>(line)];
  const std::string tail = text.substr(static_cast<std::size_t>(col));
  text.erase(static_cast<std::size_t>(col));
  buffer->lines.insert(buffer->lines.begin() + line + 1, tail);

  for (auto& cursor : buffer->cursors) {
    if (cursor.head.line == line) {
      if (cursor.head.col > col) {
        cursor.head.line = line + 1;
        cursor.head.col -= col;
      } else if (cursor.head.col == col) {
        cursor.head.line = line + 1;
        cursor.head.col = 0;
      }
    } else if (cursor.head.line > line) {
      ++cursor.head.line;
    }

    if (cursor.anchor.line == line) {
      if (cursor.anchor.col > col) {
        cursor.anchor.line = line + 1;
        cursor.anchor.col -= col;
      } else if (cursor.anchor.col == col) {
        cursor.anchor.line = line + 1;
        cursor.anchor.col = 0;
      }
    } else if (cursor.anchor.line > line) {
      ++cursor.anchor.line;
    }
  }
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
  auto& head = buffer->primary().head;
  if (buffer->lines.empty()) {
    return;
  }
  head.line = std::max(0, std::min(head.line, static_cast<int>(buffer->lines.size()) - 1));
  const int len = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
  head.col = std::max(0, std::min(head.col, len));
}

void finish_move(EditorBuffer* buffer, bool extend_selection) {
  clamp_head_col(buffer);
  if (!extend_selection) {
    buffer->primary().anchor = buffer->primary().head;
  }
  clamp_all_cursors(buffer);
}

}  // namespace

void ensure_scroll_visible(EditorBuffer* buffer, int visible_lines) {
  const int primary = buffer->primary_line();
  if (primary < buffer->scroll) {
    buffer->scroll = primary;
  } else if (primary >= buffer->scroll + visible_lines) {
    buffer->scroll = std::max(0, primary - visible_lines + 1);
  }
  buffer->scroll = std::max(
      0, std::min(buffer->scroll,
                  max_scroll(static_cast<int>(buffer->lines.size()), visible_lines)));
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

void insert_char(EditorBuffer* buffer, char c) {
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
    insert_char_at(buffer, pos.line, pos.col, c);
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

void newline(EditorBuffer* buffer) {
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
  push_undo(buffer);
  exit_multi_cursor_mode(buffer);
  if (any_cursor_has_selection(*buffer)) {
    delete_all_selections(buffer);
  }
  for (char c : text) {
    if (c == '\n') {
      apply_to_all_cursors(buffer, newline_at);
    } else {
      insert_char_at(buffer, buffer->primary().head.line, buffer->primary().head.col, c);
      buffer->primary().head.col += 1;
      buffer->primary().anchor = buffer->primary().head;
    }
  }
  clamp_all_cursors(buffer);
  mark_dirty(buffer);
}

bool undo_edit(EditorBuffer* buffer) { return undo(buffer); }

void clear_primary_selection(EditorBuffer* buffer) {
  buffer->primary().collapse_to_head();
}

void move_primary_left(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  auto& head = buffer->primary().head;
  if (head.col > 0) {
    --head.col;
  } else if (head.line > 0) {
    --head.line;
    head.col = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
  }
  finish_move(buffer, extend_selection);
}

void move_primary_right(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  auto& head = buffer->primary().head;
  const int len = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
  if (head.col < len) {
    ++head.col;
  } else if (head.line + 1 < static_cast<int>(buffer->lines.size())) {
    ++head.line;
    head.col = 0;
  }
  finish_move(buffer, extend_selection);
}

void move_primary_up(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  auto& head = buffer->primary().head;
  if (head.line > 0) {
    --head.line;
  }
  finish_move(buffer, extend_selection);
}

void move_primary_down(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  auto& head = buffer->primary().head;
  if (head.line + 1 < static_cast<int>(buffer->lines.size())) {
    ++head.line;
  }
  finish_move(buffer, extend_selection);
}

void move_primary_home(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  buffer->primary().head.col = 0;
  finish_move(buffer, extend_selection);
}

void move_primary_end(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  const int line = buffer->primary().head.line;
  buffer->primary().head.col =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
  finish_move(buffer, extend_selection);
}

void move_primary_page_up(EditorBuffer* buffer, int visible_lines, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  buffer->primary().head.line = std::max(0, buffer->primary().head.line - visible_lines);
  finish_move(buffer, extend_selection);
}

void move_primary_page_down(EditorBuffer* buffer, int visible_lines, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  buffer->primary().head.line =
      std::min(buffer->primary().head.line + visible_lines,
               static_cast<int>(buffer->lines.size()) - 1);
  finish_move(buffer, extend_selection);
}

void move_primary_word_left(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  auto& head = buffer->primary().head;
  if (head.col > 0 || head.line > 0) {
    if (head.col == 0) {
      --head.line;
      head.col = static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
    }
    head.col = word_left_col(*buffer, head.line, head.col);
  }
  finish_move(buffer, extend_selection);
}

void move_primary_word_right(EditorBuffer* buffer, bool extend_selection) {
  if (!extend_selection) {
    buffer->primary().collapse_to_head();
  }
  auto& head = buffer->primary().head;
  const int line_len =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(head.line)].size());
  if (head.col < line_len || head.line + 1 < static_cast<int>(buffer->lines.size())) {
    if (head.col >= line_len) {
      ++head.line;
      head.col = 0;
    }
    head.col = word_right_col(*buffer, head.line, head.col);
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

}  // namespace tgdb
