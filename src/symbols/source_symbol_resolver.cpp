#include "symbols/source_symbol_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include "search/workspace_search.hpp"

namespace tuide {

namespace fs = std::filesystem;

namespace {

std::string trim_copy(const std::string& value) {
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string base_symbol_name(const std::string& name) {
  const auto paren = name.find('(');
  if (paren != std::string::npos) {
    return trim_copy(name.substr(0, paren));
  }
  const auto angle = name.rfind("::");
  if (angle != std::string::npos && angle + 2 < name.size()) {
    return name.substr(angle + 2);
  }
  return name;
}

bool names_match(const std::string& a, const std::string& b) {
  if (a == b) {
    return true;
  }
  return base_symbol_name(a) == base_symbol_name(b);
}

SourceLocation make_location(const std::string& path, int line, int character = 0) {
  SourceLocation loc;
  loc.path = path;
  loc.line = line;
  loc.character = character;
  loc.valid = !path.empty() && line >= 0;
  return loc;
}

std::string absolute_from_nm_source(const std::string& workspace_root,
                                    const std::string& source_file) {
  if (source_file.empty()) {
    return {};
  }
  std::error_code ec;
  fs::path path = source_file;
  if (path.is_absolute()) {
    return path.string();
  }
  if (!workspace_root.empty()) {
    const fs::path joined = fs::path(workspace_root) / path;
    if (fs::exists(joined, ec)) {
      return fs::absolute(joined, ec).string();
    }
  }
  return source_file;
}

std::optional<SourceLocation> lookup_in_symbol_index(const NmSymbol& symbol,
                                                     const std::string& workspace_root,
                                                     SymbolWorkspaceIndexer* symbol_indexer) {
  if (symbol_indexer == nullptr) {
    return std::nullopt;
  }
  const auto snapshot = symbol_indexer->snapshot();
  if (!snapshot) {
    return std::nullopt;
  }

  std::vector<const IndexedSymbol*> matches;
  for (const auto& indexed : snapshot->symbols) {
    if (names_match(indexed.display_name, symbol.name)) {
      matches.push_back(&indexed);
    }
  }
  if (matches.empty()) {
    return std::nullopt;
  }

  const IndexedSymbol* chosen = matches.front();
  if (matches.size() > 1) {
    for (const IndexedSymbol* candidate : matches) {
      if (candidate->kind == SymbolKind::kFunction || candidate->kind == SymbolKind::kMethod) {
        chosen = candidate;
        break;
      }
    }
  }

  std::string absolute = chosen->file;
  if (!absolute.empty() && !fs::path(absolute).is_absolute()) {
    absolute = (fs::path(workspace_root) / absolute).string();
  }
  return make_location(absolute, std::max(0, chosen->line - 1));
}

std::optional<SourceLocation> lookup_in_lsp(const NmSymbol& symbol,
                                            const std::string& workspace_root,
                                            const std::shared_ptr<ISymbolProvider>& symbols) {
  if (symbols == nullptr || workspace_root.empty()) {
    return std::nullopt;
  }
  const auto found = symbols->workspace_symbols(workspace_root, symbol.name);
  if (found.empty()) {
    const std::string short_name = base_symbol_name(symbol.name);
    if (short_name != symbol.name) {
      const auto retry = symbols->workspace_symbols(workspace_root, short_name);
      for (const auto& item : retry) {
        if (names_match(item.name, symbol.name)) {
          std::string absolute = item.file;
          if (!absolute.empty() && !fs::path(absolute).is_absolute()) {
            absolute = (fs::path(workspace_root) / absolute).string();
          }
          return make_location(absolute, std::max(0, item.line - 1));
        }
      }
    }
    return std::nullopt;
  }

  for (const auto& item : found) {
    if (names_match(item.name, symbol.name)) {
      std::string absolute = item.file;
      if (!absolute.empty() && !fs::path(absolute).is_absolute()) {
        absolute = (fs::path(workspace_root) / absolute).string();
      }
      return make_location(absolute, std::max(0, item.line - 1));
    }
  }
  return std::nullopt;
}

std::optional<SourceLocation> lookup_with_search(const NmSymbol& symbol,
                                                 const std::string& workspace_root,
                                                 WorkspaceIndexer* file_indexer) {
  if (workspace_root.empty()) {
    return std::nullopt;
  }

  WorkspaceSearchOptions opts;
  opts.workspace_root = workspace_root;
  opts.needle = base_symbol_name(symbol.name);
  if (opts.needle.empty()) {
    return std::nullopt;
  }
  if (file_indexer != nullptr) {
    if (const auto snapshot = file_indexer->snapshot()) {
      if (snapshot->workspace_root == workspace_root) {
        opts.files = snapshot->files;
      }
    }
  }
  if (opts.files.empty()) {
    opts.files = scan_workspace_files(workspace_root);
  }

  const auto results = search_workspace(opts);
  for (const auto& hit : results) {
    if (hit.file.find(".cpp") != std::string::npos || hit.file.find(".hpp") != std::string::npos ||
        hit.file.find(".h") != std::string::npos || hit.file.find(".cc") != std::string::npos) {
      std::string absolute = hit.file;
      if (!fs::path(absolute).is_absolute()) {
        absolute = (fs::path(workspace_root) / absolute).string();
      }
      return make_location(absolute, std::max(0, hit.line - 1), std::max(0, hit.col - 1));
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<SourceLocation> resolve_nm_symbol_in_workspace(
    const NmSymbol& symbol, const std::string& workspace_root,
    SymbolWorkspaceIndexer* symbol_indexer, const std::shared_ptr<ISymbolProvider>& symbols,
    WorkspaceIndexer* file_indexer) {
  if (!symbol.source_file.empty() && symbol.source_line > 0) {
    const std::string absolute = absolute_from_nm_source(workspace_root, symbol.source_file);
    if (!absolute.empty()) {
      return make_location(absolute, std::max(0, symbol.source_line - 1));
    }
  }

  if (auto loc = lookup_in_lsp(symbol, workspace_root, symbols)) {
    return loc;
  }
  if (auto loc = lookup_in_symbol_index(symbol, workspace_root, symbol_indexer)) {
    return loc;
  }
  return lookup_with_search(symbol, workspace_root, file_indexer);
}

}  // namespace tuide
