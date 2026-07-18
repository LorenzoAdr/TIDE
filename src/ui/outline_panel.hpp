#pragma once

#include <memory>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

ftxui::Component MakeOutlinePanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                  MainLayoutState* layout_state);

}  // namespace tuide
