#include "ui/file_tree_sticky.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace tuide {
namespace {

struct Row {
  int depth = 0;
  bool is_folder = false;
};

std::vector<int> sticky_of(const std::vector<Row>& rows, int scroll, int max_sticky = 7) {
  return sticky_explorer_indices(
      static_cast<int>(rows.size()), scroll, max_sticky,
      [&](int i) { return rows[static_cast<std::size_t>(i)].depth; },
      [&](int i) { return rows[static_cast<std::size_t>(i)].is_folder; });
}

void test_no_sticky_at_top() {
  const std::vector<Row> rows = {
      {0, true},
      {1, true},
      {2, false},
      {2, false},
  };
  const auto sticky = sticky_of(rows, 0);
  assert(sticky.empty());
}

void test_sticky_parent_when_scrolled_into_folder() {
  // src/
  //   a.cpp
  //   b.cpp
  const std::vector<Row> rows = {
      {0, true},
      {1, false},
      {1, false},
  };
  const auto sticky = sticky_of(rows, 2);
  assert(sticky.size() == 1);
  assert(sticky[0] == 0);
}

void test_nested_ancestors() {
  // src/
  //   ui/
  //     file_tree_panel.cpp
  //     z.cpp
  const std::vector<Row> rows = {
      {0, true},
      {1, true},
      {2, false},
      {2, false},
  };
  const auto sticky = sticky_of(rows, 3);
  assert(sticky.size() == 2);
  assert(sticky[0] == 0);
  assert(sticky[1] == 1);
}

void test_does_not_pick_sibling_file_as_parent() {
  // src/
  //   a.cpp
  //   ui/
  //     x.cpp
  const std::vector<Row> rows = {
      {0, true},
      {1, false},
      {1, true},
      {2, false},
  };
  const auto sticky = sticky_of(rows, 3);
  assert(sticky.size() == 2);
  assert(sticky[0] == 0);
  assert(sticky[1] == 2);
}

void test_max_sticky_keeps_innermost() {
  const std::vector<Row> rows = {
      {0, true},
      {1, true},
      {2, true},
      {3, false},
  };
  const auto sticky = sticky_of(rows, 3, 2);
  assert(sticky.size() == 2);
  assert(sticky[0] == 1);
  assert(sticky[1] == 2);
}

void test_out_of_range_and_empty() {
  assert(sticky_of({}, 0).empty());
  const std::vector<Row> rows = {{0, true}, {1, false}};
  assert(sticky_of(rows, 2).empty());
  assert(sticky_of(rows, 1, 0).empty());
}

void test_sticky_cap() {
  assert(explorer_sticky_cap(1) == 0);
  assert(explorer_sticky_cap(2) == 0);
  assert(explorer_sticky_cap(4) == 2);
  assert(explorer_sticky_cap(20) == kMaxExplorerStickyRows);
  assert(explorer_sticky_cap(20, 3) == 3);
}

}  // namespace
}  // namespace tuide

int main() {
  tuide::test_no_sticky_at_top();
  tuide::test_sticky_parent_when_scrolled_into_folder();
  tuide::test_nested_ancestors();
  tuide::test_does_not_pick_sibling_file_as_parent();
  tuide::test_max_sticky_keeps_innermost();
  tuide::test_out_of_range_and_empty();
  tuide::test_sticky_cap();
  std::cout << "file_tree_sticky_test: ok\n";
  return 0;
}
