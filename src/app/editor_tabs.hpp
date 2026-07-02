#pragma once

#include <string>
#include <vector>

#include "editor/editor_state.hpp"

namespace tgdb {

struct EditorTab {
  std::string path;
  EditorBuffer buffer;
  bool external = false;
};

struct TabVisibleRange {
  int start = 0;
  int end = 0;
  bool has_overflow = false;
  int hidden_count = 0;
};

TabVisibleRange compute_visible_tab_range(int tab_count, int active_tab, int bar_width_chars);

}  // namespace tgdb
