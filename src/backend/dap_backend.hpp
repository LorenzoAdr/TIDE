#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "dap/debug_adapter_process.hpp"
#include "dap/debug_adapter_spec.hpp"
#include "util/thread_safe_queue.hpp"

namespace dap {
class Session;
}

namespace tuide {

using DebugWakeCallback = std::function<void(DebugEventKind)>;

struct RunInTerminalArgs {
  std::string cwd;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  std::string kind;
  std::string title;
};

struct RunInTerminalResult {
  bool ok = false;
  std::string error;
  int process_id = -1;
  int shell_process_id = -1;
};

using RunInTerminalHandler = std::function<RunInTerminalResult(const RunInTerminalArgs&)>;
// Returns slave PTY path for GDB `set inferior-tty`, or empty to skip.
using PrepareAppTtyHandler = std::function<std::string(int cols, int rows)>;

class DapBackend : public IDebugBackend {
 public:
  DapBackend(ThreadSafeQueue<UiCommand>& commands,
             ThreadSafeQueue<DebugEvent>& events);
  ~DapBackend() override;

  void start() override;
  void stop() override;
  void submit(const UiCommand& command) override;
  void set_wake_callback(DebugWakeCallback callback);
  void set_run_in_terminal_handler(RunInTerminalHandler handler);
  void set_prepare_app_tty_handler(PrepareAppTtyHandler handler);
  void set_preferred_adapter(DebugAdapterKind kind);
  void set_backend_epoch(uint64_t epoch) { backend_epoch_.store(epoch, std::memory_order_release); }
  uint64_t backend_epoch() const { return backend_epoch_.load(std::memory_order_acquire); }
  DebugAdapterKind preferred_adapter() const { return preferred_adapter_; }

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
                               const std::vector<int>& lines,
                               bool require_stopped = true);
  bool wait_for_initialized_event(int timeout_ms);
  bool launch_debugpy(const UiCommand& command);
  bool launch_bashdb(const UiCommand& command);
  bool finish_late_configuration_unlocked(bool send_configuration_done = true);
  bool pause_inferior_locked();
  bool continue_inferior_locked();
  void refresh_active_thread_locked();
  bool verify_inferior_attached_locked();
  void on_inferior_launched();
  void on_inferior_attached();
  void on_inferior_core_loaded();
  bool configure_glibc_debug_symbols_locked();
  bool exec_repl_locked(const std::string& gdb_command, bool emit_output = true);
  bool exec_repl_capture_locked(const std::string& gdb_command, std::string* output,
                                  bool silent = false);
  bool configure_packet_monitor_env_locked(const LaunchConfig& launch);
  bool configure_inferior_app_tty_locked();
  int fetch_inferior_pid_locked(bool silent = false);
  void emit_inferior_pid(int pid);
  void push_event(DebugEvent event);
  void push_error(const std::string& message);
  bool adapter_is_gdb() const;
  bool adapter_is_debugpy() const;
  bool adapter_is_bashdb() const;

  ThreadSafeQueue<UiCommand>& commands_;
  ThreadSafeQueue<DebugEvent>& events_;
  DebugWakeCallback wake_callback_;
  std::mutex wake_mutex_;
  RunInTerminalHandler run_in_terminal_handler_;
  std::mutex run_in_terminal_mutex_;
  PrepareAppTtyHandler prepare_app_tty_handler_;
  std::mutex prepare_app_tty_mutex_;

  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> backend_epoch_{0};
  DebugAdapterKind preferred_adapter_ = DebugAdapterKind::kGdb;

  std::unique_ptr<IDebugAdapterProcess> adapter_;
  std::unique_ptr<dap::Session> session_;
  // Invalidated in stop() before killing the adapter so a late cppdap onClose
  // cannot push "connection closed" into a replacement DapBackend (UAF/reuse).
  std::shared_ptr<std::atomic<bool>> session_close_guard_;
  std::recursive_mutex session_mutex_;
  int active_thread_id_ = 1;
  bool inferior_attached_ = false;
  bool inferior_launched_ = false;
  std::atomic<bool> inferior_stopped_{false};
  bool expecting_interrupt_for_breakpoints_ = false;
  bool breakpoints_pending_sync_ = false;
  bool resume_after_breakpoint_sync_ = false;
  bool expecting_stop_after_pause_ = false;
  int last_exit_code_ = -1;
  bool configuration_done_ = false;
  std::atomic<int> reported_inferior_pid_{0};
  std::unordered_map<std::string, std::vector<int>> breakpoints_by_file_;

  // debugpy/bashdb: "initialized" arrives after launch is received (not after
  // initialize). launch_debugpy waits on this before setBreakpoints + configurationDone.
  std::mutex dap_initialized_mutex_;
  std::condition_variable dap_initialized_cv_;
  bool dap_initialized_event_ = false;
};

}  // namespace tuide
