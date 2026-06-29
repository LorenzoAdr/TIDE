#include "ui/main_layout.hpp"

#include <algorithm>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "lsp/diagnostics.hpp"
#include "terminal/shell_session.hpp"
#include "ui/console_panel.hpp"
#include "ui/editor_panel.hpp"
#include "ui/file_tree_panel.hpp"
#include "ui/call_hierarchy_panel.hpp"
#include "ui/outline_panel.hpp"
#include "ui/right_sidebar_panel.hpp"
#include "ui/search_panel.hpp"
#include "ui/panel.hpp"
#include "ui/source_panel.hpp"
#include "ui/theme.hpp"
#include "ui/watches_panel.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct LayoutState {
  int left_width = 22;
  int right_width = 22;
  int bottom_height = 8;
  int outline_height = 12;
  uint64_t last_diag_revision = 0;
  int diag_errors = 0;
  int diag_warnings = 0;
};

struct FocusSyncState {
  FocusRegion region = FocusRegion::Editor;
  AppMode mode = AppMode::kNormal;
  bool initialized = false;
};

void sync_panel_focus(FocusSyncState* sync, AppMode* app_mode, FocusManagerState* focus,
                      MainLayoutState* layout_state) {
  if (sync == nullptr || focus == nullptr) {
    return;
  }

  const AppMode mode = app_mode != nullptr ? *app_mode : AppMode::kNormal;
  if (sync->initialized && sync->region == focus->region && sync->mode == mode) {
    return;
  }

  sync->initialized = true;
  sync->region = focus->region;
  sync->mode = mode;

  if (layout_state == nullptr) {
    return;
  }

  if (focus->region == FocusRegion::Terminal) {
    if (mode == AppMode::kDebug &&
        layout_state->console_tabs.selected_tab == ConsolePanelTabs::kDebug) {
      return;
    }
    layout_state->text_input_focus = TextInputFocus::Console;
    return;
  }

  switch (layout_state->text_input_focus) {
    case TextInputFocus::SearchQuery:
    case TextInputFocus::SearchReplace:
    case TextInputFocus::SearchPath:
    case TextInputFocus::SearchExclude:
      if (focus->region != FocusRegion::RightPanel) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      break;
    case TextInputFocus::Watch:
    case TextInputFocus::WatchInject:
      if (focus->region != FocusRegion::RightPanel) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      break;
    case TextInputFocus::EditorFind:
    case TextInputFocus::EditorGotoLine:
    case TextInputFocus::EditorCompletion:
      if (focus->region != FocusRegion::Editor) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      break;
    default:
      layout_state->text_input_focus = TextInputFocus::None;
      break;
  }
}

// Conmuta hijos según app_mode sin Container::Tab (Tab dentro de ResizableSplit
// ignora el ancho fijo inicial y TakeFocus corrompe el selector).
class ModeLayout : public ComponentBase {
 public:
  ModeLayout(AppMode* app_mode, Component normal_child, Component debug_child)
      : app_mode_(app_mode) {
    Add(std::move(normal_child));
    Add(std::move(debug_child));
  }

  Element OnRender() override {
    Component child = VisibleChild();
    if (!child) {
      return text("");
    }
    return child->Render();
  }

  bool OnEvent(Event event) override {
    Component child = VisibleChild();
    if (!child) {
      return false;
    }
    return child->OnEvent(std::move(event));
  }

  bool Focusable() const override {
    Component child = VisibleChild();
    return child && child->Focusable();
  }

  Component ActiveChild() override { return VisibleChild(); }

  void SetActiveChild(ComponentBase* child) override {
    if (Component visible = VisibleChild()) {
      visible->SetActiveChild(child);
    }
  }

 private:
  Component VisibleChild() const {
    if (children_.empty()) {
      return nullptr;
    }
    if (children_.size() == 1) {
      return children_.front();
    }
    const bool debug = app_mode_ != nullptr && *app_mode_ == AppMode::kDebug;
    return children_[debug ? 1 : 0];
  }

  AppMode* app_mode_;
};

Component MakeModeLayout(AppMode* app_mode, Component normal_child, Component debug_child) {
  return Make<ModeLayout>(app_mode, std::move(normal_child), std::move(debug_child));
}

// Panel derecho: un solo outline; en debug muestra watches debajo.
class RightPanelLayout : public ComponentBase {
 public:
  RightPanelLayout(AppMode* app_mode, Component outline, Component watches,
                   int* outline_height, MainLayoutState* layout_state)
      : app_mode_(app_mode), outline_height_(outline_height), layout_state_(layout_state) {
    Add(std::move(outline));
    Add(std::move(watches));
  }

  Element OnRender() override {
    if (app_mode_ == nullptr || *app_mode_ != AppMode::kDebug) {
      return ChildAt(0)->Render();
    }
    return vbox({
               ChildAt(0)->Render() | size(HEIGHT, EQUAL, *outline_height_),
               separator(),
               ChildAt(1)->Render() | yflex,
           }) |
           flex;
  }

  bool OnEvent(Event event) override {
    if (app_mode_ != nullptr && *app_mode_ == AppMode::kDebug) {
      Component sidebar = ChildAt(0);
      Component watches = ChildAt(1);
      const bool watch_input =
          layout_state_ != nullptr &&
          is_watch_input_focus(layout_state_->text_input_focus);

      if (event.is_mouse()) {
        if (watches && watches->OnEvent(event)) {
          return true;
        }
        if (sidebar && sidebar->OnEvent(event)) {
          if (layout_state_ != nullptr) {
            layout_state_->right_panel_active_section = 0;
          }
          return true;
        }
        return false;
      }

      if (watch_input) {
        if (watches && watches->OnEvent(event)) {
          return true;
        }
        return false;
      }

      if (layout_state_ != nullptr && layout_state_->right_panel_active_section == 1) {
        if (watches && watches->OnEvent(event)) {
          return true;
        }
        if (sidebar && sidebar->OnEvent(event)) {
          return true;
        }
        return false;
      }

      if (sidebar && sidebar->OnEvent(event)) {
        return true;
      }
      if (watches && watches->OnEvent(event)) {
        return true;
      }
      return false;
    }
    return ChildAt(0)->OnEvent(std::move(event));
  }

  bool Focusable() const override {
    if (children_.empty()) {
      return false;
    }
    if (app_mode_ != nullptr && *app_mode_ == AppMode::kDebug) {
      return (children_.size() > 0 && children_[0] && children_[0]->Focusable()) ||
             (children_.size() > 1 && children_[1] && children_[1]->Focusable());
    }
    return children_[0] && children_[0]->Focusable();
  }

  Component ActiveChild() override {
    if (children_.empty()) {
      return nullptr;
    }
    if (app_mode_ == nullptr || *app_mode_ != AppMode::kDebug) {
      return ChildAt(0);
    }
    if (layout_state_ != nullptr &&
        (layout_state_->right_panel_active_section == 1 ||
         is_watch_input_focus(layout_state_->text_input_focus))) {
      return ChildAt(1);
    }
    return ChildAt(0);
  }

  void SetActiveChild(ComponentBase* child) override {
    if (children_.empty()) {
      return;
    }
    if (child == nullptr) {
      return;
    }
    for (std::size_t i = 0; i < children_.size(); ++i) {
      if (children_[i].get() == child) {
        if (layout_state_ != nullptr) {
          layout_state_->right_panel_active_section = static_cast<int>(i);
        }
        return;
      }
    }
    Component active = ActiveChild();
    if (active) {
      active->SetActiveChild(child);
      for (std::size_t i = 0; i < children_.size(); ++i) {
        if (children_[i].get() == active.get()) {
          if (layout_state_ != nullptr) {
            layout_state_->right_panel_active_section = static_cast<int>(i);
          }
          return;
        }
      }
    }
  }

 private:
  AppMode* app_mode_;
  int* outline_height_;
  MainLayoutState* layout_state_;
};

Component MakeRightPanel(AppMode* app_mode, Component outline, Component watches,
                         int* outline_height, MainLayoutState* layout_state) {
  return Make<RightPanelLayout>(app_mode, std::move(outline), std::move(watches),
                                outline_height, layout_state);
}

Component MakeVSplitLeft(Component main, Component back, int* main_size) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Left;
  options.main_size = main_size;
  options.separator_func = [] { return SplitSeparatorVertical(); };
  return ResizableSplit(std::move(options));
}

Component MakeVSplitRight(Component main, Component back, int* main_size) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Right;
  options.main_size = main_size;
  options.separator_func = [] { return SplitSeparatorVertical(); };
  return ResizableSplit(std::move(options));
}

Component MakeHSplitBottom(Component main, Component back, int* main_size) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Down;
  options.main_size = main_size;
  options.separator_func = [] { return SplitSeparatorHorizontal(); };
  return ResizableSplit(std::move(options));
}

Component MakeHSplitTop(Component main, Component back, int* main_size) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Up;
  options.main_size = main_size;
  options.separator_func = [] { return SplitSeparatorHorizontal(); };
  return ResizableSplit(std::move(options));
}

Component WrapClearInputFocus(Component child, MainLayoutState* layout_state) {
  return CatchEvent(
      Renderer(std::move(child), [child = child] { return child->Render(); }),
      [layout_state](Event event) {
        if (layout_state == nullptr) {
          return false;
        }
        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          if (!is_search_input_focus(layout_state->text_input_focus) &&
              !is_watch_input_focus(layout_state->text_input_focus)) {
            layout_state->text_input_focus = TextInputFocus::None;
          }
        }
        return false;
      });
}

std::string status_shortcuts(AppMode mode) {
  if (mode == AppMode::kDebug) {
    return "F1 atajos  F2 debug  F3 workspace  F5 continuar  F7 buscar  F8 outline  F10 step  F11 into";
  }
  return "F1 atajos  F2 debug  F3 workspace  F4 terminal  F7 buscar  F8 outline  F9 problemas";
}

}  // namespace

Component MakeMainLayout(AppMode* app_mode, DebugModel* model,
                         WorkspaceModel* workspace, SourceViewState* source_state,
                         FocusManagerState* focus,
                         std::shared_ptr<ISymbolProvider> symbols,
                         CommandCallback on_command, MainLayoutState* layout_state,
                         StopDebugCallback on_stop_debug, ShellSession* shell,
                         WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer) {
  auto split_state = std::make_shared<LayoutState>();
  auto focus_sync = std::make_shared<FocusSyncState>();
  split_state->left_width = 22;
  split_state->right_width = 22;
  split_state->bottom_height = 8;

  auto file_tree = MakeFileTreePanel(model, workspace, focus, indexer, on_command,
                                     layout_state);
  auto editor = MakeEditorPanel(workspace, focus, layout_state, symbols, indexer,
                                symbol_indexer);
  auto source = MakeSourcePanel(model, source_state, on_command, focus, layout_state);
  auto center = MakeModeLayout(app_mode, editor, source);

  auto outline = MakeOutlinePanel(workspace, focus, symbols, layout_state);
  auto search = MakeSearchPanel(workspace, model, focus, layout_state, indexer,
                                &layout_state->right_sidebar);
  auto call_hierarchy =
      MakeCallHierarchyPanel(workspace, focus, layout_state, &layout_state->right_sidebar, symbols);
  auto sidebar = MakeRightSidebarPanel(outline, search, call_hierarchy,
                                       &layout_state->right_sidebar, layout_state);
  auto watches = MakeWatchesPanel(model, on_command, layout_state, on_stop_debug, focus);
  auto right_panel =
      MakeRightPanel(app_mode, sidebar, watches, &split_state->outline_height, layout_state);

  auto explorer_and_center = MakeVSplitLeft(file_tree, center, &split_state->left_width);
  auto workspace_area =
      MakeVSplitRight(right_panel, explorer_and_center, &split_state->right_width);
  workspace_area = WrapClearInputFocus(std::move(workspace_area), layout_state);
  auto workspace_only = workspace_area;
  auto workspace_no_secondary =
      WrapClearInputFocus(explorer_and_center, layout_state);

  auto console = MakeConsolePanel(app_mode, model, shell, on_command, layout_state, focus,
                                  &split_state->bottom_height);
  auto with_console =
      MakeHSplitBottom(console, workspace_area, &split_state->bottom_height);
  auto with_console_no_secondary =
      MakeHSplitBottom(console, workspace_no_secondary, &split_state->bottom_height);

  auto with_focus_sync = CatchEvent(
      with_console,
      [split_state, app_mode, focus, layout_state, focus_sync](Event event) {
        if (event != Event::Custom) {
          return false;
        }

        const AppMode mode = app_mode != nullptr ? *app_mode : AppMode::kNormal;
        const bool focus_dirty =
            focus != nullptr &&
            (!focus_sync->initialized || focus_sync->region != focus->region ||
             focus_sync->mode != mode);

        if (!focus_dirty &&
            (layout_state == nullptr || !layout_state->focus_sync_needed)) {
          return false;
        }

        sync_panel_focus(focus_sync.get(), app_mode, focus, layout_state);
        if (layout_state != nullptr) {
          layout_state->focus_sync_needed = false;
        }
        return false;
      });

  return Renderer(with_focus_sync, [=] {
    const bool show_secondary =
        layout_state == nullptr || layout_state->app_settings == nullptr ||
        layout_state->app_settings->secondary_panel_enabled;
    Element main;
    if (layout_state != nullptr && layout_state->console_visible) {
      main = show_secondary ? with_focus_sync->Render()
                            : with_console_no_secondary->Render();
    } else {
      main = show_secondary ? workspace_only->Render()
                            : workspace_no_secondary->Render();
    }

    std::string status_msg = model->status_message;
    if (app_mode != nullptr && *app_mode == AppMode::kNormal && workspace != nullptr &&
        !workspace->status_message.empty()) {
      status_msg = workspace->status_message;
    }

    if (symbols && symbols->supports_diagnostics() && layout_state != nullptr &&
        !layout_state->diagnostics_panel_visible) {
      const uint64_t revision = symbols->diagnostics_revision();
      if (revision != split_state->last_diag_revision) {
        split_state->last_diag_revision = revision;
        count_workspace_diagnostics(symbols->workspace_diagnostics(),
                                    &split_state->diag_errors, &split_state->diag_warnings);
      }
      if (split_state->diag_errors > 0 || split_state->diag_warnings > 0) {
        status_msg += "  │ ";
        if (split_state->diag_errors > 0) {
          status_msg += std::to_string(split_state->diag_errors) +
                        (split_state->diag_errors == 1 ? " error" : " errores");
        }
        if (split_state->diag_warnings > 0) {
          if (split_state->diag_errors > 0) {
            status_msg += ", ";
          }
          status_msg += std::to_string(split_state->diag_warnings) +
                        (split_state->diag_warnings == 1 ? " aviso" : " avisos");
        }
      }
    }

    std::string focus_label;
    if (focus != nullptr) {
      focus_label = std::string("[") + focus->region_label() + "] ";
    }

    const AppMode mode = app_mode != nullptr ? *app_mode : AppMode::kNormal;

    Element status = hbox({
        text(" tide ") | bold | color(theme::Accent()),
        text(" │ "),
        text(focus_label + status_msg) | flex | color(theme::Header()),
        text(status_shortcuts(mode)) | color(theme::Muted()),
    }) | bgcolor(theme::StatusBar());

    return vbox({main | flex | bgcolor(theme::PanelBg()), status});
  });
}

}  // namespace tgdb
