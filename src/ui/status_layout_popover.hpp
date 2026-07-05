#pragma once

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

namespace tgdb {

struct MainLayoutState;

struct StatusLayoutPopoverState {
  bool open = false;
  ftxui::Box menu_box;
  ftxui::Box files_row_box;
  ftxui::Box outline_row_box;
  ftxui::Box terminal_row_box;
};

ftxui::Element RenderStatusLayoutPopoverOverlay(StatusLayoutPopoverState* popover,
                                                MainLayoutState* layout_state,
                                                ftxui::Element base,
                                                const ftxui::Box& anchor_box);

bool HandleStatusLayoutPopoverMouse(StatusLayoutPopoverState* popover,
                                    MainLayoutState* layout_state,
                                    const ftxui::Box& anchor_box, ftxui::Event event);

bool HandleStatusLayoutPopoverKeys(StatusLayoutPopoverState* popover, ftxui::Event event);

}  // namespace tgdb
