#include "toolpacks/paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

std::string non_empty_env(const char* name) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    return {};
  }
  return raw;
}

}  // namespace

std::string toolpacks_root() {
  if (const std::string override_root = non_empty_env("TUIDE_TOOLPACKS_ROOT");
      !override_root.empty()) {
    return override_root;
  }
  if (const std::string xdg = non_empty_env("XDG_DATA_HOME"); !xdg.empty()) {
    return (fs::path(xdg) / "tuide" / "toolpacks").string();
  }
  if (const std::string home = non_empty_env("HOME"); !home.empty()) {
    return (fs::path(home) / ".local" / "share" / "tuide" / "toolpacks").string();
  }
  return "tuide-toolpacks";
}

std::string manifest_path() {
  return (fs::path(toolpacks_root()) / "manifest.json").string();
}

std::string cache_root() {
  if (const std::string xdg = non_empty_env("XDG_CACHE_HOME"); !xdg.empty()) {
    return (fs::path(xdg) / "tuide").string();
  }
  if (const std::string home = non_empty_env("HOME"); !home.empty()) {
    return (fs::path(home) / ".cache" / "tuide").string();
  }
  return "tuide-cache";
}

std::string downloads_dir() {
  return (fs::path(cache_root()) / "downloads").string();
}

std::string default_catalog_url() {
  if (const std::string override_url = non_empty_env("TUIDE_TOOLPACKS_CATALOG_URL");
      !override_url.empty()) {
    return override_url;
  }
  return "https://github.com/LorenzoAdr/TIDE/releases/download/catalog-latest/catalog.json";
}

}  // namespace tuide::toolpacks
