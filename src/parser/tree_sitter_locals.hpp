#pragma once

#include <string>
#include <vector>

#include "editor/bracket_match.hpp"
#include "editor/editor_folds.hpp"
#include "symbols/symbol_provider.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tuide {

struct ScopeLineRange {
  int start_line = 0;
  int end_line = 0;
  bool valid = false;

  bool contains(int line_0based) const {
    return valid && line_0based >= start_line && line_0based <= end_line;
  }
};

std::vector<SymbolInfo> scope_symbols_from_tree(TSNode root, const std::string& source,
                                                const std::string& file_path);

std::vector<const SymbolInfo*> scope_chain_at_point(const std::vector<SymbolInfo>& scope_symbols,
                                                    int line_0based);

ScopeLineRange innermost_scope_range_from_symbols(const std::vector<SymbolInfo>& scope_symbols,
                                                  int line_0based, int col_0based);

BracketPairHighlight scope_bracket_pair_from_tree(TSNode root, const std::string& source,
                                                  int line_0based, int col_0based);

std::vector<FoldRegion> fold_regions_from_tree(TSNode root, const std::string& source);

std::vector<std::string> visible_local_names_at(TSNode root, const std::string& source, int line,
                                                int col);

}  // namespace tuide
