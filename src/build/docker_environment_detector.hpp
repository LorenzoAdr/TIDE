#pragma once

#include <string>
#include <vector>

#include "build/build_environment.hpp"

namespace tgdb {

std::vector<BuildEnvironment> discover_docker_environments(const std::string& workspace_root);

}  // namespace tgdb
