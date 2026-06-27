#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/app_mode.hpp"
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
#include "ui/shortcuts_modal.hpp"
#include "ui/symbol_picker.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"
#include "ui/source_panel.hpp"
#include "ui/workspace_wizard.hpp"
#include "util/thread_safe_queue.hpp"

namespace tgdb {

struct AppConfig {
  SessionMode mode = SessionMode::kLaunch;
  std::string program;
  std::string workspace_root;
  std::vector<std::string> args;
  int attach_pid = 0;
  std::string attach_target;
  bool use_workspace_wizard = true;
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
  void open_connection_wizard();
  void open_workspace_wizard();
  void on_connection_complete(const ConnectionResult& result);
  void apply_pending_connection();
  void on_workspace_complete(const std::string& workspace_root);
  void set_workspace(const std::string& workspace_root);
  void exit_debug_mode();
  bool connection_config_complete() const;
  void ensure_backend_started();
  bool handle_focus_shortcuts(const ftxui::Event& event);
  bool any_modal_open() const;
  void set_status(const std::string& message);
  void set_workspace_status(const std::string& message);

  AppConfig config_;
  AppMode app_mode_ = AppMode::kNormal;
  DebugModel model_;
  WorkspaceModel workspace_;
  SourceViewState source_state_;
  FilePickerState file_picker_state_;
  SymbolPickerState symbol_picker_state_;
  QuitConfirmState quit_confirm_state_;
  ShortcutsModalState shortcuts_modal_state_;
  ShellSession shell_session_;
  ConnectionWizardState connection_wizard_state_;
  WorkspaceWizardState workspace_wizard_state_;
  MainLayoutState layout_state_;
  FocusManagerState focus_state_;
  WorkspaceIndexer indexer_;
  SymbolWorkspaceIndexer symbol_indexer_;
  WorkspaceWatcher workspace_watcher_;
  std::shared_ptr<LspSymbolProvider> symbol_provider_;

  ThreadSafeQueue<UiCommand> command_queue_;
  ThreadSafeQueue<DebugEvent> event_queue_;
  std::unique_ptr<DapBackend> backend_;
  bool session_ready_ = false;
  bool debugging_started_ = false;
  bool backend_started_ = false;
  std::optional<ConnectionResult> pending_connection_;
};

}  // namespace tgdb
