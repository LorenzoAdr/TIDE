#include "terminal/terminal_emulator.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <vterm.h>

#include "ftxui/dom/elements.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr std::size_t kMaxScrollbackLines = 5000;

std::string utf8_from_codepoint(uint32_t codepoint) {
  std::string out;
  if (codepoint == 0) {
    return out;
  }
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return out;
}

Color xterm_palette_color(int index) {
  static const Color palette[] = {
      Color::RGB(0, 0, 0),       Color::RGB(205, 0, 0),     Color::RGB(0, 205, 0),
      Color::RGB(205, 205, 0),   Color::RGB(0, 0, 238),     Color::RGB(205, 0, 205),
      Color::RGB(0, 205, 205),   Color::RGB(229, 229, 229), Color::RGB(127, 127, 127),
      Color::RGB(255, 0, 0),     Color::RGB(0, 255, 0),     Color::RGB(255, 255, 0),
      Color::RGB(92, 92, 255),   Color::RGB(255, 0, 255),   Color::RGB(0, 255, 255),
      Color::RGB(255, 255, 255),
  };
  if (index >= 0 && index < 16) {
    return palette[index];
  }
  if (index >= 232) {
    const int level = index - 232;
    const int gray = 8 + level * 10;
    return Color::RGB(gray, gray, gray);
  }
  if (index >= 16 && index < 232) {
    const int idx = index - 16;
    const int r = (idx / 36) * 51;
    const int g = ((idx / 6) % 6) * 51;
    const int b = (idx % 6) * 51;
    return Color::RGB(r, g, b);
  }
  return theme::Header();
}

}  // namespace

TerminalEmulator::TerminalEmulator(int rows, int cols) { init_vterm(rows, cols); }

TerminalEmulator::~TerminalEmulator() { destroy_vterm(); }

void TerminalEmulator::init_vterm(int rows, int cols) {
  destroy_vterm();

  rows_ = std::max(1, rows);
  cols_ = std::max(1, cols);
  scrollback_.clear();
  dirty_ = true;

  vt_ = vterm_new(rows_, cols_);
  vterm_set_utf8(vt_, 1);
  state_ = vterm_obtain_state(vt_);
  screen_ = vterm_obtain_screen(vt_);

  VTermColor default_fg;
  VTermColor default_bg;
  vterm_color_rgb(&default_fg, 180, 200, 255);
  vterm_color_rgb(&default_bg, 0, 0, 0);
  vterm_screen_set_default_colors(screen_, &default_fg, &default_bg);

  VTermScreenCallbacks callbacks = {};
  callbacks.damage = damage_callback;
  callbacks.sb_pushline = sb_pushline_callback;
  callbacks.sb_popline = sb_popline_callback;
  vterm_screen_set_callbacks(screen_, &callbacks, this);

  vterm_state_reset(state_, 1);
  vterm_screen_reset(screen_, 1);
  vterm_screen_flush_damage(screen_);
}

void TerminalEmulator::destroy_vterm() {
  if (vt_ != nullptr) {
    vterm_free(vt_);
    vt_ = nullptr;
    screen_ = nullptr;
    state_ = nullptr;
  }
}

void TerminalEmulator::reset(int rows, int cols) { init_vterm(rows, cols); }

void TerminalEmulator::resize(int rows, int cols) {
  rows = std::max(1, rows);
  cols = std::max(1, cols);
  if (rows == rows_ && cols == cols_) {
    return;
  }
  rows_ = rows;
  cols_ = cols;
  if (vt_ != nullptr) {
    vterm_set_size(vt_, rows_, cols_);
    vterm_screen_flush_damage(screen_);
  }
  dirty_ = true;
}

void TerminalEmulator::feed(const char* data, std::size_t len) {
  if (vt_ == nullptr || data == nullptr || len == 0) {
    return;
  }
  vterm_input_write(vt_, data, len);
  vterm_screen_flush_damage(screen_);
  dirty_ = true;
}

int TerminalEmulator::damage_callback(VTermRect /*rect*/, void* user) {
  auto* self = static_cast<TerminalEmulator*>(user);
  self->dirty_ = true;
  return 1;
}

int TerminalEmulator::sb_pushline_callback(int cols, const VTermScreenCell* cells, void* user) {
  auto* self = static_cast<TerminalEmulator*>(user);
  std::string line;
  line.reserve(static_cast<std::size_t>(cols) * 2);
  for (int col = 0; col < cols;) {
    const VTermScreenCell& cell = cells[col];
    if (cell.chars[0] == 0) {
      line.push_back(' ');
      ++col;
      continue;
    }
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; ++i) {
      line += utf8_from_codepoint(cell.chars[i]);
    }
    col += cell.width > 0 ? cell.width : 1;
  }
  while (!line.empty() && line.back() == ' ') {
    line.pop_back();
  }
  self->scrollback_.push_back(std::move(line));
  if (self->scrollback_.size() > kMaxScrollbackLines) {
    self->scrollback_.erase(self->scrollback_.begin());
  }
  self->dirty_ = true;
  return 1;
}

int TerminalEmulator::sb_popline_callback(int cols, VTermScreenCell* cells, void* user) {
  auto* self = static_cast<TerminalEmulator*>(user);
  if (self->scrollback_.empty()) {
    return 0;
  }
  const std::string& line = self->scrollback_.back();
  for (int col = 0; col < cols; ++col) {
    VTermScreenCell& cell = cells[col];
    cell.chars[0] = col < static_cast<int>(line.size()) ? static_cast<uint32_t>(line[col]) : 0;
    cell.chars[1] = 0;
    cell.width = 1;
    cell.attrs = {};
    vterm_color_rgb(&cell.fg, 180, 200, 255);
    vterm_color_rgb(&cell.bg, 0, 0, 0);
  }
  self->scrollback_.pop_back();
  self->dirty_ = true;
  return 1;
}

std::string TerminalEmulator::cell_text(const VTermScreenCell& cell) const {
  if (cell.chars[0] == 0) {
    return " ";
  }
  std::string out;
  for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != 0; ++i) {
    out += utf8_from_codepoint(cell.chars[i]);
  }
  return out.empty() ? std::string(" ") : out;
}

Color TerminalEmulator::cell_fg(const VTermScreenCell& cell) const {
  VTermColor color = cell.fg;
  if (VTERM_COLOR_IS_DEFAULT_FG(&color)) {
    return theme::Header();
  }
  if (VTERM_COLOR_IS_DEFAULT_BG(&color)) {
    return theme::Header();
  }
  vterm_screen_convert_color_to_rgb(screen_, &color);
  if (VTERM_COLOR_IS_INDEXED(&color)) {
    return xterm_palette_color(color.indexed.idx);
  }
  return Color::RGB(color.rgb.red, color.rgb.green, color.rgb.blue);
}

Color TerminalEmulator::cell_bg(const VTermScreenCell& cell) const {
  VTermColor color = cell.bg;
  if (VTERM_COLOR_IS_DEFAULT_BG(&color)) {
    return theme::CodeBg();
  }
  if (VTERM_COLOR_IS_DEFAULT_FG(&color)) {
    return theme::CodeBg();
  }
  vterm_screen_convert_color_to_rgb(screen_, &color);
  if (VTERM_COLOR_IS_INDEXED(&color)) {
    return xterm_palette_color(color.indexed.idx);
  }
  return Color::RGB(color.rgb.red, color.rgb.green, color.rgb.blue);
}

Element TerminalEmulator::render_row(int row, int cursor_row, int cursor_col) const {
  Elements cells;
  cells.reserve(static_cast<std::size_t>(cols_));

  for (int col = 0; col < cols_;) {
    VTermPos pos = {row, col};
    VTermScreenCell cell = {};
    vterm_screen_get_cell(screen_, pos, &cell);

    const bool at_cursor = row == cursor_row && col == cursor_col;
    const std::string glyph = cell_text(cell);
    Color fg = cell_fg(cell);
    Color bg = cell_bg(cell);

    Element styled = text(glyph) | color(fg) | bgcolor(bg);
    if (cell.attrs.bold) {
      styled = styled | bold;
    }
    if (cell.attrs.italic) {
      styled = styled | italic;
    }
    if (cell.attrs.underline) {
      styled = styled | underlined;
    }
    if (cell.attrs.strike) {
      styled = styled | strikethrough;
    }
    if (cell.attrs.reverse) {
      styled = text(glyph) | color(bg) | bgcolor(fg);
    }
    if (at_cursor) {
      styled = styled | inverted;
    }
    cells.push_back(std::move(styled));

    col += cell.width > 0 ? cell.width : 1;
  }

  return hbox(std::move(cells));
}

Element TerminalEmulator::render() const {
  if (screen_ == nullptr || state_ == nullptr) {
    return text("(terminal no disponible)") | color(theme::Muted());
  }

  VTermPos cursor = {};
  vterm_state_get_cursorpos(state_, &cursor);

  Elements rows;
  rows.reserve(static_cast<std::size_t>(rows_));
  for (int row = 0; row < rows_; ++row) {
    rows.push_back(render_row(row, cursor.row, cursor.col));
  }
  return vbox(std::move(rows)) | bgcolor(theme::CodeBg());
}

}  // namespace tgdb
