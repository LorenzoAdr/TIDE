#pragma once

#include <string>
#include <string_view>

namespace tuide::press_id {

constexpr std::string_view kWatchesPlay = "watches.play";
constexpr std::string_view kWatchesStop = "watches.stop";
constexpr std::string_view kWatchesNext = "watches.next";
constexpr std::string_view kWatchesStep = "watches.step";
constexpr std::string_view kWatchesClearBreakpoints = "watches.clear_breakpoints";
constexpr std::string_view kConsoleTabTerminal = "console.tab.terminal";
constexpr std::string_view kConsoleTabGdb = "console.tab.gdb";
constexpr std::string_view kConsoleTabPerformance = "console.tab.performance";
constexpr std::string_view kConsoleTabProblems = "console.tab.problems";
constexpr std::string_view kConsoleTabSearch = "console.tab.search";
constexpr std::string_view kConsoleTabCallHierarchy = "console.tab.call_hierarchy";
constexpr std::string_view kConsoleTabGit = "console.tab.git";
constexpr std::string_view kConsoleTabCoreAnalyzer = "console.tab.core_analyzer";
constexpr std::string_view kConsoleTabBinarySymbols = "console.tab.binary_symbols";
constexpr std::string_view kConsoleTabPacketMonitor = "console.tab.packet_monitor";
constexpr std::string_view kPacketMonitorRecord = "packet_monitor.record";
constexpr std::string_view kPacketMonitorSave = "packet_monitor.save";
constexpr std::string_view kSidebarHide = "sidebar.hide";
constexpr std::string_view kExplorerHide = "explorer.hide";
constexpr std::string_view kExplorerRefresh = "explorer.refresh";
constexpr std::string_view kConsoleHide = "console.hide";
constexpr std::string_view kStatusIndex = "status.index";
constexpr std::string_view kStatusChgDir = "status.chg_dir";
constexpr std::string_view kStatusLaunch = "status.launch";
constexpr std::string_view kStatusLaunchQuick = "status.launch.quick";
constexpr std::string_view kStatusDebug = "status.debug";
constexpr std::string_view kStatusDebugQuick = "status.debug.quick";
constexpr std::string_view kStatusLayout = "status.layout";
constexpr std::string_view kStatusLayoutFiles = "status.layout.files";
constexpr std::string_view kStatusLayoutOutline = "status.layout.outline";
constexpr std::string_view kStatusLayoutTerminal = "status.layout.terminal";
constexpr std::string_view kStatusSettings = "status.settings";
constexpr std::string_view kStatusShortcuts = "status.shortcuts";
constexpr std::string_view kSidebarTabOutline = "sidebar.tab.outline";
constexpr std::string_view kSidebarTabSearch = "sidebar.tab.search";
constexpr std::string_view kSidebarTabCallHierarchy = "sidebar.tab.call_hierarchy";
constexpr std::string_view kEditorProblems = "editor.problems";
constexpr std::string_view kEditorTabOverflow = "editor.tab.overflow";
constexpr std::string_view kQuitYes = "quit.yes";
constexpr std::string_view kQuitNo = "quit.no";
constexpr std::string_view kDebugLaunchCancel = "debug_launch.cancel";
constexpr std::string_view kDebugLaunchClose = "debug_launch.close";
constexpr std::string_view kShutdownForceExit = "shutdown.force_exit";
constexpr std::string_view kOpenFileYes = "open_file.yes";
constexpr std::string_view kOpenFileNo = "open_file.no";
constexpr std::string_view kLspToastInstall = "lsp_toast.install";
constexpr std::string_view kLspToastBundle = "lsp_toast.bundle";
constexpr std::string_view kLspToastIgnore = "lsp_toast.ignore";
constexpr std::string_view kWelcomeExternalFile = "welcome.external_file";
constexpr std::string_view kWelcomeDebug = "welcome.debug";
constexpr std::string_view kWelcomeWorkspace = "welcome.workspace";

constexpr std::string_view kWatchesTab0 = "watches.tab.0";
constexpr std::string_view kWatchesTab1 = "watches.tab.1";
constexpr std::string_view kWatchesTab2 = "watches.tab.2";
constexpr std::string_view kWatchesTab3 = "watches.tab.3";

inline std::string watches_tab(int index) {
  return "watches.tab." + std::to_string(index);
}

inline std::string_view watches_tab_id(int index) {
  switch (index) {
    case 0:
      return kWatchesTab0;
    case 1:
      return kWatchesTab1;
    case 2:
      return kWatchesTab2;
    case 3:
      return kWatchesTab3;
    default:
      return kWatchesTab0;
  }
}

constexpr std::string_view kEditorScrollbar = "scrollbar.editor";
constexpr std::string_view kEditorHorizontalScrollbar = "scrollbar.editor.horizontal";
constexpr std::string_view kExplorerScrollbar = "scrollbar.explorer";
constexpr std::string_view kSourceScrollbar = "scrollbar.source";
constexpr std::string_view kTerminalScrollbar = "scrollbar.terminal";
constexpr std::string_view kTerminalLink = "terminal.link";

inline std::string explorer_row(int index) {
  return "explorer.row." + std::to_string(index);
}

inline std::string outline_row(int index) {
  return "outline.row." + std::to_string(index);
}

inline std::string core_analyzer_instance(int index) {
  return "core_analyzer.instance." + std::to_string(index);
}

inline bool is_core_analyzer_hover(std::string_view id) {
  return id.rfind("core_analyzer.", 0) == 0;
}

inline std::string context_menu_row(int index) {
  return "context_menu.row." + std::to_string(index);
}

inline std::string template_picker_row(int index) {
  return "template_picker.row." + std::to_string(index);
}

inline std::string f2_mode(int index) {
  return "f2.mode." + std::to_string(index);
}

inline std::string f2_browser_row(int index) {
  return "f2.browser." + std::to_string(index);
}

inline std::string f2_process_row(int index) {
  return "f2.process." + std::to_string(index);
}

inline std::string f3_browser_row(int index) {
  return "f3.browser." + std::to_string(index);
}

inline std::string f1_browser_row(int index) {
  return "f1.browser." + std::to_string(index);
}

inline std::string editor_tab(int index) {
  return "editor.tab." + std::to_string(index);
}

inline std::string editor_tab_close(int index) {
  return "editor.tab_close." + std::to_string(index);
}

inline std::string editor_symbol(int line, int start_col, int end_col) {
  return "editor.symbol." + std::to_string(line) + "." + std::to_string(start_col) + "." +
         std::to_string(end_col);
}

inline bool is_editor_chrome_hover(std::string_view id) {
  return id == kEditorProblems || id == kEditorTabOverflow ||
         id.rfind("editor.tab_close.", 0) == 0 ||
         (id.rfind("editor.tab.", 0) == 0 && id.rfind("editor.tab_close.", 0) != 0);
}

inline bool is_watches_hover(std::string_view id) {
  return id == kWatchesPlay || id == kWatchesStop || id == kWatchesNext ||
         id == kWatchesStep || id == kWatchesClearBreakpoints ||
         id.rfind("watches.tab.", 0) == 0;
}

inline bool is_console_tab_hover(std::string_view id) {
  return id == kConsoleTabTerminal || id == kConsoleTabGdb || id == kConsoleTabPerformance ||
         id == kConsoleTabProblems || id == kConsoleTabSearch || id == kConsoleTabCallHierarchy ||
         id == kConsoleTabGit || id == kConsoleTabCoreAnalyzer ||
         id == kConsoleTabBinarySymbols || id == kConsoleTabPacketMonitor;
}

inline bool is_console_header_hover(std::string_view id) {
  return is_console_tab_hover(id) || id == kConsoleHide;
}

inline bool is_sidebar_tab_hover(std::string_view id) {
  return id == kSidebarTabOutline || id == kSidebarTabSearch ||
         id == kSidebarTabCallHierarchy;
}

inline bool is_quit_hover(std::string_view id) {
  return id == kQuitYes || id == kQuitNo;
}

inline bool is_debug_launch_hover(std::string_view id) {
  return id == kDebugLaunchCancel || id == kDebugLaunchClose;
}

inline bool is_shutdown_hover(std::string_view id) {
  return id == kShutdownForceExit;
}

inline bool is_open_file_hover(std::string_view id) {
  return id == kOpenFileYes || id == kOpenFileNo;
}

inline bool is_lsp_toast_hover(std::string_view id) {
  return id == kLspToastInstall || id == kLspToastBundle || id == kLspToastIgnore;
}

inline bool is_explorer_hover(std::string_view id) {
  return id.rfind("explorer.row.", 0) == 0;
}

inline bool is_outline_hover(std::string_view id) {
  return id.rfind("outline.row.", 0) == 0;
}

inline bool is_context_menu_hover(std::string_view id) {
  return id.rfind("context_menu.row.", 0) == 0 || id.rfind("template_picker.row.", 0) == 0;
}

inline bool is_scrollbar_hover(std::string_view id) {
  return id == kEditorScrollbar || id == kEditorHorizontalScrollbar ||
         id == kExplorerScrollbar || id == kSourceScrollbar || id == kTerminalScrollbar ||
         id == kTerminalLink;
}

inline bool is_f2_hover(std::string_view id) {
  return id.rfind("f2.", 0) == 0;
}

inline std::string call_hierarchy_seg(int node_index) {
  return "call_hierarchy.seg." + std::to_string(node_index);
}

inline bool is_call_hierarchy_hover(std::string_view id) {
  return id.rfind("call_hierarchy.seg.", 0) == 0;
}

inline bool is_f3_hover(std::string_view id) {
  return id.rfind("f3.", 0) == 0;
}

inline bool is_f1_hover(std::string_view id) {
  return id.rfind("f1.", 0) == 0;
}

inline bool is_welcome_hover(std::string_view id) {
  return id == kWelcomeExternalFile || id == kWelcomeDebug || id == kWelcomeWorkspace;
}

inline bool is_packet_monitor_hover(std::string_view id) {
  return id == kPacketMonitorRecord || id == kPacketMonitorSave;
}

}  // namespace tuide::press_id
