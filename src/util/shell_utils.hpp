#pragma once

#include <string>

namespace tuide {

std::string shell_quote(const std::string& value);
std::string run_shell_capture(const std::string& command, int timeout_seconds = 0);
// Exit status of the shell command, or -1 if it could not be started. When output is
// non-null, stdout is stored there.
int run_shell_status(const std::string& command, int timeout_seconds = 0,
                     std::string* output = nullptr);
bool run_shell_stdin(const std::string& command, const std::string& stdin_data,
                     int timeout_seconds = 0);
bool command_exists(const std::string& command);

}  // namespace tuide
