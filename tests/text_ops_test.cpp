#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "editor/clipboard.hpp"
#include "editor/editor_state.hpp"
#include "editor/text_ops.hpp"
#include "editor/text_search.hpp"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

tgdb::EditorBuffer make_buffer(std::initializer_list<std::string> lines) {
  tgdb::EditorBuffer buffer;
  buffer.lines = lines;
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
  tgdb::insert_char(&buffer, 'x');
  check(buffer.lines[0] == "xfoo xfoo xfoo", "insert at three positions");
  check(buffer.cursors.size() == 3, "still three cursors");
}

void test_find_all_matches() {
  auto buffer = make_buffer({"abc abc", "abc"});
  const auto matches = tgdb::find_all_matches(buffer, "abc");
  check(matches.size() == 3, "three matches");
}

void test_select_all_matches() {
  auto buffer = make_buffer({"foo bar foo"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {0, 3};
  tgdb::select_all_matches(&buffer);
  check(buffer.cursors.size() == 2, "two foo matches");
}

void test_exit_multi_cursor() {
  auto buffer = make_buffer({"a b a"});
  buffer.cursors = {{{0, 0}, {0, 0}}, {{0, 4}, {0, 4}}};
  tgdb::exit_multi_cursor_mode(&buffer);
  check(buffer.cursors.size() == 1, "single cursor");
  check(!buffer.cursors[0].has_selection(), "selection cleared");
}

void test_backspace_multi() {
  auto buffer = make_buffer({"abcd"});
  buffer.cursors = {{{0, 2}, {0, 2}}, {{0, 4}, {0, 4}}};
  tgdb::backspace(&buffer);
  check(buffer.lines[0] == "ac", "backspace at two positions");
}

void test_replace_selection_on_type() {
  auto buffer = make_buffer({"hello world"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {0, 5};
  tgdb::insert_char(&buffer, 'X');
  check(buffer.lines[0] == "X world", "typing replaces selection");
  check(!buffer.primary().has_selection(), "selection cleared after replace");
}

void test_word_left() {
  auto buffer = make_buffer({"foo bar baz"});
  buffer.reset_to_single_cursor(0, 10);
  tgdb::move_primary_word_left(&buffer, false);
  check(buffer.primary_col() == 8, "word left to baz");
  tgdb::move_primary_word_left(&buffer, false);
  check(buffer.primary_col() == 4, "word left to bar");
}

void test_copy_selection() {
  auto buffer = make_buffer({"hello world"});
  buffer.primary().anchor = {0, 0};
  buffer.primary().head = {0, 5};
  check(tgdb::copy_selection(&buffer), "copy succeeds");
  check(tgdb::editor_clipboard() == "hello", "full selection copied");
}

void test_paste_at_end_of_line() {
  auto buffer = make_buffer({"hello"});
  buffer.reset_to_single_cursor(0, 5);
  tgdb::paste_at_primary(&buffer, "abc");
  check(buffer.lines[0] == "helloabc", "paste full text at end of line");
  check(buffer.primary_col() == 8, "cursor after pasted text");
}

void test_paste_replaces_selection() {
  auto buffer = make_buffer({"hello world"});
  buffer.primary().anchor = {0, 6};
  buffer.primary().head = {0, 11};
  tgdb::paste_at_primary(&buffer, "tgdb");
  check(buffer.lines[0] == "hello tgdb", "selection replaced by paste");
}

void test_paste_multi_cursor() {
  auto buffer = make_buffer({"foo foo foo"});
  buffer.cursors = {
      {{0, 0}, {0, 3}},
      {{0, 4}, {0, 7}},
      {{0, 8}, {0, 11}},
  };
  tgdb::paste_at_primary(&buffer, "bar");
  check(buffer.lines[0] == "bar bar bar", "paste replaces all selections");
  check(buffer.cursors.size() == 3, "multi-cursor preserved");
  check(buffer.cursors[0].head.col == 3, "first cursor after paste");
  check(buffer.cursors[1].head.col == 7, "second cursor after paste");
  check(buffer.cursors[2].head.col == 11, "third cursor after paste");
}

}  // namespace

int main() {
  test_insert_multi_cursor();
  test_find_all_matches();
  test_select_all_matches();
  test_exit_multi_cursor();
  test_backspace_multi();
  test_replace_selection_on_type();
  test_word_left();
  test_copy_selection();
  test_paste_at_end_of_line();
  test_paste_replaces_selection();
  test_paste_multi_cursor();
  return 0;
}
