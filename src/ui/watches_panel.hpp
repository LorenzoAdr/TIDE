#pragma once

#include <functional>

#include "ftxui/component/component_base.hpp"
#include "app/debug_model.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

struct MainLayoutState;

ftxui::Component MakeWatchesPanel(DebugModel* model, CommandCallback on_command,
                                  MainLayoutState* layout_state);

}  // namespace tgdb
