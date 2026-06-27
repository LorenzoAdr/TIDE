#include "terminal/terminal_emulator.hpp"

#include <algorithm>
#include <cstring>
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
  dirty_ = true;
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
  (void)cell;
  return " ";
}

Color TerminalEmulator::cell_fg(const VTermScreenCell& cell) const {
  (void)cell;
  return theme::Header();
}

Color TerminalEmulator::cell_bg(const VTermScreenCell& cell) const {
  (void)cell;
  return theme::CodeBg();
}

Element TerminalEmulator::render_row(int row, int cursor_row, int cursor_col) const {
  (void)row;
  (void)cursor_row;
  (void)cursor_col;
  return text("");
}

Element TerminalEmulator::render() {
  if (screen_ == nullptr || state_ == nullptr) {
    return text("(terminal no disponible)") | color(theme::Muted());
  }

  VTermPos cursor = {};
  vterm_state_get_cursorpos(state_, &cursor);

  std::vector<char> buffer(static_cast<std::size_t>(cols_) + 1);
  Elements rows;
  rows.reserve(static_cast<std::size_t>(rows_));

  for (int row = 0; row < rows_; ++row) {
    const VTermRect rect = {row, row + 1, 0, cols_};
    std::fill(buffer.begin(), buffer.end(), '\0');
    vterm_screen_get_text(screen_, buffer.data(), buffer.size(), rect);

    std::string line(buffer.data());
    while (!line.empty() && line.back() == ' ') {
      line.pop_back();
    }

    if (row == cursor.row) {
      const int col = cursor.col;
      if (col > static_cast<int>(line.size())) {
        line.resize(static_cast<std::size_t>(col), ' ');
      }
      if (col >= 0 && col <= static_cast<int>(line.size())) {
        char cursor_char = '_';
        if (col < static_cast<int>(line.size())) {
          cursor_char = line[static_cast<std::size_t>(col)];
          if (cursor_char == ' ') {
            cursor_char = '_';
          }
        }
        const std::string before = line.substr(0, static_cast<std::size_t>(col));
        const std::string after =
            col < static_cast<int>(line.size()) ? line.substr(static_cast<std::size_t>(col) + 1)
                                                : std::string();
        rows.push_back(hbox({
                           text(before.empty() ? "" : before) | color(theme::WatchInput()),
                           text(std::string(1, cursor_char)) | color(theme::CodeBg()) |
                               bgcolor(theme::WatchInput()) | bold,
                           text(after) | color(theme::WatchInput()),
                       }) |
                       bgcolor(theme::CodeBg()));
        continue;
      }
    }

    if (line.empty()) {
      rows.push_back(text(" ") | color(theme::CodeBg()) | bgcolor(theme::CodeBg()));
    } else {
      rows.push_back(text(line) | color(theme::WatchInput()) | bgcolor(theme::CodeBg()));
    }
  }

  dirty_ = false;
  return vbox(std::move(rows)) | bgcolor(theme::CodeBg());
}

}  // namespace tgdb
