#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/workspace_config.hpp"
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
#include "ui/path_browser.hpp"

namespace tgdb {

struct MainLayoutState;

enum class ContextMenuKind { File, Folder, EditorSymbol, EditorBackground, Problem };

enum class NamePromptKind { Rename, CreateFile, CreateFolder };

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

  std::string problem_path;
  int problem_line = 0;
  int problem_start_col = 0;
  int problem_end_col = 0;
  std::string problem_message;

  bool rename_open = false;
  bool rename_skip_return = false;
  NamePromptKind name_prompt_kind = NamePromptKind::Rename;
  bool delete_confirm_open = false;
  bool indexer_paths_open = false;
  bool move_browser_open = false;
  bool move_is_dir = false;
  PathBrowserState move_browser;
  int indexer_paths_scroll = 0;
  std::vector<std::string> indexer_paths_lines;
  std::string rename_input;
  std::vector<ftxui::Box> row_boxes;
};

bool context_menu_active(const ContextMenuState* state);

void context_menu_close(ContextMenuState* state, MainLayoutState* layout_state = nullptr);

void focus_search_with_filter(MainLayoutState* layout_state, const std::string& query,
                              const std::string& path_filter);

void context_menu_open_file(ContextMenuState* state, int x, int y,
                            const std::string& absolute_path, const std::string& relative_path,
                            bool show_format = false, bool show_secondary_open = true,
                            bool show_analyze_symbols = false);

void context_menu_open_folder(ContextMenuState* state, int x, int y,
                              const std::string& absolute_path, const std::string& relative_path);

void context_menu_open_editor_symbol(ContextMenuState* state, int x, int y, int line, int col,
                                     int sym_start, int sym_end, const std::string& symbol,
                                     const std::string& absolute_path, bool show_call_hierarchy);

void context_menu_open_editor_background(ContextMenuState* state, int x, int y,
                                         const std::string& absolute_path, int line, int col,
                                         bool show_call_hierarchy);

void context_menu_open_problem(ContextMenuState* state, int x, int y, const std::string& path,
                               int line, int start_col, int end_col, const std::string& message,
                               bool lsp_available);

bool handle_context_menu_keys(ContextMenuState* state, WorkspaceModel* workspace,
                              WorkspaceModel* secondary_workspace, DebugModel* model,
                              FocusManagerState* focus, MainLayoutState* layout_state,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer,
                              const WorkspaceConfig* workspace_config, int editor_visible_lines,
                              const ftxui::Event& event);

bool handle_context_menu_mouse(ContextMenuState* state, MainLayoutState* layout_state,
                               const ftxui::Mouse& mouse, int* clicked_row);

ftxui::Component MakeContextMenuOverlay(
    ftxui::Component main, ContextMenuState* state, WorkspaceModel* workspace,
    WorkspaceModel* secondary_workspace, DebugModel* model, FocusManagerState* focus,
    MainLayoutState* layout_state, const std::shared_ptr<ISymbolProvider>& symbols,
    WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer,
    const WorkspaceConfig* workspace_config, std::function<int()> editor_visible_lines);

}  // namespace tgdb
