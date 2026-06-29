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
    if (doc.contains("show_diagnostic_suffixes") &&
        doc["show_diagnostic_suffixes"].is_boolean()) {
      settings.show_diagnostic_suffixes = doc["show_diagnostic_suffixes"].get<bool>();
    }
    if (doc.contains("sticky_scroll_enabled") &&
        doc["sticky_scroll_enabled"].is_boolean()) {
      settings.sticky_scroll_enabled = doc["sticky_scroll_enabled"].get<bool>();
    }
    if (doc.contains("secondary_panel_enabled") &&
        doc["secondary_panel_enabled"].is_boolean()) {
      settings.secondary_panel_enabled = doc["secondary_panel_enabled"].get<bool>();
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
  doc["show_diagnostic_suffixes"] = show_diagnostic_suffixes;
  doc["sticky_scroll_enabled"] = sticky_scroll_enabled;
  doc["secondary_panel_enabled"] = secondary_panel_enabled;

  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

}  // namespace tgdb
