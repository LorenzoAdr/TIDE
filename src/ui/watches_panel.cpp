#include "ui/watches_panel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/focusable_component.hpp"
#include "ui/focus_manager.hpp"
#include "ui/press_ids.hpp"
#include "ui/clickable.hpp"
#include "ui/text_input_style.hpp"
#include "ui/ui_wake.hpp"
#include "i18n/tr.hpp"
#include "util/ui_panel_render_cache.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kTabCount = 4;

void invalidate_watches_panel(MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return;
  }
  layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
  UI_WAKE(layout_state, "wake");
}

InputOption MakeWatchesBlinkInput(StringRef content, StringRef placeholder,
                                  MainLayoutState* layout_state) {
  InputOption opt = MakeBlinkInputOption(std::move(content), std::move(placeholder));
  opt.on_change = [layout_state] { invalidate_watches_panel(layout_state); };
  return opt;
}

struct FlatVariable {
  std::string expression;
  std::string name;
  std::string value;
  std::string type;
  int depth = 0;
  int variables_reference = 0;
  bool expandable = false;
  bool expanded = false;
};

struct BreakpointRow {
  enum class Kind { kLine, kHardware } kind = Kind::kLine;
  std::string file;
  int line = 0;
  int hardware_index = -1;
  std::string hardware_label;
};

struct WatchesPanelState {
  int selected_tab = 0;
  bool interaction_active = false;
  std::array<std::string, kTabCount> tab_titles;
  std::string placeholder_expression;
  std::string placeholder_new_value;
  std::string placeholder_new_value_click;
  std::string placeholder_expression_click;
  std::string placeholder_bp_hw;
  std::string placeholder_bp_hw_click;
  std::string expr_input;
  std::string bp_expr_input;
  std::string inject_input;
  bool inject_mode = false;
  int flat_selected = 0;
  int watch_selected = 0;
  int bp_selected = 0;
  std::vector<FlatVariable> flat_variables;
  std::vector<BreakpointRow> breakpoint_rows;
  Box variables_box;
  Box stack_box;
  Box breakpoints_box;
  Box breakpoints_list_box;
  Box watches_box;
  Box watch_input_box;
  Box watch_inject_box;
  Box bp_input_box;
  std::array<Box, kTabCount> tab_boxes;
  Box play_box;
  Box stop_box;
  Box next_box;
  Box step_box;
  Box clear_bp_box;
  std::unordered_set<std::string> expanded_expressions;
  int last_bp_click_row = -1;
  int64_t last_bp_click_ms = 0;
  int last_watch_click_row = -1;
  int64_t last_watch_click_ms = 0;
};

bool watch_input_active(MainLayoutState* layout_state, int selected_tab) {
  return layout_state && selected_tab == 0 &&
         layout_state->text_input_focus == TextInputFocus::Watch;
}

bool watch_inject_active(MainLayoutState* layout_state, int selected_tab) {
  return layout_state && selected_tab == 0 &&
         layout_state->text_input_focus == TextInputFocus::WatchInject;
}

bool breakpoint_hw_input_active(MainLayoutState* layout_state, int selected_tab) {
  return layout_state && selected_tab == 3 &&
         layout_state->text_input_focus == TextInputFocus::BreakpointHw;
}

bool hardware_watch_available(const DebugModel* model) {
  return model != nullptr && model->state != DebugState::kDisconnected &&
         model->state != DebugState::kTerminated && !model->is_post_mortem &&
         model->state == DebugState::kStopped;
}

void cancel_watch_inject(WatchesPanelState* state, MainLayoutState* layout_state) {
  state->inject_mode = false;
  state->inject_input.clear();
  if (layout_state &&
      layout_state->text_input_focus == TextInputFocus::WatchInject) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
  invalidate_watches_panel(layout_state);
}

void start_watch_inject(WatchesPanelState* state, DebugModel* model,
                        MainLayoutState* layout_state) {
  if (model->watches.empty() || model->state != DebugState::kStopped) {
    return;
  }
  state->watch_selected = std::max(
      0, std::min(state->watch_selected,
                  static_cast<int>(model->watches.size()) - 1));
  const auto& watch = model->watches[state->watch_selected];
  state->inject_mode = true;
  state->inject_input = watch.value;
  if (state->inject_input.rfind("[error]", 0) == 0) {
    state->inject_input.clear();
  }
  if (layout_state) {
    layout_state->text_input_focus = TextInputFocus::WatchInject;
  }
  invalidate_watches_panel(layout_state);
}

bool is_double_click(int row, int* last_row, int64_t* last_ms) {
  using namespace std::chrono;
  const int64_t now =
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  const bool doubled = row == *last_row && *last_row >= 0 && (now - *last_ms) < 450;
  *last_row = row;
  *last_ms = now;
  return doubled;
}

void assign_watch_frame(UiCommand* command, DebugModel* model) {
  if (model->variables_frame_id >= 0) {
    command->frame_id = model->variables_frame_id;
  } else if (!model->stack_frames.empty()) {
    command->frame_id = model->stack_frames[model->selected_frame].id;
  }
}

void submit_watch_inject(WatchesPanelState* state, DebugModel* model,
                         MainLayoutState* layout_state,
                         CommandCallback on_command) {
  if (!on_command || !state->inject_mode || model->watches.empty()) {
    return;
  }
  state->watch_selected = std::max(
      0, std::min(state->watch_selected,
                  static_cast<int>(model->watches.size()) - 1));
  const auto& watch = model->watches[state->watch_selected];
  UiCommand command;
  command.kind = UiCommandKind::kSetWatchValue;
  command.expression = watch.expression;
  command.assign_value = state->inject_input;
  assign_watch_frame(&command, model);
  on_command(command);
  cancel_watch_inject(state, layout_state);
}

void submit_watch(const std::string& expression, DebugModel* model,
                  CommandCallback on_command, MainLayoutState* layout_state = nullptr) {
  if (expression.empty() || !on_command) {
    return;
  }
  model->add_watch(expression);
  UiCommand command;
  command.kind = UiCommandKind::kAddWatch;
  command.expression = expression;
  assign_watch_frame(&command, model);
  on_command(command);
  invalidate_watches_panel(layout_state);
}

void submit_hardware_watch(const std::string& expression, DebugModel* model,
                           CommandCallback on_command,
                           MainLayoutState* layout_state = nullptr) {
  if (expression.empty() || !on_command || model == nullptr) {
    return;
  }
  model->add_hardware_watch(expression, expression);
  UiCommand command;
  command.kind = UiCommandKind::kAddHardwareWatch;
  command.expression = expression;
  command.hardware_watch_index = static_cast<int>(model->hardware_watches.size()) - 1;
  on_command(command);
  invalidate_watches_panel(layout_state);
}

void sync_breakpoints_file(DebugModel* model, const std::string& file,
                           CommandCallback on_command) {
  if (!on_command) {
    return;
  }
  UiCommand command;
  command.kind = UiCommandKind::kSetBreakpoints;
  command.breakpoint_file = file;
  command.breakpoint_lines = model->enabled_breakpoint_lines(file);
  on_command(command);
}

std::string variables_frame_label(const DebugModel* model) {
  if (model->variables_frame_id < 0) {
    return i18n::tr("panel.debug.no_frame");
  }
  for (const auto& frame : model->stack_frames) {
    if (frame.id == model->variables_frame_id) {
      return frame.name;
    }
  }
  return i18n::tr("panel.debug.frame_prefix") + std::to_string(model->variables_frame_id);
}

void rebuild_flat_variables(DebugModel* model, WatchesPanelState* state) {
  state->flat_variables.clear();
  for (const auto& local : model->locals) {
    FlatVariable row;
    row.expression = local.expression.empty() ? local.name : local.expression;
    row.name = local.name;
    row.value = local.value;
    row.type = local.type;
    row.depth = local.depth;
    row.variables_reference = local.variables_reference;
    row.expandable = local.variables_reference > 0;
    row.expanded = state->expanded_expressions.count(row.expression) > 0;
    state->flat_variables.push_back(row);

    if (row.expanded) {
      const auto it = model->variable_children.find(row.expression);
      if (it != model->variable_children.end()) {
        for (const auto& child : it->second) {
          FlatVariable child_row;
          child_row.expression =
              child.expression.empty() ? child.name : child.expression;
          child_row.name = child.name;
          child_row.value = child.value;
          child_row.type = child.type;
          child_row.depth = child.depth;
          child_row.variables_reference = child.variables_reference;
          child_row.expandable = child.variables_reference > 0;
          child_row.expanded =
              state->expanded_expressions.count(child_row.expression) > 0;
          state->flat_variables.push_back(child_row);
        }
      }
    }
  }
  if (state->flat_selected >= static_cast<int>(state->flat_variables.size())) {
    state->flat_selected =
        std::max(0, static_cast<int>(state->flat_variables.size()) - 1);
  }
}

void rebuild_breakpoint_rows(DebugModel* model, WatchesPanelState* state) {
  state->breakpoint_rows.clear();
  for (const auto& [file, lines] : model->breakpoints_by_file) {
    for (int line : lines) {
      state->breakpoint_rows.push_back({BreakpointRow::Kind::kLine, file, line, -1, {}});
    }
  }
  for (int i = 0; i < static_cast<int>(model->hardware_watches.size()); ++i) {
    const auto& entry = model->hardware_watches[static_cast<std::size_t>(i)];
    state->breakpoint_rows.push_back(
        {BreakpointRow::Kind::kHardware, {}, 0, i, entry.label});
  }
  std::sort(state->breakpoint_rows.begin(), state->breakpoint_rows.end(),
            [](const BreakpointRow& a, const BreakpointRow& b) {
              if (a.kind != b.kind) {
                return a.kind == BreakpointRow::Kind::kLine;
              }
              if (a.kind == BreakpointRow::Kind::kHardware) {
                return a.hardware_label < b.hardware_label;
              }
              if (a.file != b.file) {
                return a.file < b.file;
              }
              return a.line < b.line;
            });
  if (state->bp_selected >= static_cast<int>(state->breakpoint_rows.size())) {
    state->bp_selected =
        std::max(0, static_cast<int>(state->breakpoint_rows.size()) - 1);
  }
}

void toggle_expand(DebugModel* model, WatchesPanelState* state, int index,
                   CommandCallback on_command) {
  if (index < 0 || index >= static_cast<int>(state->flat_variables.size())) {
    return;
  }
  const auto& row = state->flat_variables[index];
  if (!row.expandable) {
    return;
  }

  if (state->expanded_expressions.count(row.expression) > 0) {
    state->expanded_expressions.erase(row.expression);
    rebuild_flat_variables(model, state);
    return;
  }

  state->expanded_expressions.insert(row.expression);
  const auto cached = model->variable_children.find(row.expression);
  if (cached == model->variable_children.end()) {
    if (!on_command) {
      rebuild_flat_variables(model, state);
      return;
    }
    UiCommand command;
    command.kind = UiCommandKind::kFetchVariableChildren;
    command.variables_reference = row.variables_reference;
    command.parent_expression = row.expression;
    command.variable_depth = row.depth;
    on_command(command);
  }
  rebuild_flat_variables(model, state);
}

void add_flat_as_watch(WatchesPanelState* state, DebugModel* model,
                       CommandCallback on_command,
                       MainLayoutState* layout_state = nullptr) {
  if (state->flat_variables.empty()) {
    return;
  }
  const int index = std::max(
      0, std::min(state->flat_selected,
                  static_cast<int>(state->flat_variables.size()) - 1));
  submit_watch(state->flat_variables[index].expression, model, on_command, layout_state);
}

bool switch_tab(WatchesPanelState* state, int tab, MainLayoutState* layout_state) {
  if (tab < 0 || tab >= kTabCount) {
    return false;
  }
  state->selected_tab = tab;
  if (layout_state && layout_state->text_input_focus == TextInputFocus::Watch &&
      tab != 0) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
  if (layout_state && layout_state->text_input_focus == TextInputFocus::BreakpointHw &&
      tab != 3) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
  if (tab != 0) {
    cancel_watch_inject(state, layout_state);
  }
  if (layout_state != nullptr) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
  }
  return true;
}

bool switch_tab_from_mouse(WatchesPanelState* state, int mouse_x, int mouse_y,
                           MainLayoutState* layout_state, int* out_tab = nullptr) {
  for (int i = 0; i < kTabCount; ++i) {
    if (state->tab_boxes[i].Contain(mouse_x, mouse_y)) {
      if (out_tab != nullptr) {
        *out_tab = i;
      }
      return switch_tab(state, i, layout_state);
    }
  }
  return false;
}

bool handle_watches_hover(WatchesPanelState* state, MainLayoutState* layout_state,
                          const Mouse& mouse) {
  if (state == nullptr || layout_state == nullptr || mouse.motion != Mouse::Moved) {
    return false;
  }
  return update_panel_hover(
      layout_state, mouse.x, mouse.y,
      {{press_id::kWatchesPlay, &state->play_box},
       {press_id::kWatchesStop, &state->stop_box},
       {press_id::kWatchesNext, &state->next_box},
       {press_id::kWatchesStep, &state->step_box},
       {press_id::kWatchesClearBreakpoints, &state->clear_bp_box},
       {press_id::kWatchesTab0, &state->tab_boxes[0]},
       {press_id::kWatchesTab1, &state->tab_boxes[1]},
       {press_id::kWatchesTab2, &state->tab_boxes[2]},
       {press_id::kWatchesTab3, &state->tab_boxes[3]}},
      press_id::is_watches_hover);
}

bool handle_toolbar_mouse(WatchesPanelState* state, DebugModel* model,
                          const Mouse& mouse, MainLayoutState* layout_state,
                          const std::function<void()>& send_continue,
                          const std::function<void()>& send_pause,
                          const std::function<void()>& send_stop,
                          const std::function<void()>& send_next,
                          const std::function<void()>& send_step) {
  if (state->play_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kWatchesPlay);
    if (model->state == DebugState::kRunning) {
      if (send_pause) {
        send_pause();
      }
    } else if (model->state == DebugState::kStopped && !model->is_post_mortem) {
      if (send_continue) {
        send_continue();
      }
    }
    return true;
  }
  if (state->stop_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kWatchesStop);
    if (send_stop) {
      send_stop();
    }
    return true;
  }
  if (state->next_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kWatchesNext);
    if (model->state == DebugState::kStopped && !model->is_post_mortem && send_next) {
      send_next();
    }
    return true;
  }
  if (state->step_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kWatchesStep);
    if (model->state == DebugState::kStopped && !model->is_post_mortem && send_step) {
      send_step();
    }
    return true;
  }
  int tab = -1;
  if (switch_tab_from_mouse(state, mouse.x, mouse.y, layout_state, &tab)) {
    if (tab >= 0) {
      trigger_press(layout_state, press_id::watches_tab_id(tab));
    }
    return true;
  }
  return false;
}

std::string gdb_backtrace_line(int index, const StackFrameInfo& frame) {
  std::string line = "#" + std::to_string(index) + "  " + frame.name + " ()";
  if (!frame.file.empty()) {
    line += " at ";
    const auto basename =
        std::filesystem::path(frame.file).filename().string();
    if (frame.line > 0) {
      line += basename + ":" + std::to_string(frame.line);
    } else {
      line += basename;
    }
  }
  return line;
}

std::string breakpoint_label(const BreakpointRow& row) {
  if (row.kind == BreakpointRow::Kind::kHardware) {
    return "hw " + row.hardware_label;
  }
  const auto basename = std::filesystem::path(row.file).filename().string();
  return basename + ":" + std::to_string(row.line);
}

void sync_hardware_watch_enabled(DebugModel* model, int index, CommandCallback on_command) {
  if (!on_command || index < 0 || index >= static_cast<int>(model->hardware_watches.size())) {
    return;
  }
  const auto& entry = model->hardware_watches[static_cast<std::size_t>(index)];
  if (entry.gdb_number <= 0) {
    return;
  }
  UiCommand command;
  command.kind = UiCommandKind::kSetHardwareWatchEnabled;
  command.hardware_watch_index = index;
  command.hardware_watch_gdb_number = entry.gdb_number;
  command.hardware_watch_enabled = entry.enabled;
  on_command(command);
}

void remove_hardware_watch_at(DebugModel* model, int index, CommandCallback on_command) {
  if (index < 0 || index >= static_cast<int>(model->hardware_watches.size())) {
    return;
  }
  const int gdb_number = model->hardware_watches[static_cast<std::size_t>(index)].gdb_number;
  model->remove_hardware_watch(index);
  if (!on_command || gdb_number <= 0) {
    return;
  }
  UiCommand command;
  command.kind = UiCommandKind::kRemoveHardwareWatch;
  command.hardware_watch_index = index;
  command.hardware_watch_gdb_number = gdb_number;
  on_command(command);
}

void toggle_breakpoint_enabled(WatchesPanelState* state, DebugModel* model,
                               CommandCallback on_command,
                               MainLayoutState* layout_state = nullptr) {
  if (state->breakpoint_rows.empty()) {
    return;
  }
  const int index = std::max(
      0, std::min(state->bp_selected,
                  static_cast<int>(state->breakpoint_rows.size()) - 1));
  const auto& row = state->breakpoint_rows[index];
  if (row.kind == BreakpointRow::Kind::kHardware) {
    const bool enabled = model->is_hardware_watch_enabled(row.hardware_index);
    model->set_hardware_watch_enabled(row.hardware_index, !enabled);
    sync_hardware_watch_enabled(model, row.hardware_index, on_command);
  } else {
    const bool enabled = model->is_breakpoint_enabled(row.file, row.line);
    model->set_breakpoint_enabled(row.file, row.line, !enabled);
    sync_breakpoints_file(model, row.file, on_command);
  }
  if (layout_state != nullptr) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
  }
}

void remove_selected_breakpoint(WatchesPanelState* state, DebugModel* model,
                                CommandCallback on_command,
                                MainLayoutState* layout_state = nullptr) {
  if (state->breakpoint_rows.empty()) {
    return;
  }
  const int index = std::max(
      0, std::min(state->bp_selected,
                  static_cast<int>(state->breakpoint_rows.size()) - 1));
  const auto row = state->breakpoint_rows[index];
  if (row.kind == BreakpointRow::Kind::kHardware) {
    remove_hardware_watch_at(model, row.hardware_index, on_command);
    rebuild_breakpoint_rows(model, state);
  } else {
    model->remove_breakpoint(row.file, row.line);
    sync_breakpoints_file(model, row.file, on_command);
    rebuild_breakpoint_rows(model, state);
  }
  if (layout_state != nullptr) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
  }
}

void clear_all_breakpoints(WatchesPanelState* state, DebugModel* model,
                           CommandCallback on_command,
                           MainLayoutState* layout_state = nullptr) {
  if (model->breakpoints_by_file.empty() && model->hardware_watches.empty()) {
    return;
  }
  std::vector<std::string> files;
  files.reserve(model->breakpoints_by_file.size());
  for (const auto& entry : model->breakpoints_by_file) {
    files.push_back(entry.first);
  }
  while (!model->hardware_watches.empty()) {
    remove_hardware_watch_at(model, static_cast<int>(model->hardware_watches.size()) - 1,
                             on_command);
  }
  model->clear_all_breakpoints();
  if (on_command) {
    for (const std::string& file : files) {
      sync_breakpoints_file(model, file, on_command);
    }
  }
  state->bp_selected = 0;
  rebuild_breakpoint_rows(model, state);
  if (layout_state != nullptr) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
  }
}

Element make_clear_breakpoints_button(DebugModel* model, WatchesPanelState* state,
                                    MainLayoutState* layout_state) {
  const bool disabled =
      model->breakpoints_by_file.empty() && model->hardware_watches.empty();
  return MakeToolbarButton(
      text(" " + i18n::tr("panel.breakpoints.clear_all") + " "),
      layout_state != nullptr &&
          layout_state->clickable.is_hovered(press_id::kWatchesClearBreakpoints),
      layout_state != nullptr &&
          layout_state->clickable.is_pressed(press_id::kWatchesClearBreakpoints),
      disabled, &state->clear_bp_box);
}

bool handle_clear_breakpoints_mouse(WatchesPanelState* state, DebugModel* model,
                                    MainLayoutState* layout_state, CommandCallback on_command,
                                    const Mouse& mouse) {
  if (!state->clear_bp_box.Contain(mouse.x, mouse.y)) {
    return false;
  }
  trigger_press(layout_state, press_id::kWatchesClearBreakpoints);
  clear_all_breakpoints(state, model, on_command, layout_state);
  return true;
}

void remove_selected_watch(WatchesPanelState* state, DebugModel* model,
                           MainLayoutState* layout_state = nullptr) {
  if (model->watches.empty()) {
    return;
  }
  const int index = std::max(
      0, std::min(state->watch_selected,
                  static_cast<int>(model->watches.size()) - 1));
  model->remove_watch(index);
  if (state->watch_selected >= static_cast<int>(model->watches.size())) {
    state->watch_selected =
        std::max(0, static_cast<int>(model->watches.size()) - 1);
  }
  invalidate_watches_panel(layout_state);
}

Elements render_breakpoint_hw_footer(WatchesPanelState* state, MainLayoutState* layout_state,
                                     const Component& bp_expr_input, DebugModel* model) {
  Elements footer;
  Element input_row;
  if (breakpoint_hw_input_active(layout_state, state->selected_tab)) {
    input_row = bp_expr_input->Render() | border | flex | bgcolor(theme::PanelBg());
  } else {
    const std::string preview = state->bp_expr_input.empty() ? state->placeholder_bp_hw_click
                                                             : state->bp_expr_input;
    input_row = text(" " + preview + " ") | border | flex | bgcolor(theme::PanelBg()) |
                color(state->bp_expr_input.empty() ? theme::Muted() : theme::WatchInput());
  }
  footer.push_back(separator());
  footer.push_back(hbox({text(i18n::tr("panel.debug.hw_prefix")) | color(theme::WatchInput()),
                         input_row | flex | reflect(state->bp_input_box)}));
  if (!hardware_watch_available(model)) {
    footer.push_back(text(i18n::tr("panel.debug.hw_requires_stop")) | color(theme::Muted()));
  }
  return footer;
}

bool handle_breakpoint_hw_input(WatchesPanelState* state, DebugModel* model,
                                MainLayoutState* layout_state, Component bp_expr_input,
                                CommandCallback on_command, Event event,
                                const std::function<void()>& mark_watches_focus) {
  if (event.is_mouse() && event.mouse().button == Mouse::Left &&
      event.mouse().motion == Mouse::Pressed) {
    const auto& m = event.mouse();
    if (state->bp_input_box.Contain(m.x, m.y)) {
      mark_watches_focus();
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::BreakpointHw;
      }
      bp_expr_input->TakeFocus();
      invalidate_watches_panel(layout_state);
      return false;
    }
    return false;
  }

  if (breakpoint_hw_input_active(layout_state, state->selected_tab)) {
    if (event == Event::Return) {
      const std::string expr = state->bp_expr_input;
      if (!expr.empty() && hardware_watch_available(model)) {
        submit_hardware_watch(expr, model, on_command, layout_state);
        state->bp_expr_input.clear();
        invalidate_watches_panel(layout_state);
      }
      return true;
    }
    return false;
  }

  if (event == Event::Return) {
    mark_watches_focus();
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::BreakpointHw;
    }
    bp_expr_input->TakeFocus();
    invalidate_watches_panel(layout_state);
    return true;
  }

  return false;
}

bool watches_breakpoints_only(AppMode* app_mode) {
  return app_mode == nullptr || *app_mode != AppMode::kDebug;
}

Elements render_breakpoint_list(DebugModel* model, WatchesPanelState* state) {
  Elements body;
  int index = 0;
  for (const auto& row : state->breakpoint_rows) {
    const bool selected = index == state->bp_selected;
    const bool enabled = row.kind == BreakpointRow::Kind::kHardware
                             ? model->is_hardware_watch_enabled(row.hardware_index)
                             : model->is_breakpoint_enabled(row.file, row.line);
    const std::string mark = enabled ? "● " : "○ ";
    Element line =
        hbox({text(mark) | color(enabled ? theme::BpActive() : theme::BpDisabled()),
              text(breakpoint_label(row))});
    if (selected) {
      line = line | inverted;
    }
    body.push_back(line);
    ++index;
  }
  return body;
}

Elements render_breakpoint_hints(WatchesPanelState* state) {
  Elements hints;
  if (state->breakpoint_rows.empty()) {
    hints.push_back(text(i18n::tr("panel.debug.no_breakpoints")) | color(theme::Muted()));
    hints.push_back(text(i18n::tr("panel.debug.breakpoint_hint")) | color(theme::Muted()));
  } else {
    hints.push_back(separator());
    hints.push_back(text(i18n::tr("panel.debug.breakpoint_toggle_hint")) | color(theme::Muted()));
  }
  return hints;
}

bool handle_breakpoints_tab_keys(WatchesPanelState* state, DebugModel* model,
                                 MainLayoutState* layout_state, FocusManagerState* focus,
                                 CommandCallback on_command, Event event,
                                 const std::function<void()>& mark_watches_focus) {
  rebuild_breakpoint_rows(model, state);
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->bp_selected = std::min(
        state->bp_selected + 1,
        std::max(0, static_cast<int>(state->breakpoint_rows.size()) - 1));
    if (layout_state != nullptr) {
      layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
    }
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->bp_selected = std::max(0, state->bp_selected - 1);
    if (layout_state != nullptr) {
      layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
    }
    return true;
  }
  if (event == Event::Return || event == Event::Character(' ')) {
    mark_watches_focus();
    toggle_breakpoint_enabled(state, model, on_command, layout_state);
    return true;
  }
  if (event == Event::Delete || event == Event::Character('x') ||
      event == Event::Character('d')) {
    remove_selected_breakpoint(state, model, on_command, layout_state);
    return true;
  }
  if (event.is_mouse() && event.mouse().button == Mouse::Left &&
      event.mouse().motion == Mouse::Pressed) {
    const auto& m = event.mouse();
    if (state->breakpoints_list_box.Contain(m.x, m.y)) {
      mark_watches_focus();
      const int row = m.y - state->breakpoints_list_box.y_min;
      if (row >= 0 && row < static_cast<int>(state->breakpoint_rows.size())) {
        const int rel_x = m.x - state->breakpoints_list_box.x_min;
        state->bp_selected = row;
        const auto& bp = state->breakpoint_rows[row];
        // Mark column (● / ○) or first few cells: toggle enabled.
        if (rel_x <= 3) {
          toggle_breakpoint_enabled(state, model, on_command, layout_state);
          return true;
        }
        if (is_double_click(row, &state->last_bp_click_row, &state->last_bp_click_ms)) {
          remove_selected_breakpoint(state, model, on_command, layout_state);
          return true;
        }
        if (bp.kind == BreakpointRow::Kind::kLine) {
          model->active_file = bp.file;
          model->active_line = bp.line;
          model->view_token++;
        }
        if (focus != nullptr) {
          focus->region = FocusRegion::Editor;
        }
        if (layout_state != nullptr) {
          layout_state->focus_sync_needed = true;
          layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
        }
        return true;
      }
    }
  }
  return false;
}

}  // namespace

Component MakeWatchesPanel(DebugModel* model, CommandCallback on_command,
                           MainLayoutState* layout_state,
                           const std::function<void()>& on_stop_debug,
                           FocusManagerState* focus, AppMode* app_mode) {
  auto state = std::make_shared<WatchesPanelState>();
  state->tab_titles = {i18n::tr("panel.debug.tab.watch"), i18n::tr("panel.debug.tab.vars"),
                       i18n::tr("panel.debug.tab.backtrace"), i18n::tr("panel.debug.tab.breakpoints")};
  state->placeholder_expression = i18n::tr("panel.debug.placeholder.expression");
  state->placeholder_new_value = i18n::tr("panel.debug.placeholder.new_value");
  state->placeholder_new_value_click = i18n::tr("panel.debug.placeholder.new_value_click");
  state->placeholder_expression_click = i18n::tr("panel.debug.placeholder.expression_click");
  state->placeholder_bp_hw = i18n::tr("panel.debug.placeholder.hw_symbol");
  state->placeholder_bp_hw_click = i18n::tr("panel.debug.placeholder.hw_symbol_click");
  auto expr_input =
      Input(MakeWatchesBlinkInput(&state->expr_input, &state->placeholder_expression,
                                  layout_state));
  auto bp_expr_input =
      Input(MakeWatchesBlinkInput(&state->bp_expr_input, &state->placeholder_bp_hw,
                                  layout_state));
  auto inject_input =
      Input(MakeWatchesBlinkInput(&state->inject_input, &state->placeholder_new_value,
                                  layout_state));
  auto expr_maybe = Maybe(expr_input, [layout_state, state] {
    return watch_input_active(layout_state, state->selected_tab);
  });
  auto bp_expr_maybe = Maybe(bp_expr_input, [layout_state, state] {
    return breakpoint_hw_input_active(layout_state, state->selected_tab);
  });
  auto inject_maybe = Maybe(inject_input, [layout_state, state] {
    return watch_inject_active(layout_state, state->selected_tab);
  });
  auto inputs = Container::Vertical({inject_maybe, expr_maybe, bp_expr_maybe});

  auto select_frame = [model, on_command, layout_state](int index) {
    if (!on_command || model->stack_frames.empty()) {
      return;
    }
    index = std::max(0, std::min(index, static_cast<int>(model->stack_frames.size()) - 1));
    model->selected_frame = index;
    const auto& frame = model->stack_frames[index];
    model->active_file = frame.file;
    model->active_line = frame.line;
    model->view_token++;
    if (layout_state != nullptr) {
      layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
    }
    UiCommand command;
    command.kind = UiCommandKind::kFetchVariables;
    command.frame_id = frame.id;
    on_command(command);
  };

  auto send_continue = [on_command] {
    if (!on_command) {
      return;
    }
    UiCommand command;
    command.kind = UiCommandKind::kContinue;
    on_command(command);
  };

  auto send_pause = [on_command] {
    if (!on_command) {
      return;
    }
    UiCommand command;
    command.kind = UiCommandKind::kPause;
    on_command(command);
  };

  auto send_stop = [on_stop_debug] {
    if (on_stop_debug) {
      on_stop_debug();
    }
  };

  auto send_next = [on_command] {
    if (!on_command) {
      return;
    }
    UiCommand command;
    command.kind = UiCommandKind::kNext;
    on_command(command);
  };

  auto send_step = [on_command] {
    if (!on_command) {
      return;
    }
    UiCommand command;
    command.kind = UiCommandKind::kStepIn;
    on_command(command);
  };

  auto handler = [model, state, layout_state, focus, select_frame, on_command, send_continue,
                  send_pause, send_stop, send_next, send_step, expr_input, bp_expr_input,
                  inject_input, app_mode](Event event) {
    const bool breakpoints_only = watches_breakpoints_only(app_mode);
    if (breakpoints_only) {
      state->selected_tab = 3;
    }

    const auto mark_watches_focus = [&]() {
      state->interaction_active = true;
      if (layout_state != nullptr) {
        layout_state->right_panel_active_section = 1;
      }
      if (focus != nullptr) {
        focus->region = FocusRegion::RightPanel;
      }
    };

    if (event == Event::Custom) {
      if (layout_state != nullptr && layout_state->pending_watches_focus) {
        layout_state->pending_watches_focus = false;
        mark_watches_focus();
      }
      return false;
    }

    if (event == Event::Escape) {
      if (state->inject_mode) {
        cancel_watch_inject(state.get(), layout_state);
        return true;
      }
      if (breakpoint_hw_input_active(layout_state, state->selected_tab)) {
        if (layout_state) {
          layout_state->text_input_focus = TextInputFocus::None;
        }
        return true;
      }
      state->interaction_active = false;
      if (layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }

    if (breakpoints_only) {
      if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
        return handle_watches_hover(state.get(), layout_state, event.mouse());
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        if (handle_clear_breakpoints_mouse(state.get(), model, layout_state, on_command,
                                           event.mouse())) {
          mark_watches_focus();
          return true;
        }
      }
      if (handle_breakpoint_hw_input(state.get(), model, layout_state, bp_expr_input, on_command,
                                     event, mark_watches_focus)) {
        return true;
      }
      if (handle_breakpoints_tab_keys(state.get(), model, layout_state, focus, on_command, event,
                                      mark_watches_focus)) {
        return true;
      }
      return false;
    }

    if (event == Event::Character('1')) {
      mark_watches_focus();
      trigger_press(layout_state, press_id::kWatchesTab0);
      return switch_tab(state.get(), 0, layout_state);
    }
    if (event == Event::Character('2')) {
      mark_watches_focus();
      trigger_press(layout_state, press_id::kWatchesTab1);
      return switch_tab(state.get(), 1, layout_state);
    }
    if (event == Event::Character('3')) {
      mark_watches_focus();
      trigger_press(layout_state, press_id::kWatchesTab2);
      return switch_tab(state.get(), 2, layout_state);
    }
    if (event == Event::Character('4')) {
      mark_watches_focus();
      trigger_press(layout_state, press_id::kWatchesTab3);
      return switch_tab(state.get(), 3, layout_state);
    }

    if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
      handle_watches_hover(state.get(), layout_state, event.mouse());
      return false;
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      if (handle_toolbar_mouse(state.get(), model, event.mouse(), layout_state,
                               send_continue, send_pause, send_stop, send_next, send_step)) {
        mark_watches_focus();
        return true;
      }
      if (state->selected_tab == 3 &&
          handle_clear_breakpoints_mouse(state.get(), model, layout_state, on_command,
                                         event.mouse())) {
        mark_watches_focus();
        return true;
      }
    }

    const auto owns_watches_navigation = [&]() {
      return state->interaction_active ||
             watch_input_active(layout_state, state->selected_tab) ||
             watch_inject_active(layout_state, state->selected_tab) ||
             breakpoint_hw_input_active(layout_state, state->selected_tab);
    };

    if (state->selected_tab == 0) {
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->watch_inject_box.Contain(m.x, m.y) && state->inject_mode) {
          mark_watches_focus();
          if (layout_state) {
            layout_state->text_input_focus = TextInputFocus::WatchInject;
          }
          inject_input->TakeFocus();
          invalidate_watches_panel(layout_state);
          return false;
        }
        if (state->watch_input_box.Contain(m.x, m.y)) {
          mark_watches_focus();
          cancel_watch_inject(state.get(), layout_state);
          if (layout_state) {
            layout_state->text_input_focus = TextInputFocus::Watch;
          }
          expr_input->TakeFocus();
          invalidate_watches_panel(layout_state);
          return false;
        }
        if (state->watches_box.Contain(m.x, m.y)) {
          mark_watches_focus();
          const int row = m.y - state->watches_box.y_min;
          if (row >= 0 && row < static_cast<int>(model->watches.size())) {
            if (is_double_click(row, &state->last_watch_click_row,
                                &state->last_watch_click_ms)) {
              state->watch_selected = row;
              remove_selected_watch(state.get(), model, layout_state);
            } else {
              state->watch_selected = row;
              invalidate_watches_panel(layout_state);
            }
            return true;
          }
        }
      }

      if (watch_inject_active(layout_state, state->selected_tab)) {
        if (event == Event::Return) {
          submit_watch_inject(state.get(), model, layout_state, on_command);
          return true;
        }
        return false;
      }

      if (watch_input_active(layout_state, state->selected_tab)) {
        if (event == Event::Return) {
          const std::string expr = state->expr_input;
          if (!expr.empty()) {
            submit_watch(expr, model, on_command, layout_state);
            state->expr_input.clear();
            invalidate_watches_panel(layout_state);
          }
          return true;
        }
        return false;
      }

      if (event == Event::Character('e') || event == Event::Character('=')) {
        if (model->state == DebugState::kStopped && !model->watches.empty()) {
          mark_watches_focus();
          start_watch_inject(state.get(), model, layout_state);
          inject_input->TakeFocus();
          return true;
        }
        return false;
      }

      if (event == Event::Return) {
        mark_watches_focus();
        if (model->state == DebugState::kStopped && !model->watches.empty()) {
          start_watch_inject(state.get(), model, layout_state);
          inject_input->TakeFocus();
          return true;
        }
        cancel_watch_inject(state.get(), layout_state);
        if (layout_state) {
          layout_state->text_input_focus = TextInputFocus::Watch;
        }
        expr_input->TakeFocus();
        invalidate_watches_panel(layout_state);
        return true;
      }

      if (event == Event::ArrowDown || event == Event::Character('j')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->watch_selected = std::min(
            state->watch_selected + 1,
            std::max(0, static_cast<int>(model->watches.size()) - 1));
        invalidate_watches_panel(layout_state);
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->watch_selected = std::max(0, state->watch_selected - 1);
        invalidate_watches_panel(layout_state);
        return true;
      }
      if (event == Event::Delete || event == Event::Character('x') ||
          event == Event::Character('d')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        remove_selected_watch(state.get(), model, layout_state);
        return true;
      }
      return false;
    }

    if (state->selected_tab == 1) {
      rebuild_flat_variables(model, state.get());

      if (event == Event::ArrowDown || event == Event::Character('j')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->flat_selected = std::min(
            state->flat_selected + 1,
            std::max(0, static_cast<int>(state->flat_variables.size()) - 1));
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->flat_selected = std::max(0, state->flat_selected - 1);
        return true;
      }
      if (event == Event::ArrowRight) {
        if (!owns_watches_navigation()) {
          return false;
        }
        toggle_expand(model, state.get(), state->flat_selected, on_command);
        return true;
      }
      if (event == Event::ArrowLeft) {
        if (!state->flat_variables.empty()) {
          const auto& row = state->flat_variables[state->flat_selected];
          if (row.expanded) {
            state->expanded_expressions.erase(row.expression);
            rebuild_flat_variables(model, state.get());
            return true;
          }
        }
        return false;
      }
      if (event == Event::Character('w') || event == Event::Character('a')) {
        mark_watches_focus();
        add_flat_as_watch(state.get(), model, on_command, layout_state);
        return true;
      }
      if (event == Event::Return) {
        mark_watches_focus();
        add_flat_as_watch(state.get(), model, on_command, layout_state);
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->variables_box.Contain(m.x, m.y)) {
          mark_watches_focus();
          const int row = m.y - state->variables_box.y_min;
          if (row >= 0 && row < static_cast<int>(state->flat_variables.size())) {
            state->flat_selected = row;
            return true;
          }
        }
      }
      return false;
    }

    if (state->selected_tab == 2) {
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        select_frame(model->selected_frame + 1);
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        select_frame(model->selected_frame - 1);
        return true;
      }
      if (event == Event::Return) {
        mark_watches_focus();
        select_frame(model->selected_frame);
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->stack_box.Contain(m.x, m.y)) {
          mark_watches_focus();
          const int row = m.y - state->stack_box.y_min;
          if (row >= 0 && row < static_cast<int>(model->stack_frames.size())) {
            select_frame(row);
            return true;
          }
        }
      }
      return false;
    }

    if (state->selected_tab == 3) {
      if (handle_breakpoint_hw_input(state.get(), model, layout_state, bp_expr_input, on_command,
                                     event, mark_watches_focus)) {
        return true;
      }
      if (handle_breakpoints_tab_keys(state.get(), model, layout_state, focus, on_command, event,
                                      mark_watches_focus)) {
        return true;
      }
      return false;
    }

    return false;
  };

  auto panel = WrapFocusable(CatchEvent(
      Renderer(inputs, [model, state, expr_input, bp_expr_input, inject_input, layout_state, focus,
                        app_mode] {
    if (focus != nullptr && focus->region != FocusRegion::RightPanel) {
      state->interaction_active = false;
    }
    if (state->inject_mode && layout_state &&
        layout_state->text_input_focus != TextInputFocus::WatchInject) {
      state->inject_mode = false;
      state->inject_input.clear();
    }
    rebuild_flat_variables(model, state.get());
    rebuild_breakpoint_rows(model, state.get());

    const bool breakpoints_only = watches_breakpoints_only(app_mode);
    if (breakpoints_only) {
      Elements body;
      body.push_back(make_clear_breakpoints_button(model, state.get(), layout_state));
      body.push_back(separator());
      Elements list = render_breakpoint_list(model, state.get());
      body.push_back(vbox(std::move(list)) | reflect(state->breakpoints_list_box));
      Elements hints = render_breakpoint_hints(state.get());
      body.insert(body.end(), std::make_move_iterator(hints.begin()),
                  std::make_move_iterator(hints.end()));
      Elements footer = render_breakpoint_hw_footer(state.get(), layout_state, bp_expr_input, model);
      Element panel_list =
          vbox({vbox(std::move(body)) | flex | vscroll_indicator | reflect(state->breakpoints_box),
                vbox(std::move(footer))}) |
          flex;
      return MakePanel(i18n::tr("panel.breakpoints.title"), std::move(panel_list));
    }

    const bool running = model->state == DebugState::kRunning;
    const bool stopped = model->state == DebugState::kStopped;
    const bool play_disabled =
        (!running && !stopped) || (stopped && model->is_post_mortem);
    const bool step_disabled =
        !stopped || model->is_post_mortem;

    Element play_content;
    if (running) {
      play_content = text(" ⏸ ") | bold | color(theme::Pause());
    } else if (stopped && !model->is_post_mortem) {
      play_content = text(" ▶ ") | bold | color(theme::Play());
    } else if (stopped && model->is_post_mortem) {
      play_content = text(" ▶ ") | color(theme::Muted());
    } else {
      play_content = text(" ▶ ");
    }
    Element play_btn = MakeToolbarButton(
        std::move(play_content),
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kWatchesPlay),
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kWatchesPlay),
        play_disabled, &state->play_box, true) |
                       size(WIDTH, EQUAL, 5);

    Element stop_btn = MakeToolbarButton(
        text(" ⏹ ") | bold | color(theme::Stop()),
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kWatchesStop),
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kWatchesStop),
        false, &state->stop_box, true) |
                       size(WIDTH, EQUAL, 5);

    Element next_btn = MakeToolbarButton(
        text(" ⏭ ") | bold | color(theme::Accent()),
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kWatchesNext),
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kWatchesNext),
        step_disabled, &state->next_box, true) |
                       size(WIDTH, EQUAL, 5);

    Element step_btn = MakeToolbarButton(
        text(" ↳ ") | bold | color(theme::Accent()),
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kWatchesStep),
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kWatchesStep),
        step_disabled, &state->step_box, true) |
                       size(WIDTH, EQUAL, 5);

    Elements tab_row;
    for (int i = 0; i < kTabCount; ++i) {
      const bool selected = i == state->selected_tab;
      const std::string_view tab_id = press_id::watches_tab_id(i);
      tab_row.push_back(MakeTabButton(
          state->tab_titles[i], selected,
          layout_state != nullptr && layout_state->clickable.is_hovered(tab_id),
          layout_state != nullptr && layout_state->clickable.is_pressed(tab_id),
          &state->tab_boxes[i]));
    }

    Element toolbar = vbox({
        hbox({std::move(play_btn), std::move(stop_btn), std::move(next_btn), std::move(step_btn)}) |
            bgcolor(theme::TabIdle()),
        hbox(std::move(tab_row)),
        separator(),
    });

    Elements body;
    if (state->selected_tab == 0) {
      int index = 0;
      for (const auto& watch : model->watches) {
        const bool selected = index == state->watch_selected;
        Element line = hbox({text(watch.expression) | bold | color(theme::Accent()),
                             text(" = "), text(watch.value)});
        if (selected) {
          line = line | inverted;
        }
        body.push_back(line);
        ++index;
      }
      if (model->watches.empty()) {
        body.push_back(text(i18n::tr("panel.debug.no_watches")) | color(theme::Muted()));
      } else {
        body.push_back(separator());
        body.push_back(text(i18n::tr("panel.debug.watch_hint")) |
                       color(theme::Muted()));
      }
    } else if (state->selected_tab == 1) {
      body.push_back(text(i18n::tr_fmt("panel.debug.frame_label", {variables_frame_label(model)})) |
                     color(theme::Muted()));
      body.push_back(separator());
      int index = 0;
      for (const auto& row : state->flat_variables) {
        const bool selected = index == state->flat_selected;
        std::string indent(static_cast<std::size_t>(row.depth * 2), ' ');
        std::string prefix = row.expandable ? (row.expanded ? "v " : "> ") : "  ";

        Element type_part =
            row.type.empty() ? text("") : text(i18n::tr_fmt("panel.debug.type_paren", {row.type}));
        Element line = hbox({text(indent + prefix), text(row.name) | bold,
                             type_part, text(" = "), text(row.value)});
        if (selected) {
          line = line | inverted;
        }
        body.push_back(line);
        ++index;
      }
      if (state->flat_variables.empty()) {
        body.push_back(text(i18n::tr("panel.debug.no_variables")) | color(theme::Muted()));
      }
    } else if (state->selected_tab == 2) {
      int index = 0;
      for (const auto& frame : model->stack_frames) {
        const bool selected = index == model->selected_frame;
        Element row = text(gdb_backtrace_line(index, frame)) | color(theme::StackFrame());
        if (selected) {
          row = row | inverted | bold;
        }
        body.push_back(row);
        ++index;
      }
      if (model->stack_frames.empty()) {
        body.push_back(text(i18n::tr("panel.debug.no_backtrace")) | color(theme::Muted()));
      } else {
        body.push_back(separator());
        body.push_back(text(i18n::tr("panel.debug.backtrace_hint")) | color(theme::Muted()));
      }
    } else {
      Elements bp_body;
      bp_body.push_back(make_clear_breakpoints_button(model, state.get(), layout_state));
      bp_body.push_back(separator());
      Elements bp_list = render_breakpoint_list(model, state.get());
      bp_body.push_back(vbox(std::move(bp_list)) | reflect(state->breakpoints_list_box));
      Elements hints = render_breakpoint_hints(state.get());
      bp_body.insert(bp_body.end(), std::make_move_iterator(hints.begin()),
                     std::make_move_iterator(hints.end()));
      body = std::move(bp_body);
    }

    Elements footer;
    if (state->selected_tab == 0) {
      if (state->inject_mode && !model->watches.empty()) {
        const auto& watch = model->watches[state->watch_selected];
        Element inject_row;
        if (watch_inject_active(layout_state, state->selected_tab)) {
          inject_row =
              inject_input->Render() | border | flex | bgcolor(theme::PanelBg());
        } else {
          const std::string preview =
              state->inject_input.empty() ? state->placeholder_new_value_click
                                          : state->inject_input;
          inject_row = text(" " + preview + " ") | border | flex |
                       bgcolor(theme::PanelBg()) |
                       color(state->inject_input.empty() ? theme::Muted()
                                                         : theme::WatchInput());
        }
        footer.push_back(separator());
        footer.push_back(hbox({
            text(" " + watch.expression + " = ") | color(theme::Accent()),
            inject_row | flex | reflect(state->watch_inject_box),
        }));
        footer.push_back(text(i18n::tr("panel.debug.inject_apply")) | color(theme::Muted()));
      }

      Element input_row;
      if (watch_input_active(layout_state, state->selected_tab)) {
        input_row = expr_input->Render() | border | flex | bgcolor(theme::PanelBg());
      } else {
        const std::string preview = state->expr_input.empty()
                                        ? state->placeholder_expression_click
                                        : state->expr_input;
        input_row = text(" " + preview + " ") | border | flex | bgcolor(theme::PanelBg()) |
                    color(state->expr_input.empty() ? theme::Muted() : theme::WatchInput());
      }
      footer.push_back(separator());
      footer.push_back(hbox({text(i18n::tr("panel.debug.watch_prefix")) | color(theme::WatchInput()),
                             input_row | reflect(state->watch_input_box)}));
    } else if (state->selected_tab == 3) {
      Elements hw_footer =
          render_breakpoint_hw_footer(state.get(), layout_state, bp_expr_input, model);
      footer.insert(footer.end(), std::make_move_iterator(hw_footer.begin()),
                    std::make_move_iterator(hw_footer.end()));
    }

    Element list = vbox(std::move(body));
    if (state->selected_tab == 0) {
      list = list | reflect(state->watches_box);
    } else if (state->selected_tab == 1) {
      list = list | reflect(state->variables_box);
    } else if (state->selected_tab == 2) {
      list = list | reflect(state->stack_box);
    } else if (state->selected_tab == 3) {
      list = list | reflect(state->breakpoints_box);
    }

    Element content =
        vbox({list | flex | vscroll_indicator, vbox(std::move(footer))}) | flex;

    return MakePanel(
        i18n::tr("panel.debug.title"),
        vbox({toolbar, content | bgcolor(theme::PanelBg()) | flex}));
      }),
      handler));

  if (layout_state != nullptr) {
    // En modo debug el workspace usa Renderer sin hijos interactivos; los eventos
    // del panel de depuración se enrutan desde Application vía estos handlers.
    layout_state->watches_mouse_handler = [panel](const Event& event) {
      if (!event.is_mouse()) {
        return false;
      }
      return panel->OnEvent(event);
    };
    layout_state->watches_key_handler = [panel](const Event& event) {
      if (event.is_mouse()) {
        return false;
      }
      return panel->OnEvent(event);
    };
  }

  return panel;
}

}  // namespace tgdb
