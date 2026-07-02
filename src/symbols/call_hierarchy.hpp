#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "symbols/symbol_kind.hpp"

namespace tgdb {

struct CallHierarchyItem {
  bool valid = false;
  std::string name;
  std::string detail;
  std::string path;
  int line = 0;
  int character = 0;
  SymbolKind kind = SymbolKind::kFunction;
  bool has_call_site = false;
  int call_site_line = 0;
  int call_site_character = 0;
  nlohmann::json lsp_payload = nlohmann::json::object();
};

struct CallHierarchyParams {
  std::string path;
  std::string text;
  int line = 0;
  int character = 0;
};

}  // namespace tgdb
