#include "editor/indent_guides.hpp"

#include <algorithm>

namespace tuide {

namespace {

constexpr char kGuideChar = '|';

bool is_whitespace_char(unsigned char c) { return c == ' ' || c == '\t'; }

bool is_blank_line(const std::string& line) {
  for (unsigned char c : line) {
    if (!is_whitespace_char(c)) {
      return false;
    }
  }
  return true;
}

bool is_guide_boundary_column(int col, int tab_size) {
  // Align with the parent's first column (start of each indent unit), not the
  // last column of the previous unit.
  return tab_size > 0 && col > 0 && (col % tab_size) == 0;
}

char guide_cell_at_column(int col, int tab_size, int guide_depth) {
  if (guide_depth <= 0 || !is_guide_boundary_column(col, tab_size)) {
    return ' ';
  }
  const int level = (col / tab_size) - 1;
  if (level < 0 || level >= guide_depth) {
    return ' ';
  }
  return kGuideChar;
}

}  // namespace

int leading_indent_columns(const std::string& line, int tab_size) {
  if (tab_size <= 0) {
    tab_size = 4;
  }
  int col = 0;
  for (unsigned char c : line) {
    if (c == ' ') {
      ++col;
    } else if (c == '\t') {
      col += tab_size - (col % tab_size);
    } else {
      break;
    }
  }
  return col;
}

int visual_column_to_byte_index(const std::string& line, int visual_col, int tab_size) {
  if (tab_size <= 0) {
    tab_size = 4;
  }
  if (visual_col <= 0) {
    return 0;
  }
  int disp = 0;
  int byte = 0;
  while (byte < static_cast<int>(line.size()) && disp < visual_col) {
    const unsigned char c = static_cast<unsigned char>(line[static_cast<std::size_t>(byte)]);
    if (c == '\t') {
      const int width = tab_size - (disp % tab_size);
      if (disp + width > visual_col) {
        return byte + 1;
      }
      disp += width;
      ++byte;
    } else {
      ++disp;
      ++byte;
    }
  }
  return byte;
}

int byte_index_to_visual_column(const std::string& line, int byte_index, int tab_size) {
  if (tab_size <= 0) {
    tab_size = 4;
  }
  if (byte_index <= 0) {
    return 0;
  }
  const int limit = std::min(byte_index, static_cast<int>(line.size()));
  int disp = 0;
  for (int byte = 0; byte < limit; ++byte) {
    const unsigned char c = static_cast<unsigned char>(line[static_cast<std::size_t>(byte)]);
    if (c == '\t') {
      disp += tab_size - (disp % tab_size);
    } else {
      ++disp;
    }
  }
  return disp;
}

int source_byte_at_display_column(const std::string& line, int body_source_byte,
                                  int fragment_display_col, int tab_size) {
  const int base_vis =
      byte_index_to_visual_column(line, std::max(0, body_source_byte), tab_size);
  return visual_column_to_byte_index(line, base_vis + fragment_display_col, tab_size);
}

void IndentGuideTracker::reset() {
  prev_indent_cols_ = 0;
  current_depth_ = 0;
}

int IndentGuideTracker::advance(const std::string& line, int tab_size) {
  if (tab_size <= 0) {
    tab_size = 4;
  }
  const int indent_cols = leading_indent_columns(line, tab_size);
  int indent_levels = 0;
  if (is_blank_line(line)) {
    indent_levels = prev_indent_cols_ / tab_size;
  } else {
    indent_levels = indent_cols > 0 ? (indent_cols + tab_size - 1) / tab_size : 0;
    prev_indent_cols_ = indent_cols;
  }
  // Draw parent indent stops only — not the stop immediately before the text.
  current_depth_ = indent_levels > 0 ? indent_levels - 1 : 0;
  return current_depth_;
}

std::string expand_tabs_for_display(const std::string& line, int tab_size) {
  if (tab_size <= 0) {
    tab_size = 4;
  }
  if (line.find('\t') == std::string::npos) {
    return line;
  }

  std::string out;
  out.reserve(line.size());
  int col = 0;
  for (unsigned char c : line) {
    if (c == '\t') {
      const int width = tab_size - (col % tab_size);
      out.append(static_cast<std::size_t>(width), ' ');
      col += width;
    } else {
      out.push_back(static_cast<char>(c));
      ++col;
    }
  }
  return out;
}

IndentGuideSplit split_indent_guide_prefix(const std::string& view_line, int tab_size,
                                           int guide_depth) {
  IndentGuideSplit split;
  if (view_line.empty() || guide_depth <= 0 || tab_size <= 0) {
    split.suffix = view_line;
    return split;
  }

  int col = 0;
  std::size_t byte_index = 0;
  while (byte_index < view_line.size()) {
    const unsigned char c = static_cast<unsigned char>(view_line[byte_index]);
    if (!is_whitespace_char(c)) {
      break;
    }

    if (c == ' ') {
      split.guide_text += guide_cell_at_column(col, tab_size, guide_depth);
      ++col;
      ++byte_index;
    } else {
      const int width = tab_size - (col % tab_size);
      for (int i = 0; i < width; ++i) {
        split.guide_text += guide_cell_at_column(col, tab_size, guide_depth);
        ++col;
      }
      ++byte_index;
    }
  }

  split.prefix_visual_width = col;
  split.prefix_byte_length = static_cast<int>(byte_index);
  if (byte_index < view_line.size()) {
    split.suffix = view_line.substr(byte_index);
  }
  return split;
}

std::string build_blank_line_guides(int tab_size, int guide_depth, int max_width) {
  if (guide_depth <= 0 || tab_size <= 0) {
    return {};
  }
  const int target = guide_depth * tab_size;
  const int width = max_width > 0 ? std::min(target, max_width) : target;
  std::string out;
  out.reserve(static_cast<std::size_t>(width));
  for (int col = 0; col < width; ++col) {
    out += guide_cell_at_column(col, tab_size, guide_depth);
  }
  return out;
}

int indent_guide_depth_for_line(const std::vector<std::string>& lines, int line_index,
                                int tab_size) {
  IndentGuideTracker tracker;
  if (line_index < 0) {
    return 0;
  }
  const int last = std::min(line_index, static_cast<int>(lines.size()) - 1);
  for (int i = 0; i <= last; ++i) {
    tracker.advance(lines[static_cast<std::size_t>(i)], tab_size);
  }
  return tracker.current_depth();
}

}  // namespace tuide