#pragma once

#include <functional>

#include "ftxui/component/component_base.hpp"
#include "app/debug_model.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

ftxui::Component MakeWatchesPanel(DebugModel* model, CommandCallback on_command);

}  // namespace tgdb
