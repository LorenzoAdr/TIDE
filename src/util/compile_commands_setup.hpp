#pragma once

#include <string>

namespace tgdb {

// Returns the directory containing compile_commands.json, or empty if not found.
std::string find_compile_commands_dir(const std::string& workspace_root);

// Locates or generates compile_commands.json on the host (cmake configure when needed)
// and links it at the workspace root when it only exists under a build directory.
// Returns the compile-commands directory for clangd's --compile-commands-dir.
std::string ensure_host_compile_commands_dir(const std::string& workspace_root);

}  // namespace tgdb
