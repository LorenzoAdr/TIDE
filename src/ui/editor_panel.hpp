#pragma once

#include <memory>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

ftxui::Component MakeEditorPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                 MainLayoutState* layout_state,
                                 std::shared_ptr<ISymbolProvider> symbols,
                                 WorkspaceIndexer* file_indexer,
                                 SymbolWorkspaceIndexer* symbol_indexer);

void flash_symbol_at_buffer_pos(WorkspaceModel* workspace, MainLayoutState* layout_state,
                                int line, int col, int visible_lines);

}  // namespace tgdb
