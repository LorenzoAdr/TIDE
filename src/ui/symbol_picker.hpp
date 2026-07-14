#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/file_picker_preview.hpp"
#include "ui/focus_manager.hpp"
#include "ui/symbol_picker_match_runner.hpp"

namespace tgdb {

struct MainLayoutState;

struct SymbolPickerState {
  bool open = false;
  std::string query;
  std::string loaded_file;
  std::string catalog_key;
  std::shared_ptr<const std::vector<SymbolCatalogEntry>> catalog;
  std::vector<SymbolPickerMatch> matches;
  int selected = 0;
  bool matches_dirty = true;
  bool searching = false;
  uint64_t search_generation = 0;
  SymbolPickerMatchRunner runner;
  FilePickerPreview preview;
  std::string preview_requested_path;
  int preview_requested_line = 0;

  void sync_catalog(const WorkspaceModel& workspace,
                    const std::shared_ptr<ISymbolProvider>& symbols,
                    SymbolWorkspaceIndexer* symbol_indexer);
  void schedule_search();
  void poll_search();
  void mark_matches_dirty();
  void set_search_notify(std::function<void()> notify);
  void set_preview_notify(std::function<void()> notify);
  void notify_search_tick();
  void update_preview_for_selection(const std::string& workspace_root);
  void reset_preview();
  void jump_to_selected(WorkspaceModel* workspace, FocusManagerState* focus);
  void on_closed();

 private:
  std::function<void()> search_notify_;
};

ftxui::Component MakeSymbolPickerOverlay(
    ftxui::Component main, WorkspaceModel* workspace, SymbolPickerState* state,
    FocusManagerState* focus, std::shared_ptr<ISymbolProvider> symbols,
    SymbolWorkspaceIndexer* symbol_indexer, MainLayoutState* layout_state);

}  // namespace tgdb
