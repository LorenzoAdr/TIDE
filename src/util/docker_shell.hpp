#pragma once

#include <string>
#include <vector>

#include "app/workspace_config.hpp"
#include "terminal/shell_session.hpp"

namespace tuide {

enum class DockerContainerRuntimeState { Missing, Stopped, Running };

enum class DockerReadyGateResult { Proceed, Wait, Decline };

std::string host_path_to_container_path(const std::string& host_path,
                                        const std::vector<PathMapping>& mount_mappings);

ShellLaunchConfig resolve_shell_launch_config(const std::string& workspace_root,
                                              const WorkspaceConfig& config);

DockerContainerRuntimeState docker_container_runtime_state(const std::string& container_name);

bool start_docker_container(const std::string& container_name);

}  // namespace tuide
