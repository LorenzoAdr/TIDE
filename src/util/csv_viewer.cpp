#include "util/csv_viewer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>

#include "ui/theme.hpp"

namespace tuide {

namespace {

using namespace ftxui;

constexpr std::array<theme::ColorRgb, 12> kColumnColorRgb = {{
    theme::ColorRgb{156, 220, 254}, theme::ColorRgb{206, 145, 120}, theme::ColorRgb{181, 206, 168},
    theme::ColorRgb{220, 180, 255}, theme::ColorRgb{255, 200, 120}, theme::ColorRgb{120, 200, 200},
    theme::ColorRgb{255, 160, 160}, theme::ColorRgb{160, 200, 255}, theme::ColorRgb{200, 255, 160},
    theme::ColorRgb{255, 220, 160}, theme::ColorRgb{180, 180, 255}, theme::ColorRgb{255, 180, 220},
}};

constexpr int kColumnSeparatorWidth = 3;
constexpr const char* kColumnSeparator = " | ";

Color column_color(int index) {
  const theme::ColorRgb rgb =
      kColumnColorRgb[static_cast<std::size_t>(index) % kColumnColorRgb.size()];
  return Color::RGB(rgb.r, rgb.g, rgb.b);
}

int display_width(const std::string& text) {
  return static_cast<int>(text.size());
}

char delimiter_char(TabularDelimiter delimiter) {
  switch (delimiter) {
    case TabularDelimiter::kTab:
      return '\t';
    case TabularDelimiter::kSemicolon:
      return ';';
    case TabularDelimiter::kComma:
    default:
      return ',';
  }
}

int count_unquoted(const std::string& line, char delim) {
  bool in_quotes = false;
  int count = 1;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
        ++i;
        continue;
      }
      in_quotes = !in_quotes;
      continue;
    }
    if (!in_quotes && c == delim) {
      ++count;
    }
  }
  return count;
}

}  // namespace

bool is_tabular_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::string ext = std::filesystem::path(path).extension().string();
  for (char& ch : ext) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return ext == ".csv" || ext == ".tsv";
}

TabularDelimiter detect_tabular_delimiter(const std::string& path,
                                          const std::vector<std::string>& lines) {
  const std::string ext = std::filesystem::path(path).extension().string();
  if (ext == ".tsv") {
    return TabularDelimiter::kTab;
  }

  std::string sample;
  for (const std::string& line : lines) {
    if (!line.empty()) {
      sample = line;
      break;
    }
  }
  if (sample.empty()) {
    return TabularDelimiter::kComma;
  }

  const int commas = count_unquoted(sample, ',');
  const int semicolons = count_unquoted(sample, ';');
  const int tabs = count_unquoted(sample, '\t');
  if (tabs >= commas && tabs >= semicolons && tabs > 1) {
    return TabularDelimiter::kTab;
  }
  if (semicolons > commas) {
    return TabularDelimiter::kSemicolon;
  }
  return TabularDelimiter::kComma;
}

std::vector<std::string> parse_tabular_row(const std::string& line, TabularDelimiter delimiter) {
  std::vector<std::string> cells;
  const char delim = delimiter_char(delimiter);
  std::string current;
  bool in_quotes = false;

  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
        current.push_back('"');
        ++i;
        continue;
      }
      in_quotes = !in_quotes;
      continue;
    }
    if (!in_quotes && c == delim) {
      cells.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  cells.push_back(current);
  return cells;
}

TabularTableLayout compute_tabular_layout(const std::vector<std::string>& lines,
                                          TabularDelimiter delimiter) {
  TabularTableLayout layout;
  int max_columns = 0;

  for (const std::string& line : lines) {
    if (line.empty()) {
      continue;
    }
    const auto cells = parse_tabular_row(line, delimiter);
    max_columns = std::max(max_columns, static_cast<int>(cells.size()));
    if (static_cast<int>(layout.columns.size()) < max_columns) {
      layout.columns.resize(static_cast<std::size_t>(max_columns));
    }
    for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
      const int w = display_width(cells[static_cast<std::size_t>(i)]);
      layout.columns[static_cast<std::size_t>(i)].width =
          std::max(layout.columns[static_cast<std::size_t>(i)].width, w);
    }
  }

  layout.total_columns = max_columns;
  for (int i = 0; i < max_columns; ++i) {
    auto& col = layout.columns[static_cast<std::size_t>(i)];
    col.width = std::max(col.width, 1);
    col.color = column_color(i);
  }
  return layout;
}

Element render_tabular_row(const std::vector<std::string>& cells,
                           const TabularTableLayout& layout, bool header) {
  Elements parts;
  const int column_count = layout.total_columns;
  for (int i = 0; i < column_count; ++i) {
    std::string cell;
    if (i < static_cast<int>(cells.size())) {
      cell = cells[static_cast<std::size_t>(i)];
    }
    const int target =
        std::max(i < static_cast<int>(layout.columns.size())
                     ? layout.columns[static_cast<std::size_t>(i)].width
                     : 1,
                 static_cast<int>(cell.size()));
    if (static_cast<int>(cell.size()) < target) {
      cell.append(static_cast<std::size_t>(target - cell.size()), ' ');
    }
    Color cell_color = theme::UiText();
    if (i < static_cast<int>(layout.columns.size())) {
      cell_color = layout.columns[static_cast<std::size_t>(i)].color;
    }
    Element text_el = text(cell.empty() ? " " : cell) | color(cell_color);
    if (header) {
      text_el = text_el | bold;
    }
    parts.push_back(std::move(text_el));
    if (i + 1 < column_count) {
      parts.push_back(text(kColumnSeparator) | color(theme::Muted()));
    }
  }
  return hbox(std::move(parts));
}

int tabular_column_width(const TabularTableLayout& layout, int index) {
  if (index < static_cast<int>(layout.columns.size())) {
    return std::max(layout.columns[static_cast<std::size_t>(index)].width, 1);
  }
  return 1;
}

int tabular_row_width_for_cells(const std::vector<std::string>& cells,
                                const TabularTableLayout& layout) {
  if (layout.total_columns <= 0) {
    return 0;
  }
  int width = 0;
  for (int i = 0; i < layout.total_columns; ++i) {
    std::string cell;
    if (i < static_cast<int>(cells.size())) {
      cell = cells[static_cast<std::size_t>(i)];
    }
    width += tabular_column_width(layout, i);
    if (i + 1 < layout.total_columns) {
      width += kColumnSeparatorWidth;
    }
    if (static_cast<int>(cell.size()) > tabular_column_width(layout, i)) {
      width += static_cast<int>(cell.size()) - tabular_column_width(layout, i);
    }
  }
  return width;
}

int tabular_column_offset(const TabularTableLayout& layout, int index) {
  int offset = 0;
  for (int i = 0; i < index; ++i) {
    offset += tabular_column_width(layout, i);
    offset += kColumnSeparatorWidth;
  }
  return offset;
}

int tabular_row_width(const TabularTableLayout& layout) {
  if (layout.total_columns <= 0) {
    return 0;
  }
  int width = 0;
  for (int i = 0; i < layout.total_columns; ++i) {
    width += tabular_column_width(layout, i);
    if (i + 1 < layout.total_columns) {
      width += kColumnSeparatorWidth;
    }
  }
  return width;
}

std::string separator_viewport_slice(int start, int length) {
  if (length <= 0 || start >= kColumnSeparatorWidth) {
    return {};
  }
  const int end = std::min(kColumnSeparatorWidth, start + length);
  return std::string(kColumnSeparator + start, kColumnSeparator + end);
}

std::string cell_viewport_slice(const std::string& cell, int col_width, int start, int length) {
  if (length <= 0) {
    return {};
  }
  std::string padded = cell;
  if (static_cast<int>(padded.size()) > col_width) {
    padded = padded.substr(0, static_cast<std::size_t>(col_width));
  }
  if (static_cast<int>(padded.size()) < col_width) {
    padded.append(static_cast<std::size_t>(col_width - padded.size()), ' ');
  }
  if (start >= col_width) {
    return {};
  }
  const int end = std::min(col_width, start + length);
  return padded.substr(static_cast<std::size_t>(start),
                      static_cast<std::size_t>(end - start));
}

ftxui::Element render_tabular_row_viewport(const std::vector<std::string>& cells,
                                           const TabularTableLayout& layout, bool header,
                                           int scroll_col, int view_width) {
  if (view_width <= 0) {
    return text(" ");
  }
  const int viewport_end = scroll_col + view_width;
  const int column_count = layout.total_columns;
  Elements parts;

  for (int i = 0; i < column_count; ++i) {
    const int col_start = tabular_column_offset(layout, i);
    const int col_width = tabular_column_width(layout, i);
    const int col_end = col_start + col_width;
    if (col_end <= scroll_col || col_start >= viewport_end) {
      continue;
    }

    if (i > 0) {
      const int sep_start = col_start - kColumnSeparatorWidth;
      const int sep_visible_start = std::max(scroll_col, sep_start);
      const int sep_visible_end = std::min(viewport_end, col_start);
      if (sep_visible_end > sep_visible_start) {
        parts.push_back(text(separator_viewport_slice(sep_visible_start - sep_start,
                                                      sep_visible_end - sep_visible_start)) |
                        color(theme::Muted()));
      }
    }

    std::string cell;
    if (i < static_cast<int>(cells.size())) {
      cell = cells[static_cast<std::size_t>(i)];
    }
    const int visible_start = std::max(scroll_col, col_start);
    const int visible_end = std::min(viewport_end, col_end);
    const int slice_start = visible_start - col_start;
    const int slice_len = visible_end - visible_start;
    const std::string visible = cell_viewport_slice(cell, col_width, slice_start, slice_len);
    if (visible.empty()) {
      continue;
    }

    Color cell_color = theme::UiText();
    if (i < static_cast<int>(layout.columns.size())) {
      cell_color = layout.columns[static_cast<std::size_t>(i)].color;
    }
    if (header) {
      cell_color = theme::Accent();
    }
    parts.push_back(text(visible) | color(cell_color));
  }

  if (parts.empty()) {
    return text(" ");
  }
  return hbox(std::move(parts));
}

}  // namespace tuide
