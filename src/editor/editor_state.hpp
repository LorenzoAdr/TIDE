#pragma once

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "editor/editor_buffer_source.hpp"
#include "editor/editor_folds.hpp"
#include "editor/editor_text.hpp"

namespace tgdb {

struct CursorPos {
  int line = 0;
  int col = 0;

  bool operator==(const CursorPos& other) const {
    return line == other.line && col == other.col;
  }

  bool operator!=(const CursorPos& other) const { return !(*this == other); }

  bool operator<(const CursorPos& other) const {
    if (line != other.line) {
      return line < other.line;
    }
    return col < other.col;
  }
};

struct MultiCursor {
  CursorPos anchor;
  CursorPos head;

  bool has_selection() const { return anchor != head; }

  void collapse_to_head() { anchor = head; }

  void set_pos(int line, int col) {
    anchor = {line, col};
    head = {line, col};
  }

  void normalized_range(int* start_line, int* start_col, int* end_line, int* end_col) const;
};

struct EditorSnapshot {
  EditorText lines;
  std::vector<MultiCursor> cursors;
};

struct EditorBuffer {
  std::string path;
  EditorText lines;
  std::vector<MultiCursor> cursors;
  std::vector<EditorSnapshot> undo_stack;
  std::vector<EditorSnapshot> redo_stack;
  int scroll = 0;
  int scroll_col = 0;
  bool dirty = false;
  uint64_t view_token = 0;
  bool semantic_layout_dirty = false;
  // Lowest line index whose content/index may have shifted since the last time
  // semantic_layout_dirty was consumed (see mark_editor_content_edited in
  // editor_panel.cpp). Lines below this boundary are guaranteed untouched by the edit
  // and can keep trusting their cached LSP semantic tokens even while the rest of the
  // document is treated as stale. INT_MAX means "no pending edit registered yet".
  int semantic_layout_dirty_from_line = std::numeric_limits<int>::max();
  bool undo_coalesce_open = false;
  int cached_max_line_len = -1;
  std::set<int> collapsed_folds;
  std::vector<FoldRegion> fold_regions;
  mutable EditorJoinedSourceCache joined_source_cache;

  int primary_line() const;
  int primary_col() const;
  void set_primary(int line, int col);
  void reset_to_single_cursor(int line, int col);
  void ensure_cursors();
  bool multi_cursor_active() const;
  MultiCursor& primary();
  const MultiCursor& primary() const;
};

void clamp_cursor(MultiCursor* cursor, const EditorBuffer& buffer);
void clamp_all_cursors(EditorBuffer* buffer);
void merge_overlapping_cursors(EditorBuffer* buffer);
void sort_cursors_for_edit(std::vector<MultiCursor>* cursors);
void exit_multi_cursor_mode(EditorBuffer* buffer);

}  // namespace tgdb
