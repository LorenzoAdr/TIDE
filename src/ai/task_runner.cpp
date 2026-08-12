#include "ai/task_runner.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>
#include <unistd.h>

#include <fcntl.h>
#include <sys/wait.h>

#include "util/shell_args.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string shell_quote(const std::string& value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

bool file_exists(const std::string& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

void close_fd(int fd) {
  if (fd >= 0) {
    ::close(fd);
  }
}

}  // namespace

void TaskRunner::set_whitelist(std::vector<std::string> whitelist) {
  std::lock_guard lock(mu_);
  whitelist_ = std::move(whitelist);
}

void TaskRunner::set_tasks(std::vector<AiTaskSpec> tasks) {
  std::lock_guard lock(mu_);
  tasks_ = std::move(tasks);
}

void TaskRunner::ensure_default_tasks(const std::string& workspace_root) {
  std::lock_guard lock(mu_);
  auto has = [&](const std::string& name) {
    for (const auto& t : tasks_) {
      if (t.name == name) {
        return true;
      }
    }
    return false;
  };

  if (!has("compile")) {
    // -y: never launch the interactive bundle wizard from AI (no TTY).
    std::string cmd = "cmake --build build";
    if (!workspace_root.empty()) {
      if (file_exists((fs::path(workspace_root) / "tools" / "compile.sh").string())) {
        cmd = "./tools/compile.sh -y";
      } else if (file_exists((fs::path(workspace_root) / "compile.sh").string())) {
        cmd = "./compile.sh -y";
      } else if (file_exists((fs::path(workspace_root) / "Makefile").string())) {
        cmd = "make -j";
      }
    }
    tasks_.push_back({"compile", cmd});
  }
  if (!has("launch")) {
    std::string cmd = "./launch.sh";
    if (!workspace_root.empty() &&
        file_exists((fs::path(workspace_root) / "launch.sh").string())) {
      cmd = "./launch.sh";
    } else if (!workspace_root.empty() &&
               file_exists((fs::path(workspace_root) / "tuide").string())) {
      cmd = "./tuide";
    }
    tasks_.push_back({"launch", cmd});
  }
}

bool TaskRunner::is_whitelisted(const std::string& name_or_command) const {
  std::lock_guard lock(mu_);
  for (const auto& entry : whitelist_) {
    if (entry == name_or_command) {
      return true;
    }
  }
  return matches_whitelist(split_shell_args(name_or_command));
}

bool TaskRunner::matches_whitelist(const std::vector<std::string>& argv) const {
  if (argv.empty()) {
    return false;
  }
  for (const auto& entry : whitelist_) {
    if (entry == argv[0]) {
      return true;
    }
    const auto allowed = split_shell_args(entry);
    if (!allowed.empty() && allowed.size() <= argv.size()) {
      bool ok = true;
      for (std::size_t i = 0; i < allowed.size(); ++i) {
        if (allowed[i] != argv[i]) {
          ok = false;
          break;
        }
      }
      if (ok) {
        return true;
      }
    }
  }
  return false;
}

TaskRunnerResult TaskRunner::deny(const std::string& reason) const {
  TaskRunnerResult r;
  r.allowed = false;
  r.deny_reason = reason;
  return r;
}

TaskRunnerResult TaskRunner::run(const std::string& name_or_command, const std::string& cwd,
                                 const LineCallback& on_line) {
  if (name_or_command.empty()) {
    return deny("comando vacío");
  }

  std::string command;
  {
    std::lock_guard lock(mu_);
    bool found_task = false;
    for (const auto& t : tasks_) {
      if (t.name == name_or_command) {
        command = t.command;
        found_task = true;
        bool allowed = false;
        for (const auto& w : whitelist_) {
          if (w == t.name) {
            allowed = true;
            break;
          }
        }
        if (!allowed && !matches_whitelist(split_shell_args(command))) {
          return deny("task '" + t.name + "' no está en ai.command_whitelist");
        }
        break;
      }
    }
    if (!found_task) {
      command = name_or_command;
      if (!matches_whitelist(split_shell_args(command))) {
        bool named = false;
        for (const auto& w : whitelist_) {
          if (w == name_or_command) {
            named = true;
            break;
          }
        }
        if (!named) {
          return deny("comando no whitelisted: " + name_or_command +
                      " (añádelo a ai.command_whitelist)");
        }
        return deny("task desconocida: " + name_or_command);
      }
    }
  }

  if (busy_.exchange(true)) {
    return deny("ya hay una task en curso (cancela antes)");
  }
  cancel_ = false;
  child_pid_.store(-1);

  TaskRunnerResult result;
  result.allowed = true;
  result.started = true;

  // Keep cwd in the shell command so we don't need chdir before fork races.
  std::ostringstream cmd;
  if (!cwd.empty()) {
    cmd << "cd " << shell_quote(cwd) << " && ";
  }
  cmd << command;

  int pipefd[2] = {-1, -1};
  if (::pipe(pipefd) != 0) {
    busy_ = false;
    result.exit_code = -1;
    result.stderr_text = std::string("pipe failed: ") + std::strerror(errno);
    return result;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    close_fd(pipefd[0]);
    close_fd(pipefd[1]);
    busy_ = false;
    result.exit_code = -1;
    result.stderr_text = std::string("fork failed: ") + std::strerror(errno);
    return result;
  }

  if (pid == 0) {
    // Own process group so cancel can kill the whole tree (compile.sh → cmake → …).
    ::setpgid(0, 0);
    close_fd(pipefd[0]);
    ::dup2(pipefd[1], STDOUT_FILENO);
    ::dup2(pipefd[1], STDERR_FILENO);
    if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
      close_fd(pipefd[1]);
    }
    ::execl("/bin/sh", "sh", "-c", cmd.str().c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }

  close_fd(pipefd[1]);
  ::setpgid(pid, pid);  // parent side; ignore races with child
  child_pid_.store(pid);

  // Make reads interruptible via kill→EOF after cancel.
  {
    const int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
      ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }
  }

  std::array<char, 4096> buffer{};
  std::string pending;
  bool saw_eof = false;
  while (!saw_eof) {
    if (cancel_.load()) {
      const pid_t live = child_pid_.load();
      if (live > 0) {
        ::kill(-live, SIGTERM);
      }
      break;
    }

    const ssize_t n = ::read(pipefd[0], buffer.data(), buffer.size());
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Poll cancel without spinning the CPU hard.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      break;
    }
    if (n == 0) {
      saw_eof = true;
      break;
    }
    pending.append(buffer.data(), static_cast<std::size_t>(n));
    std::size_t pos = 0;
    while ((pos = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, pos);
      pending.erase(0, pos + 1);
      result.stdout_text += line;
      result.stdout_text.push_back('\n');
      if (on_line) {
        on_line(line);
      }
    }
  }
  if (!pending.empty()) {
    result.stdout_text += pending;
    if (on_line) {
      on_line(pending);
    }
  }

  close_fd(pipefd[0]);

  if (cancel_.load()) {
    const pid_t live = child_pid_.exchange(-1);
    if (live > 0) {
      ::kill(-live, SIGTERM);
      // Brief grace, then force-kill the group.
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      ::kill(-live, SIGKILL);
    }
  }

  int status = 0;
  const pid_t wait_pid = child_pid_.exchange(-1);
  const pid_t reap = wait_pid > 0 ? wait_pid : pid;
  while (::waitpid(reap, &status, 0) < 0) {
    if (errno != EINTR) {
      status = -1;
      break;
    }
  }

  if (cancel_.load()) {
    result.exit_code = -1;
    result.stderr_text = "cancelado";
  } else if (status < 0) {
    result.exit_code = -1;
  } else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else {
    result.exit_code = -1;
  }
  busy_ = false;
  return result;
}

void TaskRunner::cancel() {
  cancel_ = true;
  const pid_t pid = child_pid_.load();
  if (pid > 0) {
    ::kill(-pid, SIGTERM);
  }
}

}  // namespace tuide
