#pragma once

#include <functional>
#include <memory>

#include "app/app_mode.hpp"
#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "symbols/symbol_provider.hpp"
#include "terminal/shell_session.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "ui/focus_manager.hpp"
#include "ui/context_menu.hpp"
#include "ui/source_panel.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

enum class TextInputFocus {
  None,
  Console,
  Watch,
  WatchInject,
  EditorFind,
  EditorGotoLine,
  EditorCompletion,
  SearchQuery,
  SearchReplace,
  SearchPath,
  SearchExclude,
};

struct RightSidebarState {
  int selected_tab = 0;
  bool pending_focus_search = false;
  bool pending_search_setup = false;
  std::string pending_search_query;
  std::string pending_search_path_filter;
};

using StopDebugCallback = std::function<void()>;

struct ConsolePanelTabs {
  static constexpr int kTerminal = 0;
  static constexpr int kDebug = 1;
  int selected_tab = kTerminal;
};

struct MainLayoutState {
  bool console_visible = true;
  bool diagnostics_panel_visible = false;
  int diagnostics_panel_height = 6;
  bool terminal_start_requested = true;
  bool request_ui_tick = false;
  ConsolePanelTabs console_tabs;
  TextInputFocus text_input_focus = TextInputFocus::None;
  bool focus_sync_needed = false;
  bool pending_watches_focus = false;
  int right_panel_active_section = 0;  // 0 = outline/búsqueda, 1 = depuración
  RightSidebarState right_sidebar;
  ContextMenuState context_menu;
  std::function<bool(const ftxui::Event&)> editor_key_handler;
  std::function<bool(const ftxui::Event&)> editor_mouse_handler;
  std::function<bool(const ftxui::Event&)> editor_chrome_mouse_handler;
  std::function<bool(const ftxui::Event&)> explorer_context_handler;
  std::function<void(ftxui::Event&)> editor_modifier_handler;
  std::function<void()> editor_tick_callback;
  std::function<int()> editor_visible_line_count;
  std::function<void()> outline_tick_callback;
  std::function<bool(const ftxui::Event&)> console_key_handler;
  std::function<bool(const ftxui::Event&)> search_key_handler;
  std::function<void()> terminal_tick_callback;
  std::function<int()> terminal_width;
  std::function<void()> schedule_ui_tick;
};

inline bool is_search_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::SearchQuery ||
         focus == TextInputFocus::SearchReplace ||
         focus == TextInputFocus::SearchPath ||
         focus == TextInputFocus::SearchExclude;
}

inline bool is_watch_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::Watch || focus == TextInputFocus::WatchInject;
}

ftxui::Component MakeMainLayout(AppMode* app_mode, DebugModel* model,
                                WorkspaceModel* workspace, SourceViewState* source_state,
                                FocusManagerState* focus,
                                std::shared_ptr<ISymbolProvider> symbols,
                                CommandCallback on_command,
                                MainLayoutState* layout_state,
                                StopDebugCallback on_stop_debug,
                                ShellSession* shell,
                                WorkspaceIndexer* indexer,
                                SymbolWorkspaceIndexer* symbol_indexer);

}  // namespace tgdb
