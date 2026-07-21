#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

class WorkspaceModel;

struct ExternalFileConflictState {
  bool open = false;
  int selected = 0;  // 0 = overwrite, 1 = load
  std::string path;
  std::string display_name;
  std::int64_t disk_mtime_sec = 0;
  WorkspaceModel* workspace = nullptr;
  ftxui::Box overwrite_box;
  ftxui::Box load_box;

  bool is_open() const { return open; }

  void show(WorkspaceModel* owner, const std::string& absolute_path, std::int64_t mtime_sec);
  void close();
};

ftxui::Component MakeExternalFileConflictOverlay(
    ftxui::Component main, ExternalFileConflictState* state, MainLayoutState* layout_state,
    std::function<void(WorkspaceModel* workspace)> on_resolved);

}  // namespace tuide
