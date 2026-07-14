#pragma once

#include <string>
#include <vector>

#include "editor/editor_state.hpp"

namespace tgdb {

struct EditorTab {
  std::string path;
  EditorBuffer buffer;
  bool external = false;
  bool read_only = false;
  bool git_diff_head = false;
};

struct TabVisibleRange {
  int start = 0;
  int end = 0;
  bool has_overflow = false;
  int hidden_count = 0;
};

// Layout fijo por pestaña (~doble del tamaño anterior) para alinear render y hit-testing.
inline constexpr int kEditorTabLabelMaxLen = 24;
inline constexpr int kEditorTabCloseWidth = 3;
inline constexpr int kEditorTabLabelAreaWidth = 27;
inline constexpr int kEditorTabCellWidth =
    kEditorTabLabelAreaWidth + kEditorTabCloseWidth;  // 30
inline constexpr int kEditorTabOverflowWidth = 6;  // " +99  "

TabVisibleRange compute_visible_tab_range(int tab_count, int active_tab, int bar_width_chars);

std::string format_editor_tab_overflow_button(int hidden_count);

}  // namespace tgdb
