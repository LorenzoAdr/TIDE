#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tuide {

struct CompilerLocationMatch {
  std::string path;
  int line = 0;
  int column = 0;
  int span_start = 0;
  int span_end = 0;
};

// container prefix (as printed by a build inside Docker) → host prefix (the bind mount).
struct ContainerHostPrefix {
  std::string container;
  std::string host;
};

std::optional<CompilerLocationMatch> find_compiler_location(const std::string& line);

std::string resolve_compiler_path(const std::string& path, const std::string& workspace_root,
                                  const std::string& cwd);

// Rewrites an absolute container path onto the host. Empty when no prefix matches.
std::string map_container_compiler_path(const std::string& path,
                                       const std::vector<ContainerHostPrefix>& mappings);

}  // namespace tuide
