#pragma once

#include <functional>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

struct WelcomeScreenState {
  ftxui::Box external_file_action_box;
  ftxui::Box debug_action_box;
  ftxui::Box workspace_action_box;
};

ftxui::Element RenderTuideLogo();

ftxui::Component MakeWelcomeScreen(MainLayoutState* layout_state, WelcomeScreenState* state,
                                   std::function<void()> on_external_file,
                                   std::function<void()> on_debug,
                                   std::function<void()> on_workspace);

}  // namespace tuide
