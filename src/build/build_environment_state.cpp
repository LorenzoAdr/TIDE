#include "build/build_environment_state.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr const char* kStateFile = "state.json";

nlohmann::json env_vars_to_json(const std::map<std::string, std::string>& env_vars) {
  nlohmann::json out = nlohmann::json::object();
  for (const auto& entry : env_vars) {
    out[entry.first] = entry.second;
  }
  return out;
}

void parse_env_vars(const nlohmann::json& doc, std::map<std::string, std::string>* out) {
  if (out == nullptr || !doc.is_object()) {
    return;
  }
  for (const auto& entry : doc.items()) {
    if (entry.value().is_string()) {
      (*out)[entry.key()] = entry.value().get<std::string>();
    }
  }
}

BuildSystemKind parse_build_system_kind(const std::string& value) {
  if (value == "cmake") {
    return BuildSystemKind::kCMake;
  }
  if (value == "makefile") {
    return BuildSystemKind::kMakefile;
  }
  if (value == "hybrid") {
    return BuildSystemKind::kHybrid;
  }
  return BuildSystemKind::kUnknown;
}

nlohmann::json environment_to_json(const BuildEnvironment& env) {
  return {
      {"id", env.id},
      {"label", env.label},
      {"system", build_system_kind_name(env.system)},
      {"working_dir", env.working_dir},
      {"make_command", env.make_command},
      {"setup_scripts", env.setup_scripts},
      {"env_vars", env_vars_to_json(env.env_vars)},
      {"docker_container", env.docker_container},
      {"marker_paths", env.marker_paths},
      {"fallback_compile_flags", env.fallback_compile_flags},
  };
}

BuildEnvironment environment_from_json(const nlohmann::json& doc) {
  BuildEnvironment env;
  if (!doc.is_object()) {
    return env;
  }
  if (doc.contains("id") && doc["id"].is_string()) {
    env.id = doc["id"].get<std::string>();
  }
  if (doc.contains("label") && doc["label"].is_string()) {
    env.label = doc["label"].get<std::string>();
  }
  if (doc.contains("system") && doc["system"].is_string()) {
    env.system = parse_build_system_kind(doc["system"].get<std::string>());
  }
  if (doc.contains("working_dir") && doc["working_dir"].is_string()) {
    env.working_dir = doc["working_dir"].get<std::string>();
  }
  if (doc.contains("make_command") && doc["make_command"].is_string()) {
    env.make_command = doc["make_command"].get<std::string>();
  }
  if (doc.contains("setup_scripts") && doc["setup_scripts"].is_array()) {
    for (const auto& entry : doc["setup_scripts"]) {
      if (entry.is_string()) {
        env.setup_scripts.push_back(entry.get<std::string>());
      }
    }
  }
  if (doc.contains("env_vars")) {
    parse_env_vars(doc["env_vars"], &env.env_vars);
  }
  if (doc.contains("docker_container") && doc["docker_container"].is_string()) {
    env.docker_container = doc["docker_container"].get<std::string>();
  }
  if (doc.contains("marker_paths") && doc["marker_paths"].is_array()) {
    for (const auto& entry : doc["marker_paths"]) {
      if (entry.is_string()) {
        env.marker_paths.push_back(entry.get<std::string>());
      }
    }
  }
  if (doc.contains("fallback_compile_flags") && doc["fallback_compile_flags"].is_array()) {
    for (const auto& entry : doc["fallback_compile_flags"]) {
      if (entry.is_string()) {
        env.fallback_compile_flags.push_back(entry.get<std::string>());
      }
    }
  }
  return env;
}

}  // namespace

std::string BuildEnvironmentStateStore::state_path(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / ".tuide" / kStateFile).string();
}

BuildEnvironmentState BuildEnvironmentStateStore::load(const std::string& workspace_root) {
  BuildEnvironmentState state;
  if (workspace_root.empty()) {
    return state;
  }

  std::ifstream input(state_path(workspace_root));
  if (!input) {
    return state;
  }

  try {
    nlohmann::json doc;
    input >> doc;
    if (doc.contains("last_active_environment_id") &&
        doc["last_active_environment_id"].is_string()) {
      state.last_active_environment_id = doc["last_active_environment_id"].get<std::string>();
    }
    if (doc.contains("discovered_environments") && doc["discovered_environments"].is_array()) {
      for (const auto& entry : doc["discovered_environments"]) {
        state.discovered_environments.push_back(environment_from_json(entry));
      }
    }
    if (doc.contains("make_qp_cache_mtime") && doc["make_qp_cache_mtime"].is_number_integer()) {
      state.make_qp_cache_mtime = doc["make_qp_cache_mtime"].get<std::int64_t>();
    }
    if (doc.contains("make_qp_cache_text") && doc["make_qp_cache_text"].is_string()) {
      state.make_qp_cache_text = doc["make_qp_cache_text"].get<std::string>();
    }
  } catch (...) {
    return BuildEnvironmentState{};
  }
  return state;
}

bool BuildEnvironmentStateStore::save(const std::string& workspace_root,
                                      const BuildEnvironmentState& state) {
  if (workspace_root.empty()) {
    return false;
  }

  std::error_code ec;
  fs::create_directories(fs::path(workspace_root) / ".tuide", ec);

  nlohmann::json discovered = nlohmann::json::array();
  for (const auto& env : state.discovered_environments) {
    discovered.push_back(environment_to_json(env));
  }

  nlohmann::json doc = {
      {"last_active_environment_id", state.last_active_environment_id},
      {"discovered_environments", std::move(discovered)},
      {"make_qp_cache_mtime", state.make_qp_cache_mtime},
      {"make_qp_cache_text", state.make_qp_cache_text},
  };

  std::ofstream output(state_path(workspace_root));
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

}  // namespace tuide
