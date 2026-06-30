#pragma once

#include <string>
#include <vector>

#include "app/workspace_config.hpp"
#include "terminal/shell_session.hpp"

namespace tgdb {

std::string host_path_to_container_path(const std::string& host_path,
                                        const std::vector<PathMapping>& mount_mappings);

ShellLaunchConfig resolve_shell_launch_config(const std::string& workspace_root,
                                              const WorkspaceConfig& config);

}  // namespace tgdb
