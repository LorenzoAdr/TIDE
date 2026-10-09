#pragma once

#include <optional>
#include <string>

#include "app/workspace_config.hpp"
#include "util/docker_clangd_spec.hpp"

namespace tuide {

// Copies the host clangd tree (bundled cache or another dedicated prefix) into the
// container and returns the argv pieces for `docker exec -i`. Nullopt means clangd
// should keep running on the host: no container, host mode, the workspace is not
// mounted, or the injected binary cannot start (typically a glibc mismatch).
std::optional<DockerClangdLaunch> prepare_docker_clangd_launch(
    const std::string& workspace_root, const std::string& host_compile_commands_dir,
    const CompileCommandsSettings& settings);

}  // namespace tuide
