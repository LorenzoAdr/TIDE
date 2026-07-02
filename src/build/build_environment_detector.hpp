#pragma once

#include <string>
#include <vector>

#include "app/workspace_config.hpp"
#include "build/build_environment.hpp"
#include "build/build_environment_state.hpp"

namespace tgdb {

std::vector<BuildEnvironment> discover_build_environments(
    const std::string& workspace_root, const WorkspaceConfig& config,
    BuildEnvironmentState* state);

std::vector<std::string> discover_setup_script_candidates(const std::string& workspace_root);
std::map<std::string, std::string> capture_env_after_sourcing(
    const std::string& workspace_root, const std::string& script_path);

std::vector<std::string> discover_recent_artifact_paths(const std::string& workspace_root,
                                                      const std::vector<std::string>& roots,
                                                      int max_results = 32);

}  // namespace tgdb
