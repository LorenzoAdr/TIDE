#include "editor/editor_folds.hpp"

#include <algorithm>

#include "editor/editor_state.hpp"

namespace tgdb {

namespace {

int index_of_visible_at_or_after(const std::vector<int>& visible_lines, int buffer_line) {
  for (std::size_t i = 0; i < visible_lines.size(); ++i) {
    if (visible_lines[i] >= buffer_line) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(visible_lines.size());
}

}  // namespace

bool fold_line_hidden(int line, const std::vector<FoldRegion>& regions,
                      const std::set<int>& collapsed_open_lines) {
  for (const FoldRegion& region : regions) {
    if (collapsed_open_lines.count(region.open_line) == 0) {
      continue;
    }
    if (line > region.open_line && line <= region.close_line) {
      return true;
    }
  }
  return false;
}

std::vector<int> visible_buffer_lines(int total_lines, const std::vector<FoldRegion>& regions,
                                      const std::set<int>& collapsed_open_lines) {
  std::vector<int> visible;
  visible.reserve(static_cast<std::size_t>(total_lines));
  for (int line = 0; line < total_lines; ++line) {
    if (!fold_line_hidden(line, regions, collapsed_open_lines)) {
      visible.push_back(line);
    }
  }
  return visible;
}

std::vector<int> viewport_buffer_lines(const EditorBuffer& buffer,
                                       const std::vector<FoldRegion>& regions,
                                       int viewport_count) {
  const int total = static_cast<int>(buffer.lines.size());
  if (total <= 0 || viewport_count <= 0) {
    return {};
  }
  const std::vector<int> visible =
      visible_buffer_lines(total, regions, buffer.collapsed_folds);
  if (visible.empty()) {
    return {};
  }

  std::vector<int> viewport;
  viewport.reserve(static_cast<std::size_t>(viewport_count));
  for (int line : visible) {
    if (line < buffer.scroll) {
      continue;
    }
    viewport.push_back(line);
    if (static_cast<int>(viewport.size()) >= viewport_count) {
      break;
    }
  }

  if (viewport.empty()) {
    const int start =
        std::max(0, static_cast<int>(visible.size()) - viewport_count);
    for (int i = start; i < static_cast<int>(visible.size()); ++i) {
      viewport.push_back(visible[static_cast<std::size_t>(i)]);
    }
  }
  return viewport;
}

const FoldRegion* fold_region_at_open_line(const std::vector<FoldRegion>& regions, int open_line) {
  for (const FoldRegion& region : regions) {
    if (region.open_line == open_line) {
      return &region;
    }
  }
  return nullptr;
}

char fold_gutter_marker(int buffer_line, const std::vector<FoldRegion>& regions,
                        const std::set<int>& collapsed_open_lines) {
  const FoldRegion* region = fold_region_at_open_line(regions, buffer_line);
  if (region == nullptr || region->close_line <= region->open_line) {
    return '\0';
  }
  return collapsed_open_lines.count(buffer_line) > 0 ? '+' : '-';
}

bool toggle_fold_at(EditorBuffer* buffer, int open_line, const std::vector<FoldRegion>& regions) {
  if (buffer == nullptr || fold_region_at_open_line(regions, open_line) == nullptr) {
    return false;
  }
  if (buffer->collapsed_folds.count(open_line) > 0) {
    buffer->collapsed_folds.erase(open_line);
  } else {
    buffer->collapsed_folds.insert(open_line);
    clamp_cursors_for_folds(buffer, regions);
  }
  buffer->view_token++;
  return true;
}

void clamp_cursors_for_folds(EditorBuffer* buffer, const std::vector<FoldRegion>& regions) {
  if (buffer == nullptr) {
    return;
  }
  for (MultiCursor& cursor : buffer->cursors) {
    if (!fold_line_hidden(cursor.head.line, regions, buffer->collapsed_folds)) {
      continue;
    }
    for (const FoldRegion& region : regions) {
      if (buffer->collapsed_folds.count(region.open_line) == 0) {
        continue;
      }
      if (cursor.head.line > region.open_line && cursor.head.line <= region.close_line) {
        const int line_len =
            static_cast<int>(buffer->lines[static_cast<std::size_t>(region.open_line)].size());
        cursor.set_pos(region.open_line, std::min(cursor.head.col, line_len));
        break;
      }
    }
  }
  clamp_all_cursors(buffer);
}

int fold_scroll_max(const EditorBuffer& buffer, const std::vector<FoldRegion>& regions,
                    int viewport_count) {
  const int total = static_cast<int>(buffer.lines.size());
  if (total <= 0) {
    return 0;
  }
  const std::vector<int> visible =
      visible_buffer_lines(total, regions, buffer.collapsed_folds);
  if (visible.empty()) {
    return 0;
  }
  if (static_cast<int>(visible.size()) <= viewport_count) {
    return visible.front();
  }
  return visible[visible.size() - static_cast<std::size_t>(viewport_count)];
}

void scroll_view_by_lines_fold_aware(EditorBuffer* buffer, int delta_lines,
                                     const std::vector<FoldRegion>& regions, int viewport_count) {
  if (buffer == nullptr || delta_lines == 0) {
    return;
  }
  const int total = static_cast<int>(buffer->lines.size());
  if (total <= 0) {
    return;
  }
  const std::vector<int> visible =
      visible_buffer_lines(total, regions, buffer->collapsed_folds);
  if (visible.empty()) {
    return;
  }

  int index = index_of_visible_at_or_after(visible, buffer->scroll);
  if (index >= static_cast<int>(visible.size())) {
    index = static_cast<int>(visible.size()) - 1;
  }
  index = std::max(0, std::min(static_cast<int>(visible.size()) - 1, index + delta_lines));
  buffer->scroll = visible[static_cast<std::size_t>(index)];

  const int max_scroll = fold_scroll_max(*buffer, regions, viewport_count);
  buffer->scroll = std::min(buffer->scroll, max_scroll);
  buffer->scroll = std::max(0, buffer->scroll);
}

void ensure_scroll_visible_fold_aware(EditorBuffer* buffer, const std::vector<FoldRegion>& regions,
                                      int viewport_count, int code_width) {
  if (buffer == nullptr) {
    return;
  }
  clamp_cursors_for_folds(buffer, regions);

  const int total = static_cast<int>(buffer->lines.size());
  if (total <= 0) {
    return;
  }
  const std::vector<int> visible =
      visible_buffer_lines(total, regions, buffer->collapsed_folds);
  if (visible.empty()) {
    return;
  }

  const int primary = buffer->primary_line();
  int primary_index = visible_line_index(visible, primary);
  if (primary_index < 0) {
    primary_index = 0;
  }

  int scroll_index = index_of_visible_at_or_after(visible, buffer->scroll);
  if (scroll_index >= static_cast<int>(visible.size())) {
    scroll_index = static_cast<int>(visible.size()) - 1;
  }

  if (primary_index < scroll_index) {
    buffer->scroll = visible[static_cast<std::size_t>(primary_index)];
    scroll_index = primary_index;
  } else if (primary_index >= scroll_index + viewport_count) {
    const int new_index = std::max(0, primary_index - viewport_count + 1);
    buffer->scroll = visible[static_cast<std::size_t>(new_index)];
  }

  const int max_scroll = fold_scroll_max(*buffer, regions, viewport_count);
  buffer->scroll = std::min(buffer->scroll, max_scroll);
  buffer->scroll = std::max(0, buffer->scroll);

  if (code_width <= 0) {
    return;
  }
  const int col = buffer->primary_col();
  constexpr int kMargin = 8;
  if (col < buffer->scroll_col + kMargin) {
    buffer->scroll_col = std::max(0, col - kMargin);
  } else if (col >= buffer->scroll_col + code_width - kMargin) {
    buffer->scroll_col = std::max(0, col - code_width + kMargin + 1);
  }
  const int line_len =
      static_cast<int>(buffer->lines[static_cast<std::size_t>(buffer->primary_line())].size());
  const int max_scroll_col = std::max(0, line_len - code_width + 1);
  buffer->scroll_col = std::min(buffer->scroll_col, max_scroll_col);
}

int visible_line_index(const std::vector<int>& visible_lines, int buffer_line) {
  for (std::size_t i = 0; i < visible_lines.size(); ++i) {
    if (visible_lines[i] == buffer_line) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace tgdb
