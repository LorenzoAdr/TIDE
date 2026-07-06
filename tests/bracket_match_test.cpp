#include "editor/bracket_match.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

namespace tgdb {
namespace {

EditorBuffer make_buffer(std::initializer_list<const char*> lines) {
  EditorBuffer buffer;
  for (const char* line : lines) {
    buffer.lines.emplace_back(line);
  }
  return buffer;
}

void test_simple_pair() {
  const EditorBuffer buffer = make_buffer({"int main() {", "  return 0;", "}"});
  const BracketPairHighlight open =
      find_bracket_pair_highlight(buffer, 0, std::string("int main() {").find('{') + 1);
  assert(open.valid);
  assert(open.line_a == 0);
  assert(open.line_b == 2);

  const BracketPairHighlight close = find_bracket_pair_highlight(buffer, 2, 0);
  assert(close.valid);
  assert(close.line_a == 2);
  assert(close.line_b == 0);
}

void test_nested() {
  const EditorBuffer buffer = make_buffer({"foo(bar[baz])"});
  const int open_paren = static_cast<int>(std::string("foo(bar[baz])").find('('));
  const BracketPairHighlight match = find_bracket_pair_highlight(buffer, 0, open_paren + 1);
  assert(match.valid);
  assert(match.col_a == open_paren);
  assert(match.col_b == static_cast<int>(std::string("foo(bar[baz])").find(')')));
}

void test_ignores_string() {
  const std::string line = "if (c == \")\") return;";
  const EditorBuffer buffer = make_buffer({line.c_str()});
  const int open_paren = static_cast<int>(line.find('('));
  const BracketPairHighlight match =
      find_bracket_pair_highlight(buffer, 0, open_paren + 1);
  assert(match.valid);
  assert(match.col_b == static_cast<int>(line.rfind(')')));
}

void test_empty_lines_no_hang() {
  const EditorBuffer buffer = make_buffer({"", "// comment", "", "int main() {", "}"});
  const int open_brace = static_cast<int>(std::string("int main() {").find('{'));
  const BracketPairHighlight match =
      find_bracket_pair_highlight(buffer, 3, open_brace + 1);
  assert(match.valid);
  assert(match.line_b == 4);
}

void test_empty_string_literal_cursor() {
  const EditorBuffer buffer = make_buffer({
      "void foo(const char* p, const char* q);",
      "foo(x, \"\");",
      "int bar;",
  });
  assert(cursor_in_code(buffer, 2, 4));
}

void test_application_cpp_tail_cursor_in_code() {
  EditorBuffer buffer;
  std::ifstream input("/home/lorenzo/workspace/tgdb/src/app/application.cpp");
  std::string line;
  while (std::getline(input, line)) {
    buffer.lines.push_back(line);
  }
  assert(cursor_in_code(buffer, 2470, 4));
  assert(cursor_in_code(buffer, 2538, 4));
}

}  // namespace
}  // namespace tgdb

int main() {
  tgdb::test_simple_pair();
  tgdb::test_nested();
  tgdb::test_ignores_string();
  tgdb::test_empty_lines_no_hang();
  tgdb::test_empty_string_literal_cursor();
  tgdb::test_application_cpp_tail_cursor_in_code();
  std::cout << "bracket_match_test ok\n";
  return 0;
}
