#pragma once

#include <string>
#include <vector>

#include "ui/focus_manager.hpp"

namespace tgdb {

struct GitService;
struct MainLayoutState;
struct WorkspaceModel;

struct GitDiffSyncState {
  bool active = false;
  std::string path;
  std::vector<int> working_to_head;
  std::vector<int> head_to_working;
  int last_working_scroll = -1;
  int last_head_scroll = -1;
  bool syncing = false;
  WorkspaceModel* working_ws = nullptr;
  WorkspaceModel* head_ws = nullptr;
};

void git_diff_sync_deactivate(MainLayoutState* layout);

void git_diff_sync_refresh_map(MainLayoutState* layout);

bool open_git_diff_split_view(WorkspaceModel* working_ws, WorkspaceModel* head_ws, GitService* git,
                              MainLayoutState* layout, FocusManagerState* focus,
                              const std::string& workspace_rel_path);

void git_diff_sync_on_scroll(MainLayoutState* layout, FocusRegion source);

}  // namespace tgdb
