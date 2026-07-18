#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_provider.hpp"

namespace tuide {

struct BreadcrumbItem {
  std::string label;
  int line = 0;
};

struct StickyLine {
  int source_line = 0;
  std::string text;
  int depth = 0;
};

std::vector<const SymbolInfo*> scope_chain_at_line(
    const std::vector<SymbolInfo>& symbols, int line_0based);

// 1-based inclusive end line for a symbol entry.
int symbol_end_line_1based(const std::vector<SymbolInfo>& symbols, std::size_t index);

const SymbolInfo* innermost_scope_symbol(const std::vector<SymbolInfo>& symbols,
                                         int line_0based,
                                         bool (*predicate)(SymbolKind kind));

std::vector<BreadcrumbItem> build_breadcrumbs(const std::string& file_label,
                                              const std::vector<SymbolInfo>& symbols,
                                              int cursor_line_0based);

std::vector<StickyLine> sticky_lines_for_scroll(const std::vector<SymbolInfo>& symbols,
                                                const std::vector<std::string>& buffer_lines,
                                                int scroll_line_0based, int max_lines = 3);

std::string truncate_line_preview(const std::string& line, int max_cols = 120);

}  // namespace tuide
