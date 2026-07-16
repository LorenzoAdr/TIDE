#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "ui/file_picker_match_runner.hpp"
#include "ui/file_picker_preview.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

struct MainLayoutState;

struct FilePickerState {
  bool open = false;
  std::string query;
  std::string indexed_root;
  std::shared_ptr<const IndexSnapshot> index_snapshot;
  std::vector<FilePickerMatch> matches;
  int selected = 0;
  bool matches_dirty = true;
  bool searching = false;
  uint64_t search_generation = 0;
  FilePickerMatchRunner runner;
  FilePickerPreview preview;
  std::function<void()> repaint_notify;
  std::function<void()> file_opened_notify;
  std::string preview_requested_path;
  bool ctrl_chord_armed = false;
  bool ctrl_chord_active = false;

  void sync_index(const std::shared_ptr<const IndexSnapshot>& snapshot,
                  const std::string& workspace_root);
  void schedule_search(const WorkspaceModel* workspace);
  void poll_search(const WorkspaceModel* workspace);
  void mark_matches_dirty();
  void open_file(DebugModel* model, WorkspaceModel* workspace,
                 FocusManagerState* focus, int index);
  void set_search_notify(std::function<void()> notify);
  void set_preview_notify(std::function<void()> notify);
  void set_repaint_notify(std::function<void()> notify);
  void set_file_opened_notify(std::function<void()> notify);
  void notify_search_tick();
  void update_preview_for_selection(const std::string& workspace_root);
  void reset_preview();
  void on_opened(const std::string& workspace_root);
  void on_closed();
  void arm_ctrl_chord();
  void cancel_ctrl_chord();
  void confirm_ctrl_chord_selection(DebugModel* model, WorkspaceModel* workspace,
                                    FocusManagerState* focus);

 private:
  void refresh_empty_query_matches(const WorkspaceModel* workspace);
  std::function<void()> search_notify_;
};

ftxui::Component MakeFilePickerOverlay(ftxui::Component main, DebugModel* model,
                                      WorkspaceModel* workspace,
                                      FilePickerState* state,
                                      FocusManagerState* focus,
                                      WorkspaceIndexer* indexer,
                                      MainLayoutState* layout_state);

}  // namespace tgdb
