#pragma once

#include <string>
#include <vector>

#include "build/build_environment.hpp"

namespace tuide {

struct EnvironmentSelectionResult {
  BuildEnvironment environment;
  bool changed = false;
};

EnvironmentSelectionResult select_active_environment(
    const std::vector<BuildEnvironment>& candidates, const std::string& configured_active_id,
    const EnvironmentSelectionHints& hints, const std::string& previous_environment_id);

int score_environment(const BuildEnvironment& env, const EnvironmentSelectionHints& hints,
                      const std::string& previous_environment_id);

}  // namespace tuide
