#pragma once

#include "editor/line_comment.hpp"
#include "editor/editor_state.hpp"

namespace tgdb {

struct SnippetResult;

void ensure_scroll_visible(EditorBuffer* buffer, int visible_lines, int code_width = -1);
void ensure_scroll_centered(EditorBuffer* buffer, int visible_lines, int code_width = -1);
void scroll_view_by_lines(EditorBuffer* buffer, int delta_lines, int visible_lines);
void scroll_view_by_columns(EditorBuffer* buffer, int delta_columns, int code_width);
void move_primary_half_page_up(EditorBuffer* buffer, int visible_lines,
                               bool extend_selection = false);
void move_primary_half_page_down(EditorBuffer* buffer, int visible_lines,
                                 bool extend_selection = false);

void insert_char(EditorBuffer* buffer, char c);
void insert_tab_stop(EditorBuffer* buffer, int tab_size = 4);
void replace_word_at_cursor(EditorBuffer* buffer, const std::string& replacement);
void replace_text_range(EditorBuffer* buffer, int line, int start_col, int end_col,
                        const std::string& replacement);
void replace_text_range_with_caret(EditorBuffer* buffer, int line, int start_col, int end_col,
                                   const std::string& replacement, int caret_line_offset,
                                   int caret_col, int sel_start_col, int sel_end_col);
void apply_completion_at_all_cursors(EditorBuffer* buffer, const SnippetResult& snippet);
void backspace(EditorBuffer* buffer);
void delete_char(EditorBuffer* buffer);
void delete_word_backward(EditorBuffer* buffer);
void delete_word_forward(EditorBuffer* buffer);
void newline(EditorBuffer* buffer);
void paste_at_primary(EditorBuffer* buffer, const std::string& text);
bool undo_edit(EditorBuffer* buffer);
bool redo_edit(EditorBuffer* buffer);
void delete_all_selections(EditorBuffer* buffer);

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
void select_word_at(EditorBuffer* buffer, int line, int col);
void select_words_range(EditorBuffer* buffer, int anchor_line, int anchor_col, int head_line,
                        int head_col);
void select_line_at(EditorBuffer* buffer, int line);
void select_lines_range(EditorBuffer* buffer, int anchor_line, int head_line);

void extend_block_selection_vertical(EditorBuffer* buffer, int direction);
void goto_buffer_line(EditorBuffer* buffer, int line_one_based, int visible_lines);

void comment_lines(EditorBuffer* buffer, const LineCommentStyle& style);
void uncomment_lines(EditorBuffer* buffer, const LineCommentStyle& style);

}  // namespace tgdb
