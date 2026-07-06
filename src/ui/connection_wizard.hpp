#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "backend/idebug_backend.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"
#include "ui/path_browser.hpp"
#include "util/process_list.hpp"

namespace tgdb {

enum class WizardStep {
  ChooseMode,
  PickBinary,
  PickArgs,
  PickProcess,
  PickCoreFile,
  ChooseCoreBackend,
};

enum class WizardMode { Launch, Attach, LoadCore, AnalyzeSymbols };

struct ConnectionResult {
  SessionMode mode = SessionMode::kLaunch;
  std::string program;
  std::string workspace_root;
  std::vector<std::string> args;
  std::string args_line;
  int attach_pid = 0;
  std::string core_path;
  CoreAnalysisMode core_analysis = CoreAnalysisMode::kGdbOnly;
  bool packet_monitor_enabled = false;
  std::string packet_monitor_filter_src;
  std::string packet_monitor_filter_dst;
};

struct ConnectionWizardState {
  bool open = false;
  WizardStep step = WizardStep::ChooseMode;
  WizardMode mode = WizardMode::Attach;
  int mode_selected = 0;
  int core_backend_selected = 0;

  std::string workspace_root;
  PathBrowserState browser;

  std::string selected_program;
  std::string selected_core_path;
  std::string launch_cwd;
  std::string args_line;
  std::vector<std::string> args_completion_matches;
  std::map<std::string, std::string> launch_args_by_program;

  bool packet_monitor_enabled = false;
  std::string packet_monitor_filter_src = "127.0.0.1";
  std::string packet_monitor_filter_dst;

  std::string process_query;
  std::vector<ProcessEntry> all_processes;
  std::vector<ProcessEntry> process_matches;
  int process_selected = 0;
  int process_list_start = 0;

  ftxui::Box launch_mode_box;
  ftxui::Box attach_mode_box;
  ftxui::Box core_mode_box;
  ftxui::Box symbols_mode_box;
  ftxui::Box gdb_backend_box;
  ftxui::Box ca_backend_box;
  ftxui::Box process_list_box;

  void reset();
  void refresh_process_matches();
};

using ConnectionCompleteCallback = std::function<void(const ConnectionResult&)>;

ftxui::Component MakeConnectionWizardOverlay(
    ftxui::Component main, ConnectionWizardState* state, DebugModel* model,
    MainLayoutState* layout_state, ConnectionCompleteCallback on_complete,
    std::function<void()> on_request_quit = {});

}  // namespace tgdb
