#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

struct MainLayoutState;

enum class ContextMenuKind { File, Folder, EditorSymbol, EditorBackground };

struct ContextMenuState {
  bool open = false;
  int anchor_x = 0;
  int anchor_y = 0;
  ftxui::Box menu_box;
  int selected = 0;
  std::vector<std::string> labels;
  std::vector<std::string> action_ids;
  ContextMenuKind kind = ContextMenuKind::File;

  std::string absolute_path;
  std::string relative_path;

  int editor_line = 0;
  int editor_col = 0;
  int editor_sym_start = 0;
  int editor_sym_end = 0;
  std::string symbol_name;

  bool rename_open = false;
  bool rename_skip_return = false;
  bool delete_confirm_open = false;
  std::string rename_input;
  std::vector<ftxui::Box> row_boxes;
};

bool context_menu_active(const ContextMenuState* state);

void context_menu_close(ContextMenuState* state, MainLayoutState* layout_state = nullptr);

void context_menu_open_file(ContextMenuState* state, int x, int y,
                            const std::string& absolute_path, const std::string& relative_path,
                            bool show_format = false);

void context_menu_open_folder(ContextMenuState* state, int x, int y,
                              const std::string& absolute_path, const std::string& relative_path);

void context_menu_open_editor_symbol(ContextMenuState* state, int x, int y, int line, int col,
                                     int sym_start, int sym_end, const std::string& symbol,
                                     bool show_call_hierarchy);

void context_menu_open_editor_background(ContextMenuState* state, int x, int y,
                                         const std::string& absolute_path, int line, int col,
                                         bool show_call_hierarchy);

bool handle_context_menu_keys(ContextMenuState* state, WorkspaceModel* workspace,
                              DebugModel* model, FocusManagerState* focus,
                              MainLayoutState* layout_state,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer,
                              int editor_visible_lines, const ftxui::Event& event);

bool handle_context_menu_mouse(ContextMenuState* state, MainLayoutState* layout_state,
                               const ftxui::Mouse& mouse, int* clicked_row);

ftxui::Component MakeContextMenuOverlay(
    ftxui::Component main, ContextMenuState* state, WorkspaceModel* workspace,
    DebugModel* model, FocusManagerState* focus, MainLayoutState* layout_state,
    const std::shared_ptr<ISymbolProvider>& symbols, WorkspaceIndexer* indexer,
    SymbolWorkspaceIndexer* symbol_indexer, std::function<int()> editor_visible_lines);

}  // namespace tgdb
