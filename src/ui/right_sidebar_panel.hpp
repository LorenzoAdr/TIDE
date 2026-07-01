#pragma once

#include "ui/main_layout.hpp"
#include "ftxui/component/component_base.hpp"

namespace tgdb {

ftxui::Component MakeRightSidebarPanel(ftxui::Component outline, MainLayoutState* layout_state);

}  // namespace tgdb
