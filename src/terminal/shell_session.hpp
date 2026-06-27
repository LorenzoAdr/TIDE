#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "terminal/terminal_emulator.hpp"
#include "util/thread_safe_queue.hpp"

namespace tgdb {

class ShellSession {
 public:
  ShellSession();
  ~ShellSession();

  ShellSession(const ShellSession&) = delete;
  ShellSession& operator=(const ShellSession&) = delete;

  void start(const std::string& cwd);
  void stop();
  bool running() const;

  void write_line(const std::string& line);
  void write_raw(const std::string& data);
  void send_interrupt();
  void resize(int cols, int rows);

  void drain_output();
  TerminalEmulator& terminal() { return terminal_; }
  const TerminalEmulator& terminal() const { return terminal_; }

 private:
  void reader_loop();
  void append_bytes(const char* data, std::size_t size);
  void apply_winsize();

  TerminalEmulator terminal_;
  int cols_ = 80;
  int rows_ = 24;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  int master_fd_ = -1;
  pid_t child_pid_ = -1;
  std::unique_ptr<std::thread> reader_thread_;
  ThreadSafeQueue<std::string> output_chunks_;
};

}  // namespace tgdb
