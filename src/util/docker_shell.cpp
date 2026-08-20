#include "util/docker_shell.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "build/build_environment_service.hpp"
#include "util/compile_commands_remap.hpp"
#include "util/shell_utils.hpp"

namespace tuide {

namespace {

std::vector<PathMapping> terminal_mount_mappings(const WorkspaceConfig& config) {
  std::vector<PathMapping> mappings = config.compile_commands.path_mappings;
  if (config.compile_commands.docker_detect_mounts &&
      !config.compile_commands.docker_container.empty()) {
    const auto detected =
        detect_docker_mount_mappings(config.compile_commands.docker_container);
    mappings.insert(mappings.end(), detected.begin(), detected.end());
  }
  return mappings;
}

}  // namespace

std::string host_path_to_container_path(const std::string& host_path,
                                        const std::vector<PathMapping>& mount_mappings) {
  if (host_path.empty()) {
    return {};
  }

  std::vector<PathMapping> sorted = mount_mappings;
  std::sort(sorted.begin(), sorted.end(), [](const PathMapping& a, const PathMapping& b) {
    return a.to.size() > b.to.size();
  });

  std::error_code ec;
  const std::filesystem::path canonical_host =
      std::filesystem::weakly_canonical(std::filesystem::path(host_path), ec);
  const std::string host = ec ? host_path : canonical_host.string();

  for (const auto& mapping : sorted) {
    if (mapping.to.empty() || mapping.from.empty()) {
      continue;
    }
    if (host.size() < mapping.to.size()) {
      continue;
    }
    if (host.compare(0, mapping.to.size(), mapping.to) != 0) {
      continue;
    }
    return mapping.from + host.substr(mapping.to.size());
  }
  return {};
}

ShellLaunchConfig resolve_shell_launch_config(const std::string& workspace_root,
                                              const WorkspaceConfig& config) {
  ShellLaunchConfig launch;
  launch.host_cwd = workspace_root;

  const BuildEnvironment& active =
      global_build_environment_service().active_environment();
  if (!active.docker_container.empty()) {
    launch.docker_container = active.docker_container;
  } else {
    launch.docker_container = config.compile_commands.docker_container;
  }
  if (!active.working_dir.empty()) {
    launch.host_cwd = active.working_dir;
  }
  launch.env_vars = active.env_vars;
  launch.setup_scripts = active.setup_scripts;
  if (launch.env_vars.find("COLORTERM") == launch.env_vars.end()) {
    launch.env_vars["COLORTERM"] = "truecolor";
  }
  if (launch.env_vars.find("TERM") == launch.env_vars.end()) {
    launch.env_vars["TERM"] = "xterm-256color";
  }

  if (launch.docker_container.empty()) {
    return launch;
  }

  const std::vector<PathMapping> mappings = terminal_mount_mappings(config);
  launch.docker_cwd = host_path_to_container_path(launch.host_cwd, mappings);
  if (launch.docker_cwd.empty()) {
    launch.docker_cwd = host_path_to_container_path(workspace_root, mappings);
  }
  // Verificar que el directorio calculado existe dentro del contenedor.
  // Si no existe (mapeo incorrecto o sin mounts), dejar vacío para que
  // docker exec use el WORKDIR de la imagen, que siempre existe.
  if (!launch.docker_cwd.empty() && !launch.docker_container.empty()) {
    const std::string check = "docker exec " + shell_quote(launch.docker_container) +
                              " test -d " + shell_quote(launch.docker_cwd) +
                              " 2>/dev/null && echo ok";
    const std::string result = run_shell_capture(check, 5);
    if (result.find("ok") == std::string::npos) {
      launch.docker_cwd.clear();
    }
  }
  return launch;
}

namespace {

std::string trim_trailing_ws(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
          value.back() == '\t')) {
    value.pop_back();
  }
  return value;
}

}  // namespace

DockerContainerRuntimeState docker_container_runtime_state(const std::string& container_name) {
  if (container_name.empty() || !command_exists("docker")) {
    return DockerContainerRuntimeState::Missing;
  }
  const std::string command = "docker inspect -f '{{.State.Running}}' " +
                              shell_quote(container_name) + " 2>/dev/null";
  const std::string output = trim_trailing_ws(run_shell_capture(command, 5));
  if (output == "true") {
    return DockerContainerRuntimeState::Running;
  }
  if (output == "false") {
    return DockerContainerRuntimeState::Stopped;
  }
  return DockerContainerRuntimeState::Missing;
}

bool start_docker_container(const std::string& container_name) {
  if (container_name.empty() || !command_exists("docker")) {
    return false;
  }
  const std::string command =
      "docker start " + shell_quote(container_name) + " >/dev/null 2>&1 && echo ok";
  const std::string output = run_shell_capture(command, 60);
  if (output.find("ok") == std::string::npos) {
    return false;
  }
  return docker_container_runtime_state(container_name) == DockerContainerRuntimeState::Running;
}

}  // namespace tuide
