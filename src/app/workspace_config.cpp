#include "app/workspace_config.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr const char* kConfigDir = ".tgdb";
constexpr const char* kConfigFile = "config.json";
constexpr const char* kLegacyConfigFile = ".tgdb.json";

void parse_path_mappings(const nlohmann::json& doc, std::vector<PathMapping>* out) {
  if (out == nullptr || !doc.is_array()) {
    return;
  }
  for (const auto& entry : doc) {
    if (!entry.is_object()) {
      continue;
    }
    PathMapping mapping;
    if (entry.contains("from") && entry["from"].is_string()) {
      mapping.from = entry["from"].get<std::string>();
    }
    if (entry.contains("to") && entry["to"].is_string()) {
      mapping.to = entry["to"].get<std::string>();
    }
    if (!mapping.from.empty() && !mapping.to.empty()) {
      out->push_back(std::move(mapping));
    }
  }
}

void parse_compile_commands_settings(const nlohmann::json& doc,
                                     CompileCommandsSettings* settings) {
  if (settings == nullptr || !doc.is_object()) {
    return;
  }
  if (doc.contains("mode") && doc["mode"].is_string()) {
    settings->mode = parse_compile_commands_mode(doc["mode"].get<std::string>());
  }
  if (doc.contains("source_path") && doc["source_path"].is_string()) {
    settings->source_path = doc["source_path"].get<std::string>();
  }
  if (doc.contains("docker_container") && doc["docker_container"].is_string()) {
    settings->docker_container = doc["docker_container"].get<std::string>();
  }
  if (doc.contains("docker_compile_commands_path") &&
      doc["docker_compile_commands_path"].is_string()) {
    settings->docker_compile_commands_path = doc["docker_compile_commands_path"].get<std::string>();
  }
  if (doc.contains("docker_detect_mounts") && doc["docker_detect_mounts"].is_boolean()) {
    settings->docker_detect_mounts = doc["docker_detect_mounts"].get<bool>();
  }
  if (doc.contains("path_mappings")) {
    parse_path_mappings(doc["path_mappings"], &settings->path_mappings);
  }
}

void migrate_legacy_config(const fs::path& workspace_root) {
  const fs::path legacy = workspace_root / kLegacyConfigFile;
  const fs::path config_dir = workspace_root / kConfigDir;
  const fs::path modern = config_dir / kConfigFile;
  std::error_code ec;
  if (fs::exists(modern, ec)) {
    return;
  }
  if (!fs::exists(legacy, ec)) {
    return;
  }
  fs::create_directories(config_dir, ec);
  ec.clear();
  fs::copy_file(legacy, modern, fs::copy_options::overwrite_existing, ec);
  if (!ec) {
    fs::remove(legacy, ec);
  }
}

}  // namespace

std::string compile_commands_mode_name(const CompileCommandsMode mode) {
  switch (mode) {
    case CompileCommandsMode::kAuto:
      return "auto";
    case CompileCommandsMode::kHost:
      return "host";
    case CompileCommandsMode::kRemap:
      return "remap";
    case CompileCommandsMode::kDockerSync:
      return "docker_sync";
  }
  return "auto";
}

CompileCommandsMode parse_compile_commands_mode(const std::string& value) {
  if (value == "host") {
    return CompileCommandsMode::kHost;
  }
  if (value == "remap") {
    return CompileCommandsMode::kRemap;
  }
  if (value == "docker_sync" || value == "docker") {
    return CompileCommandsMode::kDockerSync;
  }
  return CompileCommandsMode::kAuto;
}

std::string WorkspaceConfig::config_dir(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / kConfigDir).string();
}

std::string WorkspaceConfig::private_dir(const std::string& workspace_root) {
  return config_dir(workspace_root);
}

std::string WorkspaceConfig::config_path(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / kConfigDir / kConfigFile).string();
}

std::string WorkspaceConfig::private_compile_commands_path(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / kConfigDir / "compile_commands.json").string();
}

WorkspaceConfig WorkspaceConfig::load(const std::string& workspace_root) {
  WorkspaceConfig config;
  if (workspace_root.empty()) {
    return config;
  }

  migrate_legacy_config(fs::path(workspace_root));
  const std::string path = config_path(workspace_root);
  std::ifstream input(path);
  if (!input) {
    config.save(workspace_root);
    return config;
  }

  try {
    nlohmann::json doc;
    input >> doc;
    if (doc.contains("clangd_extra_include_paths") &&
        doc["clangd_extra_include_paths"].is_array()) {
      for (const auto& entry : doc["clangd_extra_include_paths"]) {
        if (entry.is_string()) {
          const std::string value = entry.get<std::string>();
          if (!value.empty()) {
            config.clangd_extra_include_paths.push_back(value);
          }
        }
      }
    }
    if (doc.contains("clangd_use_gcc_query_driver") &&
        doc["clangd_use_gcc_query_driver"].is_boolean()) {
      config.clangd_use_gcc_query_driver = doc["clangd_use_gcc_query_driver"].get<bool>();
    }
    if (doc.contains("clangd_background_index") && doc["clangd_background_index"].is_boolean()) {
      config.clangd_background_index = doc["clangd_background_index"].get<bool>();
    }
    if (doc.contains("theme") && doc["theme"].is_string()) {
      config.theme = theme::parse_theme_name(doc["theme"].get<std::string>());
    }
    if (doc.contains("compile_commands")) {
      parse_compile_commands_settings(doc["compile_commands"], &config.compile_commands);
    }
  } catch (...) {
    return WorkspaceConfig{};
  }
  return config;
}

bool WorkspaceConfig::save(const std::string& workspace_root) const {
  if (workspace_root.empty()) {
    return false;
  }

  std::error_code ec;
  fs::create_directories(fs::path(workspace_root) / kConfigDir, ec);

  nlohmann::json mappings = nlohmann::json::array();
  for (const auto& mapping : compile_commands.path_mappings) {
    mappings.push_back({{"from", mapping.from}, {"to", mapping.to}});
  }

  nlohmann::json doc;
  doc["clangd_extra_include_paths"] = clangd_extra_include_paths;
  doc["clangd_use_gcc_query_driver"] = clangd_use_gcc_query_driver;
  doc["clangd_background_index"] = clangd_background_index;
  doc["theme"] = theme::theme_name(theme);
  doc["compile_commands"] = {
      {"mode", compile_commands_mode_name(compile_commands.mode)},
      {"source_path", compile_commands.source_path},
      {"docker_container", compile_commands.docker_container},
      {"docker_compile_commands_path", compile_commands.docker_compile_commands_path},
      {"docker_detect_mounts", compile_commands.docker_detect_mounts},
      {"path_mappings", std::move(mappings)},
  };

  std::ofstream output(config_path(workspace_root));
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

}  // namespace tgdb
