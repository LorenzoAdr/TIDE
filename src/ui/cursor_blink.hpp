#pragma once

#include "ftxui/dom/elements.hpp"
#include "ui/theme.hpp"

namespace tgdb::cursor_blink {

void tick();
bool visible();

inline ftxui::Decorator cell_decorator() {
  return ftxui::bgcolor(theme::CursorCell()) | ftxui::color(ftxui::Color::Black) | ftxui::bold;
}

inline int effective_col(int col) { return visible() ? col : -1; }

}  // namespace tgdb::cursor_blink
