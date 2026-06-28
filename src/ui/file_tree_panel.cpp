#include "ui/file_tree_panel.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focusable_component.hpp"
#include "ui/context_menu.hpp"
#include "ui/focus_manager.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "util/filesystem_tree.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct FlatEntry {
  int depth = 0;
  std::string label;
  std::string relative_path;
  bool is_file = false;
  FileTreeNode* folder = nullptr;
};

struct FileTreePanelState {
  FileTreeNode root;
  std::vector<FlatEntry> flat;
  std::vector<std::string> indexed_files;
  int selected = 0;
  std::string loaded_workspace;
  Box content_box;

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
    root = build_file_tree_from_paths(snapshot->files);
    selected = 0;
    rebuild_flat();
  }

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
  }

  void activate(DebugModel* model, WorkspaceModel* workspace, FocusManagerState* focus,
                MainLayoutState* layout_state, int index) {
    if (index < 0 || index >= static_cast<int>(flat.size())) {
      return;
    }
    const auto& entry = flat[index];
    if (entry.is_file) {
      std::error_code ec;
      const auto absolute = std::filesystem::absolute(
          std::filesystem::path(model->workspace_root) / entry.relative_path, ec);
      model->active_file = absolute.string();
      model->active_line = 0;
      model->view_token++;
      if (workspace != nullptr) {
        workspace->open_file(absolute.string());
      }
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
    }
  }
};

bool handle_explorer_context_menu(FileTreePanelState* state, DebugModel* model,
                                  WorkspaceModel* workspace, FocusManagerState* focus,
                                  MainLayoutState* layout_state, Event event) {
  if (!event.is_mouse() || event.mouse().button != Mouse::Right ||
      event.mouse().motion != Mouse::Pressed) {
    return false;
  }
  const auto& m = event.mouse();
  if (!state->content_box.Contain(m.x, m.y)) {
    return false;
  }
  const int row = m.y - state->content_box.y_min;
  if (row < 0 || row >= static_cast<int>(state->flat.size())) {
    return false;
  }
  state->selected = row;
  const auto& entry = state->flat[row];
  if (focus != nullptr) {
    focus->region = FocusRegion::Explorer;
  }
  std::error_code ec;
  const auto absolute = std::filesystem::weakly_canonical(
      std::filesystem::path(model->workspace_root) / entry.relative_path, ec);
  if (ec || layout_state == nullptr) {
    return true;
  }
  if (entry.is_file) {
    context_menu_open_file(&layout_state->context_menu, m.x, m.y, absolute.string(),
                           entry.relative_path);
  } else {
    context_menu_open_folder(&layout_state->context_menu, m.x, m.y, absolute.string(),
                             entry.relative_path);
  }
  return true;
}

bool handle_navigation(FileTreePanelState* state, DebugModel* model,
                       WorkspaceModel* workspace, FocusManagerState* focus,
                       MainLayoutState* layout_state, Event event) {
  if (event.is_mouse() && event.mouse().button == Mouse::Left &&
      event.mouse().motion == Mouse::Pressed) {
    const auto& m = event.mouse();
    if (!state->content_box.Contain(m.x, m.y)) {
      return false;
    }
    const int row = m.y - state->content_box.y_min;
    if (row < 0 || row >= static_cast<int>(state->flat.size())) {
      return false;
    }
    if (focus != nullptr) {
      focus->region = FocusRegion::Explorer;
    }
    state->selected = row;
    state->activate(model, workspace, focus, layout_state, row);
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
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected = std::max(0, state->selected - 1);
    return true;
  }
  if (event == Event::Return) {
    state->activate(model, workspace, focus, layout_state, state->selected);
    return true;
  }
  if (event == Event::ArrowRight) {
    const auto& entry = state->flat[state->selected];
    if (!entry.is_file && entry.folder != nullptr && !entry.folder->expanded) {
      entry.folder->expanded = true;
      state->rebuild_flat();
      return true;
    }
    return false;
  }
  if (event == Event::ArrowLeft) {
    const auto& entry = state->flat[state->selected];
    if (!entry.is_file && entry.folder != nullptr && entry.folder->expanded) {
      entry.folder->expanded = false;
      state->rebuild_flat();
      return true;
    }
    return false;
  }
  return false;
}

}  // namespace

Component MakeFileTreePanel(DebugModel* model, WorkspaceModel* workspace,
                            FocusManagerState* focus, WorkspaceIndexer* indexer,
                            CommandCallback on_command, MainLayoutState* layout_state) {
  (void)on_command;
  auto state = std::make_shared<FileTreePanelState>();

  auto renderer = Renderer([model, focus, state, indexer] {
    const bool scanning = indexer != nullptr && indexer->scanning();
    state->sync_index(indexer != nullptr ? indexer->snapshot() : nullptr,
                      model->workspace_root);

    Elements rows;
    if (state->flat.empty()) {
      if (scanning) {
        rows.push_back(text("(indexando...)") | color(theme::Muted()));
      } else {
        rows.push_back(text("(sin archivos fuente)") | color(theme::Muted()));
      }
      if (!model->workspace_root.empty()) {
        rows.push_back(text("workspace: " + model->workspace_root) |
                       color(theme::Muted()));
      }
    } else {
      for (int i = 0; i < static_cast<int>(state->flat.size()); ++i) {
        const auto& entry = state->flat[i];
        const bool selected = i == state->selected;

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
          row = row | color(theme::Muted());
        } else {
          row = row | color(theme::Header());
        }
        if (selected && (focus == nullptr || focus->region == FocusRegion::Explorer)) {
          row = row | inverted | bold;
        }
        rows.push_back(row);
      }
    }

    auto list = vbox(std::move(rows)) | vscroll_indicator | frame | flex |
                reflect(state->content_box) | bgcolor(theme::PanelBg());

    auto content = vbox({list | bgcolor(theme::PanelBg())});
    return MakePanel("Explorador", std::move(content));
  });

  if (layout_state != nullptr) {
    layout_state->explorer_context_handler =
        [state, model, workspace, focus, layout_state](const Event& event) {
          return handle_explorer_context_menu(state.get(), model, workspace, focus, layout_state,
                                              event);
        };
  }

  return WrapFocusable(CatchEvent(renderer, [model, workspace, focus, state, layout_state](Event event) {
    return handle_navigation(state.get(), model, workspace, focus, layout_state, event);
  }));
}

}  // namespace tgdb
