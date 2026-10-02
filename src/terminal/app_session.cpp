#include "terminal/app_session.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <thread>
#include <vector>

#include "util/child_process_guard.hpp"
#include "util/thread_name.hpp"

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <pty.h>
#include <sys/eventfd.h>
#elif defined(__APPLE__)
#include <util.h>
#endif
#endif

#if defined(__APPLE__) || defined(__linux__)
#define TUIDE_HAS_PTY 1
#endif

namespace tuide {

AppSession::AppSession() = default;

AppSession::~AppSession() { stop(); }

bool AppSession::running() const { return running_.load(std::memory_order_acquire); }

bool AppSession::has_live_pty() const {
  return running() && master_fd_.load(std::memory_order_acquire) >= 0;
}

pid_t AppSession::child_pid() const { return child_pid_.load(std::memory_order_acquire); }

void AppSession::apply_winsize() {
#if defined(TUIDE_HAS_PTY)
  const int master = master_fd_.load(std::memory_order_acquire);
  if (master < 0) {
    return;
  }
  winsize size{};
  size.ws_row = static_cast<unsigned short>(std::max(1, rows_));
  size.ws_col = static_cast<unsigned short>(std::max(1, cols_));
  ioctl(master, TIOCSWINSZ, &size);
#endif
}

void AppSession::reset_display() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  terminal_.reset(rows_, cols_);
  display_text_.clear();
  display_styled_rows_.clear();
}

void AppSession::ensure_emulator(int cols, int rows) {
  cols_ = std::max(1, cols);
  rows_ = std::max(1, rows);
  if (running()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    terminal_.reset(rows_, cols_);
    display_text_.clear();
    display_styled_rows_.clear();
  }
  emulator_only_.store(true, std::memory_order_release);
  running_.store(true, std::memory_order_release);
}

void AppSession::ensure_wake_fd() {
#ifdef __linux__
  if (wake_fd_.load(std::memory_order_acquire) >= 0) {
    return;
  }
  const int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd >= 0) {
    wake_fd_.store(fd, std::memory_order_release);
  }
#endif
}

void AppSession::signal_reader_wake() {
#ifdef __linux__
  const int fd = wake_fd_.load(std::memory_order_acquire);
  if (fd < 0) {
    return;
  }
  const uint64_t one = 1;
  (void)write(fd, &one, sizeof(one));
#endif
}

void AppSession::drain_wake_fd() {
#ifdef __linux__
  const int fd = wake_fd_.load(std::memory_order_acquire);
  if (fd < 0) {
    return;
  }
  uint64_t value = 0;
  while (read(fd, &value, sizeof(value)) > 0) {
  }
#endif
}

std::string AppSession::open_host_pty(int cols, int rows) {
#if defined(TUIDE_HAS_PTY)
  stop();
  stop_requested_.store(false, std::memory_order_release);
  cols_ = std::max(1, cols);
  rows_ = std::max(1, rows);
  emulator_only_.store(false, std::memory_order_release);
  ensure_wake_fd();

  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    terminal_.reset(rows_, cols_);
    display_text_.clear();
    display_styled_rows_.clear();
  }

  int master = -1;
  int slave = -1;
  char slave_name[128] = {};
  if (openpty(&master, &slave, slave_name, nullptr, nullptr) != 0) {
    return {};
  }
  // Keep our slave fd open until stop(). Closing it here makes master read()
  // return EOF immediately, the reader thread exits, and GDB never delivers
  // live inferior stdout into App (only flushed DAP OutputEvents sneak in).
  master_fd_.store(master, std::memory_order_release);
  slave_fd_.store(slave, std::memory_order_release);
  child_pid_.store(-1, std::memory_order_release);
  apply_winsize();
  const int flags = fcntl(master, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
  }

  running_.store(true, std::memory_order_release);
  reader_thread_ = std::make_unique<std::thread>([this] {
    set_current_thread_name("app-pty-reader");
    reader_loop();
  });
  notify_output();
  return std::string(slave_name);
#else
  (void)cols;
  (void)rows;
  return {};
#endif
}

int AppSession::start_command(const std::string& cwd, const std::vector<std::string>& args,
                              const std::map<std::string, std::string>& env, int cols, int rows) {
#if defined(TUIDE_HAS_PTY)
  if (args.empty() || args.front().empty()) {
    return -1;
  }
  stop();
  stop_requested_.store(false, std::memory_order_release);
  cols_ = std::max(1, cols);
  rows_ = std::max(1, rows);
  emulator_only_.store(false, std::memory_order_release);
  ensure_wake_fd();

  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    terminal_.reset(rows_, cols_);
    display_text_.clear();
    display_styled_rows_.clear();
  }

  int master = -1;
  const pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
  master_fd_.store(master, std::memory_order_release);
  child_pid_.store(pid, std::memory_order_release);
  if (pid < 0) {
    master_fd_.store(-1, std::memory_order_release);
    child_pid_.store(-1, std::memory_order_release);
    return -1;
  }

  if (pid == 0) {
    child_die_with_parent();
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    for (const auto& entry : env) {
      if (entry.second.empty()) {
        unsetenv(entry.first.c_str());
      } else {
        setenv(entry.first.c_str(), entry.second.c_str(), 1);
      }
    }
    if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
      _exit(127);
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  apply_winsize();
  const int flags = fcntl(master, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
  }

  running_.store(true, std::memory_order_release);
  reader_thread_ = std::make_unique<std::thread>([this] {
    set_current_thread_name("app-pty-reader");
    reader_loop();
  });
  notify_output();
  return static_cast<int>(pid);
#else
  (void)cwd;
  (void)args;
  (void)env;
  (void)cols;
  (void)rows;
  return -1;
#endif
}

void AppSession::stop() {
#if defined(TUIDE_HAS_PTY)
  // Never clear output_notify_: open_host_pty/start_command call stop() on every
  // launch, and the callback is only wired once from MakeConsolePanel.
  stop_requested_.store(true, std::memory_order_release);
  signal_reader_wake();

  const int master = master_fd_.exchange(-1, std::memory_order_acq_rel);
  if (master >= 0) {
    close(master);
  }
  const int slave = slave_fd_.exchange(-1, std::memory_order_acq_rel);
  if (slave >= 0) {
    close(slave);
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
        for (int i = 0; i < 50; ++i) {
          const pid_t reaped = waitpid(pid, &status, WNOHANG);
          if (reaped == pid || reaped < 0) {
            break;
          }
          usleep(static_cast<useconds_t>(kPollMs) * 1000);
        }
        break;
      }
      usleep(static_cast<useconds_t>(kPollMs) * 1000);
      waited_ms += kPollMs;
    }
  }
#endif
  running_.store(false, std::memory_order_release);
  emulator_only_.store(false, std::memory_order_release);
  output_chunks_.close();
  while (output_chunks_.try_pop().has_value()) {
  }
  output_chunks_.reset();
}

void AppSession::feed_bytes(const std::string& data) {
  if (data.empty()) {
    return;
  }
  if (!running()) {
    ensure_emulator(cols_, rows_);
  }
  on_pty_bytes(data.data(), data.size());
}

bool AppSession::consume_output_pending() {
  return output_pending_.exchange(false, std::memory_order_acquire);
}

std::string AppSession::display_text() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return display_text_;
}

std::vector<TerminalStyledRow> AppSession::display_styled_rows() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return display_styled_rows_;
}

int AppSession::cursor_col() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return terminal_.cursor_col();
}

int AppSession::cursor_row() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  return terminal_.cursor_row();
}

void AppSession::resize(int cols, int rows) {
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

void AppSession::write_raw(const std::string& data) {
#if defined(TUIDE_HAS_PTY)
  const int master = master_fd_.load(std::memory_order_acquire);
  if (!has_live_pty() || master < 0 || data.empty()) {
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
    if (written == 0 || (errno != EAGAIN && errno != EINTR)) {
      break;
    }
    break;
  }
#else
  (void)data;
#endif
}

void AppSession::send_interrupt() {
#if defined(TUIDE_HAS_PTY)
  write_raw("\x03");
#endif
}

bool AppSession::feed_pty_bytes_locked(const char* data, std::size_t len) {
  if (data == nullptr || len == 0) {
    return false;
  }
  const std::string prev_text = display_text_;
  const int prev_col = terminal_.cursor_col();
  const int prev_row = terminal_.cursor_row();
  const std::size_t prev_rows = display_styled_rows_.size();
  terminal_.feed(data, len);
  rebuild_display_locked();
  return display_text_ != prev_text || terminal_.cursor_col() != prev_col ||
         terminal_.cursor_row() != prev_row || display_styled_rows_.size() != prev_rows;
}

void AppSession::on_pty_bytes(const char* data, std::size_t len) {
  bool visual_changed = false;
  {
    std::lock_guard<std::mutex> lock(terminal_mutex_);
    while (auto chunk = output_chunks_.try_pop()) {
      if (feed_pty_bytes_locked(chunk->data(), chunk->size())) {
        visual_changed = true;
      }
    }
    if (feed_pty_bytes_locked(data, len)) {
      visual_changed = true;
    }
  }
  if (!visual_changed) {
    return;
  }
  output_pending_.store(true, std::memory_order_release);
  // Always wake: App output must repaint even if set_consumer_active races
  // briefly after open_host_pty (which used to clear consumer_active in stop()).
  notify_output();
}

void AppSession::reader_loop() {
#if defined(TUIDE_HAS_PTY)
  std::vector<char> buffer(4096);
  while (!stop_requested_.load(std::memory_order_acquire)) {
    const int master = master_fd_.load(std::memory_order_acquire);
    const int wake = wake_fd_.load(std::memory_order_acquire);
    if (master < 0) {
      break;
    }

    pollfd fds[2]{};
    nfds_t nfds = 0;
    fds[nfds++] = pollfd{master, POLLIN, 0};
    if (wake >= 0) {
      fds[nfds++] = pollfd{wake, POLLIN, 0};
    }
    const int pr = poll(fds, nfds, 100);
    if (stop_requested_.load(std::memory_order_acquire)) {
      break;
    }
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    if (wake >= 0 && nfds > 1 && (fds[1].revents & (POLLIN | POLLERR | POLLHUP))) {
      drain_wake_fd();
      if (stop_requested_.load(std::memory_order_acquire)) {
        break;
      }
    }

    if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
      continue;
    }

    const ssize_t bytes = read(master, buffer.data(), buffer.size());
    if (bytes > 0) {
      on_pty_bytes(buffer.data(), static_cast<std::size_t>(bytes));
      continue;
    }
    if (bytes == 0) {
      break;
    }
    if (errno == EAGAIN || errno == EINTR) {
      continue;
    }
    break;
  }
  running_.store(false, std::memory_order_release);
  if (!stop_requested_.load(std::memory_order_acquire)) {
    notify_output();
  }
#endif
}

void AppSession::drain_output(int max_bytes) { (void)drain_output_bytes(max_bytes); }

int AppSession::drain_output_bytes(int max_bytes) {
  if (max_bytes <= 0) {
    return 0;
  }
  int drained = 0;
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  while (drained < max_bytes) {
    auto chunk = output_chunks_.try_pop();
    if (!chunk) {
      break;
    }
    const int n = static_cast<int>(std::min(chunk->size(), static_cast<std::size_t>(max_bytes - drained)));
    if (n > 0) {
      feed_pty_bytes_locked(chunk->data(), static_cast<std::size_t>(n));
      drained += n;
    }
  }
  if (drained > 0) {
    rebuild_display_locked();
  }
  return drained;
}

void AppSession::background_drain() {
  if (consumer_active_.load(std::memory_order_acquire)) {
    return;
  }
  drain_output_bytes(8192);
}

void AppSession::rebuild_display_locked() {
  display_text_ = terminal_.text();
  display_styled_rows_ = terminal_.styled_rows();
}

void AppSession::rebuild_display() {
  std::lock_guard<std::mutex> lock(terminal_mutex_);
  rebuild_display_locked();
}

std::size_t AppSession::pending_output_chunks() const { return output_chunks_.size(); }

void AppSession::set_output_notify(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(notify_mutex_);
  output_notify_ = std::move(callback);
}

void AppSession::notify_output() {
  if (stop_requested_.load(std::memory_order_acquire)) {
    return;
  }
  std::function<void()> cb;
  {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    cb = output_notify_;
  }
  if (cb) {
    cb();
  }
}

void AppSession::set_consumer_active(bool active) {
  const bool previous = consumer_active_.exchange(active, std::memory_order_acq_rel);
  if (previous == active) {
    return;
  }
  if (active) {
    {
      std::lock_guard<std::mutex> lock(terminal_mutex_);
      rebuild_display_locked();
      output_pending_.store(true, std::memory_order_release);
    }
    notify_output();
  } else {
    background_drain();
  }
}

}  // namespace tuide
