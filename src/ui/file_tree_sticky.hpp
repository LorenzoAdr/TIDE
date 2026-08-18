#pragma once

#include <algorithm>
#include <vector>

namespace tuide {

constexpr int kMaxExplorerStickyRows = 7;

// How many ancestor folders can stay pinned without swallowing the list.
inline int explorer_sticky_cap(int visible_rows, int max_sticky = kMaxExplorerStickyRows) {
  if (visible_rows <= 2 || max_sticky <= 0) {
    return 0;
  }
  return std::min(max_sticky, visible_rows / 2);
}

// Ancestor folders of the first scrolled row that have already left the viewport.
// `depth_at(i)` and `is_folder_at(i)` describe the flattened explorer rows.
template <typename DepthAt, typename IsFolderAt>
std::vector<int> sticky_explorer_indices(int size, int scroll_offset, int max_sticky,
                                         DepthAt depth_at, IsFolderAt is_folder_at) {
  std::vector<int> sticky;
  if (size <= 0 || scroll_offset <= 0 || max_sticky <= 0 || scroll_offset >= size) {
    return sticky;
  }

  int want_depth = depth_at(scroll_offset) - 1;
  for (int i = scroll_offset - 1; i >= 0 && want_depth >= 0; --i) {
    if (is_folder_at(i) && depth_at(i) == want_depth) {
      sticky.push_back(i);
      --want_depth;
    }
  }
  std::reverse(sticky.begin(), sticky.end());

  if (static_cast<int>(sticky.size()) > max_sticky) {
    sticky.erase(sticky.begin(), sticky.end() - static_cast<std::ptrdiff_t>(max_sticky));
  }
  return sticky;
}

}  // namespace tuide
