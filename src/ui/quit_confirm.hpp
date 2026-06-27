#pragma once

#include <functional>

#include "ftxui/component/component_base.hpp"

namespace tgdb {

struct QuitConfirmState {
  bool open = false;
  int selected = 0;  // 0 = Sí, 1 = No
};

ftxui::Component MakeQuitConfirmOverlay(ftxui::Component main, QuitConfirmState* state,
                                        std::function<void()> on_confirm);

}  // namespace tgdb
