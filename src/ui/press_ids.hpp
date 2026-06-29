#pragma once

#include <string>
#include <string_view>

namespace tgdb::press_id {

constexpr std::string_view kWatchesPlay = "watches.play";
constexpr std::string_view kWatchesStop = "watches.stop";
constexpr std::string_view kConsoleTabTerminal = "console.tab.terminal";
constexpr std::string_view kConsoleTabGdb = "console.tab.gdb";
constexpr std::string_view kSidebarTabOutline = "sidebar.tab.outline";
constexpr std::string_view kSidebarTabSearch = "sidebar.tab.search";
constexpr std::string_view kEditorProblems = "editor.problems";
constexpr std::string_view kEditorTabOverflow = "editor.tab.overflow";
constexpr std::string_view kQuitYes = "quit.yes";
constexpr std::string_view kQuitNo = "quit.no";

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

inline std::string editor_tab(int index) {
  return "editor.tab." + std::to_string(index);
}

inline std::string editor_tab_close(int index) {
  return "editor.tab_close." + std::to_string(index);
}

inline bool is_editor_chrome_hover(std::string_view id) {
  return id == kEditorProblems || id == kEditorTabOverflow || id.rfind("editor.tab.", 0) == 0 ||
         id.rfind("editor.tab_close.", 0) == 0;
}

inline bool is_watches_hover(std::string_view id) {
  return id == kWatchesPlay || id == kWatchesStop || id.rfind("watches.tab.", 0) == 0;
}

inline bool is_console_tab_hover(std::string_view id) {
  return id == kConsoleTabTerminal || id == kConsoleTabGdb;
}

inline bool is_sidebar_tab_hover(std::string_view id) {
  return id == kSidebarTabOutline || id == kSidebarTabSearch;
}

inline bool is_quit_hover(std::string_view id) {
  return id == kQuitYes || id == kQuitNo;
}

}  // namespace tgdb::press_id
