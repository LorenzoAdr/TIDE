#include "app/recent_projects.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr const char* kConfigDir = ".config/tuide";
constexpr const char* kRecentFile = "recent_projects.json";

std::string normalize_workspace_path(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  std::error_code ec;
  const auto absolute = fs::absolute(workspace_root, ec);
  if (ec) {
    return normalize_path(workspace_root);
  }
  return normalize_path(absolute.string());
}

}  // namespace

std::string RecentProjects::config_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  return (fs::path(home) / kConfigDir / kRecentFile).string();
}

RecentProjects RecentProjects::load() {
  RecentProjects recent;
  const std::string path = config_path();
  if (path.empty()) {
    return recent;
  }

  std::ifstream input(path);
  if (!input) {
    return recent;
  }

  try {
    nlohmann::json doc;
    input >> doc;
    if (!doc.contains("projects") || !doc["projects"].is_array()) {
      return recent;
    }
    for (const auto& entry : doc["projects"]) {
      if (!entry.is_string()) {
        continue;
      }
      const std::string value = normalize_workspace_path(entry.get<std::string>());
      if (value.empty()) {
        continue;
      }
      bool duplicate = false;
      for (const auto& existing : recent.paths) {
        if (existing == value) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        continue;
      }
      recent.paths.push_back(value);
      if (recent.paths.size() >= kMaxRecent) {
        break;
      }
    }
  } catch (...) {
    return RecentProjects{};
  }
  return recent;
}

bool RecentProjects::save() const {
  const std::string path = config_path();
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);

  nlohmann::json doc;
  doc["projects"] = paths;

  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

bool RecentProjects::remember(const std::string& workspace_root) {
  const std::string normalized = normalize_workspace_path(workspace_root);
  if (normalized.empty()) {
    return false;
  }

  std::error_code ec;
  if (!fs::is_directory(normalized, ec)) {
    return false;
  }

  std::vector<std::string> updated;
  updated.reserve(kMaxRecent);
  updated.push_back(normalized);
  for (const auto& path : paths) {
    if (path == normalized) {
      continue;
    }
    updated.push_back(path);
    if (updated.size() >= kMaxRecent) {
      break;
    }
  }
  paths = std::move(updated);
  return save();
}

std::vector<std::string> RecentProjects::existing_paths() const {
  std::vector<std::string> existing;
  existing.reserve(paths.size());
  for (const auto& path : paths) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
      existing.push_back(path);
    }
  }
  return existing;
}

}  // namespace tuide
