#pragma once

#include <algorithm>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

struct ScrollbarLayout {
  int bar_height = 0;
  int thumb_y = 0;
  int thumb_height = 0;
  int max_scroll = 0;
  bool scrollable = false;
};

inline ScrollbarLayout compute_scrollbar_layout(int total_lines, int scroll, int visible_lines,
                                              int bar_height) {
  ScrollbarLayout layout;
  layout.bar_height = bar_height;
  layout.max_scroll = std::max(0, total_lines - visible_lines);
  if (bar_height <= 0 || total_lines <= visible_lines) {
    layout.thumb_height = std::max(0, bar_height);
    return layout;
  }

  layout.scrollable = true;
  layout.thumb_height = std::max(1, visible_lines * bar_height / total_lines);
  const int travel = bar_height - layout.thumb_height;
  layout.thumb_y =
      layout.max_scroll > 0 ? scroll * travel / layout.max_scroll : 0;
  return layout;
}

inline int scroll_for_thumb_top(const ScrollbarLayout& layout, int thumb_top) {
  if (!layout.scrollable || layout.max_scroll <= 0) {
    return 0;
  }
  const int travel = layout.bar_height - layout.thumb_height;
  if (travel <= 0) {
    return 0;
  }
  const int clamped = std::max(0, std::min(thumb_top, travel));
  return clamped * layout.max_scroll / travel;
}

inline bool scrollbar_thumb_hit(const ScrollbarLayout& layout, const Box& box, int x, int y) {
  if (!layout.scrollable || !box.Contain(x, y)) {
    return false;
  }
  const int local_y = y - box.y_min;
  return local_y >= layout.thumb_y && local_y < layout.thumb_y + layout.thumb_height;
}

inline Element vertical_scrollbar(int total_lines, int scroll, int visible_lines, int bar_height) {
  Elements track;
  if (bar_height <= 0) {
    return text("");
  }

  const ScrollbarLayout layout =
      compute_scrollbar_layout(total_lines, scroll, visible_lines, bar_height);
  if (!layout.scrollable) {
    for (int i = 0; i < bar_height; ++i) {
      track.push_back(text("│") | color(theme::Muted()));
    }
    return vbox(std::move(track));
  }

  for (int i = 0; i < bar_height; ++i) {
    if (i >= layout.thumb_y && i < layout.thumb_y + layout.thumb_height) {
      track.push_back(text("┃") | color(theme::Accent()));
    } else {
      track.push_back(text("│") | color(theme::Muted()));
    }
  }
  return vbox(std::move(track));
}

}  // namespace tgdb
