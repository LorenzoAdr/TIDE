#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_provider.hpp"

namespace tgdb {

std::vector<CompletionItem> structure_snippet_completions(const std::string& query);

bool structure_snippet_prefix_active(const std::string& prefix);

}  // namespace tgdb
