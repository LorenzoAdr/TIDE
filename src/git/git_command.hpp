#pragma once

#include <string>
#include <vector>

namespace tuide {

struct GitCommandResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
  bool success() const { return exit_code == 0; }
};

struct GitCredentials {
  std::string username;
  std::string password;
};

GitCommandResult run_git(const std::string& cwd, const std::vector<std::string>& args);
GitCommandResult run_git_with_credentials(const std::string& cwd,
                                          const std::vector<std::string>& args,
                                          const GitCredentials& credentials);

bool git_available();

// Detects auth/prompt failures from git pull/push output.
bool git_output_requires_credentials(const std::string& output);

// Prefill username from remote URL or credential.username (may be empty).
std::string git_configured_remote_username(const std::string& cwd);

}  // namespace tuide
