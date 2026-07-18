#include "app/workspace_detect.hpp"

#include <filesystem>

#include "build/build_environment.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string marker_name_at(const fs::path& dir) {
  std::error_code ec;
  if (fs::is_regular_file(dir / "CMakeLists.txt", ec)) {
    return "CMakeLists.txt";
  }
  if (fs::is_regular_file(dir / "Makefile", ec)) {
    return "Makefile";
  }
  if (fs::is_regular_file(dir / "makefile", ec)) {
    return "makefile";
  }
  if (fs::is_regular_file(dir / "GNUmakefile", ec)) {
    return "GNUmakefile";
  }
  return {};
}

fs::path normalize_anchor_directory(const std::string& anchor_path) {
  if (anchor_path.empty()) {
    return {};
  }
  std::error_code ec;
  fs::path path = fs::absolute(anchor_path, ec);
  if (path.empty()) {
    return {};
  }
  if (fs::is_regular_file(path, ec)) {
    path = path.parent_path();
  }
  return fs::absolute(path, ec);
}

}  // namespace

WorkspaceDetectResult detect_workspace_root(const std::string& anchor_path, int max_depth) {
  WorkspaceDetectResult result;
  const fs::path anchor = normalize_anchor_directory(anchor_path);
  if (anchor.empty()) {
    return result;
  }

  result.anchor_path = normalize_path(anchor.string());
  result.workspace_root = result.anchor_path;

  if (max_depth < 0) {
    max_depth = 0;
  }

  fs::path current = anchor;
  for (int depth = 0; depth <= max_depth; ++depth) {
    if (detect_build_system_kind(current.string()) != BuildSystemKind::kUnknown) {
      result.workspace_root = normalize_path(current.string());
      result.marker_found = true;
      result.marker = marker_name_at(current);
      return result;
    }

    const fs::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
      break;
    }
    current = parent;
  }

  return result;
}

}  // namespace tuide
