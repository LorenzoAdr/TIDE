#pragma once

#include <functional>
#include <memory>

#include "app/app_mode.hpp"
#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"
#include "terminal/shell_session.hpp"

namespace tgdb {

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
                                  RightSidebarState* sidebar);

bool cycle_console_tab(MainLayoutState* layout_state, FocusManagerState* focus, int delta,
                       AppMode* app_mode);

}  // namespace tgdb
