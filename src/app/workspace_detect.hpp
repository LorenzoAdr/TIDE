#pragma once

#include <string>

namespace tuide {

struct WorkspaceDetectResult {
  std::string workspace_root;
  std::string anchor_path;
  bool marker_found = false;
  std::string marker;
};

constexpr int kDefaultWorkspaceDetectMaxDepth = 20;

WorkspaceDetectResult detect_workspace_root(const std::string& anchor_path,
                                            int max_depth = kDefaultWorkspaceDetectMaxDepth);

}  // namespace tuide
