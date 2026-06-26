#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "backend/idebug_backend.hpp"
#include "backend/dap_backend.hpp"
#include "ui/file_picker.hpp"
#include "ui/main_layout.hpp"
#include "ui/source_panel.hpp"
#include "util/thread_safe_queue.hpp"

namespace tgdb {

struct AppConfig {
  SessionMode mode = SessionMode::kLaunch;
  std::string program;
  std::string workspace_root;
  std::vector<std::string> args;
  int attach_pid = 0;
  std::string attach_target;
};

class Application {
 public:
  explicit Application(AppConfig config);
  int run();

 private:
  void drain_events();
  void apply_event(const DebugEvent& event);
  void submit_command(const UiCommand& command);
  void refresh_all_watches();
  void schedule_poll();

  AppConfig config_;
  DebugModel model_;
  SourceViewState source_state_;
  FilePickerState file_picker_state_;
  MainLayoutState layout_state_;

  ThreadSafeQueue<UiCommand> command_queue_;
  ThreadSafeQueue<DebugEvent> event_queue_;
  std::unique_ptr<DapBackend> backend_;
  bool session_ready_ = false;
  bool debugging_started_ = false;
};

}  // namespace tgdb
