#pragma once

#include <string>

namespace tgdb {

std::string shell_quote(const std::string& value);
std::string run_shell_capture(const std::string& command, int timeout_seconds = 0);
bool run_shell_stdin(const std::string& command, const std::string& stdin_data,
                     int timeout_seconds = 0);
bool command_exists(const std::string& command);

}  // namespace tgdb
