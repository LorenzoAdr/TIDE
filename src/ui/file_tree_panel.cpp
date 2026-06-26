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
  int selected = 0;
  std::string loaded_workspace;
  Box content_box;

  void sync_workspace(DebugModel* model) {
    if (model->workspace_root == loaded_workspace) {
      return;
    }
    loaded_workspace = model->workspace_root;
    root = build_file_tree_root(loaded_workspace);
    selected = 0;
    rebuild_flat();
  }

  void rebuild_flat() {
    flat.clear();
    std::function<void(FileTreeNode&, int)> walk;
    walk = [&](FileTreeNode& node, int depth) {
      for (auto& child : node.children) {
        if (child.is_file) {
          flat.push_back(
              {depth, child.name, child.relative_path, true, nullptr});
        } else {
          flat.push_back({depth, child.name, "", false, &child});
          if (child.expanded) {
            walk(child, depth + 1);
          }
        }
      }
    };
    walk(root, 0);
    if (selected >= static_cast<int>(flat.size())) {
      selected = std::max(0, static_cast<int>(flat.size()) - 1);
    }
  }

  void activate(DebugModel* model, int index) {
    if (index < 0 || index >= static_cast<int>(flat.size())) {
      return;
    }
    const auto& entry = flat[index];
    if (entry.is_file) {
      std::error_code ec;
      model->active_file = std::filesystem::absolute(
          std::filesystem::path(model->workspace_root) / entry.relative_path, ec);
      model->active_line = 0;
      model->view_token++;
      return;
    }
    if (entry.folder != nullptr) {
      entry.folder->expanded = !entry.folder->expanded;
      rebuild_flat();
    }
  }
};

bool handle_navigation(FileTreePanelState* state, DebugModel* model, Event event) {
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
    state->activate(model, state->selected);
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
  if (event.is_mouse() && event.mouse().button == Mouse::Left &&
      event.mouse().motion == Mouse::Pressed) {
    const auto& m = event.mouse();
    if (!state->content_box.Contain(m.x, m.y)) {
      return false;
    }
    const int row = m.y - state->content_box.y_min;
    if (row >= 0 && row < static_cast<int>(state->flat.size())) {
      state->selected = row;
      state->activate(model, row);
      return true;
    }
    return false;
  }
  return false;
}

}  // namespace

Component MakeFileTreePanel(DebugModel* model, CommandCallback on_command) {
  (void)on_command;
  auto state = std::make_shared<FileTreePanelState>();

  auto renderer = Renderer([model, state] {
    state->sync_workspace(model);

    Elements rows;
    if (state->flat.empty()) {
      rows.push_back(text("(sin archivos fuente)") | color(theme::Muted()));
      rows.push_back(text("workspace: " + model->workspace_root) |
                     color(theme::Muted()));
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
        if (selected) {
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

  return CatchEvent(renderer, [model, state](Event event) {
    return handle_navigation(state.get(), model, event);
  });
}

}  // namespace tgdb
