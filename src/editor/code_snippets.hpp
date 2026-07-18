#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_provider.hpp"

namespace tuide {

// When false, structure/class skeleton completions are not offered (code paths remain).
constexpr bool kStructureSnippetCompletionsEnabled = false;

struct CodeTemplate {
  std::string id;
  std::string label;
  std::string detail;
  std::string body;
  SymbolKind kind = SymbolKind::kFunction;
};

// Templates for the language of `path` (empty path → C++ defaults).
std::vector<CodeTemplate> code_templates_for_path(const std::string& path);

std::vector<CompletionItem> structure_snippet_completions(const std::string& query);
std::vector<CompletionItem> structure_snippet_completions_for_path(const std::string& path,
                                                                   const std::string& query);

bool structure_snippet_prefix_active(const std::string& prefix);
bool structure_snippet_prefix_active_for_path(const std::string& path, const std::string& prefix);

}  // namespace tuide
