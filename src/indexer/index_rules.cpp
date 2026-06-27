#include "indexer/index_rules.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace tgdb {

bool should_skip_dir_name(const std::string& name) {
  if (name.empty() || name[0] == '.') {
    return true;
  }
  return name == "build" || name == "cmake-build-debug" ||
         name == "cmake-build-release" || name == "node_modules" ||
         name == "_deps" || name == ".cache" || name == "dist" || name == "out";
}

bool is_indexed_source_path(const std::string& path) {
  const auto ext = fs::path(path).extension().string();
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
         ext == ".hpp" || ext == ".c";
}

bool should_index_relative_path(const std::string& relative_path) {
  if (relative_path.empty()) {
    return false;
  }
  for (const auto& part : fs::path(relative_path)) {
    if (should_skip_dir_name(part.string())) {
      return false;
    }
  }
  return is_indexed_source_path(relative_path);
}

}  // namespace tgdb
