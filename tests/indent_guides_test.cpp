#include "editor/indent_guides.hpp"

#include <cassert>
#include <iostream>

namespace tgdb {
namespace {

void test_caret_column_after_leading_tabs() {
  const std::string line = "\t\t";
  const int tab = 4;
  const int primary_vis = byte_index_to_visual_column(line, 2, tab);
  assert(primary_vis == 8);

  const int guide_prefix_visual = 4;
  const int body_start =
      byte_index_to_visual_column(line, 0, tab) + guide_prefix_visual;
  assert(body_start == 4);

  const int caret_in_body = primary_vis - body_start;
  assert(caret_in_body == 4);
}

void test_body_source_byte_after_tabs() {
  const std::string line = "\t\tcode";
  const int tab = 4;
  const int body_start = byte_index_to_visual_column(line, 0, tab) + 4;
  const int source_byte = visual_column_to_byte_index(line, body_start, tab);
  assert(source_byte == 1);
}

}  // namespace
}  // namespace tgdb

int main() {
  tgdb::test_caret_column_after_leading_tabs();
  tgdb::test_body_source_byte_after_tabs();
  std::cout << "indent_guides_test: ok\n";
  return 0;
}
