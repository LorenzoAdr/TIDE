#pragma once

#include "symbols/symbol_provider.hpp"

#include <vector>

namespace tgdb {

std::vector<CompletionItem> local_scope_completions(const std::string& text, int line,
                                                      int character);
void merge_completion_items(std::vector<CompletionItem>* target,
                            const std::vector<CompletionItem>& extras);

}  // namespace tgdb
