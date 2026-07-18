#include "symbols/tree_sitter_symbol_provider.hpp"

#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_service.hpp"
#include "util/csv_viewer.hpp"

namespace tuide {

std::vector<SymbolInfo> TreeSitterSymbolProvider::symbols_for_file(const std::string& path) {
  if (path.empty() || is_tabular_path(path)) {
    return {};
  }
  return tree_sitter_service().symbols_for_file(path, join_editor_lines_from_file(path));
}

bool TreeSitterSymbolProvider::supports_hover() const { return true; }

HoverInfo TreeSitterSymbolProvider::hover_at(const HoverParams& params) {
  return tree_sitter_service().hover_at(params);
}

std::vector<CompletionItem> TreeSitterSymbolProvider::completions_at(
    const CompletionParams& params) {
  return tree_sitter_service().local_completions_at(params);
}

}  // namespace tuide
