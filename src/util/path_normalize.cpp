#include "util/path_normalize.hpp"

#include <filesystem>

namespace tuide {

std::string normalize_path(const std::string& path) {
  if (path.empty()) {
    return path;
  }
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(path, ec);
  return ec ? path : canonical.string();
}

}  // namespace tuide
