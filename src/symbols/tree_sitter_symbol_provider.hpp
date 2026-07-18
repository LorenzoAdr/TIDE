#pragma once

#include "symbols/symbol_provider.hpp"

namespace tuide {

class TreeSitterSymbolProvider : public ISymbolProvider {
 public:
  std::vector<SymbolInfo> symbols_for_file(const std::string& path) override;
  bool supports_hover() const override;
  HoverInfo hover_at(const HoverParams& params) override;
  std::vector<CompletionItem> completions_at(const CompletionParams& params) override;
};

}  // namespace tuide
