#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/editor_state.hpp"
#include "editor/find_match_runner.hpp"
#include "editor/text_search.hpp"

namespace tgdb {

struct EditorFindState {
  bool open = false;
  std::string query;
  int cursor_pos = 0;
  std::vector<TextMatch> matches;

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

}  // namespace tgdb
