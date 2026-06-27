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
                 bgcolor(theme::TabIdle()) | color(theme::Accent()) | bold |
                 size(HEIGHT, EQUAL, 1),
             separator() | color(theme::AccentDim()) | bgcolor(theme::PanelBg()),
             std::move(content) | bgcolor(theme::PanelBg()),
         }) |
         borderRounded | borderStyled(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

inline Element ModalInputLine(const std::string& text_line) {
  Element row = hbox({
                    text(" " + text_line) | color(theme::WatchInput()),
                    filler(),
                }) |
                bgcolor(theme::TabIdle()) | size(HEIGHT, EQUAL, 1) | flex;
  // clear_under solo en esta fila (caja acotada), no en toda la pantalla.
  return clear_under(std::move(row));
}

// Un único carácter: separador de resize (sin borde de window duplicado).
inline Element SplitSeparatorVertical() {
  return separatorCharacter("│") | color(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

inline Element SplitSeparatorHorizontal() {
  return separatorCharacter("─") | color(theme::AccentDim()) | bgcolor(theme::PanelBg());
}

}  // namespace tgdb
