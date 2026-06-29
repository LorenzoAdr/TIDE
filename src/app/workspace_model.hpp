#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/editor_tabs.hpp"
#include "editor/cursor_history.hpp"
#include "editor/editor_state.hpp"

namespace tgdb {

struct WorkspaceModel {
  std::string root;
  std::string active_file;
  EditorBuffer buffer;
  CursorHistory cursor_history;
  std::vector<EditorTab> tabs;
  std::vector<std::string> tab_mru;
  int active_tab = -1;
  std::string status_message = "Selecciona un workspace";

  std::vector<std::string> open_tabs_mru_excluding_active() const;

  void flush_active_tab();
  void load_active_tab_into_buffer();

  bool open_file(const std::string& absolute_path);
  bool open_file_at(const std::string& absolute_path, int line, int col);
  void switch_to_tab(int index);
  bool close_tab(int index);
  void move_tab(int from, int to);
  int find_tab(const std::string& absolute_path) const;

  bool load_file(const std::string& absolute_path);
  bool save_buffer();
  void open_relative(const std::string& relative_path);
  void ensure_buffer();
  void clear_tabs();
  void record_cursor_jump();
  bool navigate_cursor_back(int visible_lines);
  bool navigate_cursor_forward(int visible_lines);

 private:
  static bool load_buffer_from_disk(EditorBuffer* buffer, const std::string& absolute_path);
  static std::string normalize_path(const std::string& path);
  int open_new_tab_from_disk(const std::string& absolute_path);
  void touch_tab_mru(const std::string& absolute_path);
  void remove_tab_mru(const std::string& absolute_path);
};

}  // namespace tgdb
