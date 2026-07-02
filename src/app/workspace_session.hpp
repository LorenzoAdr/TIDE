#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct WorkspaceSession {
  std::vector<std::string> open_tabs;
  std::string active_tab_path;

  static std::string session_path(const std::string& workspace_root);
  static WorkspaceSession load(const std::string& workspace_root);
  bool save(const std::string& workspace_root) const;
};

}  // namespace tgdb
