#pragma once

#include <string>
#include <vector>

namespace tgdb {

enum class SymbolKind { kNamespace, kClass, kStruct, kFunction, kMethod, kVariable };

struct SymbolInfo {
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  int depth = 0;
};

class ISymbolProvider {
 public:
  virtual ~ISymbolProvider() = default;
  virtual std::vector<SymbolInfo> symbols_for_file(const std::string& path) = 0;
};

}  // namespace tgdb
