#include "terminal/shell_session.hpp"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "util/shell_utils.hpp"
#include "util/child_process_guard.hpp"
#include "util/monitor_log.hpp"
#include "util/thread_name.hpp"
#if defined(__unix__) || defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <pty.h>
#endif
#endif

namespace tuide {

namespace {

constexpr const char* kIntegratedShell = "/bin/bash";

std::string write_terminal_init_script(const ShellLaunchConfig& config) {
  if (config.host_cwd.empty()) {
    return {};
  }
  const std::filesystem::path init_path =
      std::filesystem::path(config.host_cwd) / ".tuide" / "terminal_init.sh";
  std::error_code ec;
  std::filesystem::create_directories(init_path.parent_path(), ec);
  std::ofstream output(init_path);
  if (!output) {
    return {};
  }
  output << "#!/bin/bash\n";
  output << "# tuide: integrated terminal (auto-generated)\n";
  output << "export PATH=\"${PATH:-/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin}\"\n";
  for (const auto& entry : config.env_vars) {
    output << "export " << entry.first << '=' << shell_quote(entry.second) << '\n';
  }
  for (const auto& script : config.setup_scripts) {
    output << "set -a\n";
    output << "source " << shell_quote(script) << " >/dev/null 2>&1 || true\n";
    output << "set +a\n";
  }
  // bash --rcfile replaces ~/.bashrc; source the usual interactive configs so
  // the prompt, ls --color aliases, and other user settings still apply.
  // TERM/COLORTERM are exported above so color_prompt detection sees them.
  // Keep going if a user's bashrc errors — an interactive shell must still start.
  output << "set +e\n";
  output << "if [ -f /etc/bash.bashrc ]; then\n";
  output << "  . /etc/bash.bashrc\n";
  output << "fi\n";
  output << "if [ -f \"${HOME}/.bashrc\" ]; then\n";
  output << "  . \"${HOME}/.bashrc\"\n";
  output << "fi\n";
  output << "cd " << shell_quote(config.host_cwd) << " 2>/dev/null || true\n";
  return init_path.string();
}

void exec_interactive_shell(const std::string& shell_bin, const std::string& init_script) {
  const char* argv0 = "bash";
  if (!init_script.empty()) {
    execlp(shell_bin.c_str(), argv0, "--rcfile", init_script.c_str(), "-i",
           static_cast<char*>(nullptr));
  }
  execlp(shell_bin.c_str(), argv0, "-l", "-i", static_cast<char*>(nullptr));
}

}  // namespace

ShellSession::ShellSession() = default;

ShellSession::~ShellSession() { stop(); }

bool ShellSession::running() const { return running_.load(std::memory_order_acquire); }

bool ShellSession::starting() const { return start_in_progress_.load(std::memory_order_acquire); }

bool ShellSession::start_failed() const { return start_failed_.load(std::memory_order_acquire); }

void ShellSession::apply_winsize() {
#if defined(__linux__)
  const int master = master_fd_.load(std::memory_order_acquire);
  if (master < 0) {
    return;
  }
  struct winsize ws = {};
  ws.ws_row = static_cast<unsigned short>(rows_);
  ws.ws_col = static_cast<unsigned short>(cols_);
  ioctl(master, TIOCSWINSZ, &ws);
#endif
}

void ShellSession::request_start(const ShellLaunchConfig& config, int cols, int rows) {
  if (running() || start_in_progress_.load(std::memory_order_acquire)) {
    return;
  }
#if defined(__linux__)
  if (master_fd_.load(std::memory_order_acquire) >= 0 ||
      child_pid_.load(std::memory_order_acquire) > 0) {
    stop();
  }
#endif
#if !defined(__linux__)
  start_failed_.store(true, std::memory_order_release);
  return;
#else
  if (!config.uses_docker() && config.host_cwd.empty()) {
    return;
  }

  cols_ = std::max(1, cols);
  rows_ = std::max(1, rows);
  start_failed_.store(false, std::memory_order_release);
  start_in_progress_.store(true, std::memory_order_release);
  stop_requested_.store(false, std::memory_order_release);
  output_chunks_.reset();
  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    display_text_.clear();
    display_styled_rows_.clear();
  }

  std::thread([this, config] {
    set_current_thread_name("shell-boot");
    bootstrap_shell(config);
  }).detach();
#endif
}

void ShellSession::bootstrap_shell(const ShellLaunchConfig& config) {
#if defined(__linux__)
  const std::string init_script = write_terminal_init_script(config);
  const std::string shell_bin = kIntegratedShell;

  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    terminal_.reset(rows_, cols_);
  }

  if (stop_requested_.load(std::memory_order_acquire)) {
    start_in_progress_.store(false, std::memory_order_release);
    return;
  }

  int master = -1;
  const pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
  master_fd_.store(master, std::memory_order_release);
  child_pid_.store(pid, std::memory_order_release);
  if (pid < 0) {
    master_fd_.store(-1, std::memory_order_release);
    child_pid_.store(-1, std::memory_order_release);
    start_failed_.store(true, std::memory_order_release);
    start_in_progress_.store(false, std::memory_order_release);
    return;
  }

  if (pid == 0) {
    child_die_with_parent();
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);

    if (config.uses_docker()) {
      std::vector<std::string> args;
      args.emplace_back("docker");
      args.emplace_back("exec");
      args.emplace_back("-i");
      if (!config.docker_cwd.empty()) {
        args.emplace_back("-w");
        args.emplace_back(config.docker_cwd);
      }
      args.emplace_back("-e");
      args.emplace_back("TERM=xterm-256color");
      args.emplace_back("-e");
      args.emplace_back("COLORTERM=truecolor");
      for (const auto& entry : config.env_vars) {
        args.emplace_back("-e");
        args.emplace_back(entry.first + "=" + entry.second);
      }
      args.emplace_back(config.docker_container);
      args.emplace_back(shell_bin);
      if (!init_script.empty() && !config.docker_cwd.empty()) {
        const std::string container_init = config.docker_cwd + "/.tuide/terminal_init.sh";
        args.emplace_back("--rcfile");
        args.emplace_back(container_init);
        args.emplace_back("-i");
      } else {
        args.emplace_back("-l");
        args.emplace_back("-i");
      }

      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (auto& arg : args) {
        argv.push_back(arg.data());
      }
      argv.push_back(nullptr);
      execvp("docker", argv.data());
      _exit(127);
    }

    if (chdir(config.host_cwd.c_str()) != 0) {
      _exit(127);
    }
    for (const auto& entry : config.env_vars) {
      setenv(entry.first.c_str(), entry.second.c_str(), 1);
    }
    exec_interactive_shell(shell_bin, init_script);
    _exit(127);
  }

  apply_winsize();

  const int flags = fcntl(master, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
  }

  if (stop_requested_.load(std::memory_order_acquire)) {
    const int fd = master_fd_.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
      close(fd);
    }
    const pid_t child = child_pid_.exchange(-1, std::memory_order_acq_rel);
    if (child > 1) {
      kill(child, SIGHUP);
      int status = 0;
      waitpid(child, &status, 0);
    }
    start_in_progress_.store(false, std::memory_order_release);
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  int status = 0;
  const pid_t exited = waitpid(pid, &status, WNOHANG);
  if (exited == pid) {
    const int fd = master_fd_.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
      close(fd);
    }
    child_pid_.store(-1, std::memory_order_release);
    start_failed_.store(true, std::memory_order_release);
    start_in_progress_.store(false, std::memory_order_release);
    return;
  }

  running_.store(true, std::memory_order_release);
  start_in_progress_.store(false, std::memory_order_release);
  reader_thread_ = std::make_unique<std::thread>([this] {
    set_current_thread_name("shell-read");
    reader_loop();
  });
  // Bootstrap finishes off the UI thread; wake so we leave "(starting...)" and
  // refresh even if the first PTY bytes arrived before the reader was ready.
  notify_output();
#endif
}

void ShellSession::stop() {
#if defined(__linux__)
  stop_requested_.store(true, std::memory_order_release);
  start_in_progress_.store(false, std::memory_order_release);

  // Close the PTY first so the reader unblocks, then join it *before* reaping
  // the child. The reader must never clear child_pid_ while stop() still uses it:
  // kill(-1, SIGHUP/SIGKILL) broadcasts to every process of the user (can kill
  // the whole desktop session).
  const int master = master_fd_.exchange(-1, std::memory_order_acq_rel);
  if (master >= 0) {
    close(master);
  }
  if (reader_thread_ && reader_thread_->joinable()) {
    reader_thread_->join();
  }
  reader_thread_.reset();

  const pid_t pid = child_pid_.exchange(-1, std::memory_order_acq_rel);
  if (pid > 1) {
    kill(pid, SIGHUP);
    int status = 0;
    constexpr int kTimeoutMs = 1500;
    constexpr int kPollMs = 20;
    int waited_ms = 0;
    for (;;) {
      const pid_t result = waitpid(pid, &status, WNOHANG);
      if (result == pid || result < 0) {
        break;
      }
      if (waited_ms >= kTimeoutMs) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        break;
      }
      usleep(static_cast<useconds_t>(kPollMs) * 1000);
      waited_ms += kPollMs;
    }
  }
#endif
  running_.store(false, std::memory_order_release);
  start_failed_.store(false, std::memory_order_release);
  output_chunks_.close();
  while (output_chunks_.try_pop().has_value()) {
  }
  output_chunks_.reset();
  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    display_text_.clear();
    display_styled_rows_.clear();
  }
}

bool ShellSession::consume_output_pending() {
  return output_pending_.exchange(false, std::memory_order_acquire);
}

std::string ShellSession::display_text() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return display_text_;
}

std::vector<TerminalStyledRow> ShellSession::display_styled_rows() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return display_styled_rows_;
}

int ShellSession::cursor_col() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return terminal_.cursor_col();
}

int ShellSession::cursor_row() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return terminal_.cursor_row();
}

void ShellSession::resize(int cols, int rows) {
  cols = std::max(1, cols);
  rows = std::max(1, rows);
  if (cols_ == cols && rows_ == rows) {
    return;
  }
  cols_ = cols;
  rows_ = rows;
  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    terminal_.resize(rows_, cols_);
  }
  apply_winsize();
}

void ShellSession::write_raw(const std::string& data) {
#if defined(__linux__)
  const int master = master_fd_.load(std::memory_order_acquire);
  if (!running() || master < 0 || data.empty()) {
    return;
  }
  const char* buf = data.c_str();
  std::size_t remaining = data.size();
  int retries = 32;
  while (remaining > 0 && retries-- > 0) {
    const ssize_t written = write(master, buf, remaining);
    if (written > 0) {
      buf += written;
      remaining -= static_cast<std::size_t>(written);
      continue;
    }
    if (written == 0) {
      break;
    }
    if (errno == EAGAIN || errno == EINTR) {
      break;
    }
    break;
  }
#else
  (void)data;
#endif
}

void ShellSession::write_line(const std::string& line) {
  std::string payload = line;
  payload.push_back('\n');
  write_raw(payload);
}

void ShellSession::send_interrupt() {
#if defined(__linux__)
  write_raw("\x03");
#endif
}

void ShellSession::reader_loop() {
#if defined(__linux__)
  std::vector<char> buffer(4096);
  while (!stop_requested_.load(std::memory_order_acquire)) {
    const int master = master_fd_.load(std::memory_order_acquire);
    if (master < 0) {
      break;
    }
    const ssize_t bytes = read(master, buffer.data(), buffer.size());
    if (bytes > 0) {
      TUIDE_MON("shell", "pty_read bytes=" + std::to_string(bytes));
      output_chunks_.push(
          std::string(buffer.data(), static_cast<std::size_t>(bytes)));
      output_pending_.store(true, std::memory_order_release);
      // Con la consola minimizada nadie drena la cola: la alimentamos aquí para
      // que el emulador (con scrollback acotado) se mantenga al día y no se
      // acumule trabajo que congelaría la UI al reabrir. Tampoco despertamos la
      // UI en ese caso, evitando ticks innecesarios.
      if (consumer_active_.load(std::memory_order_acquire)) {
        notify_output();
      } else {
        background_drain();
      }
      continue;
    }
    if (bytes == 0) {
      break;
    }
    if (errno == EAGAIN || errno == EINTR) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    break;
  }

  // Only close if we still own the fd; stop() may have closed it already.
  // Do not waitpid/clear child_pid_ here — stop() alone reaps the shell.
  const int leftover = master_fd_.exchange(-1, std::memory_order_acq_rel);
  if (leftover >= 0) {
    close(leftover);
  }
#endif
  running_.store(false, std::memory_order_release);
}

void ShellSession::drain_output(int max_bytes) {
  (void)drain_output_bytes(max_bytes);
}

int ShellSession::drain_output_bytes(int max_bytes) {
  if (max_bytes <= 0) {
    return 0;
  }
  int consumed = 0;
  while (consumed < max_bytes) {
    auto chunk = output_chunks_.try_pop();
    if (!chunk) {
      break;
    }
    consumed += static_cast<int>(chunk->size());
    {
      std::lock_guard<std::mutex> lock(terminal_mutex_);
      terminal_.feed(chunk->data(), chunk->size());
    }
  }
  if (consumed > 0) {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    rebuild_display_locked();
  }
  return consumed;
}

void ShellSession::background_drain() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  bool any = false;
  while (auto chunk = output_chunks_.try_pop()) {
    terminal_.feed(chunk->data(), chunk->size());
    any = true;
  }
  if (any) {
    // Keep display_* in sync so a later refresh (or a race with set_consumer_active)
    // does not see an empty mirror while the emulator already has the prompt.
    rebuild_display_locked();
  }
}

void ShellSession::rebuild_display_locked() {
  display_text_ = terminal_.text();
  display_styled_rows_ = terminal_.styled_rows();
}

void ShellSession::rebuild_display() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  rebuild_display_locked();
}

void ShellSession::set_consumer_active(bool active) {
  const bool previous = consumer_active_.exchange(active, std::memory_order_acq_rel);
  if (previous == active) {
    return;
  }
  if (active) {
    // La consola vuelve a ser visible: el emulador ya está al día gracias al
    // drenaje en segundo plano, así que basta reconstruir el texto visible una
    // vez para reflejar todo lo acumulado sin bloquear la UI.
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    rebuild_display_locked();
    output_pending_.store(true, std::memory_order_release);
  } else {
    // La consola se minimiza: consumimos lo que quedara en la cola para no
    // arrastrar chunks pendientes mientras esté oculta.
    background_drain();
  }
}

std::size_t ShellSession::pending_output_chunks() const { return output_chunks_.size(); }

void ShellSession::set_output_notify(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(notify_mutex_);
  output_notify_ = std::move(callback);
}

void ShellSession::notify_output() {
  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    callback = output_notify_;
  }
  if (callback) {
    callback();
  }
}

std::string ShellSession::screen_text() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return terminal_.text();
}

}  // namespace tuide
