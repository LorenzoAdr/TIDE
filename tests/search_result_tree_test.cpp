#include "ui/search_result_tree.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace tuide {
namespace {

WorkspaceSearchResult hit(const std::string& file, int line) {
  return {file, line, 1, "preview"};
}

void test_empty() {
  const auto rows = flatten_search_results({}, {});
  assert(rows.empty());
  assert(!search_all_files_collapsed({}, {}));
}

void test_groups_matches_under_file() {
  const std::vector<WorkspaceSearchResult> results = {
      hit("src/main.cpp", 12),
      hit("src/main.cpp", 45),
      hit("src/ui/search_panel.cpp", 8),
  };
  const auto rows = flatten_search_results(results, {});
  assert(rows.size() == 5);
  assert(rows[0].kind == SearchRowKind::File);
  assert(rows[0].file == "src/main.cpp");
  assert(rows[0].match_count == 2);
  assert(rows[0].depth == 0);
  assert(rows[1].kind == SearchRowKind::Match);
  assert(rows[1].result_index == 0);
  assert(rows[1].depth == 1);
  assert(rows[2].kind == SearchRowKind::Match);
  assert(rows[2].result_index == 1);
  assert(rows[3].kind == SearchRowKind::File);
  assert(rows[3].file == "src/ui/search_panel.cpp");
  assert(rows[3].match_count == 1);
  assert(rows[4].kind == SearchRowKind::Match);
  assert(rows[4].result_index == 2);
}

void test_interleaved_files_merge() {
  const std::vector<WorkspaceSearchResult> results = {
      hit("a.cpp", 1),
      hit("b.cpp", 2),
      hit("a.cpp", 3),
  };
  const auto rows = flatten_search_results(results, {});
  assert(rows.size() == 5);
  assert(rows[0].file == "a.cpp" && rows[0].kind == SearchRowKind::File);
  assert(rows[0].match_count == 2);
  assert(rows[1].result_index == 0);
  assert(rows[2].result_index == 2);
  assert(rows[3].file == "b.cpp" && rows[3].kind == SearchRowKind::File);
  assert(rows[4].result_index == 1);
}

void test_collapsed_file_hides_matches() {
  const std::vector<WorkspaceSearchResult> results = {
      hit("a.cpp", 1),
      hit("a.cpp", 2),
      hit("b.cpp", 3),
  };
  const std::unordered_set<std::string> collapsed{"a.cpp"};
  const auto rows = flatten_search_results(results, collapsed);
  assert(rows.size() == 3);
  assert(rows[0].kind == SearchRowKind::File && rows[0].file == "a.cpp");
  assert(rows[1].kind == SearchRowKind::File && rows[1].file == "b.cpp");
  assert(rows[2].kind == SearchRowKind::Match);
  assert(search_file_row_index(rows, "b.cpp") == 1);
  assert(!search_all_files_collapsed(results, collapsed));
}

void test_collapse_all() {
  const std::vector<WorkspaceSearchResult> results = {
      hit("a.cpp", 1),
      hit("b.cpp", 2),
  };
  const std::unordered_set<std::string> collapsed{"a.cpp", "b.cpp"};
  const auto rows = flatten_search_results(results, collapsed);
  assert(rows.size() == 2);
  assert(rows[0].kind == SearchRowKind::File);
  assert(rows[1].kind == SearchRowKind::File);
  assert(search_all_files_collapsed(results, collapsed));
}

}  // namespace
}  // namespace tuide

int main() {
  tuide::test_empty();
  tuide::test_groups_matches_under_file();
  tuide::test_interleaved_files_merge();
  tuide::test_collapsed_file_hides_matches();
  tuide::test_collapse_all();
  std::cout << "search_result_tree_test: ok\n";
  return 0;
}
