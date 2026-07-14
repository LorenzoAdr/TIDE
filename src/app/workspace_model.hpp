#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "app/editor_tabs.hpp"
#include "editor/cursor_history.hpp"
#include "editor/editor_state.hpp"

namespace tgdb {

struct OpenFileConfirmState;

struct WorkspaceModel {
  std::string root;
  std::string active_file;
  EditorBuffer buffer;
  CursorHistory cursor_history;
  int64_t last_buffer_edit_ms = 0;
  std::vector<EditorTab> tabs;
  std::vector<std::string> tab_mru;
  int active_tab = -1;
  std::string status_message;
  OpenFileConfirmState* open_file_confirm = nullptr;
  using UiTask = std::function<void()>;
  std::function<void(UiTask)> enqueue_ui_task;

  std::vector<std::string> open_tabs_mru() const;
  std::vector<std::string> open_tabs_mru_excluding_active() const;

  void flush_active_tab();
  void load_active_tab_into_buffer();

  bool open_file(const std::string& absolute_path);
  bool open_external_file(const std::string& absolute_path);
  bool open_file_at(const std::string& absolute_path, int line, int col);
  bool open_file_confirmed(const std::string& absolute_path);
  bool open_file_at_confirmed(const std::string& absolute_path, int line, int col);
  void switch_to_tab(int index);
  bool close_tab(int index);
  void move_tab(int from, int to);
  int find_tab(const std::string& absolute_path) const;

  bool load_file(const std::string& absolute_path);
  bool save_buffer();
  void open_relative(const std::string& relative_path);
  void ensure_buffer();
  void clear_tabs();
  std::vector<std::string> dirty_open_paths() const;
  void record_cursor_jump();
  bool navigate_cursor_back(int visible_lines);
  bool navigate_cursor_forward(int visible_lines);

  bool active_tab_read_only() const;
  bool open_git_head_tab(const std::string& absolute_path,
                         const std::vector<std::string>& head_lines);

 private:
  struct PendingOpenAt {
    int line = 0;
    int col = 0;
    bool active = false;
  };

  bool open_file_impl(const std::string& absolute_path);
  bool open_file_at_impl(const std::string& absolute_path, int line, int col);
  bool check_open_guard(const std::string& absolute_path);
  bool try_open_external_pdf(const std::string& absolute_path);

  static bool load_buffer_from_disk(EditorBuffer* buffer, const std::string& absolute_path);
  static bool load_buffer_from_lines(EditorBuffer* buffer, const std::string& absolute_path,
                                     const std::vector<std::string>& lines);
  static std::string normalize_path(const std::string& path);
  static bool is_path_in_workspace(const std::string& workspace_root,
                                   const std::string& absolute_path);
  int open_new_tab_from_disk(const std::string& absolute_path, bool external);
  void touch_tab_mru(const std::string& absolute_path);
  void remove_tab_mru(const std::string& absolute_path);

  PendingOpenAt pending_open_at_;
};

}  // namespace tgdb
