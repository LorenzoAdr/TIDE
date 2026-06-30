#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct GitCommandResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
  bool success() const { return exit_code == 0; }
};

GitCommandResult run_git(const std::string& cwd, const std::vector<std::string>& args);

bool git_available();

}  // namespace tgdb
