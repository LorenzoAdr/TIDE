#include "dap/gdb_launcher.hpp"

#include "util/bundled_tools.hpp"

#include "util/child_process_guard.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

namespace tgdb {

namespace {

class FdReader : public dap::Reader {
 public:
  explicit FdReader(int fd) : fd_(fd) {}
  ~FdReader() override { close(); }

  bool isOpen() override { return fd_ >= 0; }

  void close() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  size_t read(void* buffer, size_t n) override {
    if (fd_ < 0) {
      return 0;
    }
    const ssize_t result = ::read(fd_, buffer, n);
    if (result <= 0) {
      return 0;
    }
    return static_cast<size_t>(result);
  }

 private:
  int fd_;
};

class FdWriter : public dap::Writer {
 public:
  explicit FdWriter(int fd) : fd_(fd) {}
  ~FdWriter() override { close(); }

  bool isOpen() override { return fd_ >= 0; }

  void close() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool write(const void* buffer, size_t n) override {
    if (fd_ < 0) {
      return false;
    }
    const uint8_t* data = static_cast<const uint8_t*>(buffer);
    size_t written = 0;
    while (written < n) {
      const ssize_t result = ::write(fd_, data + written, n - written);
      if (result <= 0) {
        return false;
      }
      written += static_cast<size_t>(result);
    }
    return true;
  }

 private:
  int fd_;
};

bool probe_dap(const std::string& gdb_path) {
  if (gdb_path.empty()) {
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }

  if (pid == 0) {
    child_die_with_parent();
    const int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO) {
        ::close(devnull);
      }
    }

    std::array<const char*, 6> argv = {
        gdb_path.c_str(),
        "--quiet",
        "-i=dap",
        "-ex",
        "quit",
        nullptr,
    };
    execv(gdb_path.c_str(), const_cast<char* const*>(argv.data()));
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

bool gdb_supports_dap_at(const std::string& gdb_path) { return probe_dap(gdb_path); }

bool gdb_supports_dap() {
  if (const auto gdb = resolve_gdb(); gdb.has_value()) {
    return gdb_supports_dap_at(gdb->binary_path);
  }
  return false;
}

GdbProcess::GdbProcess() = default;

GdbProcess::~GdbProcess() {
  stop();
}

bool GdbProcess::start() {
  if (running_) {
    return true;
  }

  const auto gdb = resolve_gdb();
  if (!gdb.has_value()) {
    return false;
  }

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    return false;
  }

  const std::string gdb_path = gdb->binary_path;
  const pid_t pid = fork();
  if (pid < 0) {
    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    return false;
  }

  if (pid == 0) {
    child_die_with_parent();
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stdout_pipe[1], STDERR_FILENO);

    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);

    std::array<const char*, 4> argv = {
        gdb_path.c_str(),
        "--quiet",
        "--interpreter=dap",
        nullptr,
    };
    execv(gdb_path.c_str(), const_cast<char* const*>(argv.data()));
    _exit(127);
  }

  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);

  child_pid_ = pid;
  stdin_write_fd_ = stdin_pipe[1];
  stdout_read_fd_ = stdout_pipe[0];

  reader_ = std::make_shared<FdReader>(stdout_read_fd_);
  writer_ = std::make_shared<FdWriter>(stdin_write_fd_);
  running_ = true;
  return true;
}

void GdbProcess::stop(bool force) {
  if (!running_) {
    return;
  }

  if (writer_) {
    writer_->close();
  }
  if (reader_) {
    reader_->close();
  }

  if (child_pid_ > 0) {
    int status = 0;
    if (force) {
      kill(child_pid_, SIGKILL);
      waitpid(child_pid_, &status, 0);
    } else {
      for (int i = 0; i < 20; ++i) {
        const pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == child_pid_ || result < 0) {
          break;
        }
        usleep(100000);
      }
      if (waitpid(child_pid_, &status, WNOHANG) == 0) {
        kill(child_pid_, SIGTERM);
        waitpid(child_pid_, &status, 0);
      }
    }
    child_pid_ = -1;
  }

  stdin_write_fd_ = -1;
  stdout_read_fd_ = -1;
  reader_.reset();
  writer_.reset();
  running_ = false;
}

}  // namespace tgdb
