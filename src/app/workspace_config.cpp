#include "app/workspace_config.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr const char* kConfigDir = ".tuide";
constexpr const char* kLegacyConfigDir = ".tgdb";
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

void parse_ai_settings(const nlohmann::json& doc, AiSettings* settings) {
  if (settings == nullptr || !doc.is_object()) {
    return;
  }
  if (doc.contains("enabled") && doc["enabled"].is_boolean()) {
    settings->enabled = doc["enabled"].get<bool>();
  }
  if (doc.contains("command_whitelist") && doc["command_whitelist"].is_array()) {
    settings->command_whitelist.clear();
    for (const auto& entry : doc["command_whitelist"]) {
      if (entry.is_string()) {
        const std::string value = entry.get<std::string>();
        if (!value.empty()) {
          settings->command_whitelist.push_back(value);
        }
      }
    }
  }
  if (doc.contains("tasks") && doc["tasks"].is_object()) {
    settings->tasks.clear();
    for (const auto& entry : doc["tasks"].items()) {
      if (entry.value().is_string()) {
        settings->tasks.emplace_back(entry.key(), entry.value().get<std::string>());
      } else if (entry.value().is_object() && entry.value().contains("command") &&
                 entry.value()["command"].is_string()) {
        settings->tasks.emplace_back(entry.key(), entry.value()["command"].get<std::string>());
      }
    }
  }
  if (doc.contains("level2") && doc["level2"].is_object()) {
    const auto& level2 = doc["level2"];
    if (level2.contains("mode") && level2["mode"].is_string()) {
      settings->level2_mode = level2["mode"].get<std::string>();
    }
    if (level2.contains("workflow") && level2["workflow"].is_string()) {
      settings->level2_workflow =
          ai_workflow_kind_name(parse_ai_workflow_kind(level2["workflow"].get<std::string>()));
    }
    if (level2.contains("git_log_n") && level2["git_log_n"].is_number_integer()) {
      int n = level2["git_log_n"].get<int>();
      if (n < 1) {
        n = 1;
      }
      if (n > 50) {
        n = 50;
      }
      settings->level2_git_log_n = n;
    }
    auto& l2 = settings->level2;
    if (level2.contains("model_id") && level2["model_id"].is_string()) {
      l2.model_id = level2["model_id"].get<std::string>();
    }
    if (level2.contains("model_path") && level2["model_path"].is_string()) {
      l2.model_path = level2["model_path"].get<std::string>();
    }
    if (level2.contains("cli_path") && level2["cli_path"].is_string()) {
      l2.cli_path = level2["cli_path"].get<std::string>();
    }
    if (level2.contains("api_base") && level2["api_base"].is_string()) {
      l2.api_base = level2["api_base"].get<std::string>();
    }
    if (level2.contains("api_key") && level2["api_key"].is_string()) {
      l2.api_key = level2["api_key"].get<std::string>();
    }
    if (level2.contains("api_model") && level2["api_model"].is_string()) {
      l2.api_model = level2["api_model"].get<std::string>();
    }
    if (level2.contains("max_steps") && level2["max_steps"].is_number_integer()) {
      l2.max_steps = level2["max_steps"].get<int>();
    }
    if (level2.contains("max_tokens") && level2["max_tokens"].is_number_integer()) {
      l2.max_tokens = level2["max_tokens"].get<int>();
    }
    if (level2.contains("n_ctx") && level2["n_ctx"].is_number_integer()) {
      l2.n_ctx = level2["n_ctx"].get<int>();
    }
    if (level2.contains("n_ctx_remote") && level2["n_ctx_remote"].is_number_integer()) {
      l2.n_ctx_remote = level2["n_ctx_remote"].get<int>();
    }
    if (level2.contains("temperature") && level2["temperature"].is_number()) {
      l2.temperature = level2["temperature"].get<float>();
    }
    if (level2.contains("auto_download") && level2["auto_download"].is_boolean()) {
      l2.auto_download = level2["auto_download"].get<bool>();
    }
    if (level2.contains("clarify_pushback_max") && level2["clarify_pushback_max"].is_number_integer()) {
      l2.clarify_pushback_max = level2["clarify_pushback_max"].get<int>();
    }
    if (level2.contains("server_port") && level2["server_port"].is_number_integer()) {
      l2.server_port = level2["server_port"].get<int>();
    }
    if (level2.contains("n_gpu_layers") && level2["n_gpu_layers"].is_number_integer()) {
      l2.n_gpu_layers = level2["n_gpu_layers"].get<int>();
    }
    if (level2.contains("n_threads") && level2["n_threads"].is_number_integer()) {
      l2.n_threads = level2["n_threads"].get<int>();
    }
  } else if (doc.contains("level2_mode") && doc["level2_mode"].is_string()) {
    settings->level2_mode = doc["level2_mode"].get<std::string>();
  }
  if (doc.contains("level2_workflow") && doc["level2_workflow"].is_string()) {
    settings->level2_workflow =
        ai_workflow_kind_name(parse_ai_workflow_kind(doc["level2_workflow"].get<std::string>()));
  }
  if (doc.contains("path_scope") && doc["path_scope"].is_array()) {
    settings->path_scope.clear();
    for (const auto& entry : doc["path_scope"]) {
      if (entry.is_string()) {
        const std::string value = entry.get<std::string>();
        if (!value.empty()) {
          settings->path_scope.push_back(value);
        }
      }
    }
  }
  if (doc.contains("models") && doc["models"].is_object() &&
      doc["models"].contains("cache_dir") && doc["models"]["cache_dir"].is_string()) {
    settings->models_cache_dir = doc["models"]["cache_dir"].get<std::string>();
  }
  if (doc.contains("trace") && doc["trace"].is_object()) {
    const auto& tr = doc["trace"];
    if (tr.contains("enabled") && tr["enabled"].is_boolean()) {
      settings->trace_enabled = tr["enabled"].get<bool>();
    }
    if (tr.contains("path") && tr["path"].is_string()) {
      settings->trace_path = tr["path"].get<std::string>();
    }
  } else if (doc.contains("trace_enabled") && doc["trace_enabled"].is_boolean()) {
    settings->trace_enabled = doc["trace_enabled"].get<bool>();
  }
  if (doc.contains("llama_vulkan_bundle") && doc["llama_vulkan_bundle"].is_boolean()) {
    settings->llama_vulkan_bundle = doc["llama_vulkan_bundle"].get<bool>();
  }
  if (doc.contains("level1") && doc["level1"].is_object()) {
    const auto& l1 = doc["level1"];
    if (l1.contains("model_id") && l1["model_id"].is_string()) {
      settings->level1.model_id = l1["model_id"].get<std::string>();
    }
    if (l1.contains("model_path") && l1["model_path"].is_string()) {
      settings->level1.model_path = l1["model_path"].get<std::string>();
    }
    if (l1.contains("cli_path") && l1["cli_path"].is_string()) {
      settings->level1.cli_path = l1["cli_path"].get<std::string>();
    }
    if (l1.contains("max_steps") && l1["max_steps"].is_number_integer()) {
      settings->level1.max_steps = l1["max_steps"].get<int>();
    }
    if (l1.contains("max_tokens") && l1["max_tokens"].is_number_integer()) {
      settings->level1.max_tokens = l1["max_tokens"].get<int>();
    }
    if (l1.contains("n_ctx") && l1["n_ctx"].is_number_integer()) {
      settings->level1.n_ctx = l1["n_ctx"].get<int>();
    }
    if (l1.contains("temperature") && l1["temperature"].is_number()) {
      settings->level1.temperature = l1["temperature"].get<float>();
    }
    if (l1.contains("auto_download") && l1["auto_download"].is_boolean()) {
      settings->level1.auto_download = l1["auto_download"].get<bool>();
    }
  }
  if (doc.contains("level0") && doc["level0"].is_object()) {
    const auto& l0 = doc["level0"];
    if (l0.contains("min_score") && l0["min_score"].is_number()) {
      settings->level0.min_score = l0["min_score"].get<float>();
    }
    if (l0.contains("min_margin") && l0["min_margin"].is_number()) {
      settings->level0.min_margin = l0["min_margin"].get<float>();
    }
    if (l0.contains("embeddings") && l0["embeddings"].is_object()) {
      const auto& emb = l0["embeddings"];
      if (emb.contains("model_id") && emb["model_id"].is_string()) {
        settings->level0.embeddings.model_id = emb["model_id"].get<std::string>();
      }
      if (emb.contains("model_path") && emb["model_path"].is_string()) {
        settings->level0.embeddings.model_path = emb["model_path"].get<std::string>();
      }
      if (emb.contains("auto_download") && emb["auto_download"].is_boolean()) {
        settings->level0.embeddings.auto_download = emb["auto_download"].get<bool>();
      }
      if (emb.contains("server_port") && emb["server_port"].is_number_integer()) {
        settings->level0.embeddings.server_port = emb["server_port"].get<int>();
      }
      if (emb.contains("n_ctx") && emb["n_ctx"].is_number_integer()) {
        settings->level0.embeddings.n_ctx = emb["n_ctx"].get<int>();
      }
      if (emb.contains("n_gpu_layers") && emb["n_gpu_layers"].is_number_integer()) {
        settings->level0.embeddings.n_gpu_layers = emb["n_gpu_layers"].get<int>();
      }
      if (emb.contains("n_threads") && emb["n_threads"].is_number_integer()) {
        settings->level0.embeddings.n_threads = emb["n_threads"].get<int>();
      }
      if (emb.contains("batch_size") && emb["batch_size"].is_number_integer()) {
        settings->level0.embeddings.batch_size = emb["batch_size"].get<int>();
      }
      if (emb.contains("ubatch_size") && emb["ubatch_size"].is_number_integer()) {
        settings->level0.embeddings.ubatch_size = emb["ubatch_size"].get<int>();
      }
      if (emb.contains("n_parallel") && emb["n_parallel"].is_number_integer()) {
        settings->level0.embeddings.n_parallel = emb["n_parallel"].get<int>();
      }
      if (emb.contains("http_batch") && emb["http_batch"].is_number_integer()) {
        settings->level0.embeddings.http_batch = emb["http_batch"].get<int>();
      }
    }
  }
}

nlohmann::json serialize_ai_settings(const AiSettings& settings) {
  nlohmann::json tasks = nlohmann::json::object();
  for (const auto& [name, command] : settings.tasks) {
    tasks[name] = {{"command", command}};
  }
  return nlohmann::json{
      {"enabled", settings.enabled},
      {"command_whitelist", settings.command_whitelist},
      {"tasks", std::move(tasks)},
      {"path_scope", settings.path_scope},
      {"level2",
       {{"mode", settings.level2_mode},
        {"workflow", ai_workflow_kind_name(parse_ai_workflow_kind(settings.level2_workflow))},
        {"git_log_n", settings.level2_git_log_n},
        {"model_id", settings.level2.model_id},
        {"model_path", settings.level2.model_path},
        {"cli_path", settings.level2.cli_path},
        {"api_base", settings.level2.api_base},
        {"api_key", settings.level2.api_key},
        {"api_model", settings.level2.api_model},
        {"max_steps", settings.level2.max_steps},
        {"max_tokens", settings.level2.max_tokens},
        {"n_ctx", settings.level2.n_ctx},
        {"n_ctx_remote", settings.level2.n_ctx_remote},
        {"temperature", settings.level2.temperature},
        {"auto_download", settings.level2.auto_download},
        {"clarify_pushback_max", settings.level2.clarify_pushback_max},
        {"server_port", settings.level2.server_port},
        {"n_gpu_layers", settings.level2.n_gpu_layers},
        {"n_threads", settings.level2.n_threads}}},
      {"models", {{"cache_dir", settings.models_cache_dir}}},
      {"trace",
       {{"enabled", settings.trace_enabled}, {"path", settings.trace_path}}},
      {"llama_vulkan_bundle", settings.llama_vulkan_bundle},
      {"level0",
       {{"min_score", settings.level0.min_score},
        {"min_margin", settings.level0.min_margin},
        {"embeddings",
         {{"model_id", settings.level0.embeddings.model_id},
          {"model_path", settings.level0.embeddings.model_path},
          {"auto_download", settings.level0.embeddings.auto_download},
          {"server_port", settings.level0.embeddings.server_port},
          {"n_ctx", settings.level0.embeddings.n_ctx},
          {"n_gpu_layers", settings.level0.embeddings.n_gpu_layers},
          {"n_threads", settings.level0.embeddings.n_threads},
          {"batch_size", settings.level0.embeddings.batch_size},
          {"ubatch_size", settings.level0.embeddings.ubatch_size},
          {"n_parallel", settings.level0.embeddings.n_parallel},
          {"http_batch", settings.level0.embeddings.http_batch}}}}},
      {"level1",
       {{"model_id", settings.level1.model_id},
        {"model_path", settings.level1.model_path},
        {"cli_path", settings.level1.cli_path},
        {"max_steps", settings.level1.max_steps},
        {"max_tokens", settings.level1.max_tokens},
        {"n_ctx", settings.level1.n_ctx},
        {"temperature", settings.level1.temperature},
        {"auto_download", settings.level1.auto_download}}},
  };
}

void parse_env_vars_object(const nlohmann::json& doc, std::map<std::string, std::string>* out) {
  if (out == nullptr || !doc.is_object()) {
    return;
  }
  for (const auto& entry : doc.items()) {
    if (entry.value().is_string()) {
      (*out)[entry.key()] = entry.value().get<std::string>();
    }
  }
}

BuildEnvironmentProfile parse_build_environment_profile(const nlohmann::json& doc) {
  BuildEnvironmentProfile profile;
  if (!doc.is_object()) {
    return profile;
  }
  if (doc.contains("id") && doc["id"].is_string()) {
    profile.id = doc["id"].get<std::string>();
  }
  if (doc.contains("label") && doc["label"].is_string()) {
    profile.label = doc["label"].get<std::string>();
  }
  if (doc.contains("working_dir") && doc["working_dir"].is_string()) {
    profile.working_dir = doc["working_dir"].get<std::string>();
  }
  if (doc.contains("make_command") && doc["make_command"].is_string()) {
    profile.make_command = doc["make_command"].get<std::string>();
  }
  if (doc.contains("setup_scripts") && doc["setup_scripts"].is_array()) {
    for (const auto& entry : doc["setup_scripts"]) {
      if (entry.is_string()) {
        profile.setup_scripts.push_back(entry.get<std::string>());
      }
    }
  }
  if (doc.contains("env_vars")) {
    parse_env_vars_object(doc["env_vars"], &profile.env_vars);
  }
  if (doc.contains("docker_container") && doc["docker_container"].is_string()) {
    profile.docker_container = doc["docker_container"].get<std::string>();
  }
  if (doc.contains("marker_paths") && doc["marker_paths"].is_array()) {
    for (const auto& entry : doc["marker_paths"]) {
      if (entry.is_string()) {
        profile.marker_paths.push_back(entry.get<std::string>());
      }
    }
  }
  return profile;
}

void parse_build_environment_settings(const nlohmann::json& doc,
                                      BuildEnvironmentSettings* settings) {
  if (settings == nullptr || !doc.is_object()) {
    return;
  }
  if (doc.contains("active_environment_id") && doc["active_environment_id"].is_string()) {
    settings->active_environment_id = doc["active_environment_id"].get<std::string>();
  }
  if (doc.contains("make_intercept_tool") && doc["make_intercept_tool"].is_string()) {
    settings->make_intercept_tool =
        parse_make_intercept_tool(doc["make_intercept_tool"].get<std::string>());
  }
  if (doc.contains("make_default_target") && doc["make_default_target"].is_string()) {
    settings->make_default_target = doc["make_default_target"].get<std::string>();
  }
  if (doc.contains("profiles") && doc["profiles"].is_array()) {
    for (const auto& entry : doc["profiles"]) {
      settings->profiles.push_back(parse_build_environment_profile(entry));
    }
  }
}

void migrate_legacy_config(const fs::path& workspace_root) {
  const fs::path config_dir = workspace_root / kConfigDir;
  const fs::path modern = config_dir / kConfigFile;
  const fs::path legacy_dir = workspace_root / kLegacyConfigDir;
  const fs::path legacy_flat = workspace_root / kLegacyConfigFile;
  std::error_code ec;

  // Prefer renaming the old workspace private dir (.tgdb → .tuide) when present.
  if (!fs::exists(config_dir, ec) && fs::is_directory(legacy_dir, ec)) {
    ec.clear();
    fs::rename(legacy_dir, config_dir, ec);
  }

  if (fs::exists(modern, ec)) {
    return;
  }
  if (!fs::exists(legacy_flat, ec)) {
    return;
  }
  fs::create_directories(config_dir, ec);
  ec.clear();
  fs::copy_file(legacy_flat, modern, fs::copy_options::overwrite_existing, ec);
  if (!ec) {
    fs::remove(legacy_flat, ec);
  }
}

void parse_ui_colors_settings(const nlohmann::json& doc, theme::UiColorOverrides* overrides) {
  if (overrides == nullptr || !doc.is_object()) {
    return;
  }
  auto parse_channel = [&](const char* key) -> std::optional<theme::ColorRgb> {
    if (!doc.contains(key)) {
      return std::nullopt;
    }
    if (doc[key].is_null()) {
      return std::nullopt;
    }
    if (doc[key].is_string()) {
      theme::ColorRgb rgb;
      if (theme::parse_hex_color(doc[key].get<std::string>(), &rgb)) {
        return rgb;
      }
    }
    return std::nullopt;
  };
  overrides->panel_bg = parse_channel("panel_bg");
  overrides->code_bg = parse_channel("code_bg");
  overrides->text = parse_channel("text");
  overrides->title = parse_channel("title");
  overrides->directory = parse_channel("directory");
  overrides->file = parse_channel("file");
}

nlohmann::json serialize_ui_colors(const theme::UiColorOverrides& overrides) {
  nlohmann::json doc = nlohmann::json::object();
  auto write = [&](const char* key, const std::optional<theme::ColorRgb>& value) {
    if (value) {
      doc[key] = theme::format_hex_color(*value);
    }
  };
  write("panel_bg", overrides.panel_bg);
  write("code_bg", overrides.code_bg);
  write("text", overrides.text);
  write("title", overrides.title);
  write("directory", overrides.directory);
  write("file", overrides.file);
  return doc;
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
    if (doc.contains("ui_colors_preset") && doc["ui_colors_preset"].is_string()) {
      config.ui_colors_preset =
          theme::parse_ui_color_preset(doc["ui_colors_preset"].get<std::string>());
    }
    if (doc.contains("ui_colors")) {
      parse_ui_colors_settings(doc["ui_colors"], &config.ui_colors);
    }
    if (doc.contains("compile_commands")) {
      parse_compile_commands_settings(doc["compile_commands"], &config.compile_commands);
    }
    if (doc.contains("build_environments")) {
      parse_build_environment_settings(doc["build_environments"], &config.build_environments);
    }
    if (doc.contains("ai")) {
      parse_ai_settings(doc["ai"], &config.ai);
    }
    if (doc.contains("language_overrides") && doc["language_overrides"].is_object()) {
      for (auto it = doc["language_overrides"].begin(); it != doc["language_overrides"].end();
           ++it) {
        if (!it.key().empty() && it.value().is_string()) {
          const std::string lang = it.value().get<std::string>();
          if (!lang.empty()) {
            config.language_overrides[it.key()] = lang;
          }
        }
      }
    }
    // Legacy / hand-edited: allow llama_vulkan_bundle at workspace root.
    if (doc.contains("llama_vulkan_bundle") && doc["llama_vulkan_bundle"].is_boolean()) {
      config.ai.llama_vulkan_bundle = doc["llama_vulkan_bundle"].get<bool>();
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

  nlohmann::json profiles = nlohmann::json::array();
  for (const auto& profile : build_environments.profiles) {
    nlohmann::json env_vars = nlohmann::json::object();
    for (const auto& entry : profile.env_vars) {
      env_vars[entry.first] = entry.second;
    }
    profiles.push_back({
        {"id", profile.id},
        {"label", profile.label},
        {"working_dir", profile.working_dir},
        {"make_command", profile.make_command},
        {"setup_scripts", profile.setup_scripts},
        {"env_vars", std::move(env_vars)},
        {"docker_container", profile.docker_container},
        {"marker_paths", profile.marker_paths},
    });
  }

  nlohmann::json doc;
  doc["clangd_extra_include_paths"] = clangd_extra_include_paths;
  doc["clangd_use_gcc_query_driver"] = clangd_use_gcc_query_driver;
  doc["clangd_background_index"] = clangd_background_index;
  doc["theme"] = theme::theme_name(theme);
  doc["ui_colors_preset"] = theme::ui_color_preset_name(ui_colors_preset);
  doc["ui_colors"] = serialize_ui_colors(ui_colors);
  doc["compile_commands"] = {
      {"mode", compile_commands_mode_name(compile_commands.mode)},
      {"source_path", compile_commands.source_path},
      {"docker_container", compile_commands.docker_container},
      {"docker_compile_commands_path", compile_commands.docker_compile_commands_path},
      {"docker_detect_mounts", compile_commands.docker_detect_mounts},
      {"path_mappings", std::move(mappings)},
  };
  doc["build_environments"] = {
      {"active_environment_id", build_environments.active_environment_id},
      {"make_intercept_tool", make_intercept_tool_name(build_environments.make_intercept_tool)},
      {"make_default_target", build_environments.make_default_target},
      {"profiles", std::move(profiles)},
  };
  doc["ai"] = serialize_ai_settings(ai);
  if (!language_overrides.empty()) {
    nlohmann::json overrides = nlohmann::json::object();
    for (const auto& entry : language_overrides) {
      if (!entry.first.empty() && !entry.second.empty()) {
        overrides[entry.first] = entry.second;
      }
    }
    doc["language_overrides"] = std::move(overrides);
  }

  std::ofstream output(config_path(workspace_root));
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

}  // namespace tuide
