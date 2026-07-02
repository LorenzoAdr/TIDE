#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "dap/gdb_launcher.hpp"
#include "util/thread_safe_queue.hpp"

namespace dap {
class Session;
}

namespace tgdb {

class DapBackend : public IDebugBackend {
 public:
  DapBackend(ThreadSafeQueue<UiCommand>& commands,
             ThreadSafeQueue<DebugEvent>& events);
  ~DapBackend() override;

  void start() override;
  void stop() override;
  void submit(const UiCommand& command) override;

 private:
  void worker_main();
  void setup_session();
  bool initialize_session();
  bool send_configuration_done();
  void handle_command(const UiCommand& command);
  void refresh_stack(int thread_id);
  void refresh_variables(int frame_id);
  void fetch_variable_children(int variables_reference,
                               const std::string& parent_expression,
                               int parent_depth);
  void update_breakpoints(const std::string& file,
                          const std::vector<int>& lines);
  void apply_pending_breakpoints_locked();
  void notify_stopped(const std::string& reason, int thread_id = -1);
  void notify_continued(int thread_id = -1);
  void send_breakpoints_locked(const std::string& normalized_file,
                               const std::vector<int>& lines);
  bool pause_inferior_locked();
  bool continue_inferior_locked();
  void refresh_active_thread_locked();
  bool verify_inferior_attached_locked();
  void on_inferior_launched();
  void on_inferior_attached();
  void push_event(DebugEvent event);
  void push_error(const std::string& message);

  ThreadSafeQueue<UiCommand>& commands_;
  ThreadSafeQueue<DebugEvent>& events_;

  std::thread worker_;
  std::atomic<bool> running_{false};

  std::unique_ptr<GdbProcess> gdb_;
  std::unique_ptr<dap::Session> session_;
  std::mutex session_mutex_;
  int active_thread_id_ = 1;
  bool inferior_attached_ = false;
  bool inferior_launched_ = false;
  std::atomic<bool> inferior_stopped_{false};
  bool expecting_interrupt_for_breakpoints_ = false;
  bool breakpoints_pending_sync_ = false;
  bool resume_after_breakpoint_sync_ = false;
  bool expecting_stop_after_pause_ = false;
  int last_exit_code_ = -1;
  std::unordered_map<std::string, std::vector<int>> breakpoints_by_file_;
};

}  // namespace tgdb
