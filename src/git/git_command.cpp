#include "git/git_command.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>

#include "i18n/tr.hpp"

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

}  // namespace

GitCommandResult run_git(const std::string& cwd, const std::vector<std::string>& args) {
  GitCommandResult result;
  if (cwd.empty()) {
    result.stderr_text = i18n::tr("git.empty_workspace");
    return result;
  }

  std::ostringstream cmd;
  cmd << "cd " << shell_quote(cwd) << " && git";
  for (const auto& arg : args) {
    cmd << ' ' << shell_quote(arg);
  }
  cmd << " 2>&1";

  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(cmd.str().c_str(), "r");
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

bool git_available() {
  const GitCommandResult result = run_git(".", {"--version"});
  return result.success();
}

}  // namespace tuide