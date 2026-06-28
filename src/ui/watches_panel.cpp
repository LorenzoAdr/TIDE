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
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kTabCount = 4;

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
  std::string file;
  int line = 0;
};

struct WatchesPanelState {
  int selected_tab = 0;
  bool interaction_active = false;
  std::array<std::string, kTabCount> tab_titles = {"Wtch", "Var", "Bt", "Bkts"};
  std::string expr_input;
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
  Box watches_box;
  Box watch_input_box;
  Box watch_inject_box;
  std::array<Box, kTabCount> tab_boxes;
  Box play_box;
  Box stop_box;
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

void cancel_watch_inject(WatchesPanelState* state, MainLayoutState* layout_state) {
  state->inject_mode = false;
  state->inject_input.clear();
  if (layout_state &&
      layout_state->text_input_focus == TextInputFocus::WatchInject) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
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
                  CommandCallback on_command) {
  if (expression.empty() || !on_command) {
    return;
  }
  model->add_watch(expression);
  UiCommand command;
  command.kind = UiCommandKind::kAddWatch;
  command.expression = expression;
  assign_watch_frame(&command, model);
  on_command(command);
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
    return "(sin frame)";
  }
  for (const auto& frame : model->stack_frames) {
    if (frame.id == model->variables_frame_id) {
      return frame.name;
    }
  }
  return "frame #" + std::to_string(model->variables_frame_id);
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
      state->breakpoint_rows.push_back({file, line});
    }
  }
  std::sort(state->breakpoint_rows.begin(), state->breakpoint_rows.end(),
            [](const BreakpointRow& a, const BreakpointRow& b) {
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
                       CommandCallback on_command) {
  if (state->flat_variables.empty()) {
    return;
  }
  const int index = std::max(
      0, std::min(state->flat_selected,
                  static_cast<int>(state->flat_variables.size()) - 1));
  submit_watch(state->flat_variables[index].expression, model, on_command);
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
  if (tab != 0) {
    cancel_watch_inject(state, layout_state);
  }
  return true;
}

bool switch_tab_from_mouse(WatchesPanelState* state, int mouse_x, int mouse_y,
                           MainLayoutState* layout_state) {
  for (int i = 0; i < kTabCount; ++i) {
    if (state->tab_boxes[i].Contain(mouse_x, mouse_y)) {
      return switch_tab(state, i, layout_state);
    }
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
  const auto basename = std::filesystem::path(row.file).filename().string();
  return basename + ":" + std::to_string(row.line);
}

void toggle_breakpoint_enabled(WatchesPanelState* state, DebugModel* model,
                               CommandCallback on_command) {
  if (state->breakpoint_rows.empty()) {
    return;
  }
  const int index = std::max(
      0, std::min(state->bp_selected,
                  static_cast<int>(state->breakpoint_rows.size()) - 1));
  const auto& row = state->breakpoint_rows[index];
  const bool enabled = model->is_breakpoint_enabled(row.file, row.line);
  model->set_breakpoint_enabled(row.file, row.line, !enabled);
  sync_breakpoints_file(model, row.file, on_command);
}

void remove_selected_breakpoint(WatchesPanelState* state, DebugModel* model,
                                CommandCallback on_command) {
  if (state->breakpoint_rows.empty()) {
    return;
  }
  const int index = std::max(
      0, std::min(state->bp_selected,
                  static_cast<int>(state->breakpoint_rows.size()) - 1));
  const auto row = state->breakpoint_rows[index];
  model->remove_breakpoint(row.file, row.line);
  sync_breakpoints_file(model, row.file, on_command);
  rebuild_breakpoint_rows(model, state);
}

void remove_selected_watch(WatchesPanelState* state, DebugModel* model) {
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
}

Element make_tab_button(const std::string& label, bool selected, Box* box) {
  Element tab = text(" " + label + " ") | center | size(HEIGHT, EQUAL, 1);
  if (selected) {
    tab = tab | bold | color(theme::Header()) | bgcolor(theme::TabActive());
  } else {
    tab = tab | color(theme::Muted()) | bgcolor(theme::TabIdle());
  }
  return tab | flex | reflect(*box);
}

bool handle_toolbar_mouse(WatchesPanelState* state, DebugModel* model,
                          const Mouse& mouse, MainLayoutState* layout_state,
                          const std::function<void()>& send_continue,
                          const std::function<void()>& send_pause,
                          const std::function<void()>& send_stop) {
  if (state->play_box.Contain(mouse.x, mouse.y)) {
    if (model->state == DebugState::kRunning) {
      if (send_pause) {
        send_pause();
      }
    } else if (model->state == DebugState::kStopped) {
      if (send_continue) {
        send_continue();
      }
    }
    return true;
  }
  if (state->stop_box.Contain(mouse.x, mouse.y)) {
    if (send_stop) {
      send_stop();
    }
    return true;
  }
  return switch_tab_from_mouse(state, mouse.x, mouse.y, layout_state);
}

}  // namespace

Component MakeWatchesPanel(DebugModel* model, CommandCallback on_command,
                           MainLayoutState* layout_state,
                           const std::function<void()>& on_stop_debug,
                           FocusManagerState* focus) {
  auto state = std::make_shared<WatchesPanelState>();
  InputOption input_opt = InputOption::Default();
  input_opt.multiline = false;
  auto expr_input = Input(&state->expr_input, "expresión", input_opt);
  auto inject_input = Input(&state->inject_input, "nuevo valor", input_opt);
  auto expr_maybe = Maybe(expr_input, [layout_state, state] {
    return watch_input_active(layout_state, state->selected_tab);
  });
  auto inject_maybe = Maybe(inject_input, [layout_state, state] {
    return watch_inject_active(layout_state, state->selected_tab);
  });
  auto inputs = Container::Vertical({inject_maybe, expr_maybe});

  auto select_frame = [model, on_command](int index) {
    if (!on_command || model->stack_frames.empty()) {
      return;
    }
    index = std::max(0, std::min(index, static_cast<int>(model->stack_frames.size()) - 1));
    model->selected_frame = index;
    const auto& frame = model->stack_frames[index];
    model->active_file = frame.file;
    model->active_line = frame.line;
    model->view_token++;
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

  auto handler = [model, state, layout_state, focus, select_frame, on_command, send_continue,
                  send_pause, send_stop, expr_input, inject_input](Event event) {
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
      state->interaction_active = false;
      if (layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }

    if (event == Event::Character('1')) {
      mark_watches_focus();
      return switch_tab(state.get(), 0, layout_state);
    }
    if (event == Event::Character('2')) {
      mark_watches_focus();
      return switch_tab(state.get(), 1, layout_state);
    }
    if (event == Event::Character('3')) {
      mark_watches_focus();
      return switch_tab(state.get(), 2, layout_state);
    }
    if (event == Event::Character('4')) {
      mark_watches_focus();
      return switch_tab(state.get(), 3, layout_state);
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      if (handle_toolbar_mouse(state.get(), model, event.mouse(), layout_state,
                               send_continue, send_pause, send_stop)) {
        mark_watches_focus();
        return true;
      }
    }

    const auto owns_watches_navigation = [&]() {
      return state->interaction_active ||
             watch_input_active(layout_state, state->selected_tab) ||
             watch_inject_active(layout_state, state->selected_tab);
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
          return false;
        }
        if (state->watch_input_box.Contain(m.x, m.y)) {
          mark_watches_focus();
          cancel_watch_inject(state.get(), layout_state);
          if (layout_state) {
            layout_state->text_input_focus = TextInputFocus::Watch;
          }
          expr_input->TakeFocus();
          return false;
        }
        if (state->watches_box.Contain(m.x, m.y)) {
          mark_watches_focus();
          const int row = m.y - state->watches_box.y_min;
          if (row >= 0 && row < static_cast<int>(model->watches.size())) {
            if (is_double_click(row, &state->last_watch_click_row,
                                &state->last_watch_click_ms)) {
              state->watch_selected = row;
              remove_selected_watch(state.get(), model);
            } else {
              state->watch_selected = row;
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
            submit_watch(expr, model, on_command);
            state->expr_input.clear();
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
        return true;
      }

      if (event == Event::ArrowDown || event == Event::Character('j')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->watch_selected = std::min(
            state->watch_selected + 1,
            std::max(0, static_cast<int>(model->watches.size()) - 1));
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->watch_selected = std::max(0, state->watch_selected - 1);
        return true;
      }
      if (event == Event::Delete || event == Event::Character('x') ||
          event == Event::Character('d')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        remove_selected_watch(state.get(), model);
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
        add_flat_as_watch(state.get(), model, on_command);
        return true;
      }
      if (event == Event::Return) {
        mark_watches_focus();
        add_flat_as_watch(state.get(), model, on_command);
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
      rebuild_breakpoint_rows(model, state.get());
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->bp_selected = std::min(
            state->bp_selected + 1,
            std::max(0, static_cast<int>(state->breakpoint_rows.size()) - 1));
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        state->bp_selected = std::max(0, state->bp_selected - 1);
        return true;
      }
      if (event == Event::Return || event == Event::Character(' ')) {
        mark_watches_focus();
        toggle_breakpoint_enabled(state.get(), model, on_command);
        return true;
      }
      if (event == Event::Delete || event == Event::Character('x') ||
          event == Event::Character('d')) {
        if (!owns_watches_navigation()) {
          return false;
        }
        remove_selected_breakpoint(state.get(), model, on_command);
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->breakpoints_box.Contain(m.x, m.y)) {
          mark_watches_focus();
          const int row = m.y - state->breakpoints_box.y_min;
          if (row >= 0 &&
              row < static_cast<int>(state->breakpoint_rows.size())) {
            const int rel_x = m.x - state->breakpoints_box.x_min;
            state->bp_selected = row;
            const auto& bp = state->breakpoint_rows[row];
            if (rel_x <= 2) {
              toggle_breakpoint_enabled(state.get(), model, on_command);
              return true;
            }
            if (is_double_click(row, &state->last_bp_click_row,
                                &state->last_bp_click_ms)) {
              remove_selected_breakpoint(state.get(), model, on_command);
              return true;
            }
            model->active_file = bp.file;
            model->active_line = bp.line;
            model->view_token++;
            return true;
          }
        }
      }
      return false;
    }

    return false;
  };

  return WrapFocusable(CatchEvent(
      Renderer(inputs, [model, state, expr_input, inject_input, layout_state, focus] {
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

    const bool running = model->state == DebugState::kRunning;
    const bool stopped = model->state == DebugState::kStopped;

    Element play_btn;
    if (running) {
      play_btn = text(" ⏸ ") | bold | color(theme::Pause()) | bgcolor(theme::TabIdle()) |
                 center | size(WIDTH, EQUAL, 5);
    } else if (stopped) {
      play_btn = text(" ▶ ") | bold | color(theme::Play()) | bgcolor(theme::TabIdle()) |
                 center | size(WIDTH, EQUAL, 5);
    } else {
      play_btn = text(" ▶ ") | dim | bgcolor(theme::TabIdle()) | center | size(WIDTH, EQUAL, 5);
    }
    Element stop_btn = text(" ⏹ ") | bold | color(theme::Stop()) | bgcolor(theme::TabIdle()) |
                       center | size(WIDTH, EQUAL, 5);

    Elements tab_row;
    for (int i = 0; i < kTabCount; ++i) {
      tab_row.push_back(
          make_tab_button(state->tab_titles[i], i == state->selected_tab, &state->tab_boxes[i]));
    }

    Element toolbar = vbox({
        hbox({play_btn | reflect(state->play_box), stop_btn | reflect(state->stop_box)}),
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
        body.push_back(text("(sin watches)") | color(theme::Muted()));
      } else {
        body.push_back(separator());
        body.push_back(text("e/Enter: editar valor  x: quitar  doble-clic: quitar") |
                       color(theme::Muted()));
      }
    } else if (state->selected_tab == 1) {
      body.push_back(text("frame: " + variables_frame_label(model)) |
                     color(theme::Muted()));
      body.push_back(separator());
      int index = 0;
      for (const auto& row : state->flat_variables) {
        const bool selected = index == state->flat_selected;
        std::string indent(static_cast<std::size_t>(row.depth * 2), ' ');
        std::string prefix = row.expandable ? (row.expanded ? "v " : "> ") : "  ";

        Element type_part =
            row.type.empty() ? text("") : text(" (" + row.type + ")");
        Element line = hbox({text(indent + prefix), text(row.name) | bold,
                             type_part, text(" = "), text(row.value)});
        if (selected) {
          line = line | inverted;
        }
        body.push_back(line);
        ++index;
      }
      if (state->flat_variables.empty()) {
        body.push_back(text("(sin variables)") | color(theme::Muted()));
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
        body.push_back(text("(vacío)") | color(theme::Muted()));
      } else {
        body.push_back(separator());
        body.push_back(text("clic/Enter → ir al frame") | color(theme::Muted()));
      }
    } else {
      int index = 0;
      for (const auto& row : state->breakpoint_rows) {
        const bool selected = index == state->bp_selected;
        const bool enabled = model->is_breakpoint_enabled(row.file, row.line);
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
      if (state->breakpoint_rows.empty()) {
        body.push_back(text("(sin breakpoints)") | color(theme::Muted()));
      } else {
        body.push_back(separator());
        body.push_back(text("●/○: clic activar  doble-clic: quitar  x: quitar") |
                       color(theme::Muted()));
      }
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
              state->inject_input.empty() ? "nuevo valor (clic o Enter)"
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
        footer.push_back(text(" Enter aplicar  Esc cancelar ") | color(theme::Muted()));
      }

      Element input_row;
      if (watch_input_active(layout_state, state->selected_tab)) {
        input_row = expr_input->Render() | border | flex | bgcolor(theme::PanelBg());
      } else {
        const std::string preview = state->expr_input.empty()
                                        ? "expresión (clic o Enter)"
                                        : state->expr_input;
        input_row = text(" " + preview + " ") | border | flex | bgcolor(theme::PanelBg()) |
                    color(state->expr_input.empty() ? theme::Muted() : theme::WatchInput());
      }
      footer.push_back(separator());
      footer.push_back(hbox({text(" watch: ") | color(theme::WatchInput()),
                             input_row | reflect(state->watch_input_box)}));
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
        "Depuración",
        vbox({toolbar, content | bgcolor(theme::PanelBg()) | flex}));
      }),
      handler));
}

}  // namespace tgdb
