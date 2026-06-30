#pragma once

#include <string>
#include <vector>

#include "app/workspace_config.hpp"

namespace tgdb {

struct CompileCommandsSetupResult {
  std::string compile_dir;
  std::string status_note;
};

std::vector<PathMapping> detect_docker_mount_mappings(const std::string& container_name);

std::vector<std::string> list_running_docker_containers();

std::string container_path_for_host_path(const std::string& host_path,
                                         const std::vector<PathMapping>& mount_mappings);

CompileCommandsSetupResult ensure_compile_commands_for_clangd(
    const std::string& workspace_root, const WorkspaceConfig& config);

}  // namespace tgdb
