#include "app/editor_tabs.hpp"

#include <algorithm>

namespace tgdb {

namespace {

constexpr int kOverflowButtonWidth = 4;
constexpr int kTabCellWidth = 17;

}  // namespace

TabVisibleRange compute_visible_tab_range(int tab_count, int active_tab, int bar_width_chars) {
  TabVisibleRange range;
  if (tab_count <= 0 || bar_width_chars <= 0) {
    return range;
  }

  const int max_without_overflow = std::max(1, bar_width_chars / kTabCellWidth);
  if (tab_count <= max_without_overflow) {
    range.start = 0;
    range.end = tab_count;
    return range;
  }

  range.has_overflow = true;
  const int max_visible =
      std::max(1, (bar_width_chars - kOverflowButtonWidth) / kTabCellWidth);
  const int clamped_active = std::max(0, std::min(active_tab, tab_count - 1));
  range.start = std::max(0, std::min(clamped_active - max_visible / 2, tab_count - max_visible));
  range.end = range.start + max_visible;
  range.hidden_count = tab_count - (range.end - range.start);
  return range;
}

}  // namespace tgdb
