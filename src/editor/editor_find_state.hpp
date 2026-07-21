#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/editor_state.hpp"
#include "editor/find_match_runner.hpp"
#include "editor/text_search.hpp"

namespace tuide {

struct EditorFindState {
  bool open = false;
  std::string query;
  int cursor_pos = 0;
  // When >= 0 and different from cursor_pos, the find query has a selection
  // spanning [min(anchor, cursor), max(anchor, cursor)).
  int selection_anchor = -1;
  std::vector<TextMatch> matches;

  bool has_query_selection() const {
    return selection_anchor >= 0 && selection_anchor != cursor_pos;
  }

  void clear_query_selection() { selection_anchor = -1; }

  void select_all_query() {
    if (query.empty()) {
      cursor_pos = 0;
      selection_anchor = -1;
      return;
    }
    selection_anchor = 0;
    cursor_pos = static_cast<int>(query.size());
  }

  void query_selection_bounds(int* start, int* end) const {
    if (start == nullptr || end == nullptr) {
      return;
    }
    if (!has_query_selection()) {
      *start = cursor_pos;
      *end = cursor_pos;
      return;
    }
    *start = std::min(selection_anchor, cursor_pos);
    *end = std::max(selection_anchor, cursor_pos);
  }

  // Replaces the current selection (or inserts at the cursor if none) and
  // clears the selection.
  void replace_query_selection(const std::string& text);

  void request_matches(const EditorBuffer& buffer);
  bool tick_matches(const EditorBuffer& buffer);
  bool matches_inflight() const;
  bool jump_to_next_match(EditorBuffer* buffer, int visible_lines);
  void cancel_matches();
  void reset_search_state();

 private:
  FindMatchKey committed_key_;
  FindMatchRunner runner_;
  uint64_t request_counter_ = 0;
  uint64_t inflight_id_ = 0;
};

void open_find_bar(EditorFindState* find, EditorBuffer* buffer);
void close_find_bar(EditorFindState* find);

}  // namespace tuide
