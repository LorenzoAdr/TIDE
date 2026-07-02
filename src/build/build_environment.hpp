#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tgdb {

enum class BuildSystemKind {
  kUnknown,
  kCMake,
  kMakefile,
  kHybrid,
};

enum class MakeInterceptTool {
  kAuto,
  kBear,
  kCompiledb,
  kBuiltin,
};

struct BuildEnvironment {
  std::string id;
  std::string label;
  BuildSystemKind system = BuildSystemKind::kUnknown;
  std::string working_dir;
  std::string make_command = "make";
  std::vector<std::string> setup_scripts;
  std::map<std::string, std::string> env_vars;
  std::string docker_container;
  std::vector<std::string> marker_paths;
  std::vector<std::string> fallback_compile_flags;
  int score = 0;
};

struct BuildEnvironmentProfile {
  std::string id;
  std::string label;
  std::string working_dir;
  std::string make_command = "make";
  std::vector<std::string> setup_scripts;
  std::map<std::string, std::string> env_vars;
  std::string docker_container;
  std::vector<std::string> marker_paths;
};

struct BuildEnvironmentSettings {
  std::vector<BuildEnvironmentProfile> profiles;
  std::string active_environment_id = "auto";
  MakeInterceptTool make_intercept_tool = MakeInterceptTool::kAuto;
  std::string make_default_target;
};

struct BuildEnvironmentState {
  std::string last_active_environment_id;
  std::vector<BuildEnvironment> discovered_environments;
  std::int64_t make_qp_cache_mtime = 0;
  std::string make_qp_cache_text;
};

struct EnvironmentSelectionHints {
  std::string active_file_path;
  std::string terminal_cwd;
};

std::string build_system_kind_name(BuildSystemKind kind);
BuildSystemKind detect_build_system_kind(const std::string& workspace_root);

std::string make_intercept_tool_name(MakeInterceptTool tool);
MakeInterceptTool parse_make_intercept_tool(const std::string& value);

std::string build_environment_id(const BuildEnvironment& env);
std::string environment_compile_dir(const std::string& workspace_root,
                                    const std::string& environment_id);
std::string environment_compile_commands_path(const std::string& workspace_root,
                                                const std::string& environment_id);

}  // namespace tgdb
