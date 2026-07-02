#include "ui/file_tree_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "indexer/index_rules.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focusable_component.hpp"
#include "ui/clickable.hpp"
#include "ui/context_menu.hpp"
#include "ui/focus_manager.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/theme.hpp"
#include "util/filesystem_tree.hpp"
#include "util/path_normalize.hpp"

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

struct FileTreePanelState {
  FileTreeNode root;
  std::vector<FlatEntry> flat;
  std::vector<std::string> indexed_files;
  int selected = 0;
  int list_scroll = 0;
  std::string loaded_workspace;
  std::string last_revealed_path;
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

  void scroll_row_into_view(int row) {
    const int visible = visible_line_count(content_box);
    if (row < list_scroll) {
      list_scroll = row;
    } else if (row >= list_scroll + visible) {
      list_scroll = row - visible + 1;
    }
    clamp_scroll();
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

    if (loaded_workspace == workspace_root && indexed_files == snapshot->files) {
      return;
    }

    loaded_workspace = workspace_root;
    indexed_files = snapshot->files;
    const std::string to_reveal = last_revealed_path;
    root = build_file_tree_from_paths(snapshot->files);
    if (!to_reveal.empty()) {
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
        layout_state->request_ui_tick = true;
      }
      if (focus != nullptr) {
        focus->region = FocusRegion::Editor;
      }
      return;
    }
    if (entry.folder != nullptr) {
      entry.folder->expanded = !entry.folder->expanded;
      rebuild_flat();
      scroll_row_into_view(index);
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

bool handle_explorer_scrollbar_mouse(FileTreePanelState* state, MainLayoutState* layout_state,
                                     const Mouse& m, int total, int visible) {
  if (state == nullptr || !state->scrollbar_layout.scrollable) {
    return false;
  }

  const int max_scroll = max_scroll_offset(total, visible);
  const bool in_bar = state->scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || state->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kEditorScrollbar);
      } else {
        layout_state->clickable.clear_hover_if(
            [](std::string_view id) { return id == press_id::kEditorScrollbar; });
      }
      if (layout_state->clickable.hovered_id() != before) {
        layout_state->request_ui_tick = true;
      }
    }
    if (state->scrollbar_dragging) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->list_scroll =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top),
                               max_scroll));
      return true;
    }
    return in_bar;
  }

  if (state->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Moved) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->list_scroll =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top),
                               max_scroll));
      return true;
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    state->list_scroll = std::max(0, state->list_scroll - 3);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    state->list_scroll = std::min(state->list_scroll + 3, max_scroll);
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    trigger_press(layout_state, press_id::kEditorScrollbar);
    const int local_y = m.y - state->scrollbar_box.y_min;
    if (scrollbar_thumb_hit(state->scrollbar_layout, state->scrollbar_box, m.x, m.y)) {
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = local_y - state->scrollbar_layout.thumb_y;
    } else {
      const int thumb_top = local_y - state->scrollbar_layout.thumb_height / 2;
      state->list_scroll =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top),
                               max_scroll));
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
    context_menu_open_file(&layout_state->context_menu, m.x, m.y, absolute.string(),
                           entry.relative_path, is_lsp_trackable_path(absolute.string()));
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
  return state->content_box.Contain(x, y) || state->scrollbar_box.Contain(x, y);
}

bool update_explorer_hover(FileTreePanelState* state, MainLayoutState* layout_state, int x,
                           int y) {
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
    layout_state->request_ui_tick = true;
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
        state->content_box.Contain(m.x, m.y)) {
      if (m.button == Mouse::WheelUp) {
        state->list_scroll = std::max(0, state->list_scroll - 3);
      } else {
        state->list_scroll = std::min(state->list_scroll + 3, max_scroll);
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
    state->scroll_row_into_view(*row);
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
    state->scroll_row_into_view(state->selected);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected = std::max(0, state->selected - 1);
    state->scroll_row_into_view(state->selected);
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
      state->scroll_row_into_view(state->selected);
      return true;
    }
    return false;
  }
  if (event == Event::ArrowLeft) {
    const auto& entry = state->flat[static_cast<std::size_t>(state->selected)];
    if (!entry.is_file && entry.folder != nullptr && entry.folder->expanded) {
      entry.folder->expanded = false;
      state->rebuild_flat();
      state->scroll_row_into_view(state->selected);
      return true;
    }
    return false;
  }
  if (event == Event::PageUp) {
    state->list_scroll = std::max(0, state->list_scroll - visible);
    return true;
  }
  if (event == Event::PageDown) {
    state->list_scroll = std::min(state->list_scroll + visible, max_scroll);
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
                            CommandCallback on_command, MainLayoutState* layout_state) {
  (void)on_command;
  auto state = std::make_shared<FileTreePanelState>();

  auto renderer = Renderer([model, workspace, focus, state, indexer, layout_state] {
    const bool scanning = indexer != nullptr && indexer->scanning();
    state->sync_index(indexer != nullptr ? indexer->snapshot() : nullptr,
                      model->workspace_root);

    const std::string active_file =
        workspace != nullptr && !workspace->active_file.empty()
            ? normalize_path(workspace->active_file)
            : (workspace != nullptr && !workspace->buffer.path.empty()
                   ? normalize_path(workspace->buffer.path)
                   : std::string{});
    if (!active_file.empty() && active_file != state->last_revealed_path) {
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
      if (scanning) {
        rows.push_back(text("(indexando...)") | color(theme::Muted()));
      } else {
        rows.push_back(text("(sin archivos)") | color(theme::Muted()));
      }
      if (!model->workspace_root.empty()) {
        rows.push_back(text("workspace: " + model->workspace_root) |
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
        std::string prefix;
        if (entry.is_file) {
          prefix = "  ";
        } else if (entry.folder != nullptr && entry.folder->expanded) {
          prefix = "v ";
        } else {
          prefix = "> ";
        }

        Element row = text(indent + prefix + entry.label);
        if (entry.is_file) {
          row = row | color(theme::FileText());
        } else {
          row = row | color(theme::DirectoryText());
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
                               layout_state->clickable.is_hovered(press_id::kEditorScrollbar),
                           state->scrollbar_dragging ||
                               (layout_state != nullptr &&
                                layout_state->clickable.is_pressed(press_id::kEditorScrollbar))) |
        reflect(state->scrollbar_box);

    auto content = vbox({hbox({list | flex, scrollbar}) | flex | bgcolor(theme::PanelBg())});
    return MakePanel("Explorador", std::move(content));
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
