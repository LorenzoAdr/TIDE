#include "terminal/shell_session.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <pty.h>
#endif
#endif

namespace tgdb {

ShellSession::ShellSession() = default;

ShellSession::~ShellSession() { stop(); }

bool ShellSession::running() const { return running_.load(std::memory_order_acquire); }

void ShellSession::apply_winsize() {
#if defined(__linux__)
  if (master_fd_ < 0) {
    return;
  }
  struct winsize ws = {};
  ws.ws_row = static_cast<unsigned short>(rows_);
  ws.ws_col = static_cast<unsigned short>(cols_);
  ioctl(master_fd_, TIOCSWINSZ, &ws);
#endif
}

void ShellSession::start(const std::string& cwd) {
  stop();

#if !defined(__linux__)
  return;
#else
  if (cwd.empty()) {
    return;
  }

  terminal_.reset(rows_, cols_);

  master_fd_ = -1;
  child_pid_ = forkpty(&master_fd_, nullptr, nullptr, nullptr);
  if (child_pid_ < 0) {
    master_fd_ = -1;
    return;
  }

  if (child_pid_ == 0) {
    if (chdir(cwd.c_str()) != 0) {
      _exit(127);
    }
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    const char* shell = std::getenv("SHELL");
    if (shell == nullptr || shell[0] == '\0') {
      shell = "/bin/bash";
    }
    execlp(shell, shell, "-il", static_cast<char*>(nullptr));
    _exit(127);
  }

  apply_winsize();

  const int flags = fcntl(master_fd_, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
  }

  stop_requested_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  reader_thread_ = std::make_unique<std::thread>([this] { reader_loop(); });
#endif
}

void ShellSession::stop() {
#if defined(__linux__)
  stop_requested_.store(true, std::memory_order_release);
  if (master_fd_ >= 0) {
    close(master_fd_);
    master_fd_ = -1;
  }
  if (child_pid_ > 0) {
    kill(child_pid_, SIGHUP);
    int status = 0;
    waitpid(child_pid_, &status, 0);
    child_pid_ = -1;
  }
  if (reader_thread_ && reader_thread_->joinable()) {
    reader_thread_->join();
  }
  reader_thread_.reset();
#endif
  running_.store(false, std::memory_order_release);
  output_chunks_.close();
  while (output_chunks_.try_pop().has_value()) {
  }
  output_chunks_.reset();
}

void ShellSession::resize(int cols, int rows) {
  cols = std::max(1, cols);
  rows = std::max(1, rows);
  if (cols_ == cols && rows_ == rows) {
    return;
  }
  cols_ = cols;
  rows_ = rows;
  terminal_.resize(cols_, rows_);
  apply_winsize();
}

void ShellSession::write_raw(const std::string& data) {
#if defined(__linux__)
  if (!running() || master_fd_ < 0 || data.empty()) {
    return;
  }
  const char* buf = data.c_str();
  std::size_t remaining = data.size();
  int retries = 200;
  while (remaining > 0 && retries-- > 0) {
    const ssize_t written = write(master_fd_, buf, remaining);
    if (written > 0) {
      buf += written;
      remaining -= static_cast<std::size_t>(written);
      continue;
    }
    if (written == 0) {
      break;
    }
    if (errno == EAGAIN || errno == EINTR) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
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

void ShellSession::append_bytes(const char* data, std::size_t size) {
  if (size == 0) {
    return;
  }
  output_chunks_.push(std::string(data, size));
}

void ShellSession::reader_loop() {
#if defined(__linux__)
  std::vector<char> buffer(4096);
  while (!stop_requested_.load(std::memory_order_acquire)) {
    if (master_fd_ < 0) {
      break;
    }
    const ssize_t bytes = read(master_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      append_bytes(buffer.data(), static_cast<std::size_t>(bytes));
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
#endif
  running_.store(false, std::memory_order_release);
}

void ShellSession::drain_output() {
  while (auto chunk = output_chunks_.try_pop()) {
    terminal_.feed(chunk->data(), chunk->size());
  }
}

}  // namespace tgdb
