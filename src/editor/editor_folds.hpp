#pragma once

#include <set>
#include <vector>

namespace tgdb {

inline constexpr int kScrollEdgeMarginLines = 3;

struct FoldRegion {
  int open_line = 0;
  int close_line = 0;

  bool operator==(const FoldRegion& other) const {
    return open_line == other.open_line && close_line == other.close_line;
  }

  bool operator!=(const FoldRegion& other) const { return !(*this == other); }
};

struct EditorBuffer;

bool fold_line_hidden(int line, const std::vector<FoldRegion>& regions,
                      const std::set<int>& collapsed_open_lines);

std::vector<int> visible_buffer_lines(int total_lines, const std::vector<FoldRegion>& regions,
                                      const std::set<int>& collapsed_open_lines);

std::vector<int> viewport_buffer_lines(const EditorBuffer& buffer,
                                       const std::vector<FoldRegion>& regions,
                                       int viewport_count);

// Windowing-only step of viewport_buffer_lines, split out so callers that already have a
// (possibly cached) visible-lines vector can skip re-running the O(lines*regions) fold scan.
std::vector<int> viewport_window_from_visible(const std::vector<int>& visible, int scroll,
                                              int viewport_count);

const FoldRegion* fold_region_at_open_line(const std::vector<FoldRegion>& regions, int open_line);

char fold_gutter_marker(int buffer_line, const std::vector<FoldRegion>& regions,
                        const std::set<int>& collapsed_open_lines);

bool toggle_fold_at(EditorBuffer* buffer, int open_line, const std::vector<FoldRegion>& regions);

void clamp_cursors_for_folds(EditorBuffer* buffer, const std::vector<FoldRegion>& regions);

int fold_scroll_max(const EditorBuffer& buffer, const std::vector<FoldRegion>& regions,
                    int viewport_count);

void scroll_view_by_lines_fold_aware(EditorBuffer* buffer, int delta_lines,
                                     const std::vector<FoldRegion>& regions, int viewport_count);

void ensure_scroll_visible_fold_aware(EditorBuffer* buffer, const std::vector<FoldRegion>& regions,
                                      int viewport_count, int code_width = -1,
                                      int edge_margin_lines = kScrollEdgeMarginLines);

void stabilize_scroll_after_fold_change(EditorBuffer* buffer, const std::vector<FoldRegion>& regions,
                                        int viewport_count);

int visible_line_index(const std::vector<int>& visible_lines, int buffer_line);

}  // namespace tgdb
