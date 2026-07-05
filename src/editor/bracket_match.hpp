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

// Innermost `{`/`[`/`(` pair enclosing (line, col), if any.
BracketPairHighlight find_enclosing_bracket_pair(const EditorBuffer& buffer, int line, int col,
                                               char open_ch = '{');

// True when the cursor sits in normal code (not in a comment or string literal).
bool cursor_in_code(const EditorBuffer& buffer, int line, int col);

}  // namespace tgdb
