#pragma once

#include <functional>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"
#include "ui/shutdown_overlay.hpp"

namespace tgdb {

struct QuitConfirmState {
  bool open = false;
  int selected = 0;  // 0 = Sí, 1 = No
  ftxui::Box yes_box;
  ftxui::Box no_box;
};

ftxui::Component MakeQuitConfirmOverlay(ftxui::Component main, QuitConfirmState* state,
                                        MainLayoutState* layout_state,
                                        ShutdownState* shutdown_state,
                                        std::function<void()> on_confirm);

}  // namespace tgdb
