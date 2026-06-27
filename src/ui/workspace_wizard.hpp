#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ui/path_browser.hpp"

namespace tgdb {

struct WorkspaceWizardState {
  bool open = false;
  PathBrowserState browser;
  std::string launch_root;

  void reset();
};

using WorkspaceCompleteCallback = std::function<void(const std::string& workspace_root)>;

ftxui::Component MakeWorkspaceWizardOverlay(
    ftxui::Component main, WorkspaceWizardState* state,
    WorkspaceCompleteCallback on_complete,
    std::function<void()> on_request_quit = {});

}  // namespace tgdb
