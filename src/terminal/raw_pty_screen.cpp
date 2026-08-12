#include "terminal/raw_pty_screen.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace tuide {

namespace {

const ftxui::Color kDefaultFg = ftxui::Color::RGB(200, 230, 255);
const ftxui::Color kDefaultBg = ftxui::Color::RGB(0, 0, 0);

ftxui::Color ansi_base_color(int index, bool bright) {
  static const ftxui::Color palette[8] = {
      ftxui::Color::RGB(80, 80, 80),    ftxui::Color::RGB(220, 80, 80),
      ftxui::Color::RGB(80, 200, 80),   ftxui::Color::RGB(220, 200, 80),
      ftxui::Color::RGB(80, 120, 220),  ftxui::Color::RGB(200, 80, 200),
      ftxui::Color::RGB(80, 200, 200),  ftxui::Color::RGB(220, 220, 220),
  };
  static const ftxui::Color bright_palette[8] = {
      ftxui::Color::RGB(140, 140, 140), ftxui::Color::RGB(255, 120, 120),
      ftxui::Color::RGB(120, 255, 120), ftxui::Color::RGB(255, 255, 120),
      ftxui::Color::RGB(120, 180, 255), ftxui::Color::RGB(255, 120, 255),
      ftxui::Color::RGB(120, 255, 255), ftxui::Color::RGB(255, 255, 255),
  };
  index = std::max(0, std::min(index, 7));
  return bright ? bright_palette[index] : palette[index];
}

// xterm 256-color palette: 0-15 ANSI, 16-231 6x6x6 cube, 232-255 grayscale.
ftxui::Color ansi_256_color(int index) {
  index = std::max(0, std::min(index, 255));
  if (index < 8) {
    return ansi_base_color(index, false);
  }
  if (index < 16) {
    return ansi_base_color(index - 8, true);
  }
  if (index < 232) {
    const int cube = index - 16;
    const int r = cube / 36;
    const int g = (cube / 6) % 6;
    const int b = cube % 6;
    auto level = [](int c) { return c == 0 ? 0 : 55 + c * 40; };
    return ftxui::Color::RGB(level(r), level(g), level(b));
  }
  const int gray = 8 + (index - 232) * 10;
  return ftxui::Color::RGB(gray, gray, gray);
}

int clamp_byte(int value) { return std::max(0, std::min(value, 255)); }

int parse_first_param(const std::string& params, int default_value) {
  if (params.empty()) {
    return default_value;
  }
  char* end = nullptr;
  const long value = std::strtol(params.c_str(), &end, 10);
  if (end == params.c_str()) {
    return default_value;
  }
  return static_cast<int>(value);
}

std::vector<int> parse_sgr_params(const std::string& params) {
  std::vector<int> codes;
  std::size_t start = 0;
  while (start <= params.size()) {
    const std::size_t sep = params.find(';', start);
    const std::string token =
        params.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
    // Empty tokens (e.g. ";;") are treated as 0, matching typical SGR parsers.
    codes.push_back(parse_first_param(token, 0));
    if (sep == std::string::npos) {
      break;
    }
    start = sep + 1;
  }
  return codes;
}

std::string trim_trailing_spaces(std::string line) {
  while (!line.empty() && line.back() == ' ') {
    line.pop_back();
  }
  return line;
}

}  // namespace

RawPtyScreen::RawPtyScreen(int rows, int cols) {
  reset_sgr();
  reset(rows, cols);
}

void RawPtyScreen::reset_sgr() {
  bold_ = false;
  fg_ = kDefaultFg;
  bg_ = kDefaultBg;
  bg_default_ = true;
}

void RawPtyScreen::reset(int rows, int cols) {
  rows_ = std::max(1, rows);
  cols_ = std::max(1, cols);
  lines_.clear();
  current_cells_.assign(static_cast<std::size_t>(cols_), ScreenCell{});
  cursor_col_ = 0;
  reset_sgr();
  cache_valid_ = false;
}

void RawPtyScreen::resize(int rows, int cols) {
  rows = std::max(1, rows);
  cols = std::max(1, cols);
  if (rows == rows_ && cols == cols_) {
    return;
  }
  rows_ = rows;
  cols_ = cols;
  current_cells_.assign(static_cast<std::size_t>(cols_), ScreenCell{});
  if (cursor_col_ > cols_) {
    cursor_col_ = cols_;
  }
  for (TerminalStyledRow& line : lines_) {
    for (TerminalStyledSpan& span : line) {
      if (static_cast<int>(span.text.size()) > cols_) {
        span.text.resize(static_cast<std::size_t>(cols_));
      }
    }
  }
  trim_lines();
  cache_valid_ = false;
}

void RawPtyScreen::ensure_current_width() {
  if (static_cast<int>(current_cells_.size()) < cols_) {
    current_cells_.resize(static_cast<std::size_t>(cols_), ScreenCell{});
  }
}

void RawPtyScreen::apply_sgr(const std::string& params) {
  if (params.empty()) {
    reset_sgr();
    return;
  }

  const std::vector<int> codes = parse_sgr_params(params);
  for (std::size_t i = 0; i < codes.size(); ++i) {
    const int code = codes[i];
    if (code == 0) {
      reset_sgr();
    } else if (code == 1) {
      bold_ = true;
    } else if (code == 22) {
      bold_ = false;
    } else if (code >= 30 && code <= 37) {
      fg_ = ansi_base_color(code - 30, bold_);
    } else if (code >= 90 && code <= 97) {
      fg_ = ansi_base_color(code - 90, true);
    } else if (code == 39) {
      fg_ = kDefaultFg;
    } else if (code >= 40 && code <= 47) {
      bg_ = ansi_base_color(code - 40, bold_);
      bg_default_ = false;
    } else if (code >= 100 && code <= 107) {
      bg_ = ansi_base_color(code - 100, true);
      bg_default_ = false;
    } else if (code == 49) {
      bg_ = kDefaultBg;
      bg_default_ = true;
    } else if (code == 38 || code == 48) {
      // Extended colours: 38/48 ; 5 ; n  or  38/48 ; 2 ; r ; g ; b
      const bool is_fg = (code == 38);
      if (i + 1 >= codes.size()) {
        break;
      }
      const int mode = codes[++i];
      if (mode == 5 && i + 1 < codes.size()) {
        const ftxui::Color c = ansi_256_color(codes[++i]);
        if (is_fg) {
          fg_ = c;
        } else {
          bg_ = c;
          bg_default_ = false;
        }
      } else if (mode == 2 && i + 3 < codes.size()) {
        const ftxui::Color c = ftxui::Color::RGB(
            clamp_byte(codes[i + 1]), clamp_byte(codes[i + 2]), clamp_byte(codes[i + 3]));
        i += 3;
        if (is_fg) {
          fg_ = c;
        } else {
          bg_ = c;
          bg_default_ = false;
        }
      }
    }
  }
}

void RawPtyScreen::handle_csi(char cmd, const std::string& params) {
  if (cmd == 'm') {
    apply_sgr(params);
    return;
  }
  if (cmd == 'K') {
    if (params.empty() || params == "0") {
      clear_to_end_of_line();
    } else if (params == "2") {
      clear_current_line();
    }
    return;
  }
  if (cmd == 'G' || cmd == 'f') {
    const int col = parse_first_param(params, 1);
    cursor_col_ = std::min(cols_ - 1, std::max(0, col - 1));
    return;
  }
  if (cmd == 'C') {
    const int count = parse_first_param(params, 1);
    cursor_col_ = std::min(cols_, cursor_col_ + std::max(1, count));
    return;
  }
  if (cmd == 'D') {
    const int count = parse_first_param(params, 1);
    cursor_col_ = std::max(0, cursor_col_ - std::max(1, count));
  }
}

void RawPtyScreen::skip_escape(const char*& p, const char* end) {
  if (p >= end || *p != '\x1b') {
    return;
  }
  ++p;
  if (p >= end) {
    return;
  }
  if (*p == '[') {
    ++p;
    const char* params_start = p;
    while (p < end && (*p < 0x40 || *p > 0x7e)) {
      ++p;
    }
    if (p >= end) {
      return;
    }
    const char cmd = *p++;
    handle_csi(cmd, std::string(params_start, static_cast<std::size_t>(p - 1 - params_start)));
    return;
  }
  if (*p == ']') {
    ++p;
    while (p < end && *p != '\x07') {
      if (*p == '\x1b' && p + 1 < end && p[1] == '\\') {
        p += 2;
        return;
      }
      ++p;
    }
    if (p < end) {
      ++p;
    }
    return;
  }
  if (*p == '(' || *p == ')' || *p == '*' || *p == ' ' || *p == '#') {
    ++p;
    if (p < end) {
      ++p;
    }
    return;
  }
  ++p;
}

void RawPtyScreen::append_char(char ch) {
  // Soft-wrap: long lines continue on the next row instead of being truncated.
  if (cursor_col_ >= cols_) {
    newline();
  }
  ensure_current_width();
  ScreenCell& cell = current_cells_[static_cast<std::size_t>(cursor_col_)];
  cell.ch = ch;
  cell.fg = fg_;
  cell.bg = bg_;
  cell.bg_default = bg_default_;
  ++cursor_col_;
  cache_valid_ = false;
}

TerminalStyledRow RawPtyScreen::spans_from_cells(const std::vector<ScreenCell>& cells,
                                                 int preserve_cols) const {
  TerminalStyledRow spans;
  if (cells.empty()) {
    spans.push_back(TerminalStyledSpan{});
    return spans;
  }

  // Trim padding spaces past the preserved cursor column so typed trailing
  // spaces (e.g. before the next argument) stay visible and move the caret.
  const int min_end = std::max(0, std::min(preserve_cols, static_cast<int>(cells.size())));
  int end = static_cast<int>(cells.size());
  while (end > min_end && cells[static_cast<std::size_t>(end - 1)].ch == ' ') {
    --end;
  }
  if (end <= 0) {
    spans.push_back(TerminalStyledSpan{});
    return spans;
  }

  TerminalStyledSpan current;
  current.fg = cells[0].fg;
  current.bg = cells[0].bg;
  current.bg_default = cells[0].bg_default;
  for (int col = 0; col < end; ++col) {
    const ScreenCell& cell = cells[static_cast<std::size_t>(col)];
    if (!current.text.empty() &&
        (cell.fg != current.fg || cell.bg != current.bg ||
         cell.bg_default != current.bg_default)) {
      spans.push_back(current);
      current = TerminalStyledSpan{};
      current.fg = cell.fg;
      current.bg = cell.bg;
      current.bg_default = cell.bg_default;
    } else if (current.text.empty()) {
      current.fg = cell.fg;
      current.bg = cell.bg;
      current.bg_default = cell.bg_default;
    }
    current.text.push_back(cell.ch);
  }
  if (!current.text.empty()) {
    spans.push_back(std::move(current));
  }
  if (spans.empty()) {
    spans.push_back(TerminalStyledSpan{});
  }
  return spans;
}

void RawPtyScreen::newline() {
  lines_.push_back(spans_from_cells(current_cells_, cursor_col_));
  current_cells_.assign(static_cast<std::size_t>(cols_), ScreenCell{});
  cursor_col_ = 0;
  trim_lines();
  cache_valid_ = false;
}

void RawPtyScreen::carriage_return() { cursor_col_ = 0; }

void RawPtyScreen::backspace() {
  if (cursor_col_ > 0) {
    --cursor_col_;
    cache_valid_ = false;
  }
}

void RawPtyScreen::clear_current_line() {
  current_cells_.assign(static_cast<std::size_t>(cols_), ScreenCell{});
  cursor_col_ = 0;
  cache_valid_ = false;
}

void RawPtyScreen::clear_to_end_of_line() {
  ensure_current_width();
  for (int col = cursor_col_; col < cols_; ++col) {
    current_cells_[static_cast<std::size_t>(col)] = ScreenCell{};
  }
  cache_valid_ = false;
}

constexpr int kMaxScrollbackLines = 10000;

void RawPtyScreen::trim_lines() {
  if (static_cast<int>(lines_.size()) > kMaxScrollbackLines) {
    lines_.erase(lines_.begin(),
                 lines_.end() - static_cast<std::ptrdiff_t>(kMaxScrollbackLines));
  }
}

void RawPtyScreen::feed(const char* data, std::size_t len) {
  if (data == nullptr || len == 0) {
    return;
  }
  const char* p = data;
  const char* end = data + len;
  while (p < end) {
    if (*p == '\x1b') {
      skip_escape(p, end);
      continue;
    }
    const char ch = *p++;
    switch (ch) {
      case '\r':
        carriage_return();
        break;
      case '\n':
        newline();
        break;
      case '\b':
      case '\x7f':
        backspace();
        break;
      case '\t':
        append_char(' ');
        while (cursor_col_ % 8 != 0 && cursor_col_ < cols_) {
          append_char(' ');
        }
        break;
      case '\a':
        break;
      default:
        if (static_cast<unsigned char>(ch) >= 0x20) {
          append_char(ch);
        }
        break;
    }
  }
  cache_valid_ = false;
}

std::vector<TerminalStyledRow> RawPtyScreen::build_all_rows() const {
  std::vector<TerminalStyledRow> all;
  all.reserve(lines_.size() + 1);
  all.insert(all.end(), lines_.begin(), lines_.end());
  all.push_back(spans_from_cells(current_cells_, cursor_col_));
  return all;
}

std::vector<TerminalStyledRow> RawPtyScreen::build_visible_rows() const {
  std::vector<TerminalStyledRow> visible;
  visible.reserve(static_cast<std::size_t>(rows_));
  const int static_lines = std::max(0, rows_ - 1);
  if (static_cast<int>(lines_.size()) > static_lines) {
    visible.insert(visible.end(), lines_.end() - static_lines, lines_.end());
  } else {
    visible.insert(visible.end(), lines_.begin(), lines_.end());
  }

  visible.push_back(spans_from_cells(current_cells_, cursor_col_));

  while (static_cast<int>(visible.size()) < rows_) {
    visible.insert(visible.begin(), TerminalStyledRow{TerminalStyledSpan{}});
  }
  if (static_cast<int>(visible.size()) > rows_) {
    visible.erase(visible.begin(),
                  visible.end() - static_cast<std::ptrdiff_t>(rows_));
  }
  return visible;
}

void RawPtyScreen::rebuild_cache() const {
  cached_styled_rows_ = build_all_rows();

  const std::vector<TerminalStyledRow> visible = build_visible_rows();
  std::string result;
  result.reserve(static_cast<std::size_t>(rows_) * static_cast<std::size_t>(cols_ + 1));
  for (int row = 0; row < rows_; ++row) {
    std::string line;
    for (const TerminalStyledSpan& span : visible[static_cast<std::size_t>(row)]) {
      line += span.text;
    }
    // Keep trailing spaces on the active (last) row so typed spaces update
    // display_text() and the UI caret can advance.
    if (row + 1 < rows_) {
      line = trim_trailing_spaces(std::move(line));
    }
    if (line.empty()) {
      line = " ";
    }
    result += line;
    if (row + 1 < rows_) {
      result.push_back('\n');
    }
  }

  cached_text_ = std::move(result);
  cache_valid_ = true;
}

std::string RawPtyScreen::text() const {
  if (!cache_valid_) {
    rebuild_cache();
  }
  return cached_text_;
}

std::vector<TerminalStyledRow> RawPtyScreen::styled_rows() const {
  if (!cache_valid_) {
    rebuild_cache();
  }
  return cached_styled_rows_;
}

}  // namespace tuide
