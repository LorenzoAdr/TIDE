#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ui/file_picker_preview.hpp"
#include "ui/main_layout.hpp"
#include "ui/path_browser.hpp"

namespace tgdb {

struct ExternalFileWizardState {
  bool open = false;
  PathBrowserState browser;
  std::string launch_root;
  FilePickerPreview preview;
  std::string preview_requested_path;
  std::vector<BrowserEntry> folder_preview_entries;
  std::string folder_preview_path;
  bool folder_preview_active = false;

  void reset();
  void set_preview_notify(std::function<void()> notify);
  void update_preview_for_selection();
  void reset_preview();
};

using ExternalFileCompleteCallback = std::function<void(const std::string& absolute_path)>;

ftxui::Component MakeExternalFileWizardOverlay(
    ftxui::Component main, ExternalFileWizardState* state, MainLayoutState* layout_state,
    ExternalFileCompleteCallback on_open);

}  // namespace tgdb
