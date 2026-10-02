#pragma once

#include <functional>
#include <string>

namespace tuide {

// Returns the directory containing compile_commands.json, or empty if not found.
std::string find_compile_commands_dir(const std::string& workspace_root);

// Locates an existing compile_commands.json on the host and links it at the workspace
// root when it only exists under a build directory. Does not run cmake (that can block
// the UI for minutes). Returns the compile-commands directory for clangd, or empty.
std::string ensure_host_compile_commands_dir(const std::string& workspace_root);

// Runs `cmake -S/-B -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` off the calling thread.
// `on_done` receives the compile-commands directory, or empty on failure/cancel.
void request_host_cmake_compile_commands(const std::string& workspace_root,
                                         std::function<void(std::string compile_dir)> on_done);
void shutdown_host_cmake_compile_commands();

}  // namespace tuide
