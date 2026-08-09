#include "toolpacks/manifest.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

std::string read_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

}  // namespace

std::optional<Manifest> load_manifest(const std::string& path) {
  const std::string text = read_file(path);
  if (text.empty()) {
    return std::nullopt;
  }
  try {
    const nlohmann::json doc = nlohmann::json::parse(text);
    Manifest manifest;
    manifest.schema = doc.value("schema", 1);
    if (!doc.contains("installed") || !doc["installed"].is_array()) {
      return manifest;
    }
    for (const auto& item : doc["installed"]) {
      ManifestEntry entry;
      entry.id = item.value("id", "");
      entry.version = item.value("version", "");
      entry.active = item.value("active", false);
      entry.installed_at = item.value("installed_at", "");
      entry.source = item.value("source", "");
      entry.path = item.value("path", "");
      if (entry.id.empty() || entry.path.empty()) {
        continue;
      }
      manifest.installed.push_back(std::move(entry));
    }
    return manifest;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

bool save_manifest(const std::string& path, const Manifest& manifest) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  nlohmann::json doc;
  doc["schema"] = manifest.schema;
  doc["installed"] = nlohmann::json::array();
  for (const auto& entry : manifest.installed) {
    doc["installed"].push_back({
        {"id", entry.id},
        {"version", entry.version},
        {"active", entry.active},
        {"installed_at", entry.installed_at},
        {"source", entry.source},
        {"path", entry.path},
    });
  }
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

std::optional<ManifestEntry> find_active_entry(const Manifest& manifest,
                                               const std::string& id) {
  const ManifestEntry* fallback = nullptr;
  for (const auto& entry : manifest.installed) {
    if (entry.id != id) {
      continue;
    }
    if (entry.active) {
      return entry;
    }
    if (fallback == nullptr) {
      fallback = &entry;
    }
  }
  if (fallback != nullptr) {
    return *fallback;
  }
  return std::nullopt;
}

}  // namespace tuide::toolpacks
