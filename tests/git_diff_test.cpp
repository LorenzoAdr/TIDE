#include "git/git_diff.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> lines(std::initializer_list<const char*> items) {
  return std::vector<std::string>(items.begin(), items.end());
}

void expect_changed(const tgdb::LineDiffResult& result,
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
    const auto result = tgdb::compute_line_diff(head, current);
    expect_changed(result, {});
  }

  {
    const auto head = lines({"a", "b", "c"});
    const auto current = lines({"a", "x", "c"});
    const auto result = tgdb::compute_line_diff(head, current);
    expect_changed(result, {1});
    assert(result.previous_content_by_new_line.at(1) == "b");
  }

  {
    const auto head = lines({"a", "b"});
    const auto current = lines({"a", "x", "b"});
    const auto result = tgdb::compute_line_diff(head, current);
    expect_changed(result, {1});
  }

  {
    const auto head = lines({"a", "b"});
    const auto current = lines({"a", "b", "c"});
    const auto result = tgdb::compute_line_diff(head, current);
    expect_changed(result, {2});
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
    const auto parsed = tgdb::parse_unified_diff("foo.cpp", diff);
    assert(parsed.line_changes.count(2) > 0);
    assert(parsed.line_changes.count(3) > 0);
    assert(parsed.previous_content_by_line.at(2) == "old");
  }

  {
    const auto head = lines({"a", "b", "c"});
    const auto current = lines({"a", "x", "c"});
    const auto map = tgdb::build_git_line_map(head, current);
    assert(map.working_to_head.size() == 3);
    assert(map.working_to_head[1] == 1);
    assert(map.head_to_working[1] == 1);
    assert(tgdb::map_git_scroll_line(map.working_to_head, 1) == 1);
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
    const auto rows = tgdb::build_side_by_side_rows(diff, head, working);
    assert(rows.size() == 4);
    assert(rows[0].kind == tgdb::SideBySideRowKind::kContext);
    assert(rows[1].kind == tgdb::SideBySideRowKind::kModification);
    assert(rows[1].left == "old");
    assert(rows[1].right == "new");
    assert(rows[2].kind == tgdb::SideBySideRowKind::kAddition);
    assert(rows[2].right == "added");
    assert(rows[3].kind == tgdb::SideBySideRowKind::kContext);
  }

  {
    const auto head = lines({});
    const auto working = lines({"only", "new"});
    const auto rows = tgdb::build_side_by_side_rows("", head, working);
    assert(rows.size() == 2);
    assert(rows[0].kind == tgdb::SideBySideRowKind::kAddition);
    assert(rows[0].right == "only");
    assert(rows[1].right == "new");
  }

  {
    const auto head = lines({"gone", "file"});
    const auto working = lines({});
    const auto rows = tgdb::build_side_by_side_rows("", head, working);
    assert(rows.size() == 2);
    assert(rows[0].kind == tgdb::SideBySideRowKind::kDeletion);
    assert(rows[0].left == "gone");
  }

  {
    const std::vector<tgdb::SideBySideDiffRow> rows = {
        {tgdb::SideBySideRowKind::kContext, "a", "a", 1, 1},
        {tgdb::SideBySideRowKind::kAddition, "", "b", 0, 2},
        {tgdb::SideBySideRowKind::kModification, "c", "d", 3, 3},
        {tgdb::SideBySideRowKind::kDeletion, "e", "", 4, 0},
    };
    const auto marks = tgdb::build_diff_overview_lines(rows);
    assert(marks.add_lines.count(1) > 0);
    assert(marks.change_lines.count(2) > 0);
    assert(marks.change_lines.count(3) > 0);
    assert(marks.add_lines.count(0) == 0);
  }

  std::cout << "git_diff_test ok\n";
  return 0;
}
