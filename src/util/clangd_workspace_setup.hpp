#pragma once

#include "app/workspace_config.hpp"

namespace tgdb {

// Writes or removes the workspace .clangd file (background index + extra include paths).
void apply_clangd_workspace_config(const std::string& workspace_root,
                                   const WorkspaceConfig& config);

// Expands each root path into -I flags for the directory and its immediate children.
std::vector<std::string> expand_recursive_include_flags(
    const std::vector<std::string>& root_paths);

}  // namespace tgdb
