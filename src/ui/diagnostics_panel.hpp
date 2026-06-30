#pragma once

#include <memory>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

ftxui::Component MakeDiagnosticsPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                      std::shared_ptr<ISymbolProvider> symbols,
                                      MainLayoutState* layout_state,
                                      WorkspaceIndexer* indexer);

}  // namespace tgdb
