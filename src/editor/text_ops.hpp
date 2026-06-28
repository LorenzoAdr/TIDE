#pragma once

#include "editor/editor_state.hpp"

namespace tgdb {

void ensure_scroll_visible(EditorBuffer* buffer, int visible_lines);
void scroll_view_by_lines(EditorBuffer* buffer, int delta_lines, int visible_lines);
void move_primary_half_page_up(EditorBuffer* buffer, int visible_lines,
                               bool extend_selection = false);
void move_primary_half_page_down(EditorBuffer* buffer, int visible_lines,
                                 bool extend_selection = false);

void insert_char(EditorBuffer* buffer, char c);
void replace_word_at_cursor(EditorBuffer* buffer, const std::string& replacement);
void replace_text_range(EditorBuffer* buffer, int line, int start_col, int end_col,
                        const std::string& replacement);
void replace_text_range_with_caret(EditorBuffer* buffer, int line, int start_col, int end_col,
                                   const std::string& replacement, int caret_line_offset,
                                   int caret_col, int sel_start_col = -1, int sel_end_col = -1);
void backspace(EditorBuffer* buffer);
void delete_char(EditorBuffer* buffer);
void delete_word_backward(EditorBuffer* buffer);
void delete_word_forward(EditorBuffer* buffer);
void newline(EditorBuffer* buffer);
void paste_at_primary(EditorBuffer* buffer, const std::string& text);
bool undo_edit(EditorBuffer* buffer);

void move_primary_left(EditorBuffer* buffer, bool extend_selection = false);
void move_primary_right(EditorBuffer* buffer, bool extend_selection = false);
void move_primary_up(EditorBuffer* buffer, bool extend_selection = false);
void move_primary_down(EditorBuffer* buffer, bool extend_selection = false);
void move_primary_home(EditorBuffer* buffer, bool extend_selection = false);
void move_primary_end(EditorBuffer* buffer, bool extend_selection = false);
void move_primary_page_up(EditorBuffer* buffer, int visible_lines, bool extend_selection = false);
void move_primary_page_down(EditorBuffer* buffer, int visible_lines, bool extend_selection = false);
void move_primary_word_left(EditorBuffer* buffer, bool extend_selection = false);
void move_primary_word_right(EditorBuffer* buffer, bool extend_selection = false);

void clear_primary_selection(EditorBuffer* buffer);

void extend_block_selection_vertical(EditorBuffer* buffer, int direction);
void goto_buffer_line(EditorBuffer* buffer, int line_one_based, int visible_lines);

}  // namespace tgdb
