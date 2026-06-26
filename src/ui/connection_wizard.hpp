#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "backend/idebug_backend.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "util/process_list.hpp"

namespace tgdb {

enum class WizardStep { ChooseMode, PickBinary, PickWorkspace, PickProcess };

enum class WizardMode { Launch, Attach };

struct BrowserEntry {
  std::string name;
  std::string path;
  bool is_directory = false;
  bool is_parent = false;
};

struct ConnectionResult {
  SessionMode mode = SessionMode::kLaunch;
  std::string program;
  std::string workspace_root;
  int attach_pid = 0;
};

struct ConnectionWizardState {
  bool open = false;
  WizardStep step = WizardStep::ChooseMode;
  WizardMode mode = WizardMode::Attach;
  int mode_selected = 0;

  std::string browser_path;
  std::string browser_loaded_path;
  std::string launch_root;
  std::vector<BrowserEntry> entries;
  int selected = 0;
  int browser_list_start = 0;
  ftxui::Box browser_list_box;

  std::string selected_program;
  std::string selected_workspace;

  std::string process_query;
  std::vector<ProcessEntry> all_processes;
  std::vector<ProcessEntry> process_matches;
  int process_selected = 0;

  void reset();
  void reload_browser_entries(bool reset_selection);
  void ensure_browser_entries();
  void refresh_process_matches();
};

using ConnectionCompleteCallback = std::function<void(const ConnectionResult&)>;

ftxui::Component MakeConnectionWizardOverlay(
    ftxui::Component main, ConnectionWizardState* state, DebugModel* model,
    ConnectionCompleteCallback on_complete,
    std::function<void()> on_request_quit = {});

}  // namespace tgdb
