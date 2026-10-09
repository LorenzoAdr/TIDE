#include "util/docker_clangd.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "util/bundled_tools.hpp"
#include "util/compile_commands_remap.hpp"
#include "util/compile_commands_setup.hpp"
#include "util/shell_utils.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr int kDockerExecTimeoutSeconds = 20;
constexpr int kDockerCopyTimeoutSeconds = 120;

std::string trim_trailing_slashes(std::string path) {
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

std::string trim_trailing_whitespace(std::string text) {
  while (!text.empty() &&
         (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  return text;
}

std::string canonical_host_path(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
  return trim_trailing_slashes(ec ? path : canonical.string());
}

std::string read_text_file(const fs::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::optional<std::string> resolve_docker_binary() {
  const std::string output = trim_trailing_whitespace(run_shell_capture("command -v docker", 5));
  if (output.empty() || output.front() != '/') {
    return std::nullopt;
  }
  std::error_code ec;
  if (!fs::is_regular_file(output, ec)) {
    return std::nullopt;
  }
  return output;
}

std::string map_host_to_container(const std::string& host_path,
                                  std::vector<PathMapping> mappings) {
  const std::string host = canonical_host_path(host_path);
  std::sort(mappings.begin(), mappings.end(),
            [](const PathMapping& a, const PathMapping& b) { return a.to.size() > b.to.size(); });
  for (const auto& mapping : mappings) {
    const std::string host_prefix = trim_trailing_slashes(mapping.to);
    const std::string container_prefix = trim_trailing_slashes(mapping.from);
    if (host_prefix.empty() || container_prefix.empty() || host.size() < host_prefix.size()) {
      continue;
    }
    if (host.compare(0, host_prefix.size(), host_prefix) != 0) {
      continue;
    }
    if (host.size() > host_prefix.size() && host[host_prefix.size()] != '/') {
      continue;
    }
    return container_prefix + host.substr(host_prefix.size());
  }
  return host;
}

bool host_path_maps_into_container(const std::string& host_path,
                                   const std::vector<PathMapping>& mappings) {
  if (host_path.empty()) {
    return false;
  }
  const std::string host = canonical_host_path(host_path);
  const std::string mapped = map_host_to_container(host_path, mappings);
  return !mapped.empty() && mapped != host;
}

std::vector<PathMapping> collect_mappings(const CompileCommandsSettings& settings) {
  std::vector<PathMapping> mappings;
  mappings.reserve(settings.path_mappings.size());
  for (const auto& mapping : settings.path_mappings) {
    PathMapping normalized;
    normalized.from = trim_trailing_slashes(mapping.from);
    normalized.to = canonical_host_path(mapping.to);
    if (!normalized.from.empty() && !normalized.to.empty()) {
      mappings.push_back(std::move(normalized));
    }
  }
  if (settings.docker_detect_mounts && !settings.docker_container.empty()) {
    const auto detected = detect_docker_mount_mappings(settings.docker_container);
    mappings.insert(mappings.end(), detected.begin(), detected.end());
  }
  return mappings;
}

std::string install_marker(const ClangdLocation& clangd) {
  if (const auto root = docker_clangd_install_root(clangd.binary_path, clangd.resource_dir)) {
    const std::string installed = read_text_file(fs::path(*root) / ".installed");
    if (!installed.empty()) {
      return installed;
    }
  }
  std::error_code ec;
  const auto binary_size = fs::file_size(clangd.binary_path, ec);
  std::ostringstream marker;
  marker << clangd.binary_path << '\n' << (ec ? 0 : binary_size) << '\n' << clangd.resource_dir
         << '\n';
  return marker.str();
}

std::string docker_exec(const std::string& docker_binary, const std::string& container,
                        const std::string& remote_command) {
  return shell_quote(docker_binary) + " exec " + shell_quote(container) + " " + remote_command;
}

bool remote_marker_matches(const std::string& docker_binary, const std::string& container,
                           const std::string& marker) {
  std::string output;
  const std::string command =
      docker_exec(docker_binary, container,
                  "cat " + shell_quote(std::string(kDockerClangdRemoteRoot) + "/.installed")) +
      " 2>/dev/null";
  if (run_shell_status(command, kDockerExecTimeoutSeconds, &output) != 0) {
    return false;
  }
  return output == marker;
}

bool remote_clangd_runs(const std::string& docker_binary, const std::string& container,
                        const std::string& remote_binary) {
  const std::string command =
      docker_exec(docker_binary, container, shell_quote(remote_binary) + " --version") +
      " >/dev/null 2>&1";
  return run_shell_status(command, kDockerExecTimeoutSeconds) == 0;
}

bool write_remote_marker(const std::string& docker_binary, const std::string& container,
                         const std::string& marker) {
  const std::string script = "printf %s " + shell_quote(marker) + " > " +
                             shell_quote(std::string(kDockerClangdRemoteRoot) + "/.installed");
  const std::string command =
      docker_exec(docker_binary, container, "sh -c " + shell_quote(script)) + " 2>/dev/null";
  return run_shell_status(command, kDockerExecTimeoutSeconds) == 0;
}

bool copy_install_tree(const std::string& docker_binary, const std::string& container,
                       const std::string& host_root) {
  const std::string remove_command =
      docker_exec(docker_binary, container, "rm -rf " + shell_quote(kDockerClangdRemoteRoot)) +
      " 2>/dev/null";
  if (run_shell_status(remove_command, kDockerExecTimeoutSeconds) != 0) {
    return false;
  }
  const std::string copy_command = shell_quote(docker_binary) + " cp " + shell_quote(host_root) +
                                   " " +
                                   shell_quote(container + ":" + kDockerClangdRemoteRoot) +
                                   " 2>/dev/null";
  return run_shell_status(copy_command, kDockerCopyTimeoutSeconds) == 0;
}

bool copy_split_tree(const std::string& docker_binary, const std::string& container,
                     const ClangdLocation& clangd) {
  const std::string remove_command =
      docker_exec(docker_binary, container, "rm -rf " + shell_quote(kDockerClangdRemoteRoot)) +
      " 2>/dev/null";
  if (run_shell_status(remove_command, kDockerExecTimeoutSeconds) != 0) {
    return false;
  }
  const std::string mkdir_command =
      docker_exec(docker_binary, container,
                  "mkdir -p " + shell_quote(std::string(kDockerClangdRemoteRoot) + "/bin")) +
      " 2>/dev/null";
  if (run_shell_status(mkdir_command, kDockerExecTimeoutSeconds) != 0) {
    return false;
  }
  const std::string binary_dest =
      container + ":" + kDockerClangdRemoteRoot + "/bin/clangd";
  const std::string copy_binary = shell_quote(docker_binary) + " cp " +
                                  shell_quote(clangd.binary_path) + " " + shell_quote(binary_dest) +
                                  " 2>/dev/null";
  if (run_shell_status(copy_binary, kDockerCopyTimeoutSeconds) != 0) {
    return false;
  }
  if (clangd.resource_dir.empty()) {
    return true;
  }
  const std::string resource_dest = container + ":" + kDockerClangdRemoteRoot + "/resource";
  const std::string copy_resource = shell_quote(docker_binary) + " cp " +
                                    shell_quote(clangd.resource_dir) + " " +
                                    shell_quote(resource_dest) + " 2>/dev/null";
  return run_shell_status(copy_resource, kDockerCopyTimeoutSeconds) == 0;
}

bool inject_clangd(const std::string& docker_binary, const std::string& container,
                   const ClangdLocation& clangd, const std::string& marker) {
  const std::string remote_binary = std::string(kDockerClangdRemoteRoot) + "/bin/clangd";
  if (remote_marker_matches(docker_binary, container, marker)) {
    // Same bytes already in the container. A glibc mismatch will not change by copying again.
    return remote_clangd_runs(docker_binary, container, remote_binary);
  }

  bool copied = false;
  if (const auto root = docker_clangd_install_root(clangd.binary_path, clangd.resource_dir)) {
    copied = copy_install_tree(docker_binary, container, *root);
  } else {
    copied = copy_split_tree(docker_binary, container, clangd);
  }
  if (!copied) {
    return false;
  }
  const std::string chmod_command =
      docker_exec(docker_binary, container, "chmod +x " + shell_quote(remote_binary)) +
      " 2>/dev/null";
  (void)run_shell_status(chmod_command, kDockerExecTimeoutSeconds);
  (void)write_remote_marker(docker_binary, container, marker);
  return remote_clangd_runs(docker_binary, container, remote_binary);
}

bool remote_compile_commands_exist(const std::string& docker_binary, const std::string& container,
                                   const std::string& directory) {
  if (directory.empty()) {
    return false;
  }
  const std::string file = trim_trailing_slashes(directory) + "/compile_commands.json";
  const std::string script = "test -f " + shell_quote(file);
  const std::string command =
      docker_exec(docker_binary, container, "sh -c " + shell_quote(script)) + " 2>/dev/null";
  return run_shell_status(command, kDockerExecTimeoutSeconds) == 0;
}

std::string resolve_remote_compile_commands_dir(const std::string& docker_binary,
                                               const std::string& workspace_root,
                                               const std::string& host_compile_commands_dir,
                                               const CompileCommandsSettings& settings,
                                               const std::vector<PathMapping>& mappings) {
  std::vector<std::string> candidates;
  if (!settings.docker_compile_commands_path.empty()) {
    candidates.push_back(fs::path(settings.docker_compile_commands_path).parent_path().string());
  }

  std::string host_dir = host_compile_commands_dir;
  const std::string private_dir = WorkspaceConfig::private_dir(workspace_root);
  if (!private_dir.empty() && host_dir == private_dir) {
    // .tuide/compile_commands.json is the host-remapped database. Clangd's path mappings
    // rewrite LSP file URIs only, not paths inside the compilation database, so the
    // process inside the container must read the original file (container paths).
    host_dir = find_compile_commands_dir(workspace_root);
  }
  if (!host_dir.empty() && host_path_maps_into_container(host_dir, mappings)) {
    candidates.push_back(map_host_to_container(host_dir, mappings));
  }
  if (!settings.source_path.empty() && !workspace_root.empty()) {
    const fs::path source = fs::path(workspace_root) / settings.source_path;
    const std::string source_dir = source.parent_path().string();
    if (host_path_maps_into_container(source_dir, mappings)) {
      candidates.push_back(map_host_to_container(source_dir, mappings));
    }
  }
  candidates.insert(candidates.end(),
                    {"/workspace/build", "/workspace", "/project/build", "/src/build"});

  for (const auto& candidate : candidates) {
    if (remote_compile_commands_exist(docker_binary, settings.docker_container, candidate)) {
      return trim_trailing_slashes(candidate);
    }
  }
  return {};
}

}  // namespace

std::optional<DockerClangdLaunch> prepare_docker_clangd_launch(
    const std::string& workspace_root, const std::string& host_compile_commands_dir,
    const CompileCommandsSettings& settings) {
  if (workspace_root.empty() || settings.docker_container.empty() ||
      settings.mode == CompileCommandsMode::kHost) {
    return std::nullopt;
  }
  const auto clangd = resolve_clangd();
  if (!clangd.has_value() || clangd->binary_path.empty()) {
    return std::nullopt;
  }
  const auto docker_binary = resolve_docker_binary();
  if (!docker_binary.has_value()) {
    return std::nullopt;
  }

  const std::vector<PathMapping> mappings = collect_mappings(settings);
  if (!host_path_maps_into_container(workspace_root, mappings)) {
    return std::nullopt;
  }

  const std::string marker = install_marker(*clangd);
  if (!inject_clangd(*docker_binary, settings.docker_container, *clangd, marker)) {
    return std::nullopt;
  }

  std::vector<std::pair<std::string, std::string>> client_to_server;
  client_to_server.reserve(mappings.size());
  for (const auto& mapping : mappings) {
    client_to_server.emplace_back(trim_trailing_slashes(mapping.to),
                                  trim_trailing_slashes(mapping.from));
  }

  DockerClangdLaunch launch;
  launch.docker_binary = *docker_binary;
  launch.container = settings.docker_container;
  launch.remote_binary = std::string(kDockerClangdRemoteRoot) + "/bin/clangd";
  if (const auto root = docker_clangd_install_root(clangd->binary_path, clangd->resource_dir)) {
    launch.remote_resource_dir =
        docker_clangd_remote_resource_dir(*root, clangd->resource_dir);
  } else {
    launch.remote_resource_dir = std::string(kDockerClangdRemoteRoot) + "/resource";
  }
  launch.remote_compile_commands_dir = resolve_remote_compile_commands_dir(
      *docker_binary, workspace_root, host_compile_commands_dir, settings, mappings);
  launch.path_mappings = clangd_path_mappings_argument(client_to_server);
  if (launch.path_mappings.empty()) {
    return std::nullopt;
  }
  return launch;
}

}  // namespace tuide
