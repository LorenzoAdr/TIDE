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

struct HorizontalScrollbarLayout {
  int bar_width = 0;
  int thumb_x = 0;
  int thumb_width = 0;
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

inline Element vertical_scrollbar(int total_lines, int scroll, int visible_lines, int bar_height,
                                  bool hovered = false, bool active = false) {
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

  const Color track_color = hovered || active ? theme::TabHover() : Color::Default;
  const Color thumb_color = active ? theme::TabPressed() : theme::Accent();

  for (int i = 0; i < bar_height; ++i) {
    const bool in_thumb = i >= layout.thumb_y && i < layout.thumb_y + layout.thumb_height;
    if (in_thumb) {
      Element thumb = text("┃") | color(thumb_color);
      if (active) {
        thumb = thumb | bold | inverted;
      }
      track.push_back(std::move(thumb));
    } else {
      Element line = text("│") | color(theme::Muted());
      if (hovered || active) {
        line = line | bgcolor(track_color);
      }
      track.push_back(std::move(line));
    }
  }
  return vbox(std::move(track));
}

inline HorizontalScrollbarLayout compute_horizontal_scrollbar_layout(int total_width,
                                                                     int scroll_col,
                                                                     int visible_width,
                                                                     int bar_width) {
  HorizontalScrollbarLayout layout;
  layout.bar_width = bar_width;
  layout.max_scroll = std::max(0, total_width - visible_width);
  if (bar_width <= 0 || total_width <= visible_width) {
    layout.thumb_width = std::max(0, bar_width);
    return layout;
  }

  layout.scrollable = true;
  layout.thumb_width = std::max(1, visible_width * bar_width / total_width);
  const int travel = bar_width - layout.thumb_width;
  layout.thumb_x = layout.max_scroll > 0 ? scroll_col * travel / layout.max_scroll : 0;
  return layout;
}

inline int scroll_for_thumb_left(const HorizontalScrollbarLayout& layout, int thumb_left) {
  if (!layout.scrollable || layout.max_scroll <= 0) {
    return 0;
  }
  const int travel = layout.bar_width - layout.thumb_width;
  if (travel <= 0) {
    return 0;
  }
  const int clamped = std::max(0, std::min(thumb_left, travel));
  return clamped * layout.max_scroll / travel;
}

inline bool horizontal_scrollbar_thumb_hit(const HorizontalScrollbarLayout& layout, const Box& box,
                                           int x, int y) {
  if (!layout.scrollable || !box.Contain(x, y)) {
    return false;
  }
  const int local_x = x - box.x_min;
  return local_x >= layout.thumb_x && local_x < layout.thumb_x + layout.thumb_width;
}

inline Element horizontal_scrollbar(int total_width, int scroll_col, int visible_width,
                                    int bar_width, bool hovered = false, bool active = false) {
  if (bar_width <= 0) {
    return text("");
  }

  const HorizontalScrollbarLayout layout =
      compute_horizontal_scrollbar_layout(total_width, scroll_col, visible_width, bar_width);
  if (!layout.scrollable) {
    return text("");
  }

  Elements track;
  const Color track_color = hovered || active ? theme::TabHover() : Color::Default;
  const Color thumb_color = active ? theme::TabPressed() : theme::Accent();

  for (int i = 0; i < bar_width; ++i) {
    const bool in_thumb = i >= layout.thumb_x && i < layout.thumb_x + layout.thumb_width;
    if (in_thumb) {
      Element thumb = text("━") | color(thumb_color);
      if (active) {
        thumb = thumb | bold | inverted;
      }
      track.push_back(std::move(thumb));
    } else {
      Element line = text("─") | color(theme::Muted());
      if (hovered || active) {
        line = line | bgcolor(track_color);
      }
      track.push_back(std::move(line));
    }
  }
  return hbox(std::move(track));
}

}  // namespace tgdb
