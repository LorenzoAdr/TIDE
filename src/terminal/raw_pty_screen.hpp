#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ftxui/screen/color.hpp"

namespace tgdb {

struct TerminalStyledSpan {
  std::string text;
  ftxui::Color fg = ftxui::Color::RGB(200, 230, 255);
  ftxui::Color bg = ftxui::Color::RGB(0, 0, 0);
};

using TerminalStyledRow = std::vector<TerminalStyledSpan>;

// Lightweight PTY screen buffer. Handles text layout, basic cursor control and
// common ANSI SGR colors used by bash prompts.
class RawPtyScreen {
 public:
  RawPtyScreen(int rows = 24, int cols = 80);

  void reset(int rows, int cols);
  void resize(int rows, int cols);
  void feed(const char* data, std::size_t len);

  int rows() const { return rows_; }
  int cols() const { return cols_; }
  int cursor_col() const { return cursor_col_; }
  int cursor_row() const { return static_cast<int>(lines_.size()); }
  int total_rows() const { return static_cast<int>(lines_.size()) + 1; }
  std::string text() const;
  std::vector<TerminalStyledRow> styled_rows() const;

 private:
  struct ScreenCell {
    char ch = ' ';
    ftxui::Color fg = ftxui::Color::RGB(200, 230, 255);
    ftxui::Color bg = ftxui::Color::RGB(0, 0, 0);
  };

  void append_char(char ch);
  void newline();
  void carriage_return();
  void backspace();
  void clear_current_line();
  void clear_to_end_of_line();
  void trim_lines();
  void apply_sgr(const std::string& params);
  void handle_csi(char cmd, const std::string& params);
  void skip_escape(const char*& p, const char* end);
  void ensure_current_width();
  void reset_sgr();
  TerminalStyledRow spans_from_cells(const std::vector<ScreenCell>& cells) const;
  std::vector<TerminalStyledRow> build_all_rows() const;
  std::vector<TerminalStyledRow> build_visible_rows() const;
  void rebuild_cache() const;

  int rows_ = 24;
  int cols_ = 80;
  int cursor_col_ = 0;
  bool bold_ = false;
  ftxui::Color fg_;
  ftxui::Color bg_;
  std::vector<TerminalStyledRow> lines_;
  std::vector<ScreenCell> current_cells_;
  mutable std::string cached_text_;
  mutable std::vector<TerminalStyledRow> cached_styled_rows_;
  mutable bool cache_valid_ = false;
};

}  // namespace tgdb
