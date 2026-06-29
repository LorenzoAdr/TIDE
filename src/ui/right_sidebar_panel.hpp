#pragma once

#include "ui/main_layout.hpp"
#include "ftxui/component/component_base.hpp"

namespace tgdb {

ftxui::Component MakeRightSidebarPanel(ftxui::Component outline, ftxui::Component search,
                                     ftxui::Component call_hierarchy, RightSidebarState* state,
                                     MainLayoutState* layout_state);

}  // namespace tgdb
