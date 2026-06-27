#pragma once

#include <functional>
#include <memory>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

ftxui::Component MakeFileTreePanel(DebugModel* model, WorkspaceModel* workspace,
                                   FocusManagerState* focus,
                                   CommandCallback on_command);

}  // namespace tgdb
