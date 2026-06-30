#include "app/workspace_model.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>

#include "editor/undo_stack.hpp"
#include "ui/open_file_confirm.hpp"
#include "util/file_open_policy.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

void set_welcome_buffer(EditorBuffer* buffer) {
  buffer->lines.clear();
  buffer->path.clear();
  buffer->reset_to_single_cursor(0, 0);
  buffer->scroll = 0;
  buffer->dirty = false;
  buffer->lines.push_back("Abre un archivo del explorador o Ctrl+P.");
  clear_undo(buffer);
}

}  // namespace

std::string WorkspaceModel::normalize_path(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  return tgdb::normalize_path(path);
}

bool WorkspaceModel::load_buffer_from_disk(EditorBuffer* buffer,
                                           const std::string& absolute_path) {
  if (buffer == nullptr) {
    return false;
  }
  buffer->lines.clear();
  buffer->path = absolute_path;
  buffer->reset_to_single_cursor(0, 0);
  buffer->scroll = 0;
  buffer->dirty = false;
  clear_undo(buffer);

  if (absolute_path.empty()) {
    buffer->lines.push_back("Abre un archivo del explorador o Ctrl+P.");
    return false;
  }

  std::ifstream input(absolute_path);
  if (!input) {
    buffer->lines.push_back("No se pudo abrir: " + absolute_path);
    return false;
  }

  std::string line;
  while (std::getline(input, line)) {
    buffer->lines.push_back(line);
  }
  if (buffer->lines.empty()) {
    buffer->lines.push_back("");
  }
  buffer->view_token++;
  return true;
}

void WorkspaceModel::flush_active_tab() {
  if (active_tab < 0 || active_tab >= static_cast<int>(tabs.size())) {
    return;
  }
  tabs[static_cast<std::size_t>(active_tab)].buffer = buffer;
}

void WorkspaceModel::load_active_tab_into_buffer() {
  if (active_tab < 0 || active_tab >= static_cast<int>(tabs.size())) {
    set_welcome_buffer(&buffer);
    active_file.clear();
    cursor_history.clear();
    buffer.view_token++;
    return;
  }
  const EditorTab& tab = tabs[static_cast<std::size_t>(active_tab)];
  buffer = tab.buffer;
  active_file = tab.path;
  buffer.view_token++;
}

void WorkspaceModel::clear_tabs() {
  tabs.clear();
  tab_mru.clear();
  active_tab = -1;
  set_welcome_buffer(&buffer);
  active_file.clear();
  cursor_history.clear();
}

void WorkspaceModel::touch_tab_mru(const std::string& absolute_path) {
  if (absolute_path.empty()) {
    return;
  }
  const std::string path = normalize_path(absolute_path);
  tab_mru.erase(std::remove(tab_mru.begin(), tab_mru.end(), path), tab_mru.end());
  tab_mru.insert(tab_mru.begin(), path);
}

void WorkspaceModel::remove_tab_mru(const std::string& absolute_path) {
  if (absolute_path.empty()) {
    return;
  }
  const std::string path = normalize_path(absolute_path);
  tab_mru.erase(std::remove(tab_mru.begin(), tab_mru.end(), path), tab_mru.end());
}

std::vector<std::string> WorkspaceModel::open_tabs_mru_excluding_active() const {
  std::vector<std::string> result;
  const std::string current = normalize_path(active_file);
  for (const std::string& path : tab_mru) {
    if (path != current) {
      result.push_back(path);
    }
  }
  for (const EditorTab& tab : tabs) {
    const std::string path = normalize_path(tab.path);
    if (path == current) {
      continue;
    }
    if (std::find(result.begin(), result.end(), path) == result.end()) {
      result.push_back(path);
    }
  }
  return result;
}

int WorkspaceModel::open_new_tab_from_disk(const std::string& absolute_path) {
  EditorTab tab;
  tab.path = absolute_path;
  load_buffer_from_disk(&tab.buffer, absolute_path);
  tabs.push_back(std::move(tab));
  return static_cast<int>(tabs.size()) - 1;
}

bool WorkspaceModel::check_open_guard(const std::string& absolute_path) {
  if (open_file_confirm == nullptr) {
    return true;
  }

  const FileOpenAssessment assessment = assess_file_open(absolute_path);
  if (assessment.kind == FileOpenKind::Binary) {
    const std::string name = fs::path(absolute_path).filename().string();
    open_file_confirm->show_binary_warning(absolute_path, name);
    status_message = "Archivo binario, no se puede abrir: " + name;
    return false;
  }
  if (assessment.kind == FileOpenKind::Large) {
    open_file_confirm->request_large_confirm(absolute_path, assessment.size_bytes);
    if (pending_open_at_.active) {
      open_file_confirm->line = pending_open_at_.line;
      open_file_confirm->col = pending_open_at_.col;
      open_file_confirm->has_position = true;
    }
    return false;
  }
  return true;
}

bool WorkspaceModel::open_file_impl(const std::string& absolute_path) {
  if (absolute_path.empty()) {
    return false;
  }
  flush_active_tab();
  const std::string path = normalize_path(absolute_path);
  const int index = open_new_tab_from_disk(path);
  switch_to_tab(index);
  return true;
}

bool WorkspaceModel::open_file(const std::string& absolute_path) {
  if (absolute_path.empty()) {
    return false;
  }
  flush_active_tab();
  const std::string path = normalize_path(absolute_path);
  const int existing = find_tab(path);
  if (existing >= 0) {
    switch_to_tab(existing);
    return true;
  }
  if (!check_open_guard(path)) {
    return false;
  }
  return open_file_impl(absolute_path);
}

bool WorkspaceModel::open_file_confirmed(const std::string& absolute_path) {
  return open_file_impl(absolute_path);
}

bool WorkspaceModel::open_file_at_impl(const std::string& absolute_path, int line, int col) {
  if (!open_file_impl(absolute_path)) {
    return false;
  }
  buffer.reset_to_single_cursor(line, col);
  buffer.scroll = std::max(0, line - 2);
  buffer.view_token++;
  flush_active_tab();
  return true;
}

bool WorkspaceModel::open_file_at_confirmed(const std::string& absolute_path, int line, int col) {
  return open_file_at_impl(absolute_path, line, col);
}

bool WorkspaceModel::open_file_at(const std::string& absolute_path, int line, int col) {
  pending_open_at_ = {line, col, true};
  const bool opened = open_file(absolute_path);
  pending_open_at_.active = false;
  if (!opened) {
    return false;
  }
  buffer.reset_to_single_cursor(line, col);
  buffer.scroll = std::max(0, line - 2);
  buffer.view_token++;
  flush_active_tab();
  return true;
}

void WorkspaceModel::switch_to_tab(int index) {
  if (index < 0 || index >= static_cast<int>(tabs.size())) {
    return;
  }
  if (index == active_tab) {
    return;
  }
  flush_active_tab();
  active_tab = index;
  load_active_tab_into_buffer();
  touch_tab_mru(active_file);
}

bool WorkspaceModel::close_tab(int index) {
  if (index < 0 || index >= static_cast<int>(tabs.size())) {
    return false;
  }
  flush_active_tab();
  const std::string closed_path = tabs[static_cast<std::size_t>(index)].path;
  tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(index));
  remove_tab_mru(closed_path);
  if (tabs.empty()) {
    active_tab = -1;
    load_active_tab_into_buffer();
    return true;
  }
  if (active_tab == index) {
    active_tab = std::min(index, static_cast<int>(tabs.size()) - 1);
    load_active_tab_into_buffer();
    return true;
  }
  if (active_tab > index) {
    --active_tab;
  }
  return true;
}

void WorkspaceModel::move_tab(int from, int to) {
  if (from < 0 || from >= static_cast<int>(tabs.size()) || to < 0 ||
      to >= static_cast<int>(tabs.size()) || from == to) {
    return;
  }
  flush_active_tab();
  EditorTab tab = std::move(tabs[static_cast<std::size_t>(from)]);
  tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(from));
  if (to > from) {
    --to;
  }
  tabs.insert(tabs.begin() + static_cast<std::ptrdiff_t>(to), std::move(tab));
  if (active_tab == from) {
    active_tab = to;
  } else if (from < active_tab && to >= active_tab) {
    --active_tab;
  } else if (from > active_tab && to <= active_tab) {
    ++active_tab;
  }
  load_active_tab_into_buffer();
}

int WorkspaceModel::find_tab(const std::string& absolute_path) const {
  if (absolute_path.empty()) {
    return -1;
  }
  const std::string path = normalize_path(absolute_path);
  for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
    if (normalize_path(tabs[static_cast<std::size_t>(i)].path) == path) {
      return i;
    }
  }
  return -1;
}

bool WorkspaceModel::load_file(const std::string& absolute_path) {
  return open_file(absolute_path);
}

bool WorkspaceModel::save_buffer() {
  if (buffer.path.empty() || buffer.lines.empty()) {
    return false;
  }
  std::ofstream output(buffer.path, std::ios::trunc);
  if (!output) {
    return false;
  }
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    output << buffer.lines[i];
    if (i + 1 < buffer.lines.size()) {
      output << '\n';
    }
  }
  buffer.dirty = false;
  flush_active_tab();
  status_message = "Guardado: " + fs::path(buffer.path).filename().string();
  return true;
}

void WorkspaceModel::open_relative(const std::string& relative_path) {
  if (root.empty() || relative_path.empty()) {
    return;
  }
  std::error_code ec;
  const auto absolute =
      fs::weakly_canonical(fs::path(root) / relative_path, ec);
  if (ec || !fs::is_regular_file(absolute, ec)) {
    return;
  }
  open_file(absolute.string());
}

void WorkspaceModel::ensure_buffer() {
  if (active_tab < 0) {
    if (buffer.lines.empty()) {
      set_welcome_buffer(&buffer);
    }
    buffer.ensure_cursors();
    return;
  }
  buffer.ensure_cursors();
}

void WorkspaceModel::record_cursor_jump() {
  cursor_history.record_jump(this);
}

bool WorkspaceModel::navigate_cursor_back(int visible_lines) {
  return cursor_history.go_back(this, visible_lines);
}

bool WorkspaceModel::navigate_cursor_forward(int visible_lines) {
  return cursor_history.go_forward(this, visible_lines);
}

}  // namespace tgdb
