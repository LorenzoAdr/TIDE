#include "git/git_diff.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> lines(std::initializer_list<const char*> items) {
  return std::vector<std::string>(items.begin(), items.end());
}

void expect_changed(const tuide::LineDiffResult& result,
                    std::initializer_list<int> expected_lines) {
  if (result.changed_new_lines.size() != expected_lines.size()) {
    std::cerr << "expected " << expected_lines.size() << " changed lines, got "
              << result.changed_new_lines.size() << '\n';
    std::abort();
  }
  for (int line : expected_lines) {
    if (result.changed_new_lines.count(line) == 0) {
      std::cerr << "missing changed line " << line << '\n';
      std::abort();
    }
  }
}

}  // namespace

int main() {
  {
    const auto head = lines({"a", "b", "c"});
    const auto current = lines({"a", "c"});
    const auto result = tuide::compute_line_diff(head, current);
    expect_changed(result, {});
  }

  {
    const auto head = lines({"a", "b", "c"});
    const auto current = lines({"a", "x", "c"});
    const auto result = tuide::compute_line_diff(head, current);
    expect_changed(result, {1});
    assert(result.previous_content_by_new_line.at(1) == "b");
  }

  {
    const auto head = lines({"a", "b"});
    const auto current = lines({"a", "x", "b"});
    const auto result = tuide::compute_line_diff(head, current);
    expect_changed(result, {1});
  }

  {
    const auto head = lines({"a", "b"});
    const auto current = lines({"a", "b", "c"});
    const auto result = tuide::compute_line_diff(head, current);
    expect_changed(result, {2});
  }

  {
    // Multi-line insert must not desync and mark the rest of the file.
    const auto head = lines({"keep0", "keep1", "keep2", "keep3", "keep4"});
    const auto current = lines({"newA", "newB", "keep0", "keep1", "keep2", "keep3", "keep4"});
    const auto result = tuide::compute_line_diff(head, current);
    expect_changed(result, {0, 1});
  }

  {
    const auto head = lines({"a", "b", "c", "d", "e"});
    const auto current = lines({"a", "X", "Y", "Z", "e"});
    const auto result = tuide::compute_line_diff(head, current);
    expect_changed(result, {1, 2, 3});
    assert(result.previous_content_by_new_line.at(1) == "b");
    assert(result.previous_content_by_new_line.at(2) == "c");
    assert(result.previous_content_by_new_line.at(3) == "d");
  }

  {
    const std::string diff = R"(diff --git a/foo.cpp b/foo.cpp
--- a/foo.cpp
+++ b/foo.cpp
@@ -2,3 +2,4 @@
 keep
-old
+new
+added
)";
    const auto parsed = tuide::parse_unified_diff("foo.cpp", diff);
    assert(parsed.line_changes.count(2) > 0);
    assert(parsed.line_changes.count(3) > 0);
    assert(parsed.previous_content_by_line.at(2) == "old");
  }

  {
    const auto head = lines({"a", "b", "c"});
    const auto current = lines({"a", "x", "c"});
    const auto map = tuide::build_git_line_map(head, current);
    assert(map.working_to_head.size() == 3);
    assert(map.working_to_head[1] == 1);
    assert(map.head_to_working[1] == 1);
    assert(tuide::map_git_scroll_line(map.working_to_head, 1) == 1);
  }

  {
    const std::string diff = R"(diff --git a/foo.cpp b/foo.cpp
--- a/foo.cpp
+++ b/foo.cpp
@@ -1,3 +1,4 @@
 keep
-old
+new
+added
 tail
)";
    const auto head = lines({"keep", "old", "tail"});
    const auto working = lines({"keep", "new", "added", "tail"});
    const auto rows = tuide::build_side_by_side_rows(diff, head, working);
    assert(rows.size() == 4);
    assert(rows[0].kind == tuide::SideBySideRowKind::kContext);
    assert(rows[1].kind == tuide::SideBySideRowKind::kModification);
    assert(rows[1].left == "old");
    assert(rows[1].right == "new");
    assert(rows[2].kind == tuide::SideBySideRowKind::kAddition);
    assert(rows[2].right == "added");
    assert(rows[3].kind == tuide::SideBySideRowKind::kContext);
  }

  {
    const auto head = lines({});
    const auto working = lines({"only", "new"});
    const auto rows = tuide::build_side_by_side_rows("", head, working);
    assert(rows.size() == 2);
    assert(rows[0].kind == tuide::SideBySideRowKind::kAddition);
    assert(rows[0].right == "only");
    assert(rows[1].right == "new");
  }

  {
    const auto head = lines({"gone", "file"});
    const auto working = lines({});
    const auto rows = tuide::build_side_by_side_rows("", head, working);
    assert(rows.size() == 2);
    assert(rows[0].kind == tuide::SideBySideRowKind::kDeletion);
    assert(rows[0].left == "gone");
  }

  {
    const std::vector<tuide::SideBySideDiffRow> rows = {
        {tuide::SideBySideRowKind::kContext, "a", "a", 1, 1},
        {tuide::SideBySideRowKind::kAddition, "", "b", 0, 2},
        {tuide::SideBySideRowKind::kModification, "c", "d", 3, 3},
        {tuide::SideBySideRowKind::kDeletion, "e", "", 4, 0},
    };
    const auto marks = tuide::build_diff_overview_lines(rows);
    assert(marks.add_lines.count(1) > 0);
    assert(marks.change_lines.count(2) > 0);
    assert(marks.change_lines.count(3) > 0);
    assert(marks.add_lines.count(0) == 0);
  }

  {
    const auto head = lines({"keep", "old", "tail"});
    auto working = lines({"keep", "new", "added", "tail"});
    const auto rows = tuide::build_side_by_side_rows_from_lines(head, working);
    assert(rows.size() == 4);
    const auto blocks = tuide::build_diff_change_blocks(rows);
    assert(blocks.size() == 1);
    assert(tuide::revert_diff_change_block(&working, head, rows, 0));
    assert(working == head);
  }

  {
    const auto head = lines({"a", "b", "c"});
    auto working = lines({"a", "x", "c"});
    const auto rows = tuide::build_side_by_side_rows_from_lines(head, working);
    const auto blocks = tuide::build_diff_change_blocks(rows);
    assert(blocks.size() == 1);
    assert(tuide::revert_diff_change_block(&working, head, rows, 0));
    assert(working == head);
  }

  {
    const auto head = lines({"gone", "file"});
    auto working = std::vector<std::string>{};
    const auto rows = tuide::build_side_by_side_rows_from_lines(head, working);
    const auto blocks = tuide::build_diff_change_blocks(rows);
    assert(blocks.size() == 1);
    assert(tuide::revert_diff_change_block(&working, head, rows, 0));
    assert(working == head);
  }

  std::cout << "git_diff_test ok\n";
  return 0;
}
