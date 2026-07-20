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

struct ShellLaunchConfig {
  std::string host_cwd;
  std::string docker_container;
  std::string docker_cwd;
  std::string docker_shell = "/bin/bash";
  std::map<std::string, std::string> env_vars;
  std::vector<std::string> setup_scripts;

  bool uses_docker() const { return !docker_container.empty(); }
};

using ShellLaunchConfigProvider = std::function<ShellLaunchConfig()>;

class ShellSession {
 public:
  ShellSession();
  ~ShellSession();

  ShellSession(const ShellSession&) = delete;
  ShellSession& operator=(const ShellSession&) = delete;

  void request_start(const ShellLaunchConfig& config, int cols, int rows);
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
  int cursor_col();
  int cursor_row();

  void drain_output(int max_bytes = 4096);
  int drain_output_bytes(int max_bytes = 4096);
  std::size_t pending_output_chunks() const;
  std::string screen_text();
  // Rebuild display_text_/styled_rows from the emulator buffer (e.g. after
  // background drain while the terminal tab was hidden).
  void rebuild_display();

  void set_output_notify(std::function<void()> callback);

  // Marca si la UI está consumiendo la salida (consola visible y en la pestaña
  // de terminal). Cuando está inactivo, el hilo lector alimenta el emulador en
  // segundo plano en vez de acumular chunks sin límite, evitando el bloqueo del
  // hilo de UI al reabrir una terminal minimizada.
  void set_consumer_active(bool active);

 private:
  void bootstrap_shell(const ShellLaunchConfig& config);
  void reader_loop();
  void apply_winsize();
  void notify_output();
  void background_drain();
  void rebuild_display_locked();

  RawPtyScreen terminal_;
  mutable std::mutex terminal_mutex_;
  int cols_ = 80;
  int rows_ = 24;
  std::atomic<bool> running_{false};
  std::atomic<bool> start_in_progress_{false};
  std::atomic<bool> start_failed_{false};
  std::atomic<bool> stop_requested_{false};
  // Atomic so stop() / reader_loop / bootstrap never race into kill(-1, …)
  // (which signals every process of the user and can tear down the desktop).
  std::atomic<int> master_fd_{-1};
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
