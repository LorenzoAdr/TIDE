#pragma once

#include <functional>
#include <memory>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "git/git_service.hpp"
#include "ui/focus_manager.hpp"
#include "ui/panel.hpp"

namespace tgdb {

struct MainLayoutState;
struct EditorPanelHandlers;
using CommandCallback = std::function<void(const struct UiCommand&)>;

ftxui::Component MakeEditorPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                 MainLayoutState* layout_state,
                                 std::shared_ptr<ISymbolProvider> symbols,
                                 WorkspaceIndexer* file_indexer,
                                 SymbolWorkspaceIndexer* symbol_indexer,
                                 GitService* git_service, FocusRegion panel_focus,
                                 DebugModel* debug_model, CommandCallback on_command,
                                 EditorPanelHandlers* handlers);

void flash_symbol_at_buffer_pos(WorkspaceModel* workspace, MainLayoutState* layout_state,
                                int line, int col, int visible_lines);

}  // namespace tgdb
