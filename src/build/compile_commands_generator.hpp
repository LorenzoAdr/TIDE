#pragma once

#include <string>
#include <vector>

#include "app/workspace_config.hpp"
#include "build/build_environment.hpp"

namespace tgdb {

struct CompileCommandsGenerationResult {
  bool success = false;
  std::string compile_dir;
  std::string method;
  std::vector<std::string> fallback_compile_flags;
};

CompileCommandsGenerationResult generate_compile_commands(
    const std::string& workspace_root, const BuildEnvironment& environment,
    const WorkspaceConfig& config);

bool compile_commands_exists(const std::string& compile_dir);

}  // namespace tgdb
