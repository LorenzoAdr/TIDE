#include "app/workspace_session.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr const char* kSessionFile = "session.json";

}  // namespace

std::string WorkspaceSession::session_path(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / ".tgdb" / kSessionFile).string();
}

WorkspaceSession WorkspaceSession::load(const std::string& workspace_root) {
  WorkspaceSession session;
  if (workspace_root.empty()) {
    return session;
  }

  const std::string path = session_path(workspace_root);
  std::ifstream input(path);
  if (!input) {
    return session;
  }

  try {
    nlohmann::json doc;
    input >> doc;
    if (doc.contains("open_tabs") && doc["open_tabs"].is_array()) {
      for (const auto& entry : doc["open_tabs"]) {
        if (entry.is_string()) {
          const std::string value = entry.get<std::string>();
          if (!value.empty()) {
            session.open_tabs.push_back(value);
          }
        }
      }
    }
    if (doc.contains("active_tab_path") && doc["active_tab_path"].is_string()) {
      session.active_tab_path = doc["active_tab_path"].get<std::string>();
    }
  } catch (...) {
    return WorkspaceSession{};
  }
  return session;
}

bool WorkspaceSession::save(const std::string& workspace_root) const {
  if (workspace_root.empty()) {
    return false;
  }

  std::error_code ec;
  fs::create_directories(fs::path(workspace_root) / ".tgdb", ec);

  nlohmann::json doc;
  doc["open_tabs"] = open_tabs;
  doc["active_tab_path"] = active_tab_path;

  std::ofstream output(session_path(workspace_root));
  if (!output) {
    return false;
  }
  output << doc.dump(2) << '\n';
  return static_cast<bool>(output);
}

}  // namespace tgdb
