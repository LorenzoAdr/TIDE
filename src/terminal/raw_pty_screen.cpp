#include "terminal/raw_pty_screen.hpp"

#include <algorithm>
#include <cstdlib>

namespace tgdb {

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

  std::size_t start = 0;
  while (start <= params.size()) {
    const std::size_t sep = params.find(';', start);
    const std::string token =
        params.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
    const int code = parse_first_param(token, -1);
    if (code >= 0) {
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
      } else if (code >= 100 && code <= 107) {
        bg_ = ansi_base_color(code - 100, true);
      } else if (code == 49) {
        bg_ = kDefaultBg;
      }
    }
    if (sep == std::string::npos) {
      break;
    }
    start = sep + 1;
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
  if (cursor_col_ >= cols_) {
    return;
  }
  ensure_current_width();
  ScreenCell& cell = current_cells_[static_cast<std::size_t>(cursor_col_)];
  cell.ch = ch;
  cell.fg = fg_;
  cell.bg = bg_;
  ++cursor_col_;
  cache_valid_ = false;
}

TerminalStyledRow RawPtyScreen::spans_from_cells(const std::vector<ScreenCell>& cells) const {
  TerminalStyledRow spans;
  if (cells.empty()) {
    spans.push_back(TerminalStyledSpan{});
    return spans;
  }

  int end = static_cast<int>(cells.size());
  while (end > 0 && cells[static_cast<std::size_t>(end - 1)].ch == ' ') {
    --end;
  }
  if (end <= 0) {
    spans.push_back(TerminalStyledSpan{});
    return spans;
  }

  TerminalStyledSpan current;
  current.fg = cells[0].fg;
  current.bg = cells[0].bg;
  for (int col = 0; col < end; ++col) {
    const ScreenCell& cell = cells[static_cast<std::size_t>(col)];
    if (!current.text.empty() && (cell.fg != current.fg || cell.bg != current.bg)) {
      spans.push_back(current);
      current = TerminalStyledSpan{};
      current.fg = cell.fg;
      current.bg = cell.bg;
    } else if (current.text.empty()) {
      current.fg = cell.fg;
      current.bg = cell.bg;
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
  lines_.push_back(spans_from_cells(current_cells_));
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
  all.push_back(spans_from_cells(current_cells_));
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

  visible.push_back(spans_from_cells(current_cells_));

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
    line = trim_trailing_spaces(std::move(line));
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

}  // namespace tgdb
