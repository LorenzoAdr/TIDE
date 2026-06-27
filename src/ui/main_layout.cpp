#include "ui/main_layout.hpp"

#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/console_panel.hpp"
#include "ui/editor_panel.hpp"
#include "ui/file_tree_panel.hpp"
#include "ui/outline_panel.hpp"
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

  layout_state->text_input_focus =
      focus->region == FocusRegion::Terminal ? TextInputFocus::Console
                                             : TextInputFocus::None;
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

  Element OnRender() override { return VisibleChild()->Render(); }

  bool OnEvent(Event event) override { return VisibleChild()->OnEvent(std::move(event)); }

  bool Focusable() const override { return VisibleChild()->Focusable(); }

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
                   int* outline_height)
      : app_mode_(app_mode), outline_height_(outline_height) {
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
      if (ChildAt(1)->OnEvent(event)) {
        return true;
      }
      return ChildAt(0)->OnEvent(std::move(event));
    }
    return ChildAt(0)->OnEvent(std::move(event));
  }

  bool Focusable() const override {
    if (app_mode_ != nullptr && *app_mode_ == AppMode::kDebug) {
      return children_[0]->Focusable() || children_[1]->Focusable();
    }
    return children_[0]->Focusable();
  }

  Component ActiveChild() override {
    if (app_mode_ != nullptr && *app_mode_ == AppMode::kDebug) {
      if (Component watches = ChildAt(1); watches->Focused()) {
        return watches->ActiveChild() ? watches->ActiveChild() : watches;
      }
    }
    return ChildAt(0)->ActiveChild() ? ChildAt(0)->ActiveChild() : ChildAt(0);
  }

  void SetActiveChild(ComponentBase* child) override {
    if (Component active = ActiveChild()) {
      active->SetActiveChild(child);
    }
  }

 private:
  AppMode* app_mode_;
  int* outline_height_;
};

Component MakeRightPanel(AppMode* app_mode, Component outline, Component watches,
                         int* outline_height) {
  return Make<RightPanelLayout>(app_mode, std::move(outline), std::move(watches),
                                outline_height);
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
          layout_state->text_input_focus = TextInputFocus::None;
        }
        return false;
      });
}

std::string status_shortcuts(AppMode mode) {
  if (mode == AppMode::kDebug) {
    return "F5 ▶  F10 step  Ctrl+B bp  F2 debug  F3 workspace  Ctrl+A/E/O  Ctrl+P  Ctrl+T  Ctrl+Q salir ";
  }
  return "F2 debug  F3 workspace  Ctrl+A/E/O  Ctrl+S  Ctrl+P  Ctrl+Q salir ";
}

}  // namespace

Component MakeMainLayout(AppMode* app_mode, DebugModel* model,
                         WorkspaceModel* workspace, SourceViewState* source_state,
                         FocusManagerState* focus,
                         std::shared_ptr<ISymbolProvider> symbols,
                         CommandCallback on_command, MainLayoutState* layout_state,
                         StopDebugCallback on_stop_debug) {
  auto split_state = std::make_shared<LayoutState>();
  auto focus_sync = std::make_shared<FocusSyncState>();
  auto splits_initialized = std::make_shared<bool>(false);

  auto file_tree = MakeFileTreePanel(model, workspace, focus, on_command);
  auto editor = MakeEditorPanel(workspace, focus);
  auto source = MakeSourcePanel(model, source_state, on_command);
  auto center = MakeModeLayout(app_mode, editor, source);

  auto outline = MakeOutlinePanel(workspace, focus, symbols);
  auto watches = MakeWatchesPanel(model, on_command, layout_state, on_stop_debug);
  auto right_panel =
      MakeRightPanel(app_mode, outline, watches, &split_state->outline_height);

  auto explorer_and_center = MakeVSplitLeft(file_tree, center, &split_state->left_width);
  auto workspace_area =
      MakeVSplitRight(right_panel, explorer_and_center, &split_state->right_width);
  workspace_area = WrapClearInputFocus(std::move(workspace_area), layout_state);

  auto console = MakeConsolePanel(model, on_command, layout_state);
  auto with_console =
      MakeHSplitBottom(console, workspace_area, &split_state->bottom_height);

  auto with_focus_sync = CatchEvent(
      with_console,
      [split_state, splits_initialized, app_mode, focus, layout_state, focus_sync](
          Event event) {
        if (event == Event::Custom && !*splits_initialized) {
          split_state->left_width = 22;
          split_state->right_width = 22;
          split_state->bottom_height = 8;
          *splits_initialized = true;
        }

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

  return Renderer(with_focus_sync, [app_mode, model, workspace, focus, with_focus_sync,
                                    workspace_area, layout_state] {
    Element main = layout_state->console_visible ? with_focus_sync->Render()
                                                 : workspace_area->Render();

    std::string status_msg = model->status_message;
    if (app_mode != nullptr && *app_mode == AppMode::kNormal && workspace != nullptr &&
        !workspace->status_message.empty()) {
      status_msg = workspace->status_message;
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
