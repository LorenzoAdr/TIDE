#include <vector>

#include "dap/gdb_launcher.hpp"

#include "dap/debug_adapter_process.hpp"
#include "dap/debug_adapter_spec.hpp"

#include "util/bundled_tools.hpp"
#include "util/child_process_guard.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <signal.h>
#include <string>
#include <unordered_map>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

namespace tuide {

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

bool spawn_stdio_adapter(const std::string& command, const std::vector<std::string>& args,
                         int* child_pid, int* stdin_write_fd, int* stdout_read_fd,
                         std::shared_ptr<dap::Reader>* reader,
                         std::shared_ptr<dap::Writer>* writer) {
  if (command.empty() || child_pid == nullptr || stdin_write_fd == nullptr ||
      stdout_read_fd == nullptr || reader == nullptr || writer == nullptr) {
    return false;
  }

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    return false;
  }

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
    // Detach from tuide's controlling TTY so the adapter/inferior cannot steal
    // the foreground process group (SIGTTIN/SIGTTOU freezes the TUI).
    (void)::setsid();
    unsetenv("LD_PRELOAD");
    unsetenv("TUIDE_PKT_FILTER_SRC");
    unsetenv("TUIDE_PKT_FILTER_DST");
    unsetenv("TUIDE_PKT_DISABLE");
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    // Never merge adapter stderr into the DAP stdout pipe: any non-protocol
    // bytes (warnings, pydevd traces) corrupt framing and can tear down the
    // session on the next client write (SIGPIPE / silent exit).
    const int devnull = ::open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
      ::close(devnull);
    } else {
      dup2(stdout_pipe[1], STDERR_FILENO);
    }

    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);

    std::vector<std::string> args_storage;
    args_storage.reserve(args.size() + 1);
    args_storage.push_back(command);
    for (const std::string& arg : args) {
      args_storage.push_back(arg);
    }
    std::vector<char*> argv;
    argv.reserve(args_storage.size() + 1);
    for (std::string& arg : args_storage) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    execv(command.c_str(), argv.data());
    _exit(127);
  }

  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);
  *child_pid = pid;
  *stdin_write_fd = stdin_pipe[1];
  *stdout_read_fd = stdout_pipe[0];
  *reader = std::make_shared<FdReader>(*stdout_read_fd);
  *writer = std::make_shared<FdWriter>(*stdin_write_fd);
  return true;
}

bool probe_dap(const std::string& gdb_path) {
  if (gdb_path.empty()) {
    return false;
  }

  static std::mutex mu;
  static std::unordered_map<std::string, bool> cache;
  {
    std::lock_guard<std::mutex> lock(mu);
    if (const auto it = cache.find(gdb_path); it != cache.end()) {
      return it->second;
    }
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

  // Bound the probe: a stuck gdb must not freeze the UI (e.g. on settings close).
  bool ok = false;
  constexpr int kTimeoutMs = 1500;
  constexpr int kPollMs = 20;
  int waited_ms = 0;
  for (;;) {
    int status = 0;
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
      break;
    }
    if (result < 0) {
      ok = false;
      break;
    }
    if (waited_ms >= kTimeoutMs) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      ok = false;
      break;
    }
    usleep(static_cast<useconds_t>(kPollMs) * 1000);
    waited_ms += kPollMs;
  }

  {
    std::lock_guard<std::mutex> lock(mu);
    cache[gdb_path] = ok;
  }
  return ok;
}

class StdioDebugAdapterProcess : public IDebugAdapterProcess {
 public:
  explicit StdioDebugAdapterProcess(DebugAdapterSpec spec) : spec_(std::move(spec)) {}
  ~StdioDebugAdapterProcess() override { stop(); }

  bool start() override {
    if (running_) {
      return true;
    }
    if (!spawn_stdio_adapter(spec_.command, spec_.args, &child_pid_, &stdin_write_fd_,
                             &stdout_read_fd_, &reader_, &writer_)) {
      return false;
    }
    running_ = true;
    return true;
  }

  void stop(bool force = false) override {
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
      } else {
        kill(child_pid_, SIGTERM);
      }
      for (int i = 0; i < 40; ++i) {
        const pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == child_pid_ || result < 0) {
          child_pid_ = -1;
          break;
        }
        usleep(50000);
      }
      if (child_pid_ > 0) {
        kill(child_pid_, SIGKILL);
        waitpid(child_pid_, &status, 0);
        child_pid_ = -1;
      }
    }
    reader_.reset();
    writer_.reset();
    stdin_write_fd_ = -1;
    stdout_read_fd_ = -1;
    running_ = false;
  }

  std::shared_ptr<dap::Reader> reader() const override { return reader_; }
  std::shared_ptr<dap::Writer> writer() const override { return writer_; }
  bool running() const override { return running_; }
  bool process_alive() override {
    if (!running_ || child_pid_ <= 0) {
      return false;
    }
    int status = 0;
    const pid_t result = waitpid(child_pid_, &status, WNOHANG);
    if (result == child_pid_) {
      child_pid_ = -1;
      running_ = false;
      return false;
    }
    if (result < 0 && errno != EINTR) {
      child_pid_ = -1;
      running_ = false;
      return false;
    }
    return true;
  }
  DebugAdapterKind kind() const override { return spec_.kind; }
  const std::string& adapter_id() const override { return spec_.id; }

 private:
  DebugAdapterSpec spec_;
  int child_pid_ = -1;
  int stdin_write_fd_ = -1;
  int stdout_read_fd_ = -1;
  std::shared_ptr<dap::Reader> reader_;
  std::shared_ptr<dap::Writer> writer_;
  bool running_ = false;
};

}  // namespace

bool gdb_supports_dap_at(const std::string& gdb_path) { return probe_dap(gdb_path); }

bool gdb_supports_dap() {
  if (const auto gdb = resolve_gdb(); gdb.has_value()) {
    return gdb_supports_dap_at(gdb->binary_path);
  }
  return false;
}

bool debugpy_available() { return resolve_debugpy().has_value(); }

bool bashdb_dap_available() { return resolve_bash_debug_adapter().has_value(); }

GdbProcess::GdbProcess() = default;

GdbProcess::~GdbProcess() { stop(); }

bool GdbProcess::start() {
  if (running_) {
    return true;
  }
  const auto spec = make_gdb_adapter_spec();
  if (!spec.has_value()) {
    return false;
  }
  if (!spawn_stdio_adapter(spec->command, spec->args, &child_pid_, &stdin_write_fd_,
                           &stdout_read_fd_, &reader_, &writer_)) {
    return false;
  }
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
    } else {
      kill(child_pid_, SIGTERM);
    }
    for (int i = 0; i < 40; ++i) {
      const pid_t result = waitpid(child_pid_, &status, WNOHANG);
      if (result == child_pid_ || result < 0) {
        child_pid_ = -1;
        break;
      }
      usleep(50000);
    }
    if (child_pid_ > 0) {
      kill(child_pid_, SIGKILL);
      waitpid(child_pid_, &status, 0);
      child_pid_ = -1;
    }
  }

  reader_.reset();
  writer_.reset();
  stdin_write_fd_ = -1;
  stdout_read_fd_ = -1;
  running_ = false;
}

std::unique_ptr<IDebugAdapterProcess> create_debug_adapter_process(DebugAdapterKind kind) {
  const auto spec = make_debug_adapter_spec(kind);
  if (!spec.has_value()) {
    return nullptr;
  }
  return std::make_unique<StdioDebugAdapterProcess>(*spec);
}

std::unique_ptr<IDebugAdapterProcess> create_debug_adapter_process_for_program(
    const std::string& program_path) {
  return create_debug_adapter_process(debug_adapter_kind_for_program(program_path));
}

}  // namespace tuide
