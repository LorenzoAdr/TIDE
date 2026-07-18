#include "build/docker_environment_detector.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "build/build_environment.hpp"
#include "util/compile_commands_remap.hpp"
#include "util/shell_utils.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

bool path_contains_workspace(const std::string& host_path, const std::string& workspace_root) {
  if (host_path.empty() || workspace_root.empty()) {
    return false;
  }
  std::error_code ec;
  const fs::path canonical_host = fs::weakly_canonical(fs::path(host_path), ec);
  const fs::path canonical_root = fs::weakly_canonical(fs::path(workspace_root), ec);
  const std::string host = ec ? host_path : canonical_host.string();
  const std::string root = ec ? workspace_root : canonical_root.string();
  if (host.size() < root.size()) {
    return false;
  }
  return host.compare(0, root.size(), root) == 0 &&
         (host.size() == root.size() || host[root.size()] == '/');
}

std::string read_text_file(const fs::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::vector<std::string> containers_from_devcontainer(const std::string& workspace_root) {
  std::vector<std::string> names;
  const fs::path path = fs::path(workspace_root) / ".devcontainer" / "devcontainer.json";
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    return names;
  }
  try {
    const auto doc = nlohmann::json::parse(read_text_file(path));
    if (doc.contains("name") && doc["name"].is_string()) {
      names.push_back(doc["name"].get<std::string>());
    }
    if (doc.contains("runArgs") && doc["runArgs"].is_array()) {
      for (const auto& arg : doc["runArgs"]) {
        if (!arg.is_string()) {
          continue;
        }
        const std::string value = arg.get<std::string>();
        if (value.rfind("--name=", 0) == 0) {
          names.push_back(value.substr(7));
        }
      }
    }
  } catch (...) {
  }
  return names;
}

std::vector<std::string> containers_from_compose(const std::string& workspace_root) {
  std::vector<std::string> names;
  const fs::path path = fs::path(workspace_root) / "docker-compose.yml";
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    return names;
  }
  const std::string text = read_text_file(path);
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    const auto pos = line.find("container_name:");
    if (pos == std::string::npos) {
      continue;
    }
    std::string name = line.substr(pos + 15);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) {
      name.erase(name.begin());
    }
    if (!name.empty() && name.back() == '\r') {
      name.pop_back();
    }
    if (!name.empty()) {
      names.push_back(name);
    }
  }
  return names;
}

BuildEnvironment make_docker_environment(const std::string& workspace_root,
                                         const std::string& container) {
  BuildEnvironment env;
  env.system = BuildSystemKind::kMakefile;
  env.working_dir = workspace_root;
  env.docker_container = container;
  env.make_command = "make";
  env.label = "docker:" + container;
  const auto mappings = detect_docker_mount_mappings(container);
  env.marker_paths.push_back(workspace_root);
  for (const auto& mapping : mappings) {
    if (path_contains_workspace(mapping.to, workspace_root)) {
      env.marker_paths.push_back(mapping.to);
    }
  }
  env.id = build_environment_id(env);
  return env;
}

}  // namespace

std::vector<BuildEnvironment> discover_docker_environments(const std::string& workspace_root) {
  std::vector<BuildEnvironment> environments;
  if (workspace_root.empty() || !command_exists("docker")) {
    return environments;
  }

  std::vector<std::string> candidates;
  for (const auto& name : containers_from_devcontainer(workspace_root)) {
    candidates.push_back(name);
  }
  for (const auto& name : containers_from_compose(workspace_root)) {
    candidates.push_back(name);
  }
  for (const auto& name : list_running_docker_containers()) {
    candidates.push_back(name);
  }

  std::vector<std::string> seen;
  for (const auto& container : candidates) {
    if (container.empty()) {
      continue;
    }
    if (std::find(seen.begin(), seen.end(), container) != seen.end()) {
      continue;
    }
    const auto mappings = detect_docker_mount_mappings(container);
    bool mounted = mappings.empty();
    for (const auto& mapping : mappings) {
      if (path_contains_workspace(mapping.to, workspace_root)) {
        mounted = true;
        break;
      }
    }
    if (!mounted) {
      continue;
    }
    seen.push_back(container);
    environments.push_back(make_docker_environment(workspace_root, container));
  }
  return environments;
}

}  // namespace tuide
