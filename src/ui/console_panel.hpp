#pragma once

#include <functional>
#include <memory>

#include "app/app_mode.hpp"
#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"
#include "ui/git_panel.hpp"
#include "git/git_service.hpp"
#include "terminal/shell_session.hpp"

namespace tuide {

using CommandCallback = std::function<void(const struct UiCommand&)>;

struct MainLayoutState;
struct RightSidebarState;

ftxui::Component MakeConsolePanel(AppMode* app_mode, DebugModel* model,
                                  ShellSession* shell, CommandCallback on_command,
                                  MainLayoutState* layout_state,
                                  FocusManagerState* focus, int* bottom_height,
                                  ShellLaunchConfigProvider shell_launch_config,
                                  WorkspaceModel* workspace,
                                  std::shared_ptr<ISymbolProvider> symbols,
                                  WorkspaceIndexer* indexer,
                                  SymbolWorkspaceIndexer* symbol_indexer,
                                  RightSidebarState* sidebar,
                                  GitService* git_service,
                                  GitPanelState* git_panel_state);

bool cycle_console_tab(MainLayoutState* layout_state, FocusManagerState* focus, int delta,
                       AppMode* app_mode, GitService* git = nullptr,
                       GitPanelState* git_state = nullptr);

}  // namespace tuide
