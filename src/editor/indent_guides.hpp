#pragma once

#include <string>
#include <vector>

namespace tgdb {

class IndentGuideTracker {
 public:
  void reset();
  int advance(const std::string& line, int tab_size);

  int current_depth() const { return current_depth_; }

 private:
  int prev_indent_cols_ = 0;
  int current_depth_ = 0;
};

int leading_indent_columns(const std::string& line, int tab_size);

struct IndentGuideSplit {
  std::string guide_text;
  std::string suffix;
  int prefix_byte_length = 0;
  int prefix_visual_width = 0;
};

IndentGuideSplit split_indent_guide_prefix(const std::string& view_line, int tab_size,
                                           int guide_depth);

int indent_guide_depth_for_line(const std::vector<std::string>& lines, int line_index,
                                int tab_size);

// Columnas visuales de terminal (tabs expandidos) ↔ índice byte en la línea.
int visual_column_to_byte_index(const std::string& line, int visual_col, int tab_size);
int byte_index_to_visual_column(const std::string& line, int byte_index, int tab_size);

// FTXUI no expande tabs al pintar; convierte a espacios para el ancho visual correcto.
std::string expand_tabs_for_display(const std::string& line, int tab_size);

std::string build_blank_line_guides(int tab_size, int guide_depth, int max_width);

}  // namespace tgdb
