#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tuide {

struct RecentProjects {
  static constexpr std::size_t kMaxRecent = 5;

  std::vector<std::string> paths;

  static std::string config_path();
  static RecentProjects load();
  bool save() const;

  // Move path to the front (deduped), truncate to kMaxRecent, and persist.
  bool remember(const std::string& workspace_root);

  // Paths that still exist as directories, newest first.
  std::vector<std::string> existing_paths() const;
};

}  // namespace tuide
