#pragma once

#include "ftxui/dom/elements.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/theme.hpp"

namespace tgdb::cursor_blink {

inline ftxui::Decorator cell_decorator() {
  return ftxui::bgcolor(theme::CursorCell()) | ftxui::color(ftxui::Color::Black) | ftxui::bold;
}

}  // namespace tgdb::cursor_blink
