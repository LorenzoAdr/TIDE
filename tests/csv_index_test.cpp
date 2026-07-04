#include "editor/code_snippets.hpp"
#include "indexer/index_rules.hpp"
#include "util/csv_viewer.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  tgdb::IndexFilterOptions hidden;
  hidden.show_all_files = false;
  expect(tgdb::should_skip_dir_name(".git", hidden), "hidden dirs skipped by default");
  expect(!tgdb::should_list_workspace_path(".git/config", hidden), "hidden paths excluded");

  tgdb::IndexFilterOptions show_all;
  show_all.show_all_files = true;
  expect(!tgdb::should_skip_dir_name(".git", show_all), "show all keeps dot dirs");
  expect(tgdb::should_list_workspace_path(".git/config", show_all), "show all includes dot paths");
  expect(tgdb::should_list_workspace_path("build/obj.o", show_all), "show all includes build dirs");

  const std::vector<std::string> csv_lines = {
      "name,age,city",
      "\"Lopez, Ana\",30,Madrid",
      "Bob,25,\"Valencia, ES\"",
  };
  const auto cells = tgdb::parse_tabular_row(csv_lines[1], tgdb::TabularDelimiter::kComma);
  expect(cells.size() == 3, "csv row parses three columns");
  expect(cells[0] == "Lopez, Ana", "csv quoted comma preserved");

  const auto layout =
      tgdb::compute_tabular_layout(csv_lines, tgdb::TabularDelimiter::kComma);
  expect(layout.total_columns == 3, "layout detects three columns");
  expect(layout.columns[0].width >= static_cast<int>(std::string("Lopez, Ana").size()),
         "layout width fits quoted cell");

  const std::vector<std::string> tsv_lines = {"a\tb\tc", "1\t22\t333"};
  const auto tsv_layout =
      tgdb::compute_tabular_layout(tsv_lines, tgdb::TabularDelimiter::kTab);
  expect(tsv_layout.total_columns == 3, "tsv layout has three columns");
  expect(tsv_layout.columns[2].width == 3, "tsv column width aligned");

  expect(tgdb::is_tabular_path("data.csv"), "csv path detected");
  expect(tgdb::is_tabular_path("data.tsv"), "tsv path detected");
  expect(!tgdb::is_tabular_path("main.cpp"), "cpp path not tabular");

  const auto snippets = tgdb::structure_snippet_completions("str");
  expect(!snippets.empty(), "struct snippet offered for str prefix");
  expect(tgdb::structure_snippet_prefix_active("str"), "struct prefix active");

  std::cout << "csv_index_test: OK\n";
  return 0;
}
