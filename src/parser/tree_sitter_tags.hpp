#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_kind.hpp"

namespace tuide {

enum class RepoMapTagKind { Def, Ref };

struct RepoMapTag {
  RepoMapTagKind tag_kind = RepoMapTagKind::Def;
  std::string name;       // without outline prefixes ("f ", "C ", …)
  SymbolKind symbol_kind = SymbolKind::kFunction;
  std::string rel_file;
  int line = 0;           // 1-based
  std::string signature;  // Def only: trimmed source line
};

// Sync parse + defs (extract_symbols_from_tree) + identifier refs. No UI cache.
std::vector<RepoMapTag> extract_repo_map_tags(const std::string& abs_path,
                                             const std::string& rel_path,
                                             const std::string& source);

std::vector<RepoMapTag> extract_repo_map_tags_for_file(const std::string& workspace_root,
                                                      const std::string& rel_path);

}  // namespace tuide
