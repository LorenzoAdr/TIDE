#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

inline Element PanelTitle(const std::string& title) {
  return hbox({text(" " + title + " ") | bold | color(theme::TitleText()),
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

// Modal centrado sobre contenido ya dibujado debajo (p. ej. editor apilado).
inline Element CenteredModal(Element dialog) {
  return dbox({text(""), center(clear_under(std::move(dialog)))});
}

// Modal sobre la UI existente: el fondo sigue visible; solo el diálogo va encima.
inline Element ScreenModalOverlay(Element base, Element dialog) {
  return dbox({std::move(base), center(clear_under(std::move(dialog)))});
}

inline Element ModalWindow(Element title, Element content) {
  return vbox({
             hbox({text(" "), std::move(title), filler()}) |
                 bgcolor(theme::TabIdle()) | color(theme::TitleText()) | bold |
                 size(HEIGHT, EQUAL, 1),
             separator() | color(theme::AccentDim()) | bgcolor(theme::PanelBg()),
             std::move(content) | bgcolor(theme::PanelBg()),
         }) |
         borderRounded | borderStyled(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

inline Element ModalInputLine(const std::string& text_line) {
  std::string content = text_line;
  const bool has_cursor = !content.empty() && content.back() == '_';
  if (has_cursor) {
    content.pop_back();
  }

  Elements parts;
  parts.push_back(text(" " + content) | color(theme::WatchInput()));
  if (has_cursor && cursor_blink::visible()) {
    parts.push_back(text(" ") | cursor_blink::cell_decorator());
  }

  Element row = hbox({hbox(std::move(parts)), filler()}) | flex;
  row = row | bgcolor(theme::TabIdle()) | size(HEIGHT, EQUAL, 1);
  return clear_under(std::move(row));
}

// Separador de resize con feedback visual sutil al pasar el ratón o arrastrar.
inline Element SplitSeparatorVertical(bool hovered, bool dragging, Box* box) {
  Color line_color = theme::Muted();
  if (dragging) {
    line_color = theme::Accent();
  } else if (hovered) {
    line_color = theme::AccentDim();
  }
  return separatorCharacter("│") | color(line_color) | bgcolor(theme::PanelBg()) |
         size(WIDTH, EQUAL, 1) | reflect(*box);
}

inline Element SplitSeparatorHorizontal(bool hovered, bool dragging, Box* box) {
  Color line_color = theme::Muted();
  if (dragging) {
    line_color = theme::Accent();
  } else if (hovered) {
    line_color = theme::AccentDim();
  }
  return separatorCharacter("─") | color(line_color) | bgcolor(theme::PanelBg()) | reflect(*box);
}

inline Element SplitSeparatorVertical() {
  return separatorCharacter("│") | color(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

inline Element SplitSeparatorHorizontal() {
  return separatorCharacter("─") | color(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

}  // namespace tgdb
