#include "build/build_environment.hpp"

#include <filesystem>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

namespace tuide {

namespace {

bool has_makefile_at(const fs::path& dir) {
  std::error_code ec;
  return fs::is_regular_file(dir / "Makefile", ec) ||
         fs::is_regular_file(dir / "makefile", ec) ||
         fs::is_regular_file(dir / "GNUmakefile", ec);
}

}  // namespace

std::string build_system_kind_name(const BuildSystemKind kind) {
  switch (kind) {
    case BuildSystemKind::kCMake:
      return "cmake";
    case BuildSystemKind::kMakefile:
      return "makefile";
    case BuildSystemKind::kHybrid:
      return "hybrid";
    case BuildSystemKind::kUnknown:
      break;
  }
  return "unknown";
}

BuildSystemKind detect_build_system_kind(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return BuildSystemKind::kUnknown;
  }
  std::error_code ec;
  const fs::path root(workspace_root);
  const bool has_cmake = fs::is_regular_file(root / "CMakeLists.txt", ec);
  const bool has_make = has_makefile_at(root);
  if (has_cmake && has_make) {
    return BuildSystemKind::kHybrid;
  }
  if (has_cmake) {
    return BuildSystemKind::kCMake;
  }
  if (has_make) {
    return BuildSystemKind::kMakefile;
  }
  return BuildSystemKind::kUnknown;
}

std::string make_intercept_tool_name(const MakeInterceptTool tool) {
  switch (tool) {
    case MakeInterceptTool::kBear:
      return "bear";
    case MakeInterceptTool::kCompiledb:
      return "compiledb";
    case MakeInterceptTool::kBuiltin:
      return "builtin";
    case MakeInterceptTool::kAuto:
      break;
  }
  return "auto";
}

MakeInterceptTool parse_make_intercept_tool(const std::string& value) {
  if (value == "bear") {
    return MakeInterceptTool::kBear;
  }
  if (value == "compiledb") {
    return MakeInterceptTool::kCompiledb;
  }
  if (value == "builtin") {
    return MakeInterceptTool::kBuiltin;
  }
  return MakeInterceptTool::kAuto;
}

std::string build_environment_id(const BuildEnvironment& env) {
  std::ostringstream key;
  key << env.working_dir << '\0' << env.make_command << '\0' << env.docker_container;
  for (const auto& entry : env.env_vars) {
    key << '\0' << entry.first << '=' << entry.second;
  }
  const std::hash<std::string> hasher;
  const auto hash = hasher(key.str());
  std::ostringstream id;
  id << "env-" << std::hex << hash;
  return id.str();
}

std::string environment_compile_dir(const std::string& workspace_root,
                                      const std::string& environment_id) {
  if (workspace_root.empty() || environment_id.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / ".tuide" / "environments" / environment_id).string();
}

std::string environment_compile_commands_path(const std::string& workspace_root,
                                              const std::string& environment_id) {
  const std::string dir = environment_compile_dir(workspace_root, environment_id);
  if (dir.empty()) {
    return {};
  }
  return (fs::path(dir) / "compile_commands.json").string();
}

}  // namespace tuide
