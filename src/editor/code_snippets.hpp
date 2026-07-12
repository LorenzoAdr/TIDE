#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_provider.hpp"

namespace tgdb {

// When false, structure/class skeleton completions are not offered (code paths remain).
constexpr bool kStructureSnippetCompletionsEnabled = false;

std::vector<CompletionItem> structure_snippet_completions(const std::string& query);

bool structure_snippet_prefix_active(const std::string& prefix);

}  // namespace tgdb
