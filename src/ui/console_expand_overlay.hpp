#pragma once

#include "ftxui/component/component_base.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

ftxui::Component MakeConsoleExpandOverlay(ftxui::Component main, MainLayoutState* layout_state);

}  // namespace tuide
