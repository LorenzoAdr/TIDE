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
#include "editor/helix/helix_state.hpp"
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
#include "ui/status_layout_popover.hpp"
#include "git/git_service.hpp"
#include "util/system_stats.hpp"
#include "util/ui_activity_gate.hpp"
#include "util/ui_panel_render_cache.hpp"
#include "util/ui_perf_monitor.hpp"
#include "ui/mouse_velocity_tracker.hpp"
#include "ui/source_panel.hpp"
#include "util/clang_format_config.hpp"
#include "util/nm_reader.hpp"
#include "util/path_normalize.hpp"

namespace tgdb {

class UiEventDispatcher;

namespace packet_monitor {
class PacketMonitorService;
}

struct WelcomeScreenState;

using CommandCallback = std::function<void(const struct UiCommand&)>;

enum class TextInputFocus {
  None,
  Console,
  Watch,
  WatchInject,
  BreakpointHw,
  EditorFind,
  EditorGotoLine,
  EditorCompletion,
  SearchQuery,
  SearchReplace,
  SearchPath,
  SearchInclude,
  SearchExclude,
  BinarySymbolsFilter,
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

struct EditorPanelHandlers {
  std::function<bool(const ftxui::Event&)> key_handler;
  std::function<bool(const ftxui::Event&)> mouse_handler;
  std::function<bool(const ftxui::Event&)> chrome_mouse_handler;
  std::function<void(ftxui::Event&)> modifier_handler;
  std::function<void()> tick_callback;
  std::function<int()> visible_line_count;
};

struct ConsolePanelTabs {
  static constexpr int kTerminal = 0;
  static constexpr int kDebug = 1;
  static constexpr int kPerformance = 2;
  static constexpr int kProblems = 3;
  static constexpr int kSearch = 4;
  static constexpr int kCallHierarchy = 5;
  static constexpr int kGit = 6;
  static constexpr int kCoreAnalyzer = 7;
  static constexpr int kBinarySymbols = 8;
  static constexpr int kPacketMonitor = 9;
  int selected_tab = kTerminal;
};

struct BinarySymbolsPending {
  bool open_tab = false;
  bool refresh = false;
  uint64_t start_after_paint = 0;
  std::string binary_path;
  std::string name_filter;
  NmBindingFilter binding_filter = NmBindingFilter::kAll;
  std::string select_symbol_name;
};

struct HelixIdeCallbacks {
  std::function<void()> open_quick_file;
  std::function<void()> open_symbol_picker;
  std::function<void()> save_file;
  std::function<void()> request_quit;
};

struct MainLayoutState {
  MainLayoutState();
  ~MainLayoutState();
  MainLayoutState(const MainLayoutState&) = delete;
  MainLayoutState& operator=(const MainLayoutState&) = delete;
  MainLayoutState(MainLayoutState&&) = default;
  MainLayoutState& operator=(MainLayoutState&&) = default;

  bool console_visible = true;
  bool explorer_visible = true;
  bool welcome_visible = false;
  bool diagnostics_panel_visible = false;
  int diagnostics_panel_height = 6;
  bool terminal_start_requested = true;
  UiEventDispatcher* ui_events = nullptr;
  uint64_t last_editor_correlation_id = 0;
  std::atomic<bool> custom_event_pending{false};
  std::atomic<bool> terminal_sync_after_draw{false};
  std::atomic<bool> shutdown_ui_poll_paused{false};
  std::atomic<bool> ui_heartbeat{false};
  std::atomic<bool> terminal_minimal_wake{false};
  std::atomic<bool> debug_critical_wake{false};
  std::atomic<uint64_t> ui_custom_tick{0};
  std::atomic<uint64_t> ui_paint_count{0};
  std::atomic<uint64_t> ui_lsp_request_count{0};
  uint64_t panel_cache_git_revision = 0;
  uint64_t panel_cache_diagnostics_revision = 0;
  uint64_t panel_cache_symbols_revision = 0;
  std::string panel_cache_outline_path;
  int panel_cache_terminal_w = 0;
  int panel_cache_terminal_h = 0;
  UiPanelRenderCache panel_render_cache;
  UiActivityGate activity_gate;
  UiPerfMonitor ui_perf_monitor;
  MouseVelocityTracker mouse_velocity;
  HelixStatusSnapshot helix_status;
  bool editor_helix_prefix_pending = false;
  std::function<void()> reset_helix_editors;
  HelixIdeCallbacks helix_ide;
  AppSettings* app_settings = nullptr;
  ClangFormatConfig* workspace_clang_format = nullptr;
  std::function<void()> apply_app_settings_callback;
  ConsolePanelTabs console_tabs;
  BinarySymbolsPending binary_symbols_pending;
  enum class CoreAnalyzerFocus { kCommand, kSearch, kInstances };
  CoreAnalyzerFocus core_analyzer_focus = CoreAnalyzerFocus::kCommand;
  bool show_core_analyzer_tab = false;
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
  struct PendingSymbolInfoRequest {
    bool active = false;
    int line = -1;
    int col = -1;
    int anchor_x = 0;
    int anchor_y = 0;
  };
  PendingSymbolInfoRequest pending_symbol_info;
  EditorPanelHandlers primary_editor;
  EditorPanelHandlers secondary_editor;
  std::function<bool(const ftxui::Event&)> source_mouse_handler;
  std::function<bool(const ftxui::Event&)> source_key_handler;
  std::function<bool(const ftxui::Event&)> watches_mouse_handler;
  std::function<bool(const ftxui::Event&)> watches_key_handler;
  std::function<bool(const ftxui::Event&)> console_debug_mouse_handler;
  std::function<bool(const ftxui::Event&)> explorer_mouse_handler;
  std::function<bool(const ftxui::Event&)> sidebar_mouse_handler;
  std::function<bool(const ftxui::Event&)> outline_mouse_handler;
  std::function<bool(const ftxui::Event&)> status_bar_mouse_handler;
  std::function<void()> status_open_settings;
  std::function<void()> status_open_shortcuts;
  std::function<void()> status_reindex_project;
  std::function<void()> status_open_source_substitute;
  std::function<void()> status_open_launch;
  std::function<void()> status_quick_launch;
  std::function<void()> status_open_debug;
  std::function<void()> status_quick_debug;
  std::function<void(bool visible)> status_set_files_visible;
  std::function<void(bool visible)> status_set_outline_visible;
  std::function<void(bool visible)> status_set_terminal_visible;
  StatusLayoutPopoverState status_layout_popover;
  std::function<void()> outline_tick_callback;
  std::function<void()> source_tick_callback;
  std::function<bool(const ftxui::Event&)> console_key_handler;
  std::function<bool(const ftxui::Event&)> console_mouse_handler;
  std::function<bool(const ftxui::Event&)> split_mouse_handler;
  std::function<bool(const ftxui::Event&)> search_key_handler;
  std::function<bool(const ftxui::Event&)> call_hierarchy_key_handler;
  std::function<bool(const ftxui::Event&)> problems_key_handler;
  std::function<bool(const ftxui::Event&)> git_key_handler;
  std::function<bool(const ftxui::Event&)> git_mouse_handler;
  std::function<bool(const ftxui::Event&)> core_analyzer_key_handler;
  std::function<bool(const ftxui::Event&)> binary_symbols_key_handler;
  std::function<bool(const ftxui::Event&)> packet_monitor_key_handler;
  std::function<bool(const ftxui::Event&)> packet_monitor_mouse_handler;
  std::function<bool(ftxui::Event&)> welcome_key_handler;
  std::function<bool(ftxui::Event&)> welcome_mouse_handler;
  bool editor_completion_open = false;
  bool editor_ctrl_modifier_held = false;
  std::function<void()> terminal_tick_callback;
  std::function<void()> terminal_follow_input_callback;
  std::function<int()> terminal_width;
  std::function<int()> terminal_height;
  std::function<void(const std::string& path)> on_file_saved;
  PerformanceSampler performance_sampler;
  std::unique_ptr<packet_monitor::PacketMonitorService> packet_monitor_service;
  std::function<void()> packet_monitor_tick_callback;
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

inline bool git_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kGit;
}

inline bool core_analyzer_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->show_core_analyzer_tab &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kCoreAnalyzer;
}

inline bool binary_symbols_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kBinarySymbols;
}

inline bool packet_monitor_tab_active(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kPacketMonitor;
}

inline bool binary_symbols_request_pending(const MainLayoutState* layout_state) {
  return layout_state != nullptr &&
         !layout_state->binary_symbols_pending.binary_path.empty();
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

inline bool is_binary_symbols_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::BinarySymbolsFilter;
}

inline bool is_watch_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::Watch || focus == TextInputFocus::WatchInject ||
         focus == TextInputFocus::BreakpointHw;
}

inline bool is_editor_chrome_input_focus(TextInputFocus focus) {
  return focus == TextInputFocus::EditorFind || focus == TextInputFocus::EditorGotoLine ||
         focus == TextInputFocus::EditorCompletion;
}

using EditorNavigateCallback = std::function<void(const SourceLocation&)>;

inline void invalidate_editor_view(MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return;
  }
  layout_state->focus_sync_needed = true;
  layout_state->panel_render_cache.mark_dirty(UiPanelId::EditorCenter);
  layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
}

void apply_editor_navigation(MainLayoutState* layout_state, const SourceLocation& loc,
                             EditorNavigateCallback navigate);

void schedule_editor_navigation(MainLayoutState* layout_state, const SourceLocation& loc);

bool tick_pending_editor_navigation(
    MainLayoutState* layout_state,
    const std::function<void(const SourceLocation&)>& navigate);

void request_symbol_info_at(MainLayoutState* layout_state, int line, int col, int anchor_x,
                            int anchor_y);

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

inline EditorPanelHandlers& editor_handlers_for(MainLayoutState* layout_state,
                                                FocusRegion region) {
  if (layout_state != nullptr && region == FocusRegion::SecondaryEditor) {
    return layout_state->secondary_editor;
  }
  if (layout_state != nullptr) {
    return layout_state->primary_editor;
  }
  static EditorPanelHandlers empty;
  return empty;
}

inline const EditorPanelHandlers& editor_handlers_for(const MainLayoutState* layout_state,
                                                      FocusRegion region) {
  return editor_handlers_for(const_cast<MainLayoutState*>(layout_state), region);
}

ftxui::Component MakeMainLayout(AppMode* app_mode, DebugModel* model,
                                WorkspaceModel* workspace, WorkspaceModel* secondary_workspace,
                                SourceViewState* source_state,
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
