#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

struct FilePickerState {
  bool open = false;
  std::string query;
  std::string indexed_root;
  std::vector<std::string> all_files;
  std::vector<std::string> matches;
  int selected = 0;

  void sync_index(const std::shared_ptr<const IndexSnapshot>& snapshot,
                  const std::string& workspace_root);
  void refresh_matches(const WorkspaceModel* workspace = nullptr);
  void open_file(DebugModel* model, WorkspaceModel* workspace,
                 FocusManagerState* focus, int index);
};

ftxui::Component MakeFilePickerOverlay(ftxui::Component main, DebugModel* model,
                                      WorkspaceModel* workspace,
                                      FilePickerState* state,
                                      FocusManagerState* focus,
                                      WorkspaceIndexer* indexer);

}  // namespace tgdb
