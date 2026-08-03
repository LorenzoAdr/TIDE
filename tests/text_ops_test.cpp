#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/clipboard.hpp"
#include "editor/editor_buffer_source.hpp"
#include "editor/editor_state.hpp"
#include "editor/line_comment.hpp"
#include "editor/text_ops.hpp"
#include "symbols/completion_snippet.hpp"
#include "editor/text_search.hpp"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

tuide::EditorBuffer make_buffer(std::initializer_list<std::string> lines) {
  tuide::EditorBuffer buffer;
  buffer.lines.assign(std::vector<std::string>(lines));
  buffer.reset_to_single_cursor(0, 0);
  return buffer;
}

void test_insert_multi_cursor() {
  auto buffer = make_buffer({"foo foo foo"});
  buffer.cursors = {
      {{0, 0}, {0, 0}},
      {{0, 4}, {0, 4}},
      {{0, 8}, {0, 8}},
  };
  tuide::insert_char(&buffer, 'x');
  check(buffer.lines[0] == "xfoo xfoo xfoo", "insert at three positions");
  check(buffer.cursors.size() == 3, "still three cursors");
}

void test_find_all_matches() {
  auto buffer = make_buffer({"abc abc", "abc"});
  const auto matches = tuide::find_all_matches(buffer, "abc");
  check(matches.size() == 3, "three matches");
}

void test_select_all_matches() {
  auto buffer = make_buffer({"foo bar foo"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {0, 3};
  tuide::select_all_matches(&buffer);
  check(buffer.cursors.size() == 2, "two foo matches");
}

void test_select_all() {
  auto buffer = make_buffer({"alpha", "beta", "gamma"});
  buffer.reset_to_single_cursor(1, 2);
  tuide::select_all(&buffer);
  check(buffer.cursors.size() == 1, "single cursor after select all");
  check(buffer.primary().anchor.line == 0 && buffer.primary().anchor.col == 0,
        "anchor at start of file");
  check(buffer.primary().head.line == 2 && buffer.primary().head.col == 5,
        "head at end of file");
  check(buffer.primary().has_selection(), "select all creates a selection");
}

void test_exit_multi_cursor() {
  auto buffer = make_buffer({"a b a"});
  buffer.cursors = {{{0, 0}, {0, 0}}, {{0, 4}, {0, 4}}};
  tuide::exit_multi_cursor_mode(&buffer);
  check(buffer.cursors.size() == 1, "single cursor");
  check(!buffer.cursors[0].has_selection(), "selection cleared");
}

void test_backspace_multi() {
  auto buffer = make_buffer({"abcd"});
  buffer.cursors = {{{0, 2}, {0, 2}}, {{0, 4}, {0, 4}}};
  tuide::backspace(&buffer);
  check(buffer.lines[0] == "ac", "backspace at two positions");
}

void test_replace_selection_on_type() {
  auto buffer = make_buffer({"hello world"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {0, 5};
  tuide::insert_char(&buffer, 'X');
  check(buffer.lines[0] == "X world", "typing replaces selection");
  check(!buffer.primary().has_selection(), "selection cleared after replace");
}

void test_word_left() {
  auto buffer = make_buffer({"foo bar baz"});
  buffer.reset_to_single_cursor(0, 10);
  tuide::move_primary_word_left(&buffer, false);
  check(buffer.primary_col() == 8, "word left to baz");
  tuide::move_primary_word_left(&buffer, false);
  check(buffer.primary_col() == 4, "word left to bar");
}

void test_copy_selection() {
  auto buffer = make_buffer({"hello world"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {0, 5};
  check(tuide::copy_selection(&buffer), "copy succeeds");
  check(tuide::editor_clipboard() == "hello", "full selection copied");
}

void test_paste_at_end_of_line() {
  auto buffer = make_buffer({"hello"});
  buffer.reset_to_single_cursor(0, 5);
  tuide::paste_at_primary(&buffer, "abc");
  check(buffer.lines[0] == "helloabc", "paste full text at end of line");
  check(buffer.primary_col() == 8, "cursor after pasted text");
}

void test_paste_replaces_selection() {
  auto buffer = make_buffer({"hello world"});
  buffer.primary().anchor = {0, 6};
  buffer.primary().head = {0, 11};
  tuide::paste_at_primary(&buffer, "tuide");
  check(buffer.lines[0] == "hello tuide", "selection replaced by paste");
}

void test_paste_multi_cursor() {
  auto buffer = make_buffer({"foo foo foo"});
  buffer.cursors = {
      {{0, 0}, {0, 3}},
      {{0, 4}, {0, 7}},
      {{0, 8}, {0, 11}},
  };
  tuide::paste_at_primary(&buffer, "bar");
  check(buffer.lines[0] == "bar bar bar", "paste replaces all selections");
  check(buffer.cursors.size() == 3, "multi-cursor preserved");
  check(buffer.cursors[0].head.col == 3, "first cursor after paste");
  check(buffer.cursors[1].head.col == 7, "second cursor after paste");
  check(buffer.cursors[2].head.col == 11, "third cursor after paste");
}

void test_completion_multi_cursor() {
  auto buffer = make_buffer({"foo foo foo"});
  buffer.cursors = {
      {{0, 0}, {0, 0}},
      {{0, 4}, {0, 4}},
      {{0, 8}, {0, 8}},
  };
  tuide::SnippetResult snippet;
  snippet.text = "baz";
  snippet.caret_col = 3;
  tuide::apply_completion_at_all_cursors(&buffer, snippet);
  check(buffer.lines[0] == "baz baz baz", "completion at three positions");
  check(buffer.cursors.size() == 3, "still three cursors");
  check(buffer.cursors[0].head.col == 3, "first cursor after completion");
  check(buffer.cursors[1].head.col == 7, "second cursor after completion");
  check(buffer.cursors[2].head.col == 11, "third cursor after completion");
}

void test_finalize_function_call_insert() {
  {
    const auto snippet =
        tuide::finalize_function_call_insert("my_var", "int", /*is_callable=*/false);
    check(snippet.text == "my_var", "non-callable keeps plain insert text");
  }
  {
    const auto snippet =
        tuide::finalize_function_call_insert("greet", "void greet()", /*is_callable=*/true);
    check(snippet.text == "greet()", "callable without args gets empty call parens");
    check(snippet.caret_col == 6, "caret inside empty call parens");
  }
  {
    const auto snippet = tuide::finalize_function_call_insert("add", "int add(int a, int b)",
                                                             /*is_callable=*/true);
    check(snippet.text == "add(int a, int b)", "callable with args expands signature placeholders");
  }
}

void test_mouse_scroll_margin() {
  auto buffer = make_buffer({"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"});
  buffer.scroll = 0;
  buffer.reset_to_single_cursor(2, 0);
  tuide::ensure_scroll_visible(&buffer, 5, -1);
  check(buffer.scroll == 0, "near top keeps scroll at file start");

  buffer.scroll = 0;
  buffer.reset_to_single_cursor(4, 0);
  tuide::ensure_scroll_visible(&buffer, 5, -1);
  check(buffer.scroll == 2, "cursor near bottom margin scrolls early");

  buffer.scroll = 5;
  buffer.reset_to_single_cursor(9, 0);
  tuide::ensure_scroll_visible(&buffer, 5, -1);
  check(buffer.scroll == 5, "file end allows cursor at bottom edge");
}

void test_arrow_scroll_keeps_margin() {
  auto buffer = make_buffer({"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"});
  buffer.scroll = 0;
  buffer.reset_to_single_cursor(0, 0);
  constexpr int visible = 5;
  for (int line = 1; line <= 9; ++line) {
    buffer.reset_to_single_cursor(line, 0);
    tuide::ensure_scroll_visible(&buffer, visible, -1);
  }
  check(buffer.primary_line() == 9, "cursor reached last line");
  check(buffer.scroll == 5, "arrow-style navigation keeps cursor off bottom edge");
}

void test_comment_lines_updates_joined_source() {
  auto buffer = make_buffer({"int foo;", "return 0;"});
  buffer.path = "hello.cpp";
  (void)tuide::editor_buffer_joined_source(buffer);
  check(buffer.joined_source_cache.valid, "joined cache warm before comment");
  check(tuide::editor_buffer_joined_source(buffer) == "int foo;\nreturn 0;",
        "joined matches lines before comment");

  const tuide::LineCommentStyle style = tuide::line_comment_style_for_path(buffer.path);
  buffer.reset_to_single_cursor(0, 0);
  tuide::comment_lines(&buffer, style);
  check(buffer.lines[0] == "// int foo;", "line commented");
  check(tuide::editor_buffer_joined_source(buffer) == "// int foo;\nreturn 0;",
        "joined cache tracks comment prefix for tree-sitter");

  tuide::uncomment_lines(&buffer, style);
  check(buffer.lines[0] == "int foo;", "line uncommented");
  check(tuide::editor_buffer_joined_source(buffer) == "int foo;\nreturn 0;",
        "joined cache tracks uncomment");
}

void test_indent_unindent_selection() {
  auto buffer = make_buffer({"alpha", "beta", "gamma"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {2, 5};
  check(tuide::any_cursor_has_selection(buffer), "multi-line selection active");

  tuide::indent_lines(&buffer);
  check(buffer.lines[0] == "    alpha", "first line indented");
  check(buffer.lines[1] == "    beta", "second line indented");
  check(buffer.lines[2] == "    gamma", "third line indented");
  check(buffer.primary().anchor.col == 4, "anchor shifted by indent");
  check(buffer.primary().head.col == 9, "head shifted by indent");
  check(tuide::any_cursor_has_selection(buffer), "selection preserved after indent");

  tuide::unindent_lines(&buffer);
  check(buffer.lines[0] == "alpha", "first line unindented");
  check(buffer.lines[1] == "beta", "second line unindented");
  check(buffer.lines[2] == "gamma", "third line unindented");
  check(buffer.primary().anchor.col == 0, "anchor restored");
  check(buffer.primary().head.col == 5, "head restored");
}

void test_indent_single_line_selection() {
  auto buffer = make_buffer({"hello world"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {0, 5};
  tuide::indent_lines(&buffer);
  check(buffer.lines[0] == "    hello world", "single-line selection indents whole line");
  check(buffer.primary().anchor.col == 4, "anchor after indent prefix");
  check(buffer.primary().head.col == 9, "head after indent prefix");
}

}  // namespace

int main() {
  test_insert_multi_cursor();
  test_find_all_matches();
  test_select_all_matches();
  test_select_all();
  test_exit_multi_cursor();
  test_backspace_multi();
  test_replace_selection_on_type();
  test_word_left();
  test_copy_selection();
  test_paste_at_end_of_line();
  test_paste_replaces_selection();
  test_paste_multi_cursor();
  test_completion_multi_cursor();
  test_finalize_function_call_insert();
  test_mouse_scroll_margin();
  test_arrow_scroll_keeps_margin();
  test_comment_lines_updates_joined_source();
  test_indent_unindent_selection();
  test_indent_single_line_selection();
  return 0;
}