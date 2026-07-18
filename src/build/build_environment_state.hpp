#pragma once

#include <string>

#include "build/build_environment.hpp"

namespace tuide {

class BuildEnvironmentStateStore {
 public:
  static std::string state_path(const std::string& workspace_root);

  static BuildEnvironmentState load(const std::string& workspace_root);
  static bool save(const std::string& workspace_root, const BuildEnvironmentState& state);
};

}  // namespace tuide
