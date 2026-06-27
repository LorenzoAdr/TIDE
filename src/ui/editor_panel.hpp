#pragma once

#include <functional>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

ftxui::Component MakeEditorPanel(WorkspaceModel* workspace, FocusManagerState* focus);

}  // namespace tgdb
