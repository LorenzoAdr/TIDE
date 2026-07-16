#include "ui/file_tree_panel.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <system_error>
#include <vector>

#include "git/git_service.hpp"
#include "git/git_status.hpp"
#include "indexer/index_rules.hpp"
#include "util/nm_reader.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focusable_component.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/context_menu.hpp"
#include "ui/focus_manager.hpp"
#include "ui/glyphs.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"
#include "util/filesystem_tree.hpp"
#include "util/path_normalize.hpp"
#include "util/ui_panel_render_cache.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

struct FlatEntry {
  int depth = 0;
  std::string label;
  std::string relative_path;
  bool is_file = false;
  FileTreeNode* folder = nullptr;
};

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int max_scroll_offset(int total_lines, int visible_lines) {
  return std::max(0, total_lines - visible_lines);
}

void merge_unique_folder_paths(std::vector<std::string>* target,
                               const std::vector<std::string>& extra) {
  if (target == nullptr || extra.empty()) {
    return;
  }
  target->insert(target->end(), extra.begin(), extra.end());
  std::sort(target->begin(), target->end());
  target->erase(std::unique(target->begin(), target->end()), target->end());
}

struct GitExplorerMarks {
  std::unordered_map<std::string, GitStatusEntry> files;
  std::unordered_set<std::string> dirty_folders;
};

GitExplorerMarks build_git_explorer_marks(const GitService* git) {
  GitExplorerMarks marks;
  if (git == nullptr || !git->is_repo()) {
    return marks;
  }
  for (const GitStatusEntry& entry : git->status().entries) {
    marks.files[entry.path] = entry;
    std::string prefix;
    for (char ch : entry.path) {
      if (ch == '/') {
        marks.dirty_folders.insert(prefix);
      }
      prefix.push_back(ch);
    }
    const auto slash = entry.path.find_last_of('/');
    if (slash != std::string::npos) {
      marks.dirty_folders.insert(entry.path.substr(0, slash));
    }
  }
  return marks;
}

std::string git_status_badge(const GitStatusEntry& entry) {
  std::string badge;
  if (entry.staged != GitFileStatus::kUnmodified) {
    switch (entry.staged) {
      case GitFileStatus::kModified:
        badge += "M";
        break;
      case GitFileStatus::kAdded:
        badge += "A";
        break;
      case GitFileStatus::kDeleted:
        badge += "D";
        break;
      case GitFileStatus::kRenamed:
        badge += "R";
        break;
      default:
        badge += "?";
        break;
    }
  }
  if (entry.unstaged != GitFileStatus::kUnmodified) {
    switch (entry.unstaged) {
      case GitFileStatus::kModified:
        badge += "M";
        break;
      case GitFileStatus::kAdded:
        badge += "A";
        break;
      case GitFileStatus::kDeleted:
        badge += "D";
        break;
      case GitFileStatus::kUntracked:
        badge += "?";
        break;
      default:
        badge += ".";
        break;
    }
  }
  return badge;
}

Color git_status_color(const GitStatusEntry& entry) {
  if (entry.unstaged == GitFileStatus::kUntracked) {
    return theme::Warning();
  }
  if (entry.staged != GitFileStatus::kUnmodified) {
    return theme::Success();
  }
  return theme::Accent();
}

std::optional<Color> file_git_status_dot(const GitStatusEntry& entry) {
  if (entry.unstaged == GitFileStatus::kUntracked) {
    return theme::Warning();
  }
  if (entry.staged != GitFileStatus::kUnmodified ||
      (entry.unstaged != GitFileStatus::kUnmodified &&
       entry.unstaged != GitFileStatus::kUntracked)) {
    return theme::Success();
  }
  return std::nullopt;
}

std::string relative_path_in_workspace(const std::string& workspace_root,
                                       const std::string& absolute_path) {
  if (workspace_root.empty() || absolute_path.empty()) {
    return {};
  }
  std::error_code ec;
  const auto rel = fs::relative(fs::path(absolute_path), fs::path(workspace_root), ec);
  if (ec) {
    return {};
  }
  return rel.generic_string();
}

void invalidate_file_tree_panel(MainLayoutState* layout_state);

struct FileTreePanelState {
  FileTreeNode root;
  std::vector<FlatEntry> flat;
  std::vector<std::string> indexed_files;
  std::vector<std::string> indexed_skeleton_folders;
  std::vector<std::string> indexed_folders;
  int selected = 0;
  int list_scroll = 0;
  std::string loaded_workspace;
  std::string last_revealed_path;
  Box panel_box;
  Box hide_box;
  Box refresh_box;
  Box content_box;
  Box scrollbar_box;
  ScrollbarLayout scrollbar_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
  int last_visible_lines = 1;

  void rebuild_flat() {
    flat.clear();
    std::function<void(FileTreeNode&, int, const std::string&)> walk;
    walk = [&](FileTreeNode& node, int depth, const std::string& parent_path) {
      for (auto& child : node.children) {
        const std::string child_path =
            parent_path.empty() ? child.name : parent_path + "/" + child.name;
        if (child.is_file) {
          flat.push_back({depth, child.name, child.relative_path, true, nullptr});
        } else {
          flat.push_back({depth, child.name, child_path, false, &child});
          if (child.expanded) {
            walk(child, depth + 1, child_path);
          }
        }
      }
    };
    walk(root, 0, "");
    if (selected >= static_cast<int>(flat.size())) {
      selected = std::max(0, static_cast<int>(flat.size()) - 1);
    }
    clamp_scroll();
  }

  void clamp_scroll() {
    const int visible = visible_line_count(content_box);
    list_scroll = std::max(0, std::min(list_scroll, max_scroll_offset(static_cast<int>(flat.size()),
                                                                      visible)));
  }

  void scroll_row_into_view(int row, MainLayoutState* layout_state = nullptr) {
    const int before = list_scroll;
    const int visible = visible_line_count(content_box);
    if (row < list_scroll) {
      list_scroll = row;
    } else if (row >= list_scroll + visible) {
      list_scroll = row - visible + 1;
    }
    clamp_scroll();
    if (layout_state != nullptr && list_scroll != before) {
      invalidate_file_tree_panel(layout_state);
    }
  }

  void center_row(int row) {
    const int visible = visible_line_count(content_box);
    list_scroll = std::max(
        0, std::min(row - visible / 2,
                    max_scroll_offset(static_cast<int>(flat.size()), visible)));
    clamp_scroll();
  }

  bool reveal_file(const std::string& workspace_root, const std::string& absolute_path) {
    if (workspace_root.empty() || absolute_path.empty()) {
      return false;
    }
    const std::string normalized = normalize_path(absolute_path);
    const std::string rel = relative_path_in_workspace(workspace_root, normalized);
    if (rel.empty() || !expand_relative_path(&root, rel)) {
      return false;
    }
    rebuild_flat();
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
      if (flat[static_cast<std::size_t>(i)].is_file &&
          flat[static_cast<std::size_t>(i)].relative_path == rel) {
        selected = i;
        center_row(i);
        last_revealed_path = normalized;
        return true;
      }
    }
    return false;
  }

  void sync_index(const std::shared_ptr<const IndexSnapshot>& snapshot,
                  const std::string& workspace_root) {
    if (workspace_root.empty()) {
      if (loaded_workspace.empty() && indexed_files.empty()) {
        return;
      }
      loaded_workspace.clear();
      indexed_files.clear();
      indexed_skeleton_folders.clear();
      indexed_folders.clear();
      root = FileTreeNode{};
      root.expanded = false;
      selected = 0;
      list_scroll = 0;
      last_revealed_path.clear();
      rebuild_flat();
      return;
    }

    if (!snapshot || snapshot->workspace_root != workspace_root) {
      return;
    }

    if (loaded_workspace == workspace_root && indexed_files == snapshot->files &&
        indexed_skeleton_folders == snapshot->skeleton_folders &&
        indexed_folders == snapshot->folders) {
      return;
    }

    if (loaded_workspace != workspace_root) {
      last_revealed_path.clear();
    }

    loaded_workspace = workspace_root;
    indexed_files = snapshot->files;
    indexed_skeleton_folders = snapshot->skeleton_folders;
    indexed_folders = snapshot->folders;
    const std::string to_reveal = last_revealed_path;
    const bool skeleton_preview = !snapshot->skeleton_folders.empty();
    std::vector<std::string> tree_folders = snapshot->skeleton_folders;
    merge_unique_folder_paths(&tree_folders, snapshot->folders);
    if (tree_folders.empty()) {
      root = build_file_tree_from_paths(snapshot->files);
    } else {
      root = build_file_tree_from_paths_and_folders(snapshot->files, tree_folders);
    }
    if (!to_reveal.empty() && !skeleton_preview) {
      reveal_file(workspace_root, to_reveal);
    } else {
      selected = 0;
      list_scroll = 0;
      rebuild_flat();
    }
  }

  void activate(DebugModel* model, WorkspaceModel* workspace, FocusManagerState* focus,
                MainLayoutState* layout_state, int index) {
    if (index < 0 || index >= static_cast<int>(flat.size())) {
      return;
    }
    const auto& entry = flat[static_cast<std::size_t>(index)];
    if (entry.is_file) {
      std::error_code ec;
      const auto absolute = fs::absolute(fs::path(model->workspace_root) / entry.relative_path, ec);
      if (workspace != nullptr) {
        if (!workspace->open_file(absolute.string())) {
          return;
        }
      }
      model->active_file = absolute.string();
      model->active_line = 0;
      model->view_token++;
      if (layout_state != nullptr) {
        UI_WAKE(layout_state, "wake");
      }
      if (focus != nullptr) {
        focus->region = FocusRegion::Editor;
      }
      return;
    }
    if (entry.folder != nullptr) {
      entry.folder->expanded = !entry.folder->expanded;
      rebuild_flat();
      scroll_row_into_view(index, layout_state);
    }
  }

  std::optional<int> row_at_mouse(int x, int y) const {
    if (!content_box.Contain(x, y)) {
      return std::nullopt;
    }
    const int row = list_scroll + (y - content_box.y_min);
    if (row < 0 || row >= static_cast<int>(flat.size())) {
      return std::nullopt;
    }
    return row;
  }

  bool press_row(MainLayoutState* layout_state, int row) {
    if (layout_state == nullptr || row < 0 || row >= static_cast<int>(flat.size())) {
      return false;
    }
    selected = row;
    center_row(row);
    trigger_press(layout_state, press_id::explorer_row(row));
    return true;
  }

  bool press_file(MainLayoutState* layout_state, const std::string& workspace_root,
                  const std::string& absolute_path) {
    if (layout_state == nullptr || workspace_root.empty() || absolute_path.empty()) {
      return false;
    }
    const std::string normalized = normalize_path(absolute_path);
    const std::string rel = relative_path_in_workspace(workspace_root, normalized);
    if (rel.empty()) {
      return false;
    }
    if (expand_relative_path(&root, rel)) {
      rebuild_flat();
    }
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
      if (flat[static_cast<std::size_t>(i)].is_file &&
          flat[static_cast<std::size_t>(i)].relative_path == rel) {
        last_revealed_path = normalized;
        return press_row(layout_state, i);
      }
    }
    return false;
  }
};

void invalidate_file_tree_panel(MainLayoutState* layout_state) {
  if (layout_state != nullptr) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::FileTree);
  }
}

void set_explorer_list_scroll(FileTreePanelState* state, MainLayoutState* layout_state,
                              int scroll, int max_scroll = -1) {
  if (state == nullptr) {
    return;
  }
  if (max_scroll >= 0) {
    scroll = std::max(0, std::min(scroll, max_scroll));
  } else {
    scroll = std::max(0, scroll);
  }
  if (state->list_scroll == scroll) {
    return;
  }
  state->list_scroll = scroll;
  invalidate_file_tree_panel(layout_state);
}

bool handle_explorer_scrollbar_mouse(FileTreePanelState* state, MainLayoutState* layout_state,
                                     const Mouse& m, int total, int visible) {
  if (state == nullptr) {
    return false;
  }

  const int max_scroll = max_scroll_offset(total, visible);
  const bool in_bar = state->scrollbar_box.Contain(m.x, m.y);
  const bool scrollable = state->scrollbar_layout.scrollable;

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr && hover_effects_enabled()) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || state->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kExplorerScrollbar);
      } else {
        layout_state->clickable.clear_hover_if(
            [](std::string_view id) { return id == press_id::kExplorerScrollbar; });
      }
      apply_hover_repaint(layout_state, before);
    }
    if (state->scrollbar_dragging && scrollable) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      set_explorer_list_scroll(
          state, layout_state,
          scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_scroll);
      return true;
    }
    return false;
  }

  if (state->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Moved && scrollable) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      set_explorer_list_scroll(
          state, layout_state,
          scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_scroll);
      return true;
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    set_explorer_list_scroll(state, layout_state, state->list_scroll - 3);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    set_explorer_list_scroll(state, layout_state, state->list_scroll + 3, max_scroll);
    return true;
  }

  if (!scrollable) {
    return false;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    trigger_press(layout_state, press_id::kExplorerScrollbar);
    const int local_y = m.y - state->scrollbar_box.y_min;
    if (scrollbar_thumb_hit(state->scrollbar_layout, state->scrollbar_box, m.x, m.y)) {
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = local_y - state->scrollbar_layout.thumb_y;
    } else {
      const int thumb_top = local_y - state->scrollbar_layout.thumb_height / 2;
      set_explorer_list_scroll(state, layout_state,
                               scroll_for_thumb_top(state->scrollbar_layout, thumb_top),
                               max_scroll);
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = state->scrollbar_layout.thumb_height / 2;
    }
    return true;
  }

  return false;
}

bool handle_explorer_context_menu(FileTreePanelState* state, DebugModel* model,
                                  WorkspaceModel* workspace, FocusManagerState* focus,
                                  MainLayoutState* layout_state, Event event) {
  if (!event.is_mouse() || event.mouse().button != Mouse::Right ||
      event.mouse().motion != Mouse::Pressed) {
    return false;
  }
  const auto& m = event.mouse();
  const auto row = state->row_at_mouse(m.x, m.y);
  if (!row.has_value()) {
    return false;
  }
  state->selected = *row;
  const auto& entry = state->flat[static_cast<std::size_t>(*row)];
  if (focus != nullptr) {
    focus->region = FocusRegion::Explorer;
  }
  std::error_code ec;
  const auto absolute = fs::weakly_canonical(
      fs::path(model->workspace_root) / entry.relative_path, ec);
  if (ec || layout_state == nullptr) {
    return true;
  }
  if (entry.is_file) {
    const bool trackable = is_lsp_trackable_path(absolute.string());
    const bool binary = is_nm_analyzable_path(absolute.string());
    context_menu_open_file(&layout_state->context_menu, m.x, m.y, absolute.string(),
                           entry.relative_path, trackable, true, binary);
  } else {
    context_menu_open_folder(&layout_state->context_menu, m.x, m.y, absolute.string(),
                             entry.relative_path);
  }
  return true;
}

bool mouse_over_explorer(const FileTreePanelState* state, int x, int y) {
  if (state == nullptr) {
    return false;
  }
  if (state->panel_box.Contain(x, y)) {
    return true;
  }
  return state->content_box.Contain(x, y) || state->scrollbar_box.Contain(x, y);
}

void hide_explorer_panel(MainLayoutState* layout_state, FocusManagerState* focus) {
  if (layout_state == nullptr || !layout_state->explorer_visible) {
    return;
  }
  layout_state->explorer_visible = false;
  if (focus != nullptr && focus->region == FocusRegion::Explorer) {
    focus->region = FocusRegion::Editor;
    layout_state->focus_sync_needed = true;
  }
  layout_state->text_input_focus = TextInputFocus::None;
  UI_WAKE(layout_state, "wake");
}

void refresh_explorer_index(MainLayoutState* layout_state) {
  if (layout_state == nullptr || !layout_state->explorer_refresh) {
    return;
  }
  layout_state->explorer_refresh();
}

bool handle_explorer_header_mouse(FileTreePanelState* state, MainLayoutState* layout_state,
                                  FocusManagerState* focus, const Mouse& mouse) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  if (mouse.motion == Mouse::Moved) {
    return update_panel_hover(
        layout_state, mouse.x, mouse.y,
        {{press_id::kExplorerRefresh, &state->refresh_box},
         {press_id::kExplorerHide, &state->hide_box}},
        [](std::string_view id) {
          return id == press_id::kExplorerRefresh || id == press_id::kExplorerHide;
        });
  }
  if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
    return false;
  }
  if (state->refresh_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kExplorerRefresh);
    refresh_explorer_index(layout_state);
    return true;
  }
  if (!state->hide_box.Contain(mouse.x, mouse.y)) {
    return false;
  }
  trigger_press(layout_state, press_id::kExplorerHide);
  hide_explorer_panel(layout_state, focus);
  return true;
}

bool update_explorer_hover(FileTreePanelState* state, MainLayoutState* layout_state, int x,
                           int y) {
  if (!hover_effects_enabled()) {
    return false;
  }
  if (layout_state == nullptr || state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const auto row = state->row_at_mouse(x, y);
  if (row.has_value()) {
    layout_state->clickable.set_hover(press_id::explorer_row(*row));
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_explorer_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::FileTree);
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return false;
}

bool handle_navigation(FileTreePanelState* state, DebugModel* model,
                       WorkspaceModel* workspace, FocusManagerState* focus,
                       MainLayoutState* layout_state, Event event) {
  const int total = static_cast<int>(state->flat.size());
  const int visible = state->last_visible_lines;
  const int max_scroll = max_scroll_offset(total, visible);

  if (event.is_mouse()) {
    const auto& m = event.mouse();
    if (handle_explorer_scrollbar_mouse(state, layout_state, m, total, visible)) {
      return true;
    }
    if (m.motion == Mouse::Moved) {
      update_explorer_hover(state, layout_state, m.x, m.y);
      return false;
    }
    if (m.motion == Mouse::Pressed &&
        (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) &&
        (state->content_box.Contain(m.x, m.y) || state->panel_box.Contain(m.x, m.y) ||
         state->scrollbar_box.Contain(m.x, m.y))) {
      if (m.button == Mouse::WheelUp) {
        set_explorer_list_scroll(state, layout_state, state->list_scroll - 3);
      } else {
        set_explorer_list_scroll(state, layout_state, state->list_scroll + 3, max_scroll);
      }
      return true;
    }
  }

  if (event.is_mouse() && event.mouse().button == Mouse::Left &&
      event.mouse().motion == Mouse::Pressed) {
    const auto& m = event.mouse();
    const auto row = state->row_at_mouse(m.x, m.y);
    if (!row.has_value()) {
      return false;
    }
    if (focus != nullptr) {
      focus->region = FocusRegion::Explorer;
    }
    trigger_press(layout_state, press_id::explorer_row(*row));
    state->selected = *row;
    state->scroll_row_into_view(*row, layout_state);
    state->activate(model, workspace, focus, layout_state, *row);
    return true;
  }

  if (focus != nullptr && focus->region != FocusRegion::Explorer) {
    return false;
  }
  if (state->flat.empty()) {
    return false;
  }

  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected =
        std::min(state->selected + 1, static_cast<int>(state->flat.size()) - 1);
    state->scroll_row_into_view(state->selected, layout_state);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected = std::max(0, state->selected - 1);
    state->scroll_row_into_view(state->selected, layout_state);
    return true;
  }
  if (event == Event::Return) {
    trigger_press(layout_state, press_id::explorer_row(state->selected));
    state->activate(model, workspace, focus, layout_state, state->selected);
    return true;
  }
  if (event == Event::ArrowRight) {
    const auto& entry = state->flat[static_cast<std::size_t>(state->selected)];
    if (!entry.is_file && entry.folder != nullptr && !entry.folder->expanded) {
      entry.folder->expanded = true;
      state->rebuild_flat();
      state->scroll_row_into_view(state->selected, layout_state);
      return true;
    }
    return false;
  }
  if (event == Event::ArrowLeft) {
    const auto& entry = state->flat[static_cast<std::size_t>(state->selected)];
    if (!entry.is_file && entry.folder != nullptr && entry.folder->expanded) {
      entry.folder->expanded = false;
      state->rebuild_flat();
      state->scroll_row_into_view(state->selected, layout_state);
      return true;
    }
    return false;
  }
  if (event == Event::PageUp) {
    set_explorer_list_scroll(state, layout_state, state->list_scroll - visible, max_scroll);
    return true;
  }
  if (event == Event::PageDown) {
    set_explorer_list_scroll(state, layout_state, state->list_scroll + visible, max_scroll);
    return true;
  }
  return false;
}

bool handle_explorer_mouse(FileTreePanelState* state, DebugModel* model,
                           WorkspaceModel* workspace, FocusManagerState* focus,
                           MainLayoutState* layout_state, Event event) {
  if (state == nullptr || !event.is_mouse()) {
    return false;
  }
  const auto& m = event.mouse();
  if (handle_explorer_header_mouse(state, layout_state, focus, m)) {
    return true;
  }
  if (!mouse_over_explorer(state, m.x, m.y)) {
    return false;
  }
  if (m.button == Mouse::Right && m.motion == Mouse::Pressed) {
    return handle_explorer_context_menu(state, model, workspace, focus, layout_state, event);
  }
  return handle_navigation(state, model, workspace, focus, layout_state, event);
}

}  // namespace

Component MakeFileTreePanel(DebugModel* model, WorkspaceModel* workspace,
                            FocusManagerState* focus, WorkspaceIndexer* indexer,
                            CommandCallback on_command, MainLayoutState* layout_state,
                            GitService* git_service) {
  (void)on_command;
  auto state = std::make_shared<FileTreePanelState>();

  auto renderer = Renderer([model, workspace, focus, state, indexer, layout_state, git_service] {
    const GitExplorerMarks git_marks = build_git_explorer_marks(git_service);
    const std::shared_ptr<const IndexSnapshot> snapshot =
        indexer != nullptr ? indexer->snapshot() : nullptr;
    state->sync_index(snapshot, model->workspace_root);

    const std::string active_file =
        workspace != nullptr && !workspace->active_file.empty()
            ? normalize_path(workspace->active_file)
            : (workspace != nullptr && !workspace->buffer.path.empty()
                   ? normalize_path(workspace->buffer.path)
                   : std::string{});
    const bool skeleton_preview = snapshot && !snapshot->skeleton_folders.empty();
    if (!active_file.empty() && active_file != state->last_revealed_path && !skeleton_preview) {
      state->reveal_file(model->workspace_root, active_file);
    }

    const std::string active_rel =
        relative_path_in_workspace(model->workspace_root, active_file);

    const int total = static_cast<int>(state->flat.size());
    const int visible = visible_line_count(state->content_box);
    state->last_visible_lines = visible;
    state->clamp_scroll();

    const int start = state->list_scroll;
    const int end = std::min(total, start + visible);

    Elements rows;
    if (state->flat.empty()) {
      rows.push_back(text(i18n::tr("panel.explorer.no_files")) | color(theme::Muted()));
      if (!model->workspace_root.empty()) {
        rows.push_back(text(i18n::tr_fmt("common.workspace", {model->workspace_root})) |
                       color(theme::Muted()));
      }
    } else {
      for (int i = start; i < end; ++i) {
        const auto& entry = state->flat[static_cast<std::size_t>(i)];
        const bool active_entry = entry.is_file && !active_rel.empty() &&
                                  entry.relative_path == active_rel;
        const bool selected =
            (i == state->selected &&
             (focus == nullptr || focus->region == FocusRegion::Explorer)) ||
            active_entry;
        const std::string row_id = press_id::explorer_row(i);
        const bool hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
        const bool pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(row_id);

        std::string indent(static_cast<std::size_t>(entry.depth * 2), ' ');
        const std::string icon =
            entry.is_file ? file_glyph_display(entry.label)
                          : folder_glyph(entry.folder != nullptr && entry.folder->expanded);

        Element row;
        if (entry.is_file) {
          row = text(indent + icon + " " + entry.label) | color(theme::FileText());
          const auto git_it = git_marks.files.find(entry.relative_path);
          if (git_it != git_marks.files.end()) {
            if (const std::optional<Color> dot = file_git_status_dot(git_it->second)) {
              row = hbox({std::move(row) | flex, text(" ●") | color(*dot)});
            }
          }
        } else {
          row = text(indent + icon + " " + entry.label) | color(theme::DirectoryText());
          if (git_marks.dirty_folders.count(entry.relative_path) > 0) {
            row = hbox({
                std::move(row) | flex,
                text(" ●") | color(theme::Success()),
            });
          }
        }
        row = StyleListRow(std::move(row), selected, hovered, pressed);
        rows.push_back(row);
      }
    }

    const int rendered_lines = std::max(1, static_cast<int>(rows.size()));
    state->scrollbar_layout =
        compute_scrollbar_layout(total, state->list_scroll, visible, rendered_lines);

    Element list = vbox(std::move(rows)) | reflect(state->content_box) | flex |
                   bgcolor(theme::PanelBg());
    Element scrollbar =
        vertical_scrollbar(total, state->list_scroll, visible, rendered_lines,
                           layout_state != nullptr &&
                               layout_state->clickable.is_hovered(press_id::kExplorerScrollbar),
                           state->scrollbar_dragging ||
                               (layout_state != nullptr &&
                                layout_state->clickable.is_pressed(press_id::kExplorerScrollbar))) |
        reflect(state->scrollbar_box);

    auto content = vbox({hbox({list | flex, scrollbar}) | flex | bgcolor(theme::PanelBg())});

    const bool refresh_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kExplorerRefresh);
    const bool refresh_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kExplorerRefresh);
    const bool hide_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kExplorerHide);
    const bool hide_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kExplorerHide);
    Element refresh_btn = MakeToolbarButton(
        text(i18n::tr("panel.explorer.refresh")) | color(theme::Muted()),
        refresh_hovered, refresh_pressed, false, &state->refresh_box);
    Element hide_btn = MakeToolbarButton(
        text(i18n::tr("console.hide_panel")) | color(theme::Muted()),
        hide_hovered, hide_pressed, false, &state->hide_box);

    return vbox({
               hbox({
                   text(i18n::tr("panel.explorer.title")) | color(theme::Accent()) | bold,
                   filler(),
                   refresh_btn | size(WIDTH, EQUAL, 3),
                   hide_btn | size(WIDTH, EQUAL, 3),
               }) | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) | reflect(state->panel_box),
               separator(),
               std::move(content) | flex,
           }) |
           flex | bgcolor(theme::PanelBg());
  });

  if (layout_state != nullptr) {
    layout_state->explorer_mouse_handler =
        [state, model, workspace, focus, layout_state](const Event& event) {
          return handle_explorer_mouse(state.get(), model, workspace, focus, layout_state,
                                       event);
        };
  }

  return WrapFocusable(CatchEvent(renderer, [model, workspace, focus, state, layout_state](Event event) {
    if (event.is_mouse() && mouse_over_explorer(state.get(), event.mouse().x, event.mouse().y)) {
      return false;
    }
    return handle_navigation(state.get(), model, workspace, focus, layout_state, event);
  }));
}

}  // namespace tgdb
