#pragma once

#include <string>
#include <vector>

namespace tuide {

std::vector<std::string> split_shell_args(const std::string& command);

std::string last_shell_token(const std::string& line);

std::vector<std::string> path_completions(const std::string& cwd, const std::string& token);

bool apply_path_tab_completion(std::string* line, const std::string& cwd);

}  // namespace tuide
