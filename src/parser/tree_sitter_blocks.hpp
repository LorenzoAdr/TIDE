#pragma once

#include <string>
#include <vector>

#include "editor/bracket_match.hpp"
#include "symbols/symbol_provider.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tgdb {

bool cursor_in_code_node(TSNode root, const std::string& source, int line, int col);

BracketPairHighlight bracket_pair_at(TSNode root, const std::string& source, int line, int col);

BracketPairHighlight enclosing_bracket_pair(TSNode root, const std::string& source, int line,
                                            int col, char open_ch);

BracketPairHighlight enclosing_bracket_pair_for_block_nav(TSNode root, const std::string& source,
                                                          int line, int col, char open_ch,
                                                          bool jump_to_start);

TextSpan enclosing_quote_pair(TSNode root, const std::string& source, int line, int col,
                              char quote_ch);

TextSpan enclosing_line_comment(TSNode root, const std::string& source, int line, int col);

TextSpan enclosing_block_comment(TSNode root, const std::string& source, int line, int col);

std::vector<ColoredBraceMarker> colored_curly_braces(TSNode root, const std::string& source);

}  // namespace tgdb
