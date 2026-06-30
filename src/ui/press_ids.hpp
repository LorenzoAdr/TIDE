#pragma once

#include <string>
#include <string_view>

namespace tgdb::press_id {

constexpr std::string_view kWatchesPlay = "watches.play";
constexpr std::string_view kWatchesStop = "watches.stop";
constexpr std::string_view kConsoleTabTerminal = "console.tab.terminal";
constexpr std::string_view kConsoleTabGdb = "console.tab.gdb";
constexpr std::string_view kConsoleTabPerformance = "console.tab.performance";
constexpr std::string_view kSidebarTabOutline = "sidebar.tab.outline";
constexpr std::string_view kSidebarTabSearch = "sidebar.tab.search";
constexpr std::string_view kSidebarTabCallHierarchy = "sidebar.tab.call_hierarchy";
constexpr std::string_view kEditorProblems = "editor.problems";
constexpr std::string_view kEditorTabOverflow = "editor.tab.overflow";
constexpr std::string_view kQuitYes = "quit.yes";
constexpr std::string_view kQuitNo = "quit.no";
constexpr std::string_view kOpenFileYes = "open_file.yes";
constexpr std::string_view kOpenFileNo = "open_file.no";

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
constexpr std::string_view kSourceScrollbar = "scrollbar.source";
constexpr std::string_view kTerminalScrollbar = "scrollbar.terminal";

inline std::string explorer_row(int index) {
  return "explorer.row." + std::to_string(index);
}

inline std::string outline_row(int index) {
  return "outline.row." + std::to_string(index);
}

inline std::string context_menu_row(int index) {
  return "context_menu.row." + std::to_string(index);
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
  return id == kEditorProblems || id == kEditorTabOverflow || id.rfind("editor.tab.", 0) == 0 ||
         id.rfind("editor.tab_close.", 0) == 0;
}

inline bool is_watches_hover(std::string_view id) {
  return id == kWatchesPlay || id == kWatchesStop || id.rfind("watches.tab.", 0) == 0;
}

inline bool is_console_tab_hover(std::string_view id) {
  return id == kConsoleTabTerminal || id == kConsoleTabGdb || id == kConsoleTabPerformance;
}

inline bool is_sidebar_tab_hover(std::string_view id) {
  return id == kSidebarTabOutline || id == kSidebarTabSearch ||
         id == kSidebarTabCallHierarchy;
}

inline bool is_quit_hover(std::string_view id) {
  return id == kQuitYes || id == kQuitNo;
}

inline bool is_open_file_hover(std::string_view id) {
  return id == kOpenFileYes || id == kOpenFileNo;
}

inline bool is_explorer_hover(std::string_view id) {
  return id.rfind("explorer.row.", 0) == 0;
}

inline bool is_outline_hover(std::string_view id) {
  return id.rfind("outline.row.", 0) == 0;
}

inline bool is_context_menu_hover(std::string_view id) {
  return id.rfind("context_menu.row.", 0) == 0;
}

inline bool is_scrollbar_hover(std::string_view id) {
  return id == kEditorScrollbar || id == kSourceScrollbar || id == kTerminalScrollbar;
}

inline bool is_f2_hover(std::string_view id) {
  return id.rfind("f2.", 0) == 0;
}

inline bool is_f3_hover(std::string_view id) {
  return id.rfind("f3.", 0) == 0;
}

}  // namespace tgdb::press_id
