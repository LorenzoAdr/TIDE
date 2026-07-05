#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "ui/file_picker_preview.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

struct FilePickerState {
  bool open = false;
  std::string query;
  std::string indexed_root;
  std::shared_ptr<const IndexSnapshot> index_snapshot;
  std::vector<std::string> all_files;
  std::vector<std::string> matches;
  int selected = 0;
  bool matches_dirty = true;
  FilePickerPreview preview;
  std::string preview_requested_path;
  bool ctrl_chord_armed = false;
  bool ctrl_chord_active = false;

  void sync_index(const std::shared_ptr<const IndexSnapshot>& snapshot,
                  const std::string& workspace_root);
  void refresh_matches(const WorkspaceModel* workspace = nullptr);
  void mark_matches_dirty();
  void open_file(DebugModel* model, WorkspaceModel* workspace,
                 FocusManagerState* focus, int index);
  void set_preview_notify(std::function<void()> notify);
  void update_preview_for_selection(const std::string& workspace_root);
  void reset_preview();
  void on_opened(const std::string& workspace_root);
  void on_closed();
  void arm_ctrl_chord();
  void cancel_ctrl_chord();
  void confirm_ctrl_chord_selection(DebugModel* model, WorkspaceModel* workspace,
                                    FocusManagerState* focus);
};

ftxui::Component MakeFilePickerOverlay(ftxui::Component main, DebugModel* model,
                                      WorkspaceModel* workspace,
                                      FilePickerState* state,
                                      FocusManagerState* focus,
                                      WorkspaceIndexer* indexer);

}  // namespace tgdb
