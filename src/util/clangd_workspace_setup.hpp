#pragma once

#include "app/workspace_config.hpp"

namespace tgdb {

// Writes or removes the workspace .clangd file from extra include paths (recursive).
void apply_clangd_workspace_config(const std::string& workspace_root,
                                   const WorkspaceConfig& config);

// Expands each root path into -I flags for itself and every subdirectory.
std::vector<std::string> expand_recursive_include_flags(
    const std::vector<std::string>& root_paths);

}  // namespace tgdb
