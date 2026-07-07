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

struct ColoredBraceMarker {
  int line = 0;
  int col = 0;
  int depth = 0;
};

struct TextSpan {
  bool valid = false;
  int line_a = -1;
  int col_a = -1;
  int line_b = -1;
  int col_b = -1;
};

BracketPairHighlight find_bracket_pair_highlight(const EditorBuffer& buffer, int line, int col);

BracketPairHighlight find_scope_bracket_pair(const EditorBuffer& buffer, int line, int col);

std::vector<ColoredBraceMarker> find_colored_curly_braces(const EditorBuffer& buffer);

// Innermost `{`/`[`/`(` pair enclosing (line, col), if any.
BracketPairHighlight find_enclosing_bracket_pair(const EditorBuffer& buffer, int line, int col,
                                               char open_ch = '{');

// Innermost enclosing pair among `(`, `[`, `{`.
BracketPairHighlight find_innermost_enclosing_pair(const EditorBuffer& buffer, int line, int col);

// Like find_enclosing_bracket_pair, but when already on the innermost block
// delimiter, returns the next outer `{`/`[`/`(` pair (for repeated `[}` / `]}`).
BracketPairHighlight find_enclosing_bracket_pair_for_block_nav(const EditorBuffer& buffer,
                                                               int line, int col, char open_ch,
                                                               bool jump_to_start);

TextSpan find_enclosing_quote_pair(const EditorBuffer& buffer, int line, int col, char quote_ch);

TextSpan find_enclosing_line_comment(const EditorBuffer& buffer, int line, int col);

TextSpan find_enclosing_block_comment(const EditorBuffer& buffer, int line, int col);

// True when the cursor sits in normal code (not in a comment or string literal).
bool cursor_in_code(const EditorBuffer& buffer, int line, int col);

}  // namespace tgdb
