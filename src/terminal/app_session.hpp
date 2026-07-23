#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

#include "terminal/raw_pty_screen.hpp"
#include "util/thread_safe_queue.hpp"

namespace tuide {

// PTY + ANSI emulator for the debuggee (DAP runInTerminal / stdout fallback).
// Separate from ShellSession so the interactive Terminal tab is untouched.
class AppSession {
 public:
  AppSession();
  ~AppSession();

  AppSession(const AppSession&) = delete;
  AppSession& operator=(const AppSession&) = delete;

  // Spawn argv[0]… in a PTY (DAP runInTerminal). Returns child pid, or -1.
  int start_command(const std::string& cwd, const std::vector<std::string>& args,
                    const std::map<std::string, std::string>& env, int cols, int rows);

  // Open a host-side PTY (no child). Returns the slave path for GDB
  // `set inferior-tty`, or empty on failure. Used when the adapter does not
  // issue runInTerminal so inferior stdout is line-buffered into App.
  std::string open_host_pty(int cols, int rows);

  // Ensure an emulator exists without a child (OutputEvent stdout/stderr fallback).
  void ensure_emulator(int cols, int rows);

  void stop();
  void reset_display();
  bool running() const;
  bool has_live_pty() const;
  pid_t child_pid() const;

  void write_raw(const std::string& data);
  void feed_bytes(const std::string& data);
  void resize(int cols, int rows);
  void send_interrupt();

  bool consume_output_pending();
  std::string display_text();
  std::vector<TerminalStyledRow> display_styled_rows();
  int cursor_col();
  int cursor_row();

  void drain_output(int max_bytes = 4096);
  int drain_output_bytes(int max_bytes = 4096);
  std::size_t pending_output_chunks() const;
  void rebuild_display();

  void set_output_notify(std::function<void()> callback);
  void set_consumer_active(bool active);

 private:
  void reader_loop();
  void apply_winsize();
  void notify_output();
  void background_drain();
  void rebuild_display_locked();
  bool feed_pty_bytes_locked(const char* data, std::size_t len);
  void on_pty_bytes(const char* data, std::size_t len);
  void ensure_wake_fd();
  void signal_reader_wake();
  void drain_wake_fd();

  RawPtyScreen terminal_;
  mutable std::mutex terminal_mutex_;
  int cols_ = 80;
  int rows_ = 24;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> emulator_only_{false};
  std::atomic<int> master_fd_{-1};
  // Kept open for host PTY mode so the master does not see EOF before GDB
  // attaches the inferior via `set inferior-tty`.
  std::atomic<int> slave_fd_{-1};
  std::atomic<int> wake_fd_{-1};
  std::atomic<pid_t> child_pid_{-1};
  std::unique_ptr<std::thread> reader_thread_;
  ThreadSafeQueue<std::string> output_chunks_;
  std::string display_text_;
  std::vector<TerminalStyledRow> display_styled_rows_;
  std::atomic<bool> output_pending_{false};
  std::atomic<bool> consumer_active_{true};
  std::mutex notify_mutex_;
  std::function<void()> output_notify_;
};

}  // namespace tuide
