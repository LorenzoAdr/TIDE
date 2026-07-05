#include "util/compile_commands_remap.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "util/compile_commands_setup.hpp"
#include "build/build_environment_service.hpp"
#include "i18n/tr.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr const char* kPrivateCompileCommandsFile = "compile_commands.json";
constexpr int kDockerCommandTimeoutSeconds = 5;
constexpr int kDockerExecTimeoutSeconds = 10;

std::mutex docker_mount_cache_mutex;
std::string docker_mount_cache_container;
std::vector<PathMapping> docker_mount_cache;
bool docker_mount_cache_valid = false;

std::string shell_quote(const std::string& value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string run_shell_capture(const std::string& command, int timeout_seconds = 0) {
  std::string wrapped = command;
  if (timeout_seconds > 0) {
    wrapped = "timeout --foreground " + std::to_string(timeout_seconds) + "s " + command;
  }
  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(wrapped.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  pclose(pipe);
  return output;
}

std::string trim_trailing_slashes(std::string path) {
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

std::string canonical_path_string(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
  return ec ? trim_trailing_slashes(path) : trim_trailing_slashes(canonical.string());
}

void sort_mappings_longest_first(std::vector<PathMapping>* mappings) {
  if (mappings == nullptr) {
    return;
  }
  std::sort(mappings->begin(), mappings->end(),
            [](const PathMapping& a, const PathMapping& b) {
              return a.from.size() > b.from.size();
            });
}

std::vector<PathMapping> dedupe_mappings(std::vector<PathMapping> mappings) {
  sort_mappings_longest_first(&mappings);
  std::vector<PathMapping> unique;
  for (const auto& mapping : mappings) {
    if (mapping.from.empty() || mapping.to.empty()) {
      continue;
    }
    const auto duplicate = std::find_if(
        unique.begin(), unique.end(),
        [&](const PathMapping& existing) { return existing.from == mapping.from; });
    if (duplicate == unique.end()) {
      unique.push_back(mapping);
    }
  }
  return unique;
}

std::string apply_path_mappings(const std::string& value,
                                const std::vector<PathMapping>& mappings) {
  std::string result = value;
  for (const auto& mapping : mappings) {
    if (mapping.from.empty()) {
      continue;
    }
    std::size_t pos = 0;
    while ((pos = result.find(mapping.from, pos)) != std::string::npos) {
      result.replace(pos, mapping.from.size(), mapping.to);
      pos += mapping.to.size();
    }
  }
  return result;
}

bool read_text_file(const fs::path& path, std::string* out) {
  if (out == nullptr) {
    return false;
  }
  std::ifstream input(path);
  if (!input) {
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  *out = buffer.str();
  return !out->empty();
}

bool write_private_compile_commands(const fs::path& private_dir, const std::string& json_text) {
  std::error_code ec;
  fs::create_directories(private_dir, ec);
  std::ofstream output(private_dir / kPrivateCompileCommandsFile);
  if (!output) {
    return false;
  }
  output << json_text;
  if (!json_text.empty() && json_text.back() != '\n') {
    output << '\n';
  }
  return static_cast<bool>(output);
}

bool remap_compile_commands_text(const std::string& input_json,
                                 const std::vector<PathMapping>& mappings,
                                 std::string* output_json) {
  if (output_json == nullptr) {
    return false;
  }
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(input_json);
  } catch (...) {
    return false;
  }
  if (!doc.is_array()) {
    return false;
  }

  const std::vector<PathMapping> sorted = dedupe_mappings(mappings);
  for (auto& entry : doc) {
    if (!entry.is_object()) {
      continue;
    }
    if (entry.contains("file") && entry["file"].is_string()) {
      entry["file"] = apply_path_mappings(entry["file"].get<std::string>(), sorted);
    }
    if (entry.contains("directory") && entry["directory"].is_string()) {
      entry["directory"] = apply_path_mappings(entry["directory"].get<std::string>(), sorted);
    }
    if (entry.contains("command") && entry["command"].is_string()) {
      entry["command"] = apply_path_mappings(entry["command"].get<std::string>(), sorted);
    }
    if (entry.contains("arguments") && entry["arguments"].is_array()) {
      for (auto& arg : entry["arguments"]) {
        if (arg.is_string()) {
          arg = apply_path_mappings(arg.get<std::string>(), sorted);
        }
      }
    }
    if (entry.contains("output") && entry["output"].is_string()) {
      entry["output"] = apply_path_mappings(entry["output"].get<std::string>(), sorted);
    }
  }

  *output_json = doc.dump(2);
  return true;
}

std::vector<PathMapping> normalized_mappings(std::vector<PathMapping> mappings) {
  for (auto& mapping : mappings) {
    mapping.from = canonical_path_string(mapping.from);
    mapping.to = canonical_path_string(mapping.to);
  }
  return dedupe_mappings(std::move(mappings));
}

std::vector<PathMapping> collect_effective_mappings(const WorkspaceConfig& config) {
  std::vector<PathMapping> mappings = config.compile_commands.path_mappings;
  if (config.compile_commands.docker_detect_mounts &&
      !config.compile_commands.docker_container.empty()) {
    const auto detected =
        detect_docker_mount_mappings(config.compile_commands.docker_container);
    mappings.insert(mappings.end(), detected.begin(), detected.end());
  }
  return normalized_mappings(std::move(mappings));
}

std::optional<std::string> read_compile_commands_from_docker(
    const std::string& container, const std::string& path_in_container) {
  if (container.empty() || path_in_container.empty()) {
    return std::nullopt;
  }
  const std::string command = "docker exec " + shell_quote(container) + " cat " +
                              shell_quote(path_in_container) + " 2>/dev/null";
  const std::string output = run_shell_capture(command, kDockerExecTimeoutSeconds);
  if (output.empty()) {
    return std::nullopt;
  }
  try {
    const auto doc = nlohmann::json::parse(output);
    if (!doc.is_array()) {
      return std::nullopt;
    }
  } catch (...) {
    return std::nullopt;
  }
  return output;
}

std::vector<std::string> default_docker_compile_commands_paths(
    const std::string& workspace_root) {
  return {
      "/workspace/build/compile_commands.json",
      "/workspace/compile_commands.json",
      "/project/build/compile_commands.json",
      "/src/build/compile_commands.json",
  };
}

std::optional<std::string> fetch_docker_compile_commands_json(const WorkspaceConfig& config,
                                                            const std::string& workspace_root) {
  if (config.compile_commands.docker_container.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> candidates;
  if (!config.compile_commands.docker_compile_commands_path.empty()) {
    candidates.push_back(config.compile_commands.docker_compile_commands_path);
  } else {
    candidates = default_docker_compile_commands_paths(workspace_root);
  }

  for (const auto& candidate : candidates) {
    if (auto json = read_compile_commands_from_docker(config.compile_commands.docker_container,
                                                    candidate)) {
      return json;
    }
  }
  return std::nullopt;
}

bool write_remapped_private_database(const fs::path& workspace_root,
                                     const std::string& source_json,
                                     const std::vector<PathMapping>& mappings,
                                     CompileCommandsSetupResult* result) {
  if (result == nullptr) {
    return false;
  }
  const fs::path private_dir = WorkspaceConfig::private_dir(workspace_root);
  std::string remapped_json;
  if (!remap_compile_commands_text(source_json, mappings, &remapped_json)) {
    return false;
  }
  if (!write_private_compile_commands(private_dir, remapped_json)) {
    return false;
  }
  result->compile_dir = private_dir.string();
  result->status_note = i18n::tr("compile_commands.remap.ok");
  return true;
}

CompileCommandsSetupResult try_remapped_compile_commands(const std::string& workspace_root,
                                                         const WorkspaceConfig& config) {
  CompileCommandsSetupResult result;
  const std::vector<PathMapping> mappings = collect_effective_mappings(config);
  if (mappings.empty() &&
      config.compile_commands.mode != CompileCommandsMode::kDockerSync &&
      config.compile_commands.mode != CompileCommandsMode::kRemap) {
    return result;
  }

  std::optional<std::string> source_json;
  if (config.compile_commands.mode == CompileCommandsMode::kDockerSync ||
      (!config.compile_commands.docker_container.empty() &&
       (config.compile_commands.mode == CompileCommandsMode::kAuto ||
        config.compile_commands.mode == CompileCommandsMode::kRemap))) {
    source_json = fetch_docker_compile_commands_json(config, workspace_root);
  }

  if (!source_json.has_value()) {
    const fs::path source_path =
        fs::path(workspace_root) / config.compile_commands.source_path;
    std::string host_json;
    if (read_text_file(source_path, &host_json)) {
      source_json = std::move(host_json);
    }
  }

  if (!source_json.has_value()) {
    if (config.compile_commands.mode == CompileCommandsMode::kDockerSync ||
        config.compile_commands.mode == CompileCommandsMode::kRemap) {
      result.status_note = i18n::tr("compile_commands.remap.no_source");
    }
    return result;
  }

  if (write_remapped_private_database(fs::path(workspace_root), *source_json, mappings,
                                      &result)) {
    return result;
  }
  result.status_note = i18n::tr("compile_commands.remap.failed");
  return result;
}

}  // namespace

void invalidate_docker_mount_cache() {
  std::lock_guard<std::mutex> lock(docker_mount_cache_mutex);
  docker_mount_cache_valid = false;
  docker_mount_cache_container.clear();
  docker_mount_cache.clear();
}

std::vector<std::string> list_running_docker_containers() {
  const std::string output = run_shell_capture("docker ps --format '{{.Names}}' 2>/dev/null",
                                               kDockerCommandTimeoutSeconds);
  std::vector<std::string> names;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      names.push_back(line);
    }
  }
  return names;
}

std::vector<PathMapping> detect_docker_mount_mappings(const std::string& container_name,
                                                     const bool force_refresh) {
  if (container_name.empty()) {
    return {};
  }

  if (!force_refresh) {
    std::lock_guard<std::mutex> lock(docker_mount_cache_mutex);
    if (docker_mount_cache_valid && docker_mount_cache_container == container_name) {
      return docker_mount_cache;
    }
  }

  std::vector<PathMapping> mappings;
  const std::string command = "docker inspect -f '{{json .Mounts}}' " +
                              shell_quote(container_name) + " 2>/dev/null";
  const std::string output = run_shell_capture(command, kDockerCommandTimeoutSeconds);
  if (!output.empty()) {
    try {
      const nlohmann::json mounts = nlohmann::json::parse(output);
      if (mounts.is_array()) {
        for (const auto& mount : mounts) {
          if (!mount.is_object() || !mount.contains("Type") || !mount["Type"].is_string()) {
            continue;
          }
          if (mount["Type"].get<std::string>() != "bind") {
            continue;
          }
          if (!mount.contains("Source") || !mount["Source"].is_string() ||
              !mount.contains("Destination") || !mount["Destination"].is_string()) {
            continue;
          }
          PathMapping mapping;
          mapping.from = canonical_path_string(mount["Destination"].get<std::string>());
          mapping.to = canonical_path_string(mount["Source"].get<std::string>());
          if (!mapping.from.empty() && !mapping.to.empty()) {
            mappings.push_back(std::move(mapping));
          }
        }
      }
    } catch (...) {
      mappings.clear();
    }
  }

  mappings = normalized_mappings(std::move(mappings));
  {
    std::lock_guard<std::mutex> lock(docker_mount_cache_mutex);
    docker_mount_cache_container = container_name;
    docker_mount_cache = mappings;
    docker_mount_cache_valid = true;
  }
  return mappings;
}

std::string container_path_for_host_path(const std::string& host_path,
                                         const std::vector<PathMapping>& mount_mappings) {
  const std::string canonical_host = canonical_path_string(host_path);
  const std::vector<PathMapping> sorted = dedupe_mappings(mount_mappings);
  for (const auto& mapping : sorted) {
    if (canonical_host.size() < mapping.to.size()) {
      continue;
    }
    if (canonical_host.compare(0, mapping.to.size(), mapping.to) != 0) {
      continue;
    }
    std::string suffix = canonical_host.substr(mapping.to.size());
    return mapping.from + suffix;
  }
  return canonical_host;
}

CompileCommandsSetupResult ensure_compile_commands_for_clangd(
    const std::string& workspace_root, const WorkspaceConfig& config) {
  CompileCommandsSetupResult result;
  if (workspace_root.empty()) {
    return result;
  }

  if (config.compile_commands.mode != CompileCommandsMode::kHost) {
    const bool should_try_remapped =
        config.compile_commands.mode == CompileCommandsMode::kRemap ||
        config.compile_commands.mode == CompileCommandsMode::kDockerSync ||
        (config.compile_commands.mode == CompileCommandsMode::kAuto &&
         (!config.compile_commands.docker_container.empty() ||
          !config.compile_commands.path_mappings.empty()));

    if (should_try_remapped) {
      result = try_remapped_compile_commands(workspace_root, config);
      if (!result.compile_dir.empty()) {
        return result;
      }
      if (config.compile_commands.mode == CompileCommandsMode::kRemap ||
          config.compile_commands.mode == CompileCommandsMode::kDockerSync) {
        return result;
      }
    }
  }

  const auto build_env_result =
      global_build_environment_service().resolve_compile_commands(workspace_root, config);
  if (!build_env_result.compile_dir.empty()) {
    return build_env_result;
  }

  result.compile_dir = ensure_host_compile_commands_dir(workspace_root);
  return result;
}

}  // namespace tgdb
