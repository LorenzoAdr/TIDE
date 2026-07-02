#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ui/main_layout.hpp"
#include "ui/path_browser.hpp"

namespace tgdb {

struct ExternalFileWizardState {
  bool open = false;
  PathBrowserState browser;
  std::string launch_root;

  void reset();
};

using ExternalFileCompleteCallback = std::function<void(const std::string& absolute_path)>;

ftxui::Component MakeExternalFileWizardOverlay(
    ftxui::Component main, ExternalFileWizardState* state, MainLayoutState* layout_state,
    ExternalFileCompleteCallback on_open);

}  // namespace tgdb
