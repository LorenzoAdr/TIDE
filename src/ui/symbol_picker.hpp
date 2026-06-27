#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

struct SymbolPickerState {
  bool open = false;
  std::string query;
  std::string loaded_file;
  std::vector<SymbolInfo> all_symbols;
  std::vector<SymbolInfo> matches;
  int selected = 0;

  void sync_symbols(const WorkspaceModel& workspace,
                    const std::shared_ptr<ISymbolProvider>& symbols);
  void refresh_matches();
  void jump_to_selected(WorkspaceModel* workspace, FocusManagerState* focus);
};

ftxui::Component MakeSymbolPickerOverlay(
    ftxui::Component main, WorkspaceModel* workspace, SymbolPickerState* state,
    FocusManagerState* focus, std::shared_ptr<ISymbolProvider> symbols);

}  // namespace tgdb
