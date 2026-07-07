#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_provider.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tgdb {

std::vector<CompletionItem> local_completions_at(TSNode root, const std::string& source,
                                                   const CompletionParams& params);

}  // namespace tgdb
