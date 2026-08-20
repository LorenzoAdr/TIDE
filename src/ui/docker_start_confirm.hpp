#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

struct DockerStartConfirmState {
  bool open = false;
  int selected = 0;  // 0 = Sí, 1 = No
  std::string container;
  ftxui::Box yes_box;
  ftxui::Box no_box;

  bool is_open() const { return open; }

  void show(const std::string& container_name);
  void close();
};

ftxui::Component MakeDockerStartConfirmOverlay(ftxui::Component main,
                                               DockerStartConfirmState* state,
                                               MainLayoutState* layout_state,
                                               std::function<void()> on_confirm,
                                               std::function<void()> on_cancel);

}  // namespace tuide
