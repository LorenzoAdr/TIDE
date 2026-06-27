#pragma once

#include <functional>

#include "app/app_mode.hpp"
#include "app/debug_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/focus_manager.hpp"
#include "terminal/shell_session.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

struct MainLayoutState;

ftxui::Component MakeConsolePanel(AppMode* app_mode, DebugModel* model,
                                  ShellSession* shell, CommandCallback on_command,
                                  MainLayoutState* layout_state,
                                  FocusManagerState* focus);

}  // namespace tgdb
