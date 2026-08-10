#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tuide::toolpacks {

struct ManifestEntry {
  std::string id;
  std::string version;
  bool active = false;
  std::string installed_at;
  std::string source;
  std::string path;  // relative to toolpacks_root(), e.g. clangd/19.1.2
};

struct Manifest {
  int schema = 1;
  std::vector<ManifestEntry> installed;
};

std::optional<Manifest> load_manifest(const std::string& path);
bool save_manifest(const std::string& path, const Manifest& manifest);

std::optional<ManifestEntry> find_active_entry(const Manifest& manifest,
                                               const std::string& id);

}  // namespace tuide::toolpacks
