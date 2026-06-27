#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ftxui/dom/elements.hpp"
#include "terminal/terminal_emulator.hpp"
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

  void drain_output(int max_bytes = 4096);
  ftxui::Element render_terminal();

 private:
  void bootstrap_shell(const std::string& cwd);
  void reader_loop();
  void append_bytes(const char* data, std::size_t size);
  void apply_winsize();

  TerminalEmulator terminal_;
  std::mutex terminal_mutex_;
  int cols_ = 80;
  int rows_ = 24;
  std::atomic<bool> running_{false};
  std::atomic<bool> start_in_progress_{false};
  std::atomic<bool> start_failed_{false};
  std::atomic<bool> stop_requested_{false};
  int master_fd_ = -1;
  pid_t child_pid_ = -1;
  std::unique_ptr<std::thread> bootstrap_thread_;
  std::unique_ptr<std::thread> reader_thread_;
  ThreadSafeQueue<std::string> output_chunks_;
};

}  // namespace tgdb
