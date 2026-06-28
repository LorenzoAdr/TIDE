#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "terminal/raw_pty_screen.hpp"
#include "util/thread_safe_queue.hpp"

namespace tgdb {

class ShellSession {
 public:
  ShellSession();
  ~ShellSession();

  ShellSession(const ShellSession&) = delete;
  ShellSession& operator=(const ShellSession&) = delete;

  void request_start(const std::string& cwd, int cols, int rows);
  void stop();
  bool running() const;
  bool starting() const;
  bool start_failed() const;

  void write_line(const std::string& line);
  void write_raw(const std::string& data);
  void send_interrupt();
  void resize(int cols, int rows);

  bool consume_output_pending();
  std::string display_text();
  std::vector<TerminalStyledRow> display_styled_rows();

  void drain_output(int max_bytes = 4096);
  int drain_output_bytes(int max_bytes = 4096);
  std::size_t pending_output_chunks() const;
  std::string screen_text();

 private:
  void bootstrap_shell(const std::string& cwd);
  void reader_loop();
  void apply_winsize();

  RawPtyScreen terminal_;
  mutable std::mutex terminal_mutex_;
  int cols_ = 80;
  int rows_ = 24;
  std::atomic<bool> running_{false};
  std::atomic<bool> start_in_progress_{false};
  std::atomic<bool> start_failed_{false};
  std::atomic<bool> stop_requested_{false};
  int master_fd_ = -1;
  pid_t child_pid_ = -1;
  std::unique_ptr<std::thread> reader_thread_;
  ThreadSafeQueue<std::string> output_chunks_;
  std::string display_text_;
  std::vector<TerminalStyledRow> display_styled_rows_;
  std::atomic<bool> output_pending_{false};
};

}  // namespace tgdb
