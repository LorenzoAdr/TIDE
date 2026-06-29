#pragma once

#include <string>
#include <vector>

#include "editor/editor_state.hpp"
#include "editor/text_search.hpp"

namespace tgdb {

struct EditorFindState {
  bool open = false;
  std::string query;
  int cursor_pos = 0;
  std::vector<TextMatch> matches;

  void refresh_matches(const EditorBuffer& buffer);
  bool jump_to_next_match(EditorBuffer* buffer, int visible_lines);
};

void open_find_bar(EditorFindState* find, EditorBuffer* buffer);
void close_find_bar(EditorFindState* find);

}  // namespace tgdb
