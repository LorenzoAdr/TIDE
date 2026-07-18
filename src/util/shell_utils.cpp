#include "util/shell_utils.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace tuide {

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

std::string run_shell_capture(const std::string& command, const int timeout_seconds) {
  std::string wrapped = command;
  if (timeout_seconds > 0) {
    wrapped = "timeout --foreground " + std::to_string(timeout_seconds) + "s " + command;
  }
  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(wrapped.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  pclose(pipe);
  return output;
}

bool run_shell_stdin(const std::string& command, const std::string& stdin_data,
                     const int timeout_seconds) {
  std::string wrapped = command;
  if (timeout_seconds > 0) {
    wrapped = "timeout --foreground " + std::to_string(timeout_seconds) + "s " + command;
  }
  FILE* pipe = popen(wrapped.c_str(), "w");
  if (pipe == nullptr) {
    return false;
  }
  if (!stdin_data.empty()) {
    const std::size_t written =
        std::fwrite(stdin_data.data(), 1, stdin_data.size(), pipe);
    if (written != stdin_data.size()) {
      pclose(pipe);
      return false;
    }
  }
  return pclose(pipe) != -1;
}

bool command_exists(const std::string& command) {
  if (command.empty()) {
    return false;
  }
  const std::string probe = "command -v " + shell_quote(command) + " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

}  // namespace tuide
