#include "toolpacks/store.hpp"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "toolpacks/manifest.hpp"
#include "toolpacks/paths.hpp"

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

std::optional<std::string> find_clang_resource_dir(const fs::path& install_root) {
  std::error_code ec;
  const fs::path clang_base = install_root / "lib" / "clang";
  if (fs::is_directory(clang_base, ec)) {
    for (const auto& entry : fs::directory_iterator(clang_base, ec)) {
      if (!entry.is_directory()) {
        continue;
      }
      if (fs::is_directory(entry.path() / "include", ec)) {
        return entry.path().string();
      }
    }
  }
  return std::nullopt;
}

}  // namespace

bool is_executable_path(const std::string& path) {
  return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

std::optional<ToolpackMeta> load_toolpack_meta(const std::string& toolpack_json_path) {
  const std::string text = read_file(toolpack_json_path);
  if (text.empty()) {
    return std::nullopt;
  }
  try {
    const nlohmann::json doc = nlohmann::json::parse(text);
    ToolpackMeta meta;
    meta.schema = doc.value("schema", 1);
    meta.id = doc.value("id", "");
    meta.version = doc.value("version", "");
    meta.license = doc.value("license", "");
    if (doc.contains("entry") && doc["entry"].is_object()) {
      const auto& entry = doc["entry"];
      meta.entry.type = entry.value("type", "executable");
      meta.entry.path = entry.value("path", "");
    }
    meta.entry.resource_dir = doc.value("resource_dir", "");
    if (meta.id.empty() || meta.entry.path.empty()) {
      return std::nullopt;
    }
    return meta;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

std::optional<ResolvedToolpack> resolve_installed_toolpack(const std::string& id) {
  const auto manifest = load_manifest(manifest_path());
  if (!manifest.has_value()) {
    return std::nullopt;
  }
  const auto entry = find_active_entry(*manifest, id);
  if (!entry.has_value()) {
    return std::nullopt;
  }

  const fs::path root = fs::path(toolpacks_root()) / entry->path;
  const fs::path meta_path = root / "toolpack.json";
  const auto meta = load_toolpack_meta(meta_path.string());
  if (!meta.has_value()) {
    return std::nullopt;
  }
  if (meta->id != id) {
    return std::nullopt;
  }

  ResolvedToolpack resolved;
  resolved.id = meta->id;
  resolved.version = meta->version.empty() ? entry->version : meta->version;
  resolved.root_dir = root.string();
  resolved.binary_path = (root / meta->entry.path).string();
  if (!is_executable_path(resolved.binary_path) && meta->entry.type == "executable") {
    return std::nullopt;
  }

  if (!meta->entry.resource_dir.empty()) {
    resolved.resource_dir = (root / meta->entry.resource_dir).string();
  } else if (id == "clangd") {
    if (const auto detected = find_clang_resource_dir(root); detected.has_value()) {
      resolved.resource_dir = *detected;
    }
  }
  return resolved;
}

std::optional<ResolvedToolpack> resolve_clangd_toolpack() {
  return resolve_installed_toolpack("clangd");
}

}  // namespace tuide::toolpacks
