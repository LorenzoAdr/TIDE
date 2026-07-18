#pragma once

#include <memory>
#include <optional>
#include <string>

#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "util/nm_reader.hpp"

namespace tuide {

std::optional<SourceLocation> resolve_nm_symbol_in_workspace(
    const NmSymbol& symbol, const std::string& workspace_root,
    SymbolWorkspaceIndexer* symbol_indexer, const std::shared_ptr<ISymbolProvider>& symbols,
    WorkspaceIndexer* file_indexer);

}  // namespace tuide
