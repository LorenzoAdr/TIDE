#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lsp/language_server_spec.hpp"

namespace tuide {

// Tree injected into the container. Not the AppImage mount: that FUSE mount cannot be
// bind-mounted into Docker, and a running container cannot gain a volume.
inline constexpr const char* kDockerClangdRemoteRoot = "/tmp/tuide-clangd";

// client = host path the editor uses, server = path clangd sees inside the container.
// Clangd matches the first prefix, so callers pass the longest host prefix first.
struct DockerClangdLaunch {
  std::string docker_binary;
  std::string container;
  std::string remote_binary;
  std::string remote_resource_dir;
  std::string remote_compile_commands_dir;
  std::string path_mappings;
};

// Dedicated install prefix (bundled cache or toolpack): <root>/bin/clangd and
// <root>/lib/clang/<ver>. System prefixes such as /usr are rejected so a copy never
// ships the whole host /usr into the container.
std::optional<std::string> docker_clangd_install_root(const std::string& binary_path,
                                                     const std::string& resource_dir);

// Resource dir as seen inside the container after the install root is copied to
// kDockerClangdRemoteRoot. Falls back to <remote>/resource for a split copy.
std::string docker_clangd_remote_resource_dir(const std::string& host_root,
                                              const std::string& host_resource_dir);

// `--path-mappings` value: `<host>=<container>,...`. Empty pairs and paths that contain
// ',' or '=' are dropped; clangd splits the flag on those characters.
std::string clangd_path_mappings_argument(
    const std::vector<std::pair<std::string, std::string>>& client_to_server);

LanguageServerSpec make_docker_clangd_spec(const std::string& workspace_root,
                                           const DockerClangdLaunch& launch,
                                           bool use_gcc_query_driver,
                                           bool background_index);

}  // namespace tuide
