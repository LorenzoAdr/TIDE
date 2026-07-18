#include "app/editor_tabs.hpp"

#include <algorithm>
#include <string>

namespace tuide {

TabVisibleRange compute_visible_tab_range(int tab_count, int active_tab, int bar_width_chars) {
  TabVisibleRange range;
  if (tab_count <= 0 || bar_width_chars <= 0) {
    return range;
  }

  if (tab_count * kEditorTabCellWidth <= bar_width_chars) {
    range.start = 0;
    range.end = tab_count;
    return range;
  }

  range.has_overflow = true;
  const int max_visible =
      std::max(1, (bar_width_chars - kEditorTabOverflowWidth) / kEditorTabCellWidth);
  const int clamped_active = std::max(0, std::min(active_tab, tab_count - 1));
  range.start = std::max(0, std::min(clamped_active - max_visible / 2, tab_count - max_visible));
  range.end = range.start + max_visible;
  range.hidden_count = tab_count - (range.end - range.start);
  return range;
}

std::string format_editor_tab_overflow_button(int hidden_count) {
  std::string label = "+" + std::to_string(hidden_count);
  std::string out = " " + label;
  if (static_cast<int>(out.size()) < kEditorTabOverflowWidth) {
    out.append(static_cast<std::size_t>(kEditorTabOverflowWidth - out.size()), ' ');
  }
  return out;
}

}  // namespace tuide
