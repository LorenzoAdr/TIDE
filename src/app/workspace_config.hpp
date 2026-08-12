#pragma once

#include <map>
#include <string>
#include <vector>

#include "ai/ai_types.hpp"
#include "build/build_environment.hpp"
#include "ui/theme.hpp"

namespace tuide {

enum class CompileCommandsMode {
  kAuto,
  kHost,
  kRemap,
  kDockerSync,
};

struct PathMapping {
  std::string from;
  std::string to;
};

struct CompileCommandsSettings {
  CompileCommandsMode mode = CompileCommandsMode::kAuto;
  std::string source_path = "build/compile_commands.json";
  std::string docker_container;
  std::string docker_compile_commands_path;
  bool docker_detect_mounts = true;
  std::vector<PathMapping> path_mappings;
};

struct WorkspaceConfig {
  std::vector<std::string> clangd_extra_include_paths;
  bool clangd_use_gcc_query_driver = true;
  bool clangd_background_index = false;
  theme::ThemeMode theme = theme::ThemeMode::kDark;
  theme::UiColorPreset ui_colors_preset = theme::UiColorPreset::kDarkClassic;
  theme::UiColorOverrides ui_colors;
  CompileCommandsSettings compile_commands;
  BuildEnvironmentSettings build_environments;
  AiSettings ai;

  static std::string config_dir(const std::string& workspace_root);
  static std::string private_dir(const std::string& workspace_root);
  static std::string config_path(const std::string& workspace_root);
  static std::string private_compile_commands_path(const std::string& workspace_root);
  static WorkspaceConfig load(const std::string& workspace_root);
  bool save(const std::string& workspace_root) const;
};

std::string compile_commands_mode_name(CompileCommandsMode mode);
CompileCommandsMode parse_compile_commands_mode(const std::string& value);

}  // namespace tuide
