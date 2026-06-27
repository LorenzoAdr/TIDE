#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "vterm.h"

struct VTerm;
struct VTermScreen;
struct VTermState;

namespace tgdb {

class TerminalEmulator {
 public:
  TerminalEmulator(int rows = 24, int cols = 80);
  ~TerminalEmulator();

  TerminalEmulator(const TerminalEmulator&) = delete;
  TerminalEmulator& operator=(const TerminalEmulator&) = delete;

  void reset(int rows, int cols);
  void resize(int rows, int cols);
  void feed(const char* data, std::size_t len);

  int rows() const { return rows_; }
  int cols() const { return cols_; }
  ftxui::Element render();

 private:
  static int damage_callback(VTermRect rect, void* user);
  static int sb_pushline_callback(int cols, const VTermScreenCell* cells, void* user);
  static int sb_popline_callback(int cols, VTermScreenCell* cells, void* user);

  void init_vterm(int rows, int cols);
  void destroy_vterm();
  std::string cell_text(const VTermScreenCell& cell) const;
  ftxui::Color cell_fg(const VTermScreenCell& cell) const;
  ftxui::Color cell_bg(const VTermScreenCell& cell) const;
  ftxui::Element render_row(int row, int cursor_row, int cursor_col) const;

  VTerm* vt_ = nullptr;
  VTermScreen* screen_ = nullptr;
  VTermState* state_ = nullptr;
  int rows_ = 24;
  int cols_ = 80;
  bool dirty_ = true;
  std::vector<std::string> scrollback_;
};

}  // namespace tgdb
