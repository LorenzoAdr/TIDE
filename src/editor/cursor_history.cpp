#include "editor/cursor_history.hpp"

#include "app/workspace_model.hpp"
#include "editor/text_ops.hpp"

namespace tgdb {

namespace {

constexpr std::size_t kMaxEntries = 200;

}  // namespace

CursorHistoryEntry CursorHistory::current_entry(WorkspaceModel& workspace) const {
  workspace.buffer.ensure_cursors();
  CursorHistoryEntry entry;
  entry.path =
      workspace.buffer.path.empty() ? workspace.active_file : workspace.buffer.path;
  entry.line = workspace.buffer.primary_line();
  entry.col = workspace.buffer.primary_col();
  return entry;
}

void CursorHistory::apply_entry(WorkspaceModel* workspace, const CursorHistoryEntry& entry,
                                int visible_lines) {
  if (workspace == nullptr) {
    return;
  }
  const std::string current_path = workspace->buffer.path.empty() ? workspace->active_file
                                                                  : workspace->buffer.path;
  if (!entry.path.empty() && entry.path != current_path) {
    workspace->open_file_at(entry.path, entry.line, entry.col);
    return;
  }
  workspace->buffer.reset_to_single_cursor(entry.line, entry.col);
  workspace->buffer.scroll = std::max(0, entry.line - 2);
  ensure_scroll_visible(&workspace->buffer, visible_lines);
  workspace->buffer.view_token++;
}

void CursorHistory::record_jump(WorkspaceModel* workspace) {
  if (workspace == nullptr || navigating_) {
    return;
  }
  const CursorHistoryEntry entry = current_entry(*workspace);
  if (entry.path.empty()) {
    return;
  }
  if (!back_.empty() && back_.back() == entry) {
    return;
  }
  back_.push_back(entry);
  if (back_.size() > kMaxEntries) {
    back_.erase(back_.begin());
  }
  forward_.clear();
}

bool CursorHistory::go_back(WorkspaceModel* workspace, int visible_lines) {
  if (workspace == nullptr || back_.empty()) {
    return false;
  }
  navigating_ = true;
  forward_.push_back(current_entry(*workspace));
  const CursorHistoryEntry destination = back_.back();
  back_.pop_back();
  apply_entry(workspace, destination, visible_lines);
  navigating_ = false;
  return true;
}

bool CursorHistory::go_forward(WorkspaceModel* workspace, int visible_lines) {
  if (workspace == nullptr || forward_.empty()) {
    return false;
  }
  navigating_ = true;
  back_.push_back(current_entry(*workspace));
  const CursorHistoryEntry destination = forward_.back();
  forward_.pop_back();
  apply_entry(workspace, destination, visible_lines);
  navigating_ = false;
  return true;
}

void CursorHistory::clear() {
  back_.clear();
  forward_.clear();
}

}  // namespace tgdb
