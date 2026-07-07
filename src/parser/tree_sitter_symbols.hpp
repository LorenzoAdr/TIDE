#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_kind.hpp"
#include "symbols/symbol_provider.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tgdb {

std::vector<SymbolInfo> extract_symbols_from_tree(TSNode root, const std::string& source,
                                                    const std::string& file_path);

}  // namespace tgdb
