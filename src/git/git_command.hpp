#pragma once

#include <atomic>
#include <string>
#include <sys/types.h>
#include <vector>

namespace tuide {

struct GitCommandResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
  bool cancelled = false;
  bool success() const { return !cancelled && exit_code == 0; }
};

struct GitCredentials {
  std::string username;
  std::string password;
};

// Cooperative cancel for a running git child (process group after setsid).
struct GitCommandCancel {
  GitCommandCancel() = default;
  GitCommandCancel(const GitCommandCancel&) = delete;
  GitCommandCancel& operator=(const GitCommandCancel&) = delete;

  std::atomic<bool> requested{false};
  std::atomic<pid_t> pid{0};
};

void request_git_cancel(GitCommandCancel* cancel);

GitCommandResult run_git(const std::string& cwd, const std::vector<std::string>& args,
                         GitCommandCancel* cancel = nullptr);
GitCommandResult run_git_with_credentials(const std::string& cwd,
                                          const std::vector<std::string>& args,
                                          const GitCredentials& credentials,
                                          GitCommandCancel* cancel = nullptr);

bool git_available();

// Detects auth/prompt failures from git pull/push output.
bool git_output_requires_credentials(const std::string& output);

// Prefill username from remote URL or credential.username (may be empty).
std::string git_configured_remote_username(const std::string& cwd);

}  // namespace tuide
