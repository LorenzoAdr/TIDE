#include "ui/watches_panel.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/panel.hpp"
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
  std::array<std::string, kTabCount> tab_titles = {"Wtch", "Var", "Bt", "Bkts"};
  std::string expr_input;
  int flat_selected = 0;
  int bp_selected = 0;
  std::vector<FlatVariable> flat_variables;
  std::vector<BreakpointRow> breakpoint_rows;
  Box variables_box;
  Box stack_box;
  Box breakpoints_box;
  std::array<Box, kTabCount> tab_boxes;
  Box play_box;
  Box stop_box;
  std::unordered_set<std::string> expanded_expressions;
};

void assign_watch_frame(UiCommand* command, DebugModel* model) {
  if (model->variables_frame_id >= 0) {
    command->frame_id = model->variables_frame_id;
  } else if (!model->stack_frames.empty()) {
    command->frame_id = model->stack_frames[model->selected_frame].id;
  }
}

void submit_watch(const std::string& expression, DebugModel* model,
                  CommandCallback on_command) {
  if (expression.empty()) {
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

bool switch_tab(WatchesPanelState* state, int tab) {
  if (tab < 0 || tab >= kTabCount) {
    return false;
  }
  state->selected_tab = tab;
  return true;
}

bool switch_tab_from_mouse(WatchesPanelState* state, int mouse_x, int mouse_y) {
  for (int i = 0; i < kTabCount; ++i) {
    if (state->tab_boxes[i].Contain(mouse_x, mouse_y)) {
      return switch_tab(state, i);
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
                          const Mouse& mouse, const std::function<void()>& send_continue,
                          const std::function<void()>& send_pause,
                          const std::function<void()>& send_detach) {
  if (state->play_box.Contain(mouse.x, mouse.y)) {
    if (model->state == DebugState::kRunning) {
      send_pause();
    } else if (model->state == DebugState::kStopped) {
      send_continue();
    }
    return true;
  }
  if (state->stop_box.Contain(mouse.x, mouse.y)) {
    send_detach();
    return true;
  }
  return switch_tab_from_mouse(state, mouse.x, mouse.y);
}

}  // namespace

Component MakeWatchesPanel(DebugModel* model, CommandCallback on_command) {
  auto state = std::make_shared<WatchesPanelState>();
  auto expr_input = Input(&state->expr_input, "expresión");

  auto select_frame = [model, on_command](int index) {
    if (model->stack_frames.empty()) {
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
    UiCommand command;
    command.kind = UiCommandKind::kContinue;
    on_command(command);
  };

  auto send_pause = [on_command] {
    UiCommand command;
    command.kind = UiCommandKind::kPause;
    on_command(command);
  };

  auto send_detach = [on_command] {
    UiCommand command;
    command.kind = UiCommandKind::kDetach;
    on_command(command);
  };

  auto handler = [model, state, select_frame, on_command, send_continue, send_pause,
                  send_detach](Event event) {
    if (event == Event::Character('1')) {
      return switch_tab(state.get(), 0);
    }
    if (event == Event::Character('2')) {
      return switch_tab(state.get(), 1);
    }
    if (event == Event::Character('3')) {
      return switch_tab(state.get(), 2);
    }
    if (event == Event::Character('4')) {
      return switch_tab(state.get(), 3);
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      if (handle_toolbar_mouse(state.get(), model, event.mouse(), send_continue,
                               send_pause, send_detach)) {
        return true;
      }
    }

    if (state->selected_tab == 0) {
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

    if (state->selected_tab == 1) {
      rebuild_flat_variables(model, state.get());

      if (event == Event::ArrowDown || event == Event::Character('j')) {
        state->flat_selected = std::min(
            state->flat_selected + 1,
            std::max(0, static_cast<int>(state->flat_variables.size()) - 1));
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        state->flat_selected = std::max(0, state->flat_selected - 1);
        return true;
      }
      if (event == Event::ArrowRight) {
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
        add_flat_as_watch(state.get(), model, on_command);
        return true;
      }
      if (event == Event::Return) {
        add_flat_as_watch(state.get(), model, on_command);
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->variables_box.Contain(m.x, m.y)) {
          const int row = m.y - state->variables_box.y_min;
          if (row >= 0 && row < static_cast<int>(state->flat_variables.size())) {
            state->flat_selected = row;
            return true;
          }
        }
      }
      return true;
    }

    if (state->selected_tab == 2) {
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        select_frame(model->selected_frame + 1);
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        select_frame(model->selected_frame - 1);
        return true;
      }
      if (event == Event::Return) {
        select_frame(model->selected_frame);
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->stack_box.Contain(m.x, m.y)) {
          const int row = m.y - state->stack_box.y_min;
          if (row >= 0 && row < static_cast<int>(model->stack_frames.size())) {
            select_frame(row);
            return true;
          }
        }
      }
      return true;
    }

    if (state->selected_tab == 3) {
      rebuild_breakpoint_rows(model, state.get());
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        state->bp_selected = std::min(
            state->bp_selected + 1,
            std::max(0, static_cast<int>(state->breakpoint_rows.size()) - 1));
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        state->bp_selected = std::max(0, state->bp_selected - 1);
        return true;
      }
      if (event == Event::Return || event == Event::Character(' ')) {
        toggle_breakpoint_enabled(state.get(), model, on_command);
        return true;
      }
      if (event == Event::Delete || event == Event::Character('x') ||
          event == Event::Character('d')) {
        remove_selected_breakpoint(state.get(), model, on_command);
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->breakpoints_box.Contain(m.x, m.y)) {
          const int row = m.y - state->breakpoints_box.y_min;
          if (row >= 0 &&
              row < static_cast<int>(state->breakpoint_rows.size())) {
            state->bp_selected = row;
            const auto& bp = state->breakpoint_rows[row];
            model->active_file = bp.file;
            model->active_line = bp.line;
            model->view_token++;
            return true;
          }
        }
      }
      return true;
    }

    return false;
  };

  return CatchEvent(
      Renderer(expr_input, [model, state, expr_input] {
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
      for (const auto& watch : model->watches) {
        body.push_back(hbox({text(watch.expression) | bold | color(theme::Accent()),
                             text(" = "), text(watch.value)}));
      }
      if (model->watches.empty()) {
        body.push_back(text("(sin watches)") | color(theme::Muted()));
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
        body.push_back(text("Espacio: activar/des  x: quitar") | color(theme::Muted()));
      }
    }

    Elements footer;
    if (state->selected_tab == 0) {
      footer = {separator(),
                hbox({text(" watch: ") | color(theme::WatchInput()),
                      expr_input->Render() | border | flex | bgcolor(theme::PanelBg())})};
    }

    Element list = vbox(std::move(body));
    if (state->selected_tab == 1) {
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
      handler);
}

}  // namespace tgdb
