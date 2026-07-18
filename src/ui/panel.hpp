#pragma once

#include <algorithm>
#include <functional>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/theme.hpp"

namespace tuide {

using namespace ftxui;

struct LargeModalLayout {
  int modal_width = 120;
  int modal_height = 22;
  int left_pane_width = 54;
  int right_pane_width = 65;
  int max_rows = 18;
};

inline LargeModalLayout compute_large_modal_layout(int term_w, int term_h) {
  LargeModalLayout layout;
  layout.modal_width = std::max(80, std::min(term_w - 2, (term_w * 92) / 100));
  layout.modal_height = std::max(16, std::min(term_h - 3, (term_h * 88) / 100));
  layout.left_pane_width = std::max(32, layout.modal_width * 42 / 100);
  layout.right_pane_width = layout.modal_width - layout.left_pane_width - 1;
  layout.max_rows = std::max(8, layout.modal_height - 5);
  return layout;
}

inline int terminal_width_or_default(const std::function<int()>& width_fn, int fallback = 120) {
  return width_fn ? width_fn() : fallback;
}

inline int terminal_height_or_default(const std::function<int()>& height_fn, int fallback = 40) {
  return height_fn ? height_fn() : fallback;
}

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

}  // namespace tuide
