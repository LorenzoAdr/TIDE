#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

struct AiMissingToastState {
  bool open = false;
  std::string pack_id;
  int selected = 0;  // 0=install, 1=ignore
  ftxui::Box install_box;
  ftxui::Box ignore_box;

  void show(std::string pack);
  void close();
  int action_count() const { return 2; }
};

ftxui::Component MakeAiMissingToastOverlay(ftxui::Component main, AiMissingToastState* state,
                                           MainLayoutState* layout_state,
                                           std::function<void()> on_install,
                                           std::function<void()> on_ignore);

}  // namespace tuide
