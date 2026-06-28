#pragma once

#include "editor/editor_state.hpp"

namespace tgdb {

struct BracketPairHighlight {
  bool valid = false;
  int line_a = -1;
  int col_a = -1;
  int line_b = -1;
  int col_b = -1;
};

BracketPairHighlight find_bracket_pair_highlight(const EditorBuffer& buffer, int line, int col);

}  // namespace tgdb
