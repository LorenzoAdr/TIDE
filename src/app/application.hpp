#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "app/app_mode.hpp"
#include "app/app_settings.hpp"
#include "app/workspace_config.hpp"
#include "app/workspace_detect.hpp"
#include "app/workspace_session.hpp"
#include "build/build_artifact_watcher.hpp"
#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "backend/idebug_backend.hpp"
#include "backend/dap_backend.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/lsp_symbol_provider.hpp"
#include "terminal/shell_session.hpp"
#include "ui/connection_wizard.hpp"
#include "ui/file_picker.hpp"
#include "ui/quit_confirm.hpp"
#include "ui/debug_launch_modal.hpp"
#include "ui/shutdown_overlay.hpp"
#include "ui/open_file_confirm.hpp"
#include "ui/lsp_missing_toast.hpp"
#include "ui/settings_modal.hpp"
#include "ui/shortcuts_modal.hpp"
#include "ui/source_substitute_modal.hpp"
#include "ui/symbol_picker.hpp"
#include "git/git_service.hpp"
#include "ui/git_panel.hpp"
#include "ui/main_layout.hpp"
#include "ui/ui_event_dispatcher.hpp"
#include "ui/source_panel.hpp"
#include "ui/workspace_wizard.hpp"
#include "ui/welcome_screen.hpp"
#include "ui/external_file_wizard.hpp"
#include "util/thread_safe_queue.hpp"
#include "util/lsp_missing_prompt.hpp"

namespace ftxui {
class ScreenInteractive;
}

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
  bool packet_monitor_enabled = false;
  std::string packet_monitor_filter_src;
  std::string packet_monitor_filter_dst;
};

class Application {
 public:
  explicit Application(AppConfig config);
  ~Application();
  int run();
  void run_input_sync_drain(int64_t now_ms);

 private:
  void drain_events();
  void process_index_changes();
  void apply_event(const DebugEvent& event);
  void submit_command(const UiCommand& command);
  void refresh_all_watches();
  void apply_connection_and_start();
  void enter_debug_ui_layout();
  void submit_debug_session_start();
  void open_debug_launch_modal();
  void cancel_debug_launch();
  void dismiss_debug_launch_error();
  void complete_debug_launch_success();
  void fail_debug_launch(const std::string& message);
  bool debug_launch_generation_active() const;
  void dismiss_welcome_screen();
  bool prepare_connection_wizard();
  void open_connection_wizard();
  void open_launch_wizard();
  void open_debug_wizard();
  void quick_launch_last();
  void quick_attach_last();
  void open_workspace_wizard();
  void open_external_file_wizard(bool from_active_file_dir = false);
  void open_quick_file_picker(bool arm_ctrl_chord = false);
  void cycle_quick_file_picker();
  void notify_file_tree_reveal();
  void sync_model_breakpoints_to_backend();
  void on_connection_complete(const ConnectionResult& result);
  void apply_pending_connection();
  void on_workspace_complete(const std::string& workspace_root,
                             ftxui::ScreenInteractive* screen);
  void set_workspace(const std::string& workspace_root,
                     const WorkspaceDetectResult* detect = nullptr,
                     const std::string& open_file_hint = {});
  WorkspaceDetectResult resolve_workspace_for_anchor(const std::string& anchor) const;
  void exit_debug_mode(bool force_kill = false);
  bool connection_config_complete() const;
  void ensure_backend_started();
  void register_backend_wake_callback();
  void invalidate_debug_ui();
  bool handle_focus_shortcuts(const ftxui::Event& event);
  bool any_modal_open() const;
  void apply_app_settings();
  void toggle_helix_mode();
  void apply_workspace_settings(const WorkspaceConfig& config);
  void save_workspace_session();
  void restore_workspace_session();
  std::string launch_cwd_for_program(const std::string& program) const;
  void restart_lsp_for_workspace();
  void sync_symbol_workspace_indexer();
  void set_status(const std::string& message);
  void set_workspace_status(const std::string& message);
  void request_terminal_autostart();
  void inject_terminal_command(const std::string& command);
  void try_flush_terminal_inject();
  void maybe_show_lsp_missing_toast(const std::string& status_i18n_key);
  void on_lsp_missing_install();
  void on_lsp_missing_bundle();
  void on_lsp_missing_ignore();
  void rebuild_shell_launch_config();
  void setup_build_environment_watching();
  void process_build_environment_updates();
  void schedule_debounced_lsp_restart();
  IndexFilterOptions index_filter_options() const;
  void restart_workspace_indexing();
  void refresh_workspace_explorer();
  void reindex_project();
  void enqueue_ui_task(std::function<void()> task);
  void drain_ui_tasks();
  void process_pending_workspace_load();
  void begin_shutdown(ftxui::ScreenInteractive* screen);
  void schedule_next_shutdown_step(ftxui::ScreenInteractive* screen);
  void tick_shutdown();
  void force_exit();
  void stop_all_subprocesses();
  void sync_activity_phase_effects();
  void run_custom_event_drain(int64_t now_ms, const UiEventDrainPlan& plan, uint64_t paint_before);

  ftxui::ScreenInteractive* active_screen_ = nullptr;
  AppConfig config_;
  AppMode app_mode_ = AppMode::kNormal;
  DebugModel model_;
  WorkspaceModel workspace_;
  WorkspaceModel secondary_workspace_;
  SourceViewState source_state_;
  FilePickerState file_picker_state_;
  SymbolPickerState symbol_picker_state_;
  QuitConfirmState quit_confirm_state_;
  DebugLaunchModalState debug_launch_modal_state_;
  uint64_t debug_launch_generation_ = 0;
  ShutdownState shutdown_state_;
  ShutdownOverlayState shutdown_overlay_state_;
  std::thread shutdown_thread_;
  int shutdown_step_index_ = 0;
  bool shutdown_performed_ = false;
  OpenFileConfirmState open_file_confirm_state_;
  LspMissingToastState lsp_missing_toast_state_;
  bool lsp_missing_toast_suppressed_ = false;
  std::unordered_set<std::string> lsp_missing_notified_servers_;
  std::string pending_terminal_inject_;
  std::optional<std::string> cached_tgdb_source_root_;
  bool tgdb_source_root_probed_ = false;
  ShortcutsModalState shortcuts_modal_state_;
  SettingsModalState settings_modal_state_;
  SourceSubstituteModalState source_substitute_state_;
  AppSettings app_settings_;
  WorkspaceConfig workspace_config_;
  ClangFormatConfig clang_format_config_;
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
  uint64_t backend_epoch_ = 0;
  bool workspace_initialized_ = false;
  std::map<std::string, std::string> workspace_launch_args_;
  std::string last_launch_program_;
  std::string last_attach_program_;
  std::optional<ConnectionResult> pending_connection_;
  std::optional<std::string> pending_workspace_load_;
  mutable std::mutex ui_task_mutex_;
  std::deque<std::function<void()>> ui_tasks_;
  std::atomic<bool> reindex_in_progress_{false};
  UiActivityPhase last_activity_phase_ = UiActivityPhase::kInhibited;
  UiEventDispatcher ui_event_dispatcher_;
  mutable std::mutex tree_sitter_wake_mutex_;
  std::map<std::string, uint64_t> tree_sitter_last_wake_revision_;
};

}  // namespace tgdb
