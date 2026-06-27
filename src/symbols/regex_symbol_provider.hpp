#pragma once

#include "symbols/symbol_provider.hpp"

namespace tgdb {

class RegexSymbolProvider : public ISymbolProvider {
 public:
  std::vector<SymbolInfo> symbols_for_file(const std::string& path) override;
};

}  // namespace tgdb
