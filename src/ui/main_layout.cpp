#include "ui/git_panel.hpp"
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
#include "ui/welcome_screen.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct LayoutState {
  int left_width = 22;
  int right_width = 22;
  int bottom_height = 8;
  int outline_height = 12;
  uint64_t last_diag_revision = 0;
  uint64_t last_diag_view_token = 0;
  std::string last_diag_path;
  int diag_errors = 0;
  int diag_warnings = 0;
  Box left_sep_box;
  Box right_sep_box;
  Box bottom_sep_box;
  bool left_sep_hovered = false;
  bool right_sep_hovered = false;
  bool bottom_sep_hovered = false;
  bool split_dragging = false;
  int split_drag_kind = 0;  // 1=left, 2=right, 3=bottom
  int split_drag_start_pos = 0;
  int split_drag_start_size = 0;
  bool show_right_split = true;
  bool show_bottom_split = true;
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
    if (layout_state->console_tabs.selected_tab == ConsolePanelTabs::kPerformance) {
      layout_state->text_input_focus = TextInputFocus::None;
      return;
    }
    if (layout_state->console_tabs.selected_tab == ConsolePanelTabs::kTerminal) {
      layout_state->text_input_focus = TextInputFocus::Console;
    }
    return;
  }

  switch (layout_state->text_input_focus) {
    case TextInputFocus::SearchQuery:
    case TextInputFocus::SearchReplace:
    case TextInputFocus::SearchPath:
    case TextInputFocus::SearchInclude:
    case TextInputFocus::SearchExclude:
      if (focus->region != FocusRegion::Terminal || !search_tab_active(layout_state)) {
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

// Overlay Git: ambos hijos en el árbol FTXUI (reflect() y Draw requieren Component->Render()
// desde OnRender, no desde un Renderer huérfano de un solo argumento).
class GitOverlayLayout : public ComponentBase {
 public:
  GitOverlayLayout(MainLayoutState* layout_state, Component main_child, Component git_child)
      : layout_state_(layout_state) {
    Add(std::move(main_child));
    Add(std::move(git_child));
  }

  Element OnRender() override {
    if (children_.empty()) {
      return text("");
    }
    if (layout_state_ != nullptr && layout_state_->git_page_visible && children_.size() > 1) {
      Element git = children_[1]->Render();
      // Pantalla Git sin dibujar el workspace debajo (evita dbox + doble DOM en Draw).
      return git | flex | bgcolor(theme::PanelBg());
    }
    return children_.front()->Render();
  }

  bool OnEvent(Event event) override {
    if (children_.empty()) {
      return false;
    }
    if (layout_state_ != nullptr && layout_state_->git_page_visible && children_.size() > 1) {
      return children_[1]->OnEvent(std::move(event));
    }
    return children_.front()->OnEvent(std::move(event));
  }

  bool Focusable() const override {
    if (layout_state_ != nullptr && layout_state_->git_page_visible && children_.size() > 1) {
      return children_[1]->Focusable();
    }
    return !children_.empty() && children_.front()->Focusable();
  }

  Component ActiveChild() override {
    if (layout_state_ != nullptr && layout_state_->git_page_visible && children_.size() > 1) {
      return children_[1];
    }
    return children_.empty() ? nullptr : children_.front();
  }

  void SetActiveChild(ComponentBase* child) override {
    if (Component active = ActiveChild()) {
      active->SetActiveChild(child);
    }
  }

 private:
  MainLayoutState* layout_state_;
};

Component MakeGitOverlayLayout(MainLayoutState* layout_state, Component main_child,
                               Component git_child) {
  return Make<GitOverlayLayout>(layout_state, std::move(main_child), std::move(git_child));
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

Component MakeVSplitLeft(Component main, Component back, int* main_size,
                         std::shared_ptr<LayoutState> split_state) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Left;
  options.main_size = main_size;
  options.separator_func = [split_state]() {
    const bool dragging = split_state->split_dragging && split_state->split_drag_kind == 1;
    return SplitSeparatorVertical(split_state->left_sep_hovered, dragging,
                                  &split_state->left_sep_box);
  };
  return ResizableSplit(std::move(options));
}

Component MakeVSplitRight(Component main, Component back, int* main_size,
                          std::shared_ptr<LayoutState> split_state) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Right;
  options.main_size = main_size;
  options.separator_func = [split_state]() {
    const bool dragging = split_state->split_dragging && split_state->split_drag_kind == 2;
    return SplitSeparatorVertical(split_state->right_sep_hovered, dragging,
                                  &split_state->right_sep_box);
  };
  return ResizableSplit(std::move(options));
}

Component MakeHSplitBottom(Component main, Component back, int* main_size,
                             std::shared_ptr<LayoutState> split_state) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Down;
  options.main_size = main_size;
  options.separator_func = [split_state]() {
    const bool dragging = split_state->split_dragging && split_state->split_drag_kind == 3;
    return SplitSeparatorHorizontal(split_state->bottom_sep_hovered, dragging,
                                   &split_state->bottom_sep_box);
  };
  return ResizableSplit(std::move(options));
}

Component MakeHSplitTop(Component main, Component back, int* main_size,
                          std::shared_ptr<LayoutState> split_state) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Up;
  options.main_size = main_size;
  options.separator_func = [split_state]() {
    const bool dragging = split_state->split_dragging && split_state->split_drag_kind == 3;
    return SplitSeparatorHorizontal(split_state->bottom_sep_hovered, dragging,
                                   &split_state->bottom_sep_box);
  };
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

std::string status_shortcuts(AppMode mode, bool welcome_visible) {
  if (welcome_visible) {
    return "F1 archivo  Alt+F1 atajos  F2 depurar  F3 workspace";
  }
  if (mode == AppMode::kDebug) {
    return "F1 externo  Alt+F1 atajos  F2 debug  F3 workspace  F5 continuar  F7 buscar  F8 outline  F10 step  F11 into";
  }
  return "F1 externo  Alt+F1 atajos  F2 debug  F3 workspace  F4 terminal  F5 git  F7 buscar  F8 outline  F9 problemas";
}

std::string buffer_text(const EditorBuffer& buffer) {
  std::string text;
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    if (i > 0) {
      text.push_back('\n');
    }
    text += buffer.lines[i];
  }
  return text;
}

constexpr int kMinSplitPanelWidth = 12;
constexpr int kMinCenterWidth = 24;
constexpr int kMinBottomHeight = 3;

bool box_hit_left_sep(const Box& box, int x, int y) {
  if (box.IsEmpty()) {
    return false;
  }
  // Padding solo hacia el panel izquierdo; no invadir la scrollbar del panel derecho.
  constexpr int kPadLeft = 1;
  return x >= box.x_min - kPadLeft && x <= box.x_max && y >= box.y_min && y <= box.y_max;
}

bool box_hit_right_sep(const Box& box, int x, int y) {
  if (box.IsEmpty()) {
    return false;
  }
  // Padding solo hacia el panel derecho; no invadir la scrollbar del editor (panel izquierdo).
  constexpr int kPadRight = 1;
  return x >= box.x_min && x <= box.x_max + kPadRight && y >= box.y_min && y <= box.y_max;
}

// Separador horizontal (terminal): solo padding hacia arriba para no tapar las pestañas.
bool box_hit_bottom_sep(const Box& box, int x, int y) {
  if (box.IsEmpty()) {
    return false;
  }
  constexpr int kPaddingAbove = 1;
  return x >= box.x_min && x <= box.x_max && y >= box.y_min - kPaddingAbove &&
         y <= box.y_max;
}

bool update_split_hover(LayoutState* state, int x, int y) {
  if (state == nullptr) {
    return false;
  }
  bool changed = false;
  const auto set_left = [&](bool* hovered) {
    const bool next = box_hit_left_sep(state->left_sep_box, x, y);
    if (*hovered != next) {
      *hovered = next;
      changed = true;
    }
  };
  const auto set_right = [&](bool* hovered) {
    const bool next = box_hit_right_sep(state->right_sep_box, x, y);
    if (*hovered != next) {
      *hovered = next;
      changed = true;
    }
  };
  set_left(&state->left_sep_hovered);
  if (state->show_right_split) {
    set_right(&state->right_sep_hovered);
  } else {
    state->right_sep_hovered = false;
  }
  if (state->show_bottom_split) {
    const bool next = box_hit_bottom_sep(state->bottom_sep_box, x, y);
    if (state->bottom_sep_hovered != next) {
      state->bottom_sep_hovered = next;
      changed = true;
    }
  } else {
    state->bottom_sep_hovered = false;
  }
  return changed;
}

void apply_split_drag(LayoutState* state, int x, int y, int screen_w, int screen_h) {
  if (state == nullptr || !state->split_dragging) {
    return;
  }
  switch (state->split_drag_kind) {
    case 1: {
      const int max_left = std::max(kMinSplitPanelWidth,
                                    screen_w - state->right_width - kMinCenterWidth - 2);
      const int delta = x - state->split_drag_start_pos;
      state->left_width =
          std::max(kMinSplitPanelWidth, std::min(state->split_drag_start_size + delta, max_left));
      break;
    }
    case 2: {
      const int max_right = std::max(kMinSplitPanelWidth,
                                     screen_w - state->left_width - kMinCenterWidth - 2);
      const int delta = state->split_drag_start_pos - x;
      state->right_width =
          std::max(kMinSplitPanelWidth, std::min(state->split_drag_start_size + delta, max_right));
      break;
    }
    case 3: {
      const int max_bottom = std::max(kMinBottomHeight, screen_h / 2);
      const int delta = state->split_drag_start_pos - y;
      state->bottom_height =
          std::max(kMinBottomHeight, std::min(state->split_drag_start_size + delta, max_bottom));
      break;
    }
    default:
      break;
  }
}

bool handle_split_mouse(LayoutState* state, MainLayoutState* layout_state, Event event,
                        int screen_w, int screen_h) {
  if (state == nullptr || !event.is_mouse()) {
    return false;
  }
  Mouse& mouse = event.mouse();

  if (state->split_dragging) {
    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Released) {
      state->split_dragging = false;
      state->split_drag_kind = 0;
      return true;
    }
    if (mouse.motion == Mouse::Moved) {
      apply_split_drag(state, mouse.x, mouse.y, screen_w, screen_h);
      return true;
    }
    return true;
  }

  if (mouse.motion == Mouse::Moved) {
    if (update_split_hover(state, mouse.x, mouse.y) && layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
    return false;
  }

  if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
    return false;
  }

  if (box_hit_left_sep(state->left_sep_box, mouse.x, mouse.y)) {
    state->split_dragging = true;
    state->split_drag_kind = 1;
    state->split_drag_start_pos = mouse.x;
    state->split_drag_start_size = state->left_width;
    return true;
  }
  if (state->show_right_split && box_hit_right_sep(state->right_sep_box, mouse.x, mouse.y)) {
    state->split_dragging = true;
    state->split_drag_kind = 2;
    state->split_drag_start_pos = mouse.x;
    state->split_drag_start_size = state->right_width;
    return true;
  }
  if (state->show_bottom_split && box_hit_bottom_sep(state->bottom_sep_box, mouse.x, mouse.y)) {
    state->split_dragging = true;
    state->split_drag_kind = 3;
    state->split_drag_start_pos = mouse.y;
    state->split_drag_start_size = state->bottom_height;
    return true;
  }
  return false;
}

}  // namespace

Component MakeMainLayout(AppMode* app_mode, DebugModel* model,
                         WorkspaceModel* workspace, SourceViewState* source_state,
                         FocusManagerState* focus,
                         std::shared_ptr<ISymbolProvider> symbols,
                         CommandCallback on_command, MainLayoutState* layout_state,
                         StopDebugCallback on_stop_debug, ShellSession* shell,
                         WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer,
                         ShellLaunchConfigProvider shell_launch_config, GitService* git_service,
                         GitPanelState* git_panel_state, WelcomeScreenState* welcome_state,
                         std::function<void()> on_welcome_external_file,
                         std::function<void()> on_welcome_debug,
                         std::function<void()> on_welcome_workspace) {
  auto split_state = std::make_shared<LayoutState>();
  auto focus_sync = std::make_shared<FocusSyncState>();
  split_state->left_width = 22;
  split_state->right_width = 22;
  split_state->bottom_height = 8;

  auto file_tree = MakeFileTreePanel(model, workspace, focus, indexer, on_command,
                                     layout_state);
  auto editor = MakeEditorPanel(workspace, focus, layout_state, symbols, indexer,
                                symbol_indexer, git_service);
  auto source = MakeSourcePanel(model, source_state, on_command, focus, layout_state);
  auto center = MakeModeLayout(app_mode, editor, source);

  auto git_panel = MakeGitPanel(git_service, git_panel_state, layout_state, focus);

  auto welcome_screen =
      MakeWelcomeScreen(layout_state, welcome_state, on_welcome_external_file, on_welcome_debug,
                        on_welcome_workspace);

  auto outline = MakeOutlinePanel(workspace, focus, symbols, layout_state);
  auto sidebar = MakeRightSidebarPanel(outline, layout_state);
  auto watches = MakeWatchesPanel(model, on_command, layout_state, on_stop_debug, focus);
  auto right_panel =
      MakeRightPanel(app_mode, sidebar, watches, &split_state->outline_height, layout_state);

  auto explorer_and_center =
      MakeVSplitLeft(file_tree, center, &split_state->left_width, split_state);
  auto workspace_area =
      MakeVSplitRight(right_panel, explorer_and_center, &split_state->right_width, split_state);
  workspace_area = WrapClearInputFocus(std::move(workspace_area), layout_state);
  auto workspace_only = workspace_area;
  auto workspace_no_secondary =
      WrapClearInputFocus(explorer_and_center, layout_state);

  auto console = MakeConsolePanel(app_mode, model, shell, on_command, layout_state, focus,
                                  &split_state->bottom_height, shell_launch_config, workspace,
                                  symbols, indexer, &layout_state->right_sidebar);
  auto with_console =
      MakeHSplitBottom(console, workspace_area, &split_state->bottom_height, split_state);
  auto with_console_no_secondary =
      MakeHSplitBottom(console, workspace_no_secondary, &split_state->bottom_height, split_state);

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

  if (layout_state != nullptr) {
    layout_state->split_mouse_handler = [split_state, layout_state](Event event) {
      const int screen_w =
          layout_state != nullptr && layout_state->terminal_width
              ? layout_state->terminal_width()
              : 80;
      const int screen_h =
          layout_state != nullptr && layout_state->terminal_height
              ? layout_state->terminal_height()
              : 24;
      return handle_split_mouse(split_state.get(), layout_state, event, screen_w, screen_h);
    };
  }

  auto layout_root = Renderer(with_focus_sync, [=] {
    const bool show_secondary =
        layout_state == nullptr || layout_state->app_settings == nullptr ||
        layout_state->app_settings->secondary_panel_enabled;
    split_state->show_right_split = show_secondary;
    split_state->show_bottom_split =
        layout_state != nullptr && layout_state->console_visible;
    const bool git_page_visible =
        layout_state != nullptr && layout_state->git_page_visible;
    const bool welcome_visible =
        layout_state != nullptr && layout_state->welcome_visible;
    Element main;
    if (welcome_visible) {
      main = welcome_screen->Render() | flex | bgcolor(theme::PanelBg());
    } else if (layout_state != nullptr && layout_state->console_visible) {
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

    if (!git_page_visible && symbols && symbols->supports_diagnostics() && layout_state != nullptr &&
        !problems_tab_active(layout_state) && workspace != nullptr) {
      workspace->ensure_buffer();
      const uint64_t revision = symbols->diagnostics_revision();
      const uint64_t view_token = workspace->buffer.view_token;
      const std::string& active_path = workspace->buffer.path;
      if (revision != split_state->last_diag_revision ||
          view_token != split_state->last_diag_view_token ||
          active_path != split_state->last_diag_path) {
        split_state->last_diag_revision = revision;
        split_state->last_diag_view_token = view_token;
        split_state->last_diag_path = active_path;
        split_state->diag_errors = 0;
        split_state->diag_warnings = 0;
        if (!active_path.empty()) {
          std::vector<std::string> workspace_files;
          if (indexer != nullptr) {
            const auto snapshot = indexer->snapshot();
            if (snapshot) {
              workspace_files = snapshot->files;
            }
          }
          const auto docs = diagnostics_for_translation_unit(
              symbols->workspace_diagnostics(), active_path, workspace->root, workspace_files,
              buffer_text(workspace->buffer));
          count_workspace_diagnostics(docs, &split_state->diag_errors,
                                      &split_state->diag_warnings);
        }
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
        text(status_shortcuts(mode, welcome_visible)) | color(theme::Muted()),
    }) | bgcolor(theme::StatusBar());

    return vbox({main | flex | bgcolor(theme::PanelBg()), status});
  });

  return MakeGitOverlayLayout(layout_state, layout_root, git_panel);
}

}  // namespace tgdb
