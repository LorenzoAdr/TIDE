#include "git/git_command.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

GitCommandResult run_git_command_line(const std::string& cmd) {
  GitCommandResult result;
  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    result.stderr_text = i18n::tr("git.exec_failed");
    return result;
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  const int status = pclose(pipe);
  result.stdout_text = output;
  result.stderr_text = output;
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  }
  return result;
}

std::string build_git_command(const std::string& cwd, const std::vector<std::string>& args,
                              const std::string& env_prefix = {}) {
  std::ostringstream cmd;
  if (!env_prefix.empty()) {
    cmd << env_prefix << ' ';
  }
  cmd << "cd " << shell_quote(cwd) << " && git";
  for (const auto& arg : args) {
    cmd << ' ' << shell_quote(arg);
  }
  cmd << " 2>&1";
  return cmd.str();
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
  GitCommandResult result;
  if (cwd.empty()) {
    result.stderr_text = i18n::tr("git.empty_workspace");
    return result;
  }
  // Disable interactive prompts so auth failures surface as errors we can handle in UI.
  return run_git_command_line(
      build_git_command(cwd, args, "GIT_TERMINAL_PROMPT=0 GIT_ASKPASS=true"));
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
  std::ostringstream env;
  env << "GIT_TERMINAL_PROMPT=0 GIT_ASKPASS=" << shell_quote(askpass)
      << " SSH_ASKPASS=" << shell_quote(askpass) << " SSH_ASKPASS_REQUIRE=force";
  result = run_git_command_line(build_git_command(cwd, args, env.str()));
  cleanup_askpass_script(askpass);
  return result;
}

bool git_available() {
  const GitCommandResult result = run_git(".", {"--version"});
  return result.success();
}

bool git_output_requires_credentials(const std::string& output) {
  const std::string lower = to_lower(output);
  return lower.find("authentication failed") != std::string::npos ||
         lower.find("could not read username") != std::string::npos ||
         lower.find("could not read password") != std::string::npos ||
         lower.find("terminal prompts disabled") != std::string::npos ||
         lower.find("invalid username or password") != std::string::npos ||
         lower.find("access denied") != std::string::npos ||
         lower.find("unauthorized") != std::string::npos ||
         lower.find("403") != std::string::npos ||
         lower.find("401") != std::string::npos ||
         lower.find("permission denied") != std::string::npos ||
         lower.find("askpass") != std::string::npos ||
         lower.find("fatal: could not read") != std::string::npos;
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
