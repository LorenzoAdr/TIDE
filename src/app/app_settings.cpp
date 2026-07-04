#include "app/app_settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr const char* kConfigDir = ".config/tgdb";
constexpr const char* kConfigFile = "settings.json";

}  // namespace

std::string AppSettings::config_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  return (fs::path(home) / kConfigDir / kConfigFile).string();
}

AppSettings AppSettings::load() {
  AppSettings settings;
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_CLANGD
  settings.force_bundled_clangd = true;
#endif
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_GDB
  settings.force_bundled_gdb = true;
#endif
  const std::string path = config_path();
  if (path.empty()) {
    return settings;
  }

  std::ifstream input(path);
  if (!input) {
    return settings;
  }

  try {
    nlohmann::json doc;
    input >> doc;
    if (doc.contains("lsp_enabled") && doc["lsp_enabled"].is_boolean()) {
      settings.lsp_enabled = doc["lsp_enabled"].get<bool>();
    }
    if (doc.contains("live_lsp_completion_enabled") &&
        doc["live_lsp_completion_enabled"].is_boolean()) {
      settings.live_lsp_completion_enabled = doc["live_lsp_completion_enabled"].get<bool>();
    }
    if (doc.contains("show_diagnostic_suffixes") &&
        doc["show_diagnostic_suffixes"].is_boolean()) {
      settings.show_diagnostic_suffixes = doc["show_diagnostic_suffixes"].get<bool>();
    }
    if (doc.contains("sticky_scroll_enabled") &&
        doc["sticky_scroll_enabled"].is_boolean()) {
      settings.sticky_scroll_enabled = doc["sticky_scroll_enabled"].get<bool>();
    }
    if (doc.contains("indent_guides_enabled") && doc["indent_guides_enabled"].is_boolean()) {
      settings.indent_guides_enabled = doc["indent_guides_enabled"].get<bool>();
    }
    if (doc.contains("overview_ruler_enabled") &&
        doc["overview_ruler_enabled"].is_boolean()) {
      settings.overview_ruler_enabled = doc["overview_ruler_enabled"].get<bool>();
    }
    if (doc.contains("secondary_panel_enabled") &&
        doc["secondary_panel_enabled"].is_boolean()) {
      settings.secondary_panel_enabled = doc["secondary_panel_enabled"].get<bool>();
    }
    if (doc.contains("force_bundled_clangd") && doc["force_bundled_clangd"].is_boolean()) {
      settings.force_bundled_clangd = doc["force_bundled_clangd"].get<bool>();
    }
    if (doc.contains("force_bundled_gdb") && doc["force_bundled_gdb"].is_boolean()) {
      settings.force_bundled_gdb = doc["force_bundled_gdb"].get<bool>();
    }
    if (doc.contains("monitor_enabled") && doc["monitor_enabled"].is_boolean()) {
      settings.monitor_enabled = doc["monitor_enabled"].get<bool>();
    }
    if (doc.contains("show_all_workspace_files") &&
        doc["show_all_workspace_files"].is_boolean()) {
      settings.show_all_workspace_files = doc["show_all_workspace_files"].get<bool>();
    }
    if (doc.contains("icon_mode") && doc["icon_mode"].is_string()) {
      const std::string mode = doc["icon_mode"].get<std::string>();
      if (mode == "always") {
        settings.icon_mode = IconMode::Always;
      } else if (mode == "never") {
        settings.icon_mode = IconMode::Never;
      } else {
        settings.icon_mode = IconMode::Auto;
      }
    }
  } catch (...) {
    return AppSettings{};
  }
  return settings;
}

bool AppSettings::save() const {
  const std::string path = config_path();
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);

  nlohmann::json doc;
  doc["lsp_enabled"] = lsp_enabled;
  doc["live_lsp_completion_enabled"] = live_lsp_completion_enabled;
  doc["show_diagnostic_suffixes"] = show_diagnostic_suffixes;
  doc["sticky_scroll_enabled"] = sticky_scroll_enabled;
  doc["indent_guides_enabled"] = indent_guides_enabled;
  doc["overview_ruler_enabled"] = overview_ruler_enabled;
  doc["secondary_panel_enabled"] = secondary_panel_enabled;
  doc["force_bundled_clangd"] = force_bundled_clangd;
  doc["force_bundled_gdb"] = force_bundled_gdb;
  doc["monitor_enabled"] = monitor_enabled;
  doc["show_all_workspace_files"] = show_all_workspace_files;
  switch (icon_mode) {
    case IconMode::Always:
      doc["icon_mode"] = "always";
      break;
    case IconMode::Never:
      doc["icon_mode"] = "never";
      break;
    case IconMode::Auto:
    default:
      doc["icon_mode"] = "auto";
      break;
  }

  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

}  // namespace tgdb
