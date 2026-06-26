#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

inline Element PanelTitle(const std::string& title) {
  return hbox({text(" " + title + " ") | bold | color(theme::Accent()),
               text("") | flex}) |
         size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle());
}

inline Element PanelBody(Element content, Color bg = theme::PanelBg()) {
  return std::move(content) | flex | bgcolor(bg);
}

inline Element MakePanel(const std::string& title, Element content,
                         Color bg = theme::PanelBg()) {
  return vbox({PanelTitle(title), PanelBody(std::move(content), bg)}) | flex |
         bgcolor(bg);
}

// Un único carácter: separador de resize (sin borde de window duplicado).
inline Element SplitSeparatorVertical() {
  return separatorCharacter("│") | color(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

inline Element SplitSeparatorHorizontal() {
  return separatorCharacter("─") | color(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

}  // namespace tgdb
