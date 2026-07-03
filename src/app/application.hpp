#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/app_mode.hpp"
#include "app/app_settings.hpp"
#include "app/workspace_config.hpp"
#include "app/workspace_session.hpp"
#include "build/build_artifact_watcher.hpp"
#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "backend/idebug_backend.hpp"
#include "backend/dap_backend.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "indexer/workspace_watcher.hpp"
#include "symbols/lsp_symbol_provider.hpp"
#include "terminal/shell_session.hpp"
#include "ui/connection_wizard.hpp"
#include "ui/file_picker.hpp"
#include "ui/quit_confirm.hpp"
#include "ui/open_file_confirm.hpp"
#include "ui/settings_modal.hpp"
#include "ui/shortcuts_modal.hpp"
#include "ui/symbol_picker.hpp"
#include "git/git_service.hpp"
#include "ui/git_panel.hpp"
#include "ui/main_layout.hpp"
#include "ui/source_panel.hpp"
#include "ui/workspace_wizard.hpp"
#include "ui/welcome_screen.hpp"
#include "ui/external_file_wizard.hpp"
#include "util/thread_safe_queue.hpp"

namespace tgdb {

struct AppConfig {
  SessionMode mode = SessionMode::kLaunch;
  std::string program;
  std::string workspace_root;
  std::string initial_file;
  std::vector<std::string> args;
  int attach_pid = 0;
  std::string attach_target;
  std::string core_path;
  CoreAnalysisMode core_analysis = CoreAnalysisMode::kGdbOnly;
  bool show_welcome_screen = false;
  bool auto_debug = false;
  std::string launch_directory;
};

class Application {
 public:
  explicit Application(AppConfig config);
  ~Application();
  int run();

 private:
  void drain_events();
  void process_index_changes();
  void apply_event(const DebugEvent& event);
  void submit_command(const UiCommand& command);
  void refresh_all_watches();
  void apply_connection_and_start();
  void dismiss_welcome_screen();
  void open_connection_wizard();
  void open_workspace_wizard();
  void open_external_file_wizard();
  void sync_model_breakpoints_to_backend();
  void on_connection_complete(const ConnectionResult& result);
  void apply_pending_connection();
  void on_workspace_complete(const std::string& workspace_root);
  void set_workspace(const std::string& workspace_root);
  void exit_debug_mode();
  bool connection_config_complete() const;
  void ensure_backend_started();
  bool handle_focus_shortcuts(const ftxui::Event& event);
  bool any_modal_open() const;
  void apply_app_settings();
  void apply_workspace_settings(const WorkspaceConfig& config);
  void save_workspace_session();
  void restore_workspace_session();
  std::string launch_cwd_for_program(const std::string& program) const;
  void restart_lsp_for_workspace();
  void sync_symbol_workspace_indexer();
  void set_status(const std::string& message);
  void set_workspace_status(const std::string& message);
  void request_terminal_autostart();
  void rebuild_shell_launch_config();
  void setup_build_environment_watching();
  void process_build_environment_updates();
  void schedule_debounced_lsp_restart();

  AppConfig config_;
  AppMode app_mode_ = AppMode::kNormal;
  DebugModel model_;
  WorkspaceModel workspace_;
  WorkspaceModel secondary_workspace_;
  SourceViewState source_state_;
  FilePickerState file_picker_state_;
  SymbolPickerState symbol_picker_state_;
  QuitConfirmState quit_confirm_state_;
  OpenFileConfirmState open_file_confirm_state_;
  ShortcutsModalState shortcuts_modal_state_;
  SettingsModalState settings_modal_state_;
  AppSettings app_settings_;
  WorkspaceConfig workspace_config_;
  ShellLaunchConfig cached_shell_launch_config_;
  ShellSession shell_session_;
  ConnectionWizardState connection_wizard_state_;
  WorkspaceWizardState workspace_wizard_state_;
  WelcomeScreenState welcome_screen_state_;
  ExternalFileWizardState external_file_wizard_state_;
  MainLayoutState layout_state_;
  FocusManagerState focus_state_;
  GitService git_service_;
  GitPanelState git_panel_state_;
  WorkspaceIndexer indexer_;
  SymbolWorkspaceIndexer symbol_indexer_;
  WorkspaceWatcher workspace_watcher_;
  BuildArtifactWatcher build_artifact_watcher_;
  std::shared_ptr<LspSymbolProvider> symbol_provider_;

  std::chrono::steady_clock::time_point lsp_restart_deadline_{};
  bool pending_lsp_restart_ = false;
  std::string last_lsp_environment_fingerprint_;

  ThreadSafeQueue<UiCommand> command_queue_;
  ThreadSafeQueue<DebugEvent> event_queue_;
  std::unique_ptr<DapBackend> backend_;
  bool session_ready_ = false;
  bool debugging_started_ = false;
  bool backend_started_ = false;
  bool debug_available_ = false;
  bool workspace_initialized_ = false;
  std::map<std::string, std::string> workspace_launch_args_;
  std::optional<ConnectionResult> pending_connection_;
};

}  // namespace tgdb
