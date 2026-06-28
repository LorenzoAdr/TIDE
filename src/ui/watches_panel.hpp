#pragma once

#include <functional>

#include "ftxui/component/component_base.hpp"
#include "app/debug_model.hpp"

#include "ui/focus_manager.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

struct MainLayoutState;

ftxui::Component MakeWatchesPanel(DebugModel* model, CommandCallback on_command,
                                  MainLayoutState* layout_state,
                                  const std::function<void()>& on_stop_debug,
                                  FocusManagerState* focus);

}  // namespace tgdb
