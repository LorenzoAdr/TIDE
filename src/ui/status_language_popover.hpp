#pragma once

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

namespace tuide {

struct MainLayoutState;

struct StatusLanguagePopoverState {
  bool open = false;
  int scroll = 0;
  ftxui::Box menu_box;
  ftxui::Box auto_row_box;
  std::vector<ftxui::Box> language_row_boxes;
};

ftxui::Element RenderStatusLanguagePopoverOverlay(StatusLanguagePopoverState* popover,
                                                  MainLayoutState* layout_state,
                                                  ftxui::Element base,
                                                  const ftxui::Box& anchor_box,
                                                  const std::string& active_path);

bool HandleStatusLanguagePopoverMouse(StatusLanguagePopoverState* popover,
                                      MainLayoutState* layout_state,
                                      const ftxui::Box& anchor_box, ftxui::Event event,
                                      const std::string& active_path);

bool HandleStatusLanguagePopoverKeys(StatusLanguagePopoverState* popover, ftxui::Event event);

}  // namespace tuide
