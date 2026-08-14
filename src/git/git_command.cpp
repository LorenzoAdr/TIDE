#include "git/git_command.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "i18n/tr.hpp"

namespace tuide {

namespace {

namespace fs = std::filesystem;

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

std::string to_lower(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

void apply_noninteractive_git_env(
    const std::vector<std::pair<std::string, std::string>>& extra_env) {
  // English diagnostics so auth heuristics stay stable regardless of UI locale.
  ::setenv("LC_ALL", "C", 1);
  ::setenv("LANG", "C", 1);
  ::setenv("GIT_TERMINAL_PROMPT", "0", 1);
  ::setenv("SSH_ASKPASS_REQUIRE", "force", 1);
  ::setenv("GCM_INTERACTIVE", "never", 1);
  bool have_git_askpass = false;
  bool have_ssh_askpass = false;
  for (const auto& [key, value] : extra_env) {
    ::setenv(key.c_str(), value.c_str(), 1);
    if (key == "GIT_ASKPASS") {
      have_git_askpass = true;
    }
    if (key == "SSH_ASKPASS") {
      have_ssh_askpass = true;
    }
  }
  // `false` exits 1 with no output: fail closed instead of prompting on the TUI tty.
  if (!have_git_askpass) {
    ::setenv("GIT_ASKPASS", "false", 1);
  }
  if (!have_ssh_askpass) {
    ::setenv("SSH_ASKPASS", "false", 1);
  }
}

GitCommandResult run_git_argv(const std::string& cwd, const std::vector<std::string>& args,
                              const std::vector<std::pair<std::string, std::string>>& extra_env) {
  GitCommandResult result;
  if (cwd.empty()) {
    result.stderr_text = i18n::tr("git.empty_workspace");
    return result;
  }

  int pipefd[2];
  if (::pipe(pipefd) != 0) {
    result.stderr_text = i18n::tr("git.exec_failed");
    return result;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    result.stderr_text = i18n::tr("git.exec_failed");
    return result;
  }
  if (pid == 0) {
    // New session: no controlling tty, so ssh/credential helpers cannot write a
    // 1-line password prompt over the alternate screen (black strip + snap-back).
    (void)::setsid();
    ::close(pipefd[0]);
    if (::dup2(pipefd[1], STDOUT_FILENO) < 0 || ::dup2(pipefd[1], STDERR_FILENO) < 0) {
      _exit(127);
    }
    if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
      ::close(pipefd[1]);
    }
    const int nullfd = ::open("/dev/null", O_RDWR);
    if (nullfd >= 0) {
      (void)::dup2(nullfd, STDIN_FILENO);
      if (nullfd > 2) {
        ::close(nullfd);
      }
    }

    apply_noninteractive_git_env(extra_env);
    if (::chdir(cwd.c_str()) != 0) {
      _exit(127);
    }

    std::vector<const char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back("git");
    for (const auto& arg : args) {
      argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    ::execvp("git", const_cast<char* const*>(argv.data()));
    _exit(127);
  }

  ::close(pipefd[1]);
  std::string output;
  std::array<char, 4096> buffer{};
  while (true) {
    const ssize_t n = ::read(pipefd[0], buffer.data(), buffer.size());
    if (n <= 0) {
      break;
    }
    output.append(buffer.data(), static_cast<std::size_t>(n));
  }
  ::close(pipefd[0]);

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    result.stderr_text = i18n::tr("git.exec_failed");
    return result;
  }
  result.stdout_text = output;
  result.stderr_text = output;
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  return result;
}

std::string write_askpass_script(const GitCredentials& credentials) {
  const fs::path dir = fs::temp_directory_path() / ("tuide-git-askpass-" + std::to_string(getpid()));
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path script = dir / "askpass.sh";
  std::ofstream out(script);
  if (!out) {
    return {};
  }
  // Respond to Username*/Password* prompts from git/credential helpers.
  out << "#!/bin/sh\n"
      << "case \"$1\" in\n"
      << "  *[Uu]sername*) printf '%s\\n' " << shell_quote(credentials.username) << " ;;\n"
      << "  *) printf '%s\\n' " << shell_quote(credentials.password) << " ;;\n"
      << "esac\n";
  out.close();
  chmod(script.c_str(), 0700);
  return script.string();
}

void cleanup_askpass_script(const std::string& script_path) {
  if (script_path.empty()) {
    return;
  }
  std::error_code ec;
  const fs::path script(script_path);
  fs::remove(script, ec);
  fs::remove(script.parent_path(), ec);
}

}  // namespace

GitCommandResult run_git(const std::string& cwd, const std::vector<std::string>& args) {
  return run_git_argv(cwd, args, {});
}

GitCommandResult run_git_with_credentials(const std::string& cwd,
                                          const std::vector<std::string>& args,
                                          const GitCredentials& credentials) {
  GitCommandResult result;
  if (cwd.empty()) {
    result.stderr_text = i18n::tr("git.empty_workspace");
    return result;
  }
  const std::string askpass = write_askpass_script(credentials);
  if (askpass.empty()) {
    result.stderr_text = i18n::tr("git.exec_failed");
    return result;
  }
  result = run_git_argv(cwd, args, {{"GIT_ASKPASS", askpass}, {"SSH_ASKPASS", askpass}});
  cleanup_askpass_script(askpass);
  return result;
}

bool git_available() {
  const GitCommandResult result = run_git(".", {"--version"});
  return result.success();
}

bool git_output_requires_credentials(const std::string& output) {
  const std::string lower = to_lower(output);
  static const char* kNeedles[] = {
      "authentication failed",
      "could not read username",
      "could not read password",
      "terminal prompts disabled",
      "invalid username or password",
      "access denied",
      "unauthorized",
      "403",
      "401",
      "permission denied",
      "askpass",
      "fatal: could not read",
      "could not read from remote",
      "passphrase",
      "authentication required",
      "password for",
      "username for",
      "enter passphrase",
      "failed to execute prompt",
      "publickey",
      "could not read from remote repository",
      "missing or invalid credentials",
      "incorrect password",
      "wrong passphrase",
      "no matching key",
      "denied to",
  };
  for (const char* needle : kNeedles) {
    if (lower.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string git_configured_remote_username(const std::string& cwd) {
  if (cwd.empty()) {
    return {};
  }
  {
    const auto cfg = run_git(cwd, {"config", "--get", "credential.username"});
    if (cfg.success()) {
      std::string user = cfg.stdout_text;
      while (!user.empty() && (user.back() == '\n' || user.back() == '\r')) {
        user.pop_back();
      }
      if (!user.empty()) {
        return user;
      }
    }
  }
  const auto url = run_git(cwd, {"config", "--get", "remote.origin.url"});
  if (!url.success()) {
    return {};
  }
  std::string remote = url.stdout_text;
  while (!remote.empty() && (remote.back() == '\n' || remote.back() == '\r')) {
    remote.pop_back();
  }
  // https://user@host/... or https://user:pass@host/...
  const auto scheme = remote.find("://");
  if (scheme == std::string::npos) {
    return {};
  }
  const std::size_t start = scheme + 3;
  const auto at = remote.find('@', start);
  if (at == std::string::npos) {
    return {};
  }
  const auto slash = remote.find('/', start);
  if (slash != std::string::npos && slash < at) {
    return {};
  }
  std::string userinfo = remote.substr(start, at - start);
  const auto colon = userinfo.find(':');
  if (colon != std::string::npos) {
    userinfo = userinfo.substr(0, colon);
  }
  return userinfo;
}

}  // namespace tuide
