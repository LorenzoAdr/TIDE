#pragma once

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace tgdb {

enum class TabularDelimiter { kComma, kTab, kSemicolon };

bool is_tabular_path(const std::string& path);
TabularDelimiter detect_tabular_delimiter(const std::string& path,
                                          const std::vector<std::string>& lines);

struct TabularColumnLayout {
  int width = 0;
  ftxui::Color color;
};

struct TabularTableLayout {
  std::vector<TabularColumnLayout> columns;
  int total_columns = 0;
};

TabularTableLayout compute_tabular_layout(const std::vector<std::string>& lines,
                                          TabularDelimiter delimiter);

std::vector<std::string> parse_tabular_row(const std::string& line, TabularDelimiter delimiter);

ftxui::Element render_tabular_row(const std::vector<std::string>& cells,
                                  const TabularTableLayout& layout, bool header = false);

int tabular_row_width(const TabularTableLayout& layout);

int tabular_row_width_for_cells(const std::vector<std::string>& cells,
                                const TabularTableLayout& layout);

ftxui::Element render_tabular_row_viewport(const std::vector<std::string>& cells,
                                           const TabularTableLayout& layout, bool header,
                                           int scroll_col, int view_width);

}  // namespace tgdb
