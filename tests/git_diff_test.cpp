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

  std::cout << "git_diff_test ok\n";
  return 0;
}
