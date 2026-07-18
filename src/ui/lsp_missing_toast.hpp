#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"
#include "util/lsp_missing_prompt.hpp"

namespace tgdb {

struct LspMissingToastState {
  bool open = false;
  LspMissingPromptInfo info;
  bool show_bundle_action = false;
  int selected = 0;  // 0=install, 1=bundle(if shown), last=ignore
  ftxui::Box install_box;
  ftxui::Box bundle_box;
  ftxui::Box ignore_box;

  void show(LspMissingPromptInfo prompt, bool can_bundle);
  void close();
  int action_count() const;
};

ftxui::Component MakeLspMissingToastOverlay(
    ftxui::Component main, LspMissingToastState* state, MainLayoutState* layout_state,
    std::function<void()> on_install, std::function<void()> on_bundle,
    std::function<void()> on_ignore);

}  // namespace tgdb
