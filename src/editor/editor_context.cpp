#include "editor/editor_context.hpp"

#include <algorithm>

namespace tgdb {

namespace {

bool is_scope_kind(SymbolKind kind) {
  return kind == SymbolKind::kNamespace || kind == SymbolKind::kClass ||
         kind == SymbolKind::kStruct || kind == SymbolKind::kFunction ||
         kind == SymbolKind::kMethod;
}

}  // namespace

std::vector<const SymbolInfo*> scope_chain_at_line(
    const std::vector<SymbolInfo>& symbols, int line_0based) {
  const int line_1 = line_0based + 1;
  std::vector<const SymbolInfo*> chain;

  for (const auto& sym : symbols) {
    if (sym.line > line_1) {
      break;
    }
    while (!chain.empty() && chain.back()->depth >= sym.depth) {
      chain.pop_back();
    }
    chain.push_back(&sym);
  }

  return chain;
}

std::vector<BreadcrumbItem> build_breadcrumbs(const std::string& file_label,
                                              const std::vector<SymbolInfo>& symbols,
                                              int cursor_line_0based) {
  std::vector<BreadcrumbItem> crumbs;
  crumbs.push_back({file_label, 0});

  for (const SymbolInfo* sym : scope_chain_at_line(symbols, cursor_line_0based)) {
    if (!is_scope_kind(sym->kind)) {
      continue;
    }
    crumbs.push_back({sym->name, std::max(0, sym->line - 1)});
  }

  return crumbs;
}

std::string truncate_line_preview(const std::string& line, int max_cols) {
  if (max_cols <= 0) {
    return line;
  }
  if (static_cast<int>(line.size()) <= max_cols) {
    return line;
  }
  if (max_cols <= 3) {
    return line.substr(0, static_cast<std::size_t>(max_cols));
  }
  return line.substr(0, static_cast<std::size_t>(max_cols - 3)) + "...";
}

std::vector<StickyLine> sticky_lines_for_scroll(const std::vector<SymbolInfo>& symbols,
                                                const std::vector<std::string>& buffer_lines,
                                                int scroll_line_0based, int max_lines) {
  std::vector<StickyLine> sticky;
  if (symbols.empty() || buffer_lines.empty() || max_lines <= 0) {
    return sticky;
  }

  const auto chain = scope_chain_at_line(symbols, scroll_line_0based);
  for (const SymbolInfo* sym : chain) {
    const int sym_line_0 = sym->line - 1;
    if (sym_line_0 >= scroll_line_0based) {
      continue;
    }
    if (!is_scope_kind(sym->kind)) {
      continue;
    }
    if (sym_line_0 < 0 || sym_line_0 >= static_cast<int>(buffer_lines.size())) {
      continue;
    }
    StickyLine row;
    row.source_line = sym_line_0;
    row.text = truncate_line_preview(buffer_lines[static_cast<std::size_t>(sym_line_0)]);
    row.depth = sym->depth;
    sticky.push_back(std::move(row));
  }

  if (static_cast<int>(sticky.size()) > max_lines) {
    sticky.erase(sticky.begin(),
                 sticky.end() - static_cast<std::ptrdiff_t>(max_lines));
  }
  return sticky;
}

}  // namespace tgdb
