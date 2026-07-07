#include "ui/git_panel.hpp"
#include "ui/hover_effects.hpp"
#include "ui/main_layout.hpp"
#include "ui/status_layout_popover.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>

#include "ftxui/component/component.hpp"
#include "packet_monitor/pkt_monitor_service.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "lsp/diagnostics.hpp"
#include "terminal/shell_session.hpp"
#include "ui/console_panel.hpp"
#include "ui/editor_panel.hpp"
#include "ui/file_tree_panel.hpp"
#include "ui/call_hierarchy_panel.hpp"
#include "ui/clickable.hpp"
#include "ui/outline_panel.hpp"
#include "ui/right_sidebar_panel.hpp"
#include "ui/search_panel.hpp"
#include "ui/panel.hpp"
#include "ui/source_panel.hpp"
#include "ui/theme.hpp"
#include "ui/watches_panel.hpp"
#include "ui/welcome_screen.hpp"
#include "i18n/tr.hpp"
#include "util/bundled_tools.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

int64_t steady_now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

MainLayoutState::MainLayoutState()
    : packet_monitor_service(std::make_unique<packet_monitor::PacketMonitorService>()) {}

MainLayoutState::~MainLayoutState() = default;

namespace {

struct LayoutState {
  int left_width = 22;
  int right_width = 22;
  int bottom_height = 8;
  int outline_height = 12;
  int editor_left_width = 40;
  uint64_t last_diag_revision = 0;
  std::string last_diag_path;
  int diag_errors = 0;
  int diag_warnings = 0;
  Box left_sep_box;
  Box right_sep_box;
  Box bottom_sep_box;
  Box editor_center_sep_box;
  bool left_sep_hovered = false;
  bool right_sep_hovered = false;
  bool bottom_sep_hovered = false;
  bool editor_center_sep_hovered = false;
  bool split_dragging = false;
  int split_drag_kind = 0;  // 1=left, 2=right, 3=bottom, 4=editor center
  int split_drag_start_pos = 0;
  int split_drag_start_size = 0;
  bool show_left_split = true;
  bool show_right_split = true;
  bool show_bottom_split = true;
  bool show_editor_center_split = false;
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

  if (is_editor_focus_region(focus->region) &&
      layout_state->text_input_focus == TextInputFocus::Console) {
    layout_state->text_input_focus = TextInputFocus::None;
  }

  if (focus->region == FocusRegion::Terminal) {
    if (mode == AppMode::kDebug &&
        layout_state->console_tabs.selected_tab == ConsolePanelTabs::kDebug) {
      layout_state->text_input_focus = TextInputFocus::Console;
      return;
    }
    if (layout_state->console_tabs.selected_tab == ConsolePanelTabs::kPerformance) {
      layout_state->text_input_focus = TextInputFocus::None;
      return;
    }
    if (layout_state->console_tabs.selected_tab == ConsolePanelTabs::kTerminal ||
        layout_state->console_tabs.selected_tab == ConsolePanelTabs::kCoreAnalyzer) {
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
    case TextInputFocus::BreakpointHw:
      if (focus->region != FocusRegion::RightPanel) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      break;
    case TextInputFocus::EditorFind:
    case TextInputFocus::EditorGotoLine:
    case TextInputFocus::EditorCompletion:
      if (!is_editor_focus_region(focus->region)) {
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

class EditorCenterLayout : public ComponentBase {
 public:
  EditorCenterLayout(AppMode* app_mode, DebugModel* model, Component primary_editor,
                     Component secondary_editor, Component source_panel,
                     WorkspaceModel* secondary_workspace,
                     std::shared_ptr<LayoutState> split_state)
      : app_mode_(app_mode),
        model_(model),
        secondary_workspace_(secondary_workspace),
        split_state_(std::move(split_state)) {
    Add(std::move(primary_editor));
    Add(std::move(secondary_editor));
    Add(std::move(source_panel));
  }

  Element OnRender() override {
    if (children_.size() < 3) {
      return text("");
    }
    if (debug_source_only()) {
      return children_[2]->Render() | flex;
    }
    const bool split =
        secondary_workspace_ != nullptr && !secondary_workspace_->tabs.empty();
    if (split_state_ != nullptr) {
      split_state_->show_editor_center_split = split;
    }
    if (!split) {
      return children_[0]->Render() | flex;
    }
    const bool dragging =
        split_state_ != nullptr && split_state_->split_dragging &&
        split_state_->split_drag_kind == 4;
    Element sep = SplitSeparatorVertical(
        split_state_ != nullptr && split_state_->editor_center_sep_hovered, dragging,
        split_state_ != nullptr ? &split_state_->editor_center_sep_box : nullptr);
    return hbox({
               children_[0]->Render() | size(WIDTH, EQUAL, split_state_->editor_left_width),
               sep,
               children_[1]->Render() | flex,
           }) |
           flex;
  }

  bool OnEvent(Event event) override {
    if (children_.size() < 3) {
      return false;
    }
    if (debug_source_only()) {
      return children_[2]->OnEvent(std::move(event));
    }
    const bool split =
        secondary_workspace_ != nullptr && !secondary_workspace_->tabs.empty();
    if (!split) {
      return children_[0]->OnEvent(std::move(event));
    }
    if (children_[0]->OnEvent(event)) {
      return true;
    }
    return children_[1]->OnEvent(std::move(event));
  }

  bool Focusable() const override { return true; }

  Component ActiveChild() override {
    if (children_.empty()) {
      return nullptr;
    }
    if (debug_source_only() && children_.size() > 2) {
      return children_[2];
    }
    const bool split =
        secondary_workspace_ != nullptr && !secondary_workspace_->tabs.empty();
    return split ? children_[1] : children_[0];
  }

 private:
  bool debug_source_only() const {
    return app_mode_ != nullptr && *app_mode_ == AppMode::kDebug &&
           (model_ == nullptr || !model_->is_post_mortem);
  }

  AppMode* app_mode_;
  DebugModel* model_;
  WorkspaceModel* secondary_workspace_;
  std::shared_ptr<LayoutState> split_state_;
};

Component MakeEditorCenterLayout(AppMode* app_mode, DebugModel* model, Component primary_editor,
                                 Component secondary_editor, Component source_panel,
                                 WorkspaceModel* secondary_workspace,
                                 std::shared_ptr<LayoutState> split_state) {
  return Make<EditorCenterLayout>(app_mode, model, std::move(primary_editor),
                                    std::move(secondary_editor), std::move(source_panel),
                                    secondary_workspace, std::move(split_state));
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
    if (children_.empty()) {
      return text("");
    }
    const bool debug = app_mode_ != nullptr && *app_mode_ == AppMode::kDebug;
    if (!debug || children_.size() < 2) {
      return ChildAt(0)->Render() | flex;
    }
    return vbox({
               ChildAt(0)->Render() | size(HEIGHT, EQUAL, *outline_height_),
               separator(),
               ChildAt(1)->Render() | yflex,
           }) |
           flex;
  }

  bool OnEvent(Event event) override {
    if (children_.size() < 2) {
      return ChildAt(0)->OnEvent(std::move(event));
    }
    const bool debug = app_mode_ != nullptr && *app_mode_ == AppMode::kDebug;
    if (debug) {
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

    return ChildAt(0) && ChildAt(0)->OnEvent(std::move(event));
  }

  bool Focusable() const override {
    if (children_.empty()) {
      return false;
    }
    const bool debug = app_mode_ != nullptr && *app_mode_ == AppMode::kDebug;
    if (debug) {
      return (children_.size() > 0 && children_[0] && children_[0]->Focusable()) ||
             (children_.size() > 1 && children_[1] && children_[1]->Focusable());
    }
    return children_.size() > 0 && children_[0] && children_[0]->Focusable();
  }

  Component ActiveChild() override {
    if (children_.empty()) {
      return nullptr;
    }
    const bool debug = app_mode_ != nullptr && *app_mode_ == AppMode::kDebug;
    if (!debug) {
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

struct StatusBarUiState {
  Box chg_dir_box;
  Box index_box;
  SplitToolbarButtonBoxes launch_btn;
  SplitToolbarButtonBoxes debug_btn;
  Box layout_box;
  Box settings_box;
  Box shortcuts_box;
};

bool index_clangd_ready(const MainLayoutState* layout_state) {
  if (!resolve_clangd().has_value()) {
    return false;
  }
  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->lsp_enabled) {
    return false;
  }
  return true;
}

bool status_bar_hover_id(std::string_view id) {
  return id == press_id::kStatusChgDir || id == press_id::kStatusIndex ||
         id == press_id::kStatusLaunch || id == press_id::kStatusLaunchQuick ||
         id == press_id::kStatusDebug || id == press_id::kStatusDebugQuick ||
         id == press_id::kStatusLayout || id == press_id::kStatusSettings ||
         id == press_id::kStatusShortcuts;
}

bool handle_status_bar_mouse(StatusBarUiState* state, MainLayoutState* layout_state,
                             Event event) {
  if (state == nullptr || layout_state == nullptr || !event.is_mouse()) {
    return false;
  }
  const Mouse& mouse = event.mouse();
  if (layout_state->status_layout_popover.open) {
    if (HandleStatusLayoutPopoverMouse(&layout_state->status_layout_popover, layout_state,
                                       state->layout_box, event)) {
      return true;
    }
  }
  if (mouse.motion == Mouse::Moved) {
    return update_panel_hover(
        layout_state, mouse.x, mouse.y,
        {{press_id::kStatusChgDir, &state->chg_dir_box},
         {press_id::kStatusIndex, &state->index_box},
         {press_id::kStatusLaunch, &state->launch_btn.main},
         {press_id::kStatusLaunchQuick, &state->launch_btn.arrow},
         {press_id::kStatusDebug, &state->debug_btn.main},
         {press_id::kStatusDebugQuick, &state->debug_btn.arrow},
         {press_id::kStatusLayout, &state->layout_box},
         {press_id::kStatusSettings, &state->settings_box},
         {press_id::kStatusShortcuts, &state->shortcuts_box}},
        status_bar_hover_id);
  }
  if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
    return false;
  }
  if (state->chg_dir_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusChgDir);
    if (layout_state->status_open_source_substitute) {
      layout_state->status_open_source_substitute();
    }
    return true;
  }
  if (state->index_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusIndex);
    if (layout_state->status_reindex_project) {
      layout_state->status_reindex_project();
    }
    return true;
  }
  if (state->launch_btn.main.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusLaunch);
    if (layout_state->status_open_launch) {
      layout_state->status_open_launch();
    }
    return true;
  }
  if (state->launch_btn.arrow.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusLaunchQuick);
    if (layout_state->status_quick_launch) {
      layout_state->status_quick_launch();
    }
    return true;
  }
  if (state->debug_btn.main.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusDebug);
    if (layout_state->status_open_debug) {
      layout_state->status_open_debug();
    }
    return true;
  }
  if (state->debug_btn.arrow.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusDebugQuick);
    if (layout_state->status_quick_debug) {
      layout_state->status_quick_debug();
    }
    return true;
  }
  if (state->layout_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusLayout);
    layout_state->status_layout_popover.open = !layout_state->status_layout_popover.open;
    layout_state->request_ui_tick = true;
    return true;
  }
  if (state->settings_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusSettings);
    if (layout_state->status_open_settings) {
      layout_state->status_open_settings();
    }
    return true;
  }
  if (state->shortcuts_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusShortcuts);
    if (layout_state->status_open_shortcuts) {
      layout_state->status_open_shortcuts();
    }
    return true;
  }
  return false;
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
  if (!hover_effects_enabled()) {
    return false;
  }
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
  if (state->show_left_split) {
    set_left(&state->left_sep_hovered);
  } else {
    state->left_sep_hovered = false;
  }
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
  if (state->show_editor_center_split) {
    const bool next = box_hit_right_sep(state->editor_center_sep_box, x, y);
    if (state->editor_center_sep_hovered != next) {
      state->editor_center_sep_hovered = next;
      changed = true;
    }
  } else {
    state->editor_center_sep_hovered = false;
  }
  return changed;
}

void apply_split_drag(LayoutState* state, int x, int y, int screen_w, int screen_h) {
  if (state == nullptr || !state->split_dragging) {
    return;
  }
  switch (state->split_drag_kind) {
    case 1: {
      const int right_w = state->show_right_split ? state->right_width : 0;
      const int max_left = std::max(kMinSplitPanelWidth,
                                    screen_w - right_w - kMinCenterWidth - 2);
      const int delta = x - state->split_drag_start_pos;
      state->left_width =
          std::max(kMinSplitPanelWidth, std::min(state->split_drag_start_size + delta, max_left));
      break;
    }
    case 2: {
      const int left_w = state->show_left_split ? state->left_width : 0;
      const int max_right = std::max(kMinSplitPanelWidth,
                                     screen_w - left_w - kMinCenterWidth - 2);
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
    case 4: {
      const int center_w = std::max(kMinCenterWidth,
                                    screen_w - state->left_width - state->right_width - 2);
      const int max_left =
          std::max(kMinSplitPanelWidth, center_w - kMinSplitPanelWidth - 1);
      const int delta = x - state->split_drag_start_pos;
      state->editor_left_width = std::max(
          kMinSplitPanelWidth, std::min(state->split_drag_start_size + delta, max_left));
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

  if (state->show_left_split && box_hit_left_sep(state->left_sep_box, mouse.x, mouse.y)) {
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
  if (state->show_editor_center_split &&
      box_hit_right_sep(state->editor_center_sep_box, mouse.x, mouse.y)) {
    state->split_dragging = true;
    state->split_drag_kind = 4;
    state->split_drag_start_pos = mouse.x;
    state->split_drag_start_size = state->editor_left_width;
    return true;
  }
  return false;
}

}  // namespace

Component MakeMainLayout(AppMode* app_mode, DebugModel* model,
                         WorkspaceModel* workspace, WorkspaceModel* secondary_workspace,
                         SourceViewState* source_state,
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

  if (focus != nullptr) {
    focus->secondary_editor_visible = [secondary_workspace]() {
      return secondary_workspace != nullptr && !secondary_workspace->tabs.empty();
    };
  }

  auto file_tree = MakeFileTreePanel(model, workspace, focus, indexer, on_command,
                                     layout_state, git_service);
  auto editor_primary =
      MakeEditorPanel(workspace, focus, layout_state, symbols, indexer, symbol_indexer, git_service,
                      FocusRegion::Editor, model, on_command,
                      layout_state != nullptr ? &layout_state->primary_editor : nullptr);
  auto editor_secondary = MakeEditorPanel(
      secondary_workspace, focus, layout_state, symbols, indexer, symbol_indexer, git_service,
      FocusRegion::SecondaryEditor, model, on_command,
      layout_state != nullptr ? &layout_state->secondary_editor : nullptr);
  auto source = MakeSourcePanel(model, source_state, on_command, focus, layout_state, symbols);
  auto center = MakeEditorCenterLayout(app_mode, model, editor_primary, editor_secondary, source,
                                       secondary_workspace, split_state);

  auto console = MakeConsolePanel(app_mode, model, shell, on_command, layout_state, focus,
                                  &split_state->bottom_height, shell_launch_config, workspace,
                                  symbols, indexer, symbol_indexer, &layout_state->right_sidebar,
                                  git_service, git_panel_state);
  auto center_with_console =
      MakeHSplitBottom(console, center, &split_state->bottom_height, split_state);
  auto center_column = Renderer([=] {
    if (layout_state != nullptr && layout_state->console_visible) {
      return center_with_console->Render() | flex | bgcolor(theme::PanelBg());
    }
    return center->Render() | flex | bgcolor(theme::PanelBg());
  });

  auto welcome_screen =
      MakeWelcomeScreen(layout_state, welcome_state, on_welcome_external_file, on_welcome_debug,
                        on_welcome_workspace);

  auto outline = MakeOutlinePanel(workspace, focus, symbols, layout_state);
  auto sidebar = MakeRightSidebarPanel(outline, layout_state);
  auto watches = MakeWatchesPanel(model, on_command, layout_state, on_stop_debug, focus, app_mode);
  auto right_panel =
      MakeRightPanel(app_mode, sidebar, watches, &split_state->outline_height, layout_state);

  auto explorer_and_center =
      MakeVSplitLeft(file_tree, center_column, &split_state->left_width, split_state);
  auto workspace_lr =
      MakeVSplitRight(right_panel, explorer_and_center, &split_state->right_width, split_state);
  workspace_lr = WrapClearInputFocus(std::move(workspace_lr), layout_state);
  auto workspace_l =
      WrapClearInputFocus(explorer_and_center, layout_state);
  auto workspace_r =
      WrapClearInputFocus(
          MakeVSplitRight(right_panel, center_column, &split_state->right_width, split_state),
          layout_state);
  auto workspace_none = WrapClearInputFocus(center_column, layout_state);

  auto workspace_picker = Renderer([=] {
    const bool show_left =
        layout_state != nullptr && layout_state->explorer_visible;
    const bool show_secondary =
        layout_state == nullptr || layout_state->app_settings == nullptr ||
        layout_state->app_settings->secondary_panel_enabled;
    split_state->show_left_split = show_left;
    split_state->show_right_split = show_secondary;

    if (show_left && show_secondary) {
      return workspace_lr->Render() | flex | bgcolor(theme::PanelBg());
    }
    if (show_left) {
      return workspace_l->Render() | flex | bgcolor(theme::PanelBg());
    }
    if (show_secondary) {
      return workspace_r->Render() | flex | bgcolor(theme::PanelBg());
    }
    return workspace_none->Render() | flex | bgcolor(theme::PanelBg());
  });

  auto welcome_screen_renderer = Renderer([welcome_screen] {
    return welcome_screen->Render() | flex | bgcolor(theme::PanelBg());
  });

  auto body = Renderer([=] {
    const bool welcome_visible =
        layout_state != nullptr && layout_state->welcome_visible;
    split_state->show_bottom_split =
        layout_state != nullptr && layout_state->console_visible;
    if (welcome_visible) {
      return welcome_screen_renderer->Render();
    }
    return workspace_picker->Render() | flex | bgcolor(theme::PanelBg());
  });

  auto with_focus_sync = CatchEvent(
      body,
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

  auto status_ui = std::make_shared<StatusBarUiState>();
  if (layout_state != nullptr) {
    layout_state->status_bar_mouse_handler =
        [status_ui, layout_state](Event event) {
          return handle_status_bar_mouse(status_ui.get(), layout_state, event);
        };
  }

  auto layout_root = Renderer(with_focus_sync, [=] {
    if (layout_state != nullptr) {
      layout_state->ui_paint_count.fetch_add(1, std::memory_order_relaxed);
      layout_state->ui_perf_monitor.on_paint(steady_now_ms());
    }
    const bool git_tab_open =
        layout_state != nullptr && git_tab_active(layout_state);
    Element main = with_focus_sync->Render() | flex | bgcolor(theme::PanelBg());

    const bool editor_focus =
        focus != nullptr && is_editor_focus_region(focus->region);
    const bool helix_on =
        layout_state != nullptr && layout_state->app_settings != nullptr &&
        layout_state->app_settings->helix_mode_enabled &&
        layout_state->helix_status.active;

    std::string editor_mode_indicator;
    if (editor_focus && helix_on) {
      editor_mode_indicator = i18n::tr("status.editor_mode.helix");
      if (!layout_state->helix_status.mode.empty()) {
        editor_mode_indicator += " " + layout_state->helix_status.mode;
      }
      if (!layout_state->helix_status.pending.empty()) {
        editor_mode_indicator += " " + layout_state->helix_status.pending;
      }
    }

    std::string status_msg;
    if (!editor_focus) {
      status_msg = model->status_message;
      if (app_mode != nullptr && *app_mode == AppMode::kNormal && workspace != nullptr &&
          !workspace->status_message.empty()) {
        status_msg = workspace->status_message;
      }
    }
    if (model->is_post_mortem && !model->core_path.empty()) {
      const auto core_name = std::filesystem::path(model->core_path).filename().string();
      const std::string mode_label =
          model->core_analysis_mode == CoreAnalysisMode::kCoreAnalyzer
              ? i18n::tr("status.core_mode.ca")
              : i18n::tr("status.core_mode.gdb");
      status_msg += i18n::tr_fmt("status.core_suffix", {core_name, mode_label});
    }

    if (!git_tab_open && symbols && symbols->supports_diagnostics() && layout_state != nullptr &&
        !problems_tab_active(layout_state) && workspace != nullptr) {
      workspace->ensure_buffer();
      const uint64_t revision = symbols->diagnostics_revision();
      const std::string& active_path = workspace->buffer.path;
      const bool show_diag_counts =
          !active_path.empty() &&
          diagnostics_display_allowed(workspace->last_buffer_edit_ms, symbols.get(), active_path,
                                      layout_state->activity_gate.allows_lsp_ui());
      if (!show_diag_counts) {
        split_state->diag_errors = 0;
        split_state->diag_warnings = 0;
      } else if (revision != split_state->last_diag_revision ||
                 active_path != split_state->last_diag_path) {
        split_state->last_diag_revision = revision;
        split_state->last_diag_path = active_path;
        split_state->diag_errors = 0;
        split_state->diag_warnings = 0;
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
      if (split_state->diag_errors > 0 || split_state->diag_warnings > 0) {
        status_msg += i18n::tr("status.section_separator");
        if (split_state->diag_errors > 0) {
          status_msg += std::to_string(split_state->diag_errors) +
                        (split_state->diag_errors == 1
                             ? i18n::tr("status.diagnostics.errors_one")
                             : i18n::tr("status.diagnostics.errors_many"));
        }
        if (split_state->diag_warnings > 0) {
          if (split_state->diag_errors > 0) {
            status_msg += i18n::tr("status.diagnostics.separator");
          }
          status_msg += std::to_string(split_state->diag_warnings) +
                        (split_state->diag_warnings == 1
                             ? i18n::tr("status.diagnostics.warnings_one")
                             : i18n::tr("status.diagnostics.warnings_many"));
        }
      }
    }

    std::string focus_label;
    if (focus != nullptr) {
      focus_label = i18n::tr_fmt("status.focus_prefix", {focus->region_label()});
    }

    Element focus_status;
    if (!editor_mode_indicator.empty()) {
      focus_status = hbox({
          text(focus_label) | color(theme::Header()),
          text(editor_mode_indicator) | color(theme::Accent()) | bold,
      });
    } else {
      focus_status = text(focus_label) | color(theme::Header());
    }

    const bool index_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusIndex);
    const bool index_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusIndex);
    const bool chg_dir_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusChgDir);
    const bool chg_dir_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusChgDir);
    const bool launch_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusLaunch);
    const bool launch_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusLaunch);
    const bool launch_quick_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusLaunchQuick);
    const bool launch_quick_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusLaunchQuick);
    const bool debug_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusDebug);
    const bool debug_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusDebug);
    const bool debug_quick_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusDebugQuick);
    const bool debug_quick_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusDebugQuick);
    const bool layout_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusLayout);
    const bool layout_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusLayout);
    const bool settings_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusSettings);
    const bool settings_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusSettings);
    const bool shortcuts_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kStatusShortcuts);
    const bool shortcuts_pressed =
        layout_state != nullptr &&
        layout_state->clickable.is_pressed(press_id::kStatusShortcuts);

    const auto muted = [](Element content) { return std::move(content) | color(theme::Muted()); };
    const bool clangd_ready = index_clangd_ready(layout_state);
    Element index_label =
        text(i18n::tr("status.button.index")) |
        color(clangd_ready ? theme::Muted() : theme::Error());
    if (!clangd_ready) {
      index_label = index_label | bold;
    }
    Element index_btn = MakeToolbarButton(
        std::move(index_label), index_hovered, index_pressed, false, &status_ui->index_box, true);

    const bool show_chg_dir =
        app_mode != nullptr && *app_mode == AppMode::kDebug &&
        model->state != DebugState::kDisconnected &&
        model->state != DebugState::kConnecting;
    Element chg_dir_btn = MakeToolbarButton(
        text(i18n::tr("status.button.chg_dir")) | color(theme::Muted()),
        chg_dir_hovered, chg_dir_pressed, false, &status_ui->chg_dir_box, true);

    Element launch_btn = MakeSplitToolbarButton(
        muted(text(i18n::tr("status.button.launch"))),
        muted(text(i18n::tr("status.button.quick"))), launch_hovered, launch_pressed,
        launch_quick_hovered, launch_quick_pressed, false, &status_ui->launch_btn);
    Element debug_btn = MakeSplitToolbarButton(
        muted(text(i18n::tr("status.button.debug"))),
        muted(text(i18n::tr("status.button.quick"))), debug_hovered, debug_pressed,
        debug_quick_hovered, debug_quick_pressed, false, &status_ui->debug_btn);
    const bool layout_active =
        layout_state != nullptr && layout_state->status_layout_popover.open;
    Element layout_btn = MakeToolbarButton(
        text(i18n::tr("status.button.layout")) | color(theme::Muted()) |
            (layout_active ? bold : nothing),
        layout_hovered, layout_pressed, false, &status_ui->layout_box, true);
    Element settings_btn = MakeToolbarButton(
        text(i18n::tr("status.button.settings")) | color(theme::Muted()),
        settings_hovered, settings_pressed, false, &status_ui->settings_box, true);
    Element shortcuts_btn = MakeToolbarButton(
        text(i18n::tr("status.button.shortcuts")) | color(theme::Muted()),
        shortcuts_hovered, shortcuts_pressed, false, &status_ui->shortcuts_box, true);

    Element status = hbox({
        text(i18n::tr("status.app_name")) | bold | color(theme::Accent()),
        text(i18n::tr("status.separator")),
        focus_status,
        text(status_msg) | flex | color(theme::Header()),
        hbox({
            show_chg_dir ? chg_dir_btn : emptyElement(),
            index_btn,
            launch_btn,
            debug_btn,
            layout_btn,
            settings_btn,
            shortcuts_btn,
        }) | bgcolor(theme::TabIdle()),
    }) | bgcolor(theme::StatusBar());

    Element chrome = vbox({main | flex | bgcolor(theme::PanelBg()), status});
    if (layout_state != nullptr && layout_state->status_layout_popover.open) {
      chrome = RenderStatusLayoutPopoverOverlay(&layout_state->status_layout_popover, layout_state,
                                                std::move(chrome), status_ui->layout_box);
    }
    return chrome;
  });

  return layout_root;
}

}  // namespace tgdb
