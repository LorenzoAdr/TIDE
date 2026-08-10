#include "toolpacks/paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::string default_user_toolpacks_root() {
  if (const std::string xdg = non_empty_env("XDG_DATA_HOME"); !xdg.empty()) {
    return (fs::path(xdg) / "tuide" / "toolpacks").string();
  }
  if (const std::string home = non_empty_env("HOME"); !home.empty()) {
    return (fs::path(home) / ".local" / "share" / "tuide" / "toolpacks").string();
  }
  return "tuide-toolpacks";
}

bool path_is_writable_dir(const fs::path& root) {
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) {
    return false;
  }
  const fs::path probe = root / ".tuide-write-probe";
  {
    std::ofstream out(probe, std::ios::trunc);
    if (!out) {
      return false;
    }
    out << "ok\n";
    if (!out) {
      return false;
    }
  }
  fs::remove(probe, ec);
  return true;
}

}  // namespace

std::string toolpacks_root() {
  if (const std::string override_root = non_empty_env("TUIDE_TOOLPACKS_ROOT");
      !override_root.empty()) {
    // Old AppImages exported ROOT onto the read-only squashfs. Ignore that for
    // installs and keep writing under XDG; resolution still sees it via bundled.
    if (path_is_writable_dir(override_root)) {
      return override_root;
    }
  }
  return default_user_toolpacks_root();
}

std::string bundled_toolpacks_root() {
  if (const std::string bundled = non_empty_env("TUIDE_TOOLPACKS_BUNDLED"); !bundled.empty()) {
    return bundled;
  }
  // Compatibility with AppRun that only set TUIDE_TOOLPACKS_ROOT to the AppDir tree.
  if (const std::string override_root = non_empty_env("TUIDE_TOOLPACKS_ROOT");
      !override_root.empty() && !path_is_writable_dir(override_root)) {
    return override_root;
  }
  return {};
}

std::string manifest_path() {
  return (fs::path(toolpacks_root()) / "manifest.json").string();
}

std::string bundled_manifest_path() {
  const std::string bundled = bundled_toolpacks_root();
  if (bundled.empty()) {
    return {};
  }
  return (fs::path(bundled) / "manifest.json").string();
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

bool toolpacks_root_is_writable() {
  return path_is_writable_dir(toolpacks_root());
}

}  // namespace tuide::toolpacks
