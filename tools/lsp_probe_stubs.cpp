#include "util/compile_commands_remap.hpp"
#include "util/docker_clangd.hpp"

namespace tuide {

CompileCommandsSetupResult ensure_compile_commands_for_clangd(
    const std::string& workspace_root, const WorkspaceConfig& /*config*/) {
  return CompileCommandsSetupResult{.compile_dir = workspace_root + "/.tuide"};
}

std::optional<DockerClangdLaunch> prepare_docker_clangd_launch(
    const std::string& /*workspace_root*/, const std::string& /*host_compile_commands_dir*/,
    const CompileCommandsSettings& /*settings*/) {
  return std::nullopt;
}

}  // namespace tuide
