#pragma once

#include <chrono>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "app/app_mode.hpp"
#include "app/app_settings.hpp"
#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "symbols/symbol_provider.hpp"
#include "terminal/shell_session.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "ui/call_hierarchy_view.hpp"
#include "ui/focus_manager.hpp"
#include "ui/clickable_interaction.hpp"
#include "ui/context_menu.hpp"
#include "ui/press_ids.hpp"
#include "git/git_service.hpp"
#include "util/system_stats.hpp"
#include "ui/source_panel.hpp"
#include "util/path_normalize.hpp"

namespace tgdb {

struct WelcomeScreenState;

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
  SearchInclude,
  SearchExclude,
};

struct RightSidebarState {
  bool pending_focus_search = false;
  bool pending_search_setup = false;
  std::string pending_search_query;
  std::string pending_search_path_filter;
  bool pending_call_hierarchy = false;
  int pending_call_hierarchy_line = 0;
  int pending_call_hierarchy_col = 0;
  std::string pending_call_hierarchy_symbol;
  CallHierarchyViewState call_hierarchy;
};

using StopDebugCallback = std::function<void()>;

struct ConsolePanelTabs {
  static constexpr int kTerminal = 0;
  static constexpr int kDebug = 1;
  static constexpr int kPerformance = 2;
  static constexpr int kProblems = 3;
  static constexpr int kSearch = 4;
  static constexpr int kCallHierarchy = 5;
  int selected_tab = kTerminal;
};

struct MainLayoutState {
  bool console_visible = true;
  bool welcome_visible = false;
  bool git_page_visible = false;
  bool diagnostics_panel_visible = false;
  int diagnostics_panel_height = 6;
  bool terminal_start_requested = true;
  bool request_ui_tick = false;
  std::atomic<bool> ui_heartbeat{false};
  AppSettings* app_settings = nullptr;
  ConsolePanelTabs console_tabs;
  TextInputFocus text_input_focus = TextInputFocus::None;
  bool focus_sync_needed = false;
  bool pending_watches_focus = false;
  int right_panel_active_section = 0;  // 0 = outline/búsqueda, 1 = depuración
  RightSidebarState right_sidebar;
  ContextMenuState context_menu;
  ClickableInteractionTracker clickable;
  struct EditorSymbolPress {
    std::string path;
    int line = -1;
    int start_col = 0;
    int end_col = 0;
    int render_row = -1;
    std::chrono::steady_clock::time_point visible_until{};
  };
  EditorSymbolPress editor_symbol_press;
  struct PendingEditorNavigation {
    SourceLocation loc;
    std::chrono::steady_clock::time_point execute_after{};
    std::chrono::steady_clock::time_point deadline{};
    bool active = false;
  };
  PendingEditorNavigation pending_editor_navigation;
  std::function<bool(const ftxui::Event&)> editor_key_handler;
  std::function<bool(const ftxui::Event&)> editor_mouse_handler;
  std::function<bool(const ftxui::Event&)> editor_chrome_mouse_handler;
  std::function<bool(const ftxui::Event&)> source_mouse_handler;
  std::function<bool(const ftxui::Event&)> source_key_handler;
  std::function<bool(const ftxui::Event&)> watches_mouse_handler;
  std::function<bool(const ftxui::Event&)> console_debug_mouse_handler;
  std::function<bool(const ftxui::Event&)> explorer_mouse_handler;
  std::function<void(ftxui::Event&)> editor_modifier_handler;
  std::function<void()> editor_tick_callback;
  std::function<int()> editor_visible_line_count;
  std::function<void()> outline_tick_callback;
  std::function<bool(const ftxui::Event&)> console_key_handler;
  std::function<bool(const ftxui::Event&)> console_mouse_handler;
  std::function<bool(const ftxui::Event&)> split_mouse_handler;
  std::function<bool(const ftxui::Event&)> search_key_handler;
  std::function<bool(const ftxui::Event&)> call_hierarchy_key_handler;
  std::function<bool(const ftxui::Event&)> problems_key_handler;
  std::function<bool(const ftxui::Event&)> git_key_handler;
  std::function<bool(const ftxui::Event&)> git_mouse_handler;
  std::function<bool(ftxui::Event&)> welcome_key_handler;
  std::function<bool(ftxui::Event&)> welcome_mouse_handler;
  bool editor_completion_open = false;
  std::function<void()> terminal_tick_callback;
  std::function<void()> terminal_follow_input_callback;
  std::function<void()> terminal_wake_callback;
  std::function<int()> terminal_width;
  std::function<int()> terminal_height;
  std::function<void()> schedule_ui_tick;
  std::function<void(const std::string& path)> on_file_saved;
  PerformanceSampler performance_sampler;
};

inline bool problems_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kProblems;
}

inline bool search_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kSearch;
}

inline bool call_hierarchy_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kCallHierarchy;
}

inline bool console_panel_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->console_tabs.selected_tab != ConsolePanelTabs::kTerminal &&
         layout_state->console_tabs.selected_tab != ConsolePanelTabs::kDebug;
}

inline bool is_search_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::SearchQuery ||
         focus == TextInputFocus::SearchReplace ||
         focus == TextInputFocus::SearchPath ||
         focus == TextInputFocus::SearchInclude ||
         focus == TextInputFocus::SearchExclude;
}

inline bool is_watch_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::Watch || focus == TextInputFocus::WatchInject;
}

inline bool is_editor_chrome_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::EditorFind || focus == TextInputFocus::EditorGotoLine ||
         focus == TextInputFocus::EditorCompletion;
}

inline bool editor_symbol_press_visible(const MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return false;
  }
  const auto& press = layout_state->editor_symbol_press;
  if (press.line < 0 || press.end_col <= press.start_col) {
    return false;
  }
  return std::chrono::steady_clock::now() < press.visible_until;
}

inline void clear_editor_symbol_press(MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return;
  }
  layout_state->editor_symbol_press.line = -1;
}

inline void request_editor_symbol_press(MainLayoutState* layout_state, const std::string& path,
                                      int line, int start_col, int end_col, int render_row = -1) {
  if (layout_state == nullptr || path.empty() || line < 0 || end_col <= start_col) {
    return;
  }
  layout_state->editor_symbol_press = {
      path, line, start_col, end_col, render_row,
      std::chrono::steady_clock::now() + std::chrono::milliseconds(500)};
  layout_state->clickable.trigger_press(press_id::editor_symbol(line, start_col, end_col),
                                        std::chrono::milliseconds(500));
}

inline void schedule_editor_navigation(MainLayoutState* layout_state, const SourceLocation& loc) {
  if (layout_state == nullptr || !loc.valid || loc.path.empty()) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  layout_state->pending_editor_navigation.loc = loc;
  layout_state->pending_editor_navigation.execute_after = now + std::chrono::milliseconds(350);
  layout_state->pending_editor_navigation.deadline = now + std::chrono::milliseconds(600);
  layout_state->pending_editor_navigation.active = true;
}

inline bool tick_pending_editor_navigation(
    MainLayoutState* layout_state,
    const std::function<void(const SourceLocation&)>& navigate) {
  if (layout_state == nullptr || !layout_state->pending_editor_navigation.active) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now > layout_state->pending_editor_navigation.deadline) {
    const SourceLocation loc = layout_state->pending_editor_navigation.loc;
    layout_state->pending_editor_navigation.active = false;
    navigate(loc);
    clear_editor_symbol_press(layout_state);
    return true;
  }
  if (now < layout_state->pending_editor_navigation.execute_after) {
    return false;
  }
  const SourceLocation loc = layout_state->pending_editor_navigation.loc;
  layout_state->pending_editor_navigation.active = false;
  navigate(loc);
  clear_editor_symbol_press(layout_state);
  return true;
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
                                SymbolWorkspaceIndexer* symbol_indexer,
                                ShellLaunchConfigProvider shell_launch_config,
                                GitService* git_service,
                                struct GitPanelState* git_panel_state,
                                WelcomeScreenState* welcome_state,
                                std::function<void()> on_welcome_external_file,
                                std::function<void()> on_welcome_debug,
                                std::function<void()> on_welcome_workspace);

}  // namespace tgdb
