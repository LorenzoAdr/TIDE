#pragma once

#include "ftxui/component/component_base.hpp"

namespace tgdb {

struct ShortcutsModalState {
  bool open = false;
  int first_visible = 0;
};

ftxui::Component MakeShortcutsModalOverlay(ftxui::Component main,
                                         ShortcutsModalState* state);

}  // namespace tgdb
