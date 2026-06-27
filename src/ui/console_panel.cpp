#include "ui/console_panel.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "terminal/pty_input.hpp"
#include "ui/focus_manager.hpp"
#include "ui/key_bindings.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct ConsolePanelState {
  std::string input;
  std::string input_placeholder = "Shell";
  Box panel_box;
  Box history_box;
  Box terminal_box;
  Box input_box;
  int last_visible_lines = 1;
  int first_visible = 0;
  std::size_t last_output_size = 0;
  bool follow_tail = true;
  int last_terminal_cols = 0;
  int last_terminal_rows = 0;
};

void handle_gdb_command(const std::string& line, DebugModel* model, CommandCallback on_command) {
  if (line.empty() || !on_command) {
    return;
  }

  if (line.rfind("watch ", 0) == 0) {
    const std::string expression = line.substr(6);
    model->add_watch(expression);
    UiCommand command;
    command.kind = UiCommandKind::kAddWatch;
    command.expression = expression;
    if (!model->stack_frames.empty()) {
      if (model->variables_frame_id >= 0) {
        command.frame_id = model->variables_frame_id;
      } else {
        command.frame_id = model->stack_frames[model->selected_frame].id;
      }
    }
    on_command(command);
    return;
  }

  UiCommand command;
  command.kind = UiCommandKind::kEvaluate;
  command.expression = line;
  command.evaluate_context = EvaluateContext::kRepl;
  if (!model->stack_frames.empty()) {
    if (model->variables_frame_id >= 0) {
      command.frame_id = model->variables_frame_id;
    } else {
      command.frame_id = model->stack_frames[model->selected_frame].id;
    }
  }
  on_command(command);
}

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int visible_column_count(const Box& box) {
  if (box.x_max < box.x_min) {
    return 1;
  }
  return std::max(1, box.x_max - box.x_min + 1);
}

int max_first_visible(int total_lines, int visible_lines) {
  return std::max(0, total_lines - visible_lines);
}

void scroll_to_tail(ConsolePanelState* state, int total_lines, int visible_lines) {
  state->first_visible = max_first_visible(total_lines, visible_lines);
  state->follow_tail = true;
}

void clamp_scroll(ConsolePanelState* state, int total_lines, int visible_lines) {
  state->first_visible = std::max(
      0, std::min(state->first_visible, max_first_visible(total_lines, visible_lines)));
}

Element vertical_scrollbar(int total_lines, int first_visible, int visible_lines,
                           int bar_height) {
  Elements track;
  if (bar_height <= 0) {
    return text("");
  }

  if (total_lines <= visible_lines) {
    for (int i = 0; i < bar_height; ++i) {
      track.push_back(text("│") | color(theme::Muted()));
    }
    return vbox(std::move(track));
  }

  const int thumb_height = std::max(1, visible_lines * bar_height / total_lines);
  const int max_first = total_lines - visible_lines;
  const int thumb_y =
      max_first > 0 ? (first_visible * (bar_height - thumb_height)) / max_first : 0;

  for (int i = 0; i < bar_height; ++i) {
    if (i >= thumb_y && i < thumb_y + thumb_height) {
      track.push_back(text("┃") | color(theme::Accent()));
    } else {
      track.push_back(text("│") | color(theme::Muted()));
    }
  }
  return vbox(std::move(track));
}

bool console_input_active(MainLayoutState* layout_state) {
  return layout_state &&
         layout_state->text_input_focus == TextInputFocus::Console;
}

bool debug_console_mode(AppMode* app_mode) {
  return app_mode != nullptr && *app_mode == AppMode::kDebug;
}

std::string console_placeholder(AppMode* app_mode) {
  return debug_console_mode(app_mode) ? "Comando GDB o -exec ..." : "";
}

void ensure_shell_running(ShellSession* shell, const std::string& workspace_root) {
  if (shell != nullptr && !shell->running() && !workspace_root.empty()) {
    shell->start(workspace_root);
  }
}

std::string console_panel_title(AppMode* app_mode) {
  return debug_console_mode(app_mode) ? "GDB" : "Terminal";
}

void sync_terminal_size(ConsolePanelState* state, ShellSession* shell, const Box& box) {
  if (state == nullptr || shell == nullptr) {
    return;
  }
  const int cols = visible_column_count(box);
  const int rows = visible_line_count(box);
  if (cols == state->last_terminal_cols && rows == state->last_terminal_rows) {
    return;
  }
  state->last_terminal_cols = cols;
  state->last_terminal_rows = rows;
  shell->resize(cols, rows);
}

bool forward_pty_key(ShellSession* shell, const Event& event) {
  if (shell == nullptr || !shell->running()) {
    return false;
  }
  const std::optional<std::string> bytes = event_to_pty_bytes(event);
  if (!bytes.has_value()) {
    return false;
  }
  shell->write_raw(*bytes);
  return true;
}

void activate_console_input(MainLayoutState* layout_state, FocusManagerState* focus,
                            Component input_box) {
  if (layout_state) {
    layout_state->text_input_focus = TextInputFocus::Console;
    layout_state->focus_sync_needed = true;
  }
  if (focus) {
    focus->region = FocusRegion::Terminal;
  }
  if (input_box) {
    input_box->TakeFocus();
  }
}

void activate_shell_input(DebugModel* model, MainLayoutState* layout_state,
                          FocusManagerState* focus, ShellSession* shell) {
  activate_console_input(layout_state, focus, nullptr);
  if (model != nullptr) {
    ensure_shell_running(shell, model->workspace_root);
  }
  if (shell != nullptr) {
    shell->drain_output();
  }
}

Element render_shell_terminal(ShellSession* shell, DebugModel* model,
                              ConsolePanelState* state, MainLayoutState* layout_state) {
  if (model->workspace_root.empty()) {
    return text("(selecciona workspace con F3)") | color(theme::Muted()) | center | flex |
           bgcolor(theme::PanelBg());
  }

  ensure_shell_running(shell, model->workspace_root);
  shell->drain_output();

  if (shell == nullptr || !shell->running()) {
    return text("(no se pudo iniciar el shell)") | color(theme::Muted()) | center | flex |
           bgcolor(theme::PanelBg());
  }

  Element term = shell->terminal().render();
  return term | reflect(state->terminal_box) | flex | bgcolor(theme::CodeBg());
}

}  // namespace

Component MakeConsolePanel(AppMode* app_mode, DebugModel* model, ShellSession* shell,
                           CommandCallback on_command, MainLayoutState* layout_state,
                           FocusManagerState* focus) {
  auto state = std::make_shared<ConsolePanelState>();
  InputOption input_opt = InputOption::Default();
  input_opt.multiline = false;
  input_opt.transform = [](InputState input_state) {
    input_state.element |= bgcolor(theme::CodeBg()) | color(theme::WatchInput());
    if (input_state.is_placeholder) {
      input_state.element |= dim;
    }
    if (input_state.focused) {
      input_state.element |= inverted;
    }
    return input_state.element;
  };
  auto input_box = Input(&state->input, &state->input_placeholder, input_opt);
  auto input_maybe = Maybe(
      input_box, [app_mode, layout_state] {
        return debug_console_mode(app_mode) && console_input_active(layout_state);
      });

  auto component = Container::Vertical({input_maybe});

  auto wrapped = CatchEvent(component, [app_mode, model, shell, on_command, state,
                                        layout_state, focus, input_box](Event event) {
    const bool debug_mode = debug_console_mode(app_mode);
    const int visible = state->last_visible_lines;
    const int total = static_cast<int>(model->console_output.size());
    const int max_first = max_first_visible(total, visible);

    if (console_input_active(layout_state) && !debug_mode && event_is_ctrl_c(event)) {
      if (shell != nullptr && shell->running()) {
        shell->send_interrupt();
        shell->drain_output();
      }
      return true;
    }

    if (event == Event::Escape) {
      if (layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      if (!debug_mode && state->terminal_box.Contain(m.x, m.y)) {
        activate_shell_input(model, layout_state, focus, shell);
        return true;
      }
      if (debug_mode && state->input_box.Contain(m.x, m.y)) {
        activate_console_input(layout_state, focus, input_box);
        return false;
      }
    }

    if (console_input_active(layout_state) && !debug_mode) {
      if (forward_pty_key(shell, event)) {
        shell->drain_output();
        return true;
      }
      if (!event.is_mouse()) {
        return true;
      }
    }

    if (event == Event::Return && debug_mode) {
      if (!console_input_active(layout_state)) {
        activate_console_input(layout_state, focus, input_box);
        return true;
      }
      const std::string line = state->input;
      state->input.clear();
      model->append_console("> " + line);
      handle_gdb_command(line, model, on_command);
      scroll_to_tail(state.get(), static_cast<int>(model->console_output.size()), visible);
      return true;
    }

    if (event == Event::Return && !debug_mode) {
      activate_shell_input(model, layout_state, focus, shell);
      if (forward_pty_key(shell, event)) {
        shell->drain_output();
      }
      return true;
    }

    if (console_input_active(layout_state) && debug_mode) {
      return false;
    }

    if (debug_mode) {
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        state->follow_tail = false;
        state->first_visible = std::max(0, state->first_visible - 1);
        return true;
      }
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        state->first_visible = std::min(state->first_visible + 1, max_first);
        if (state->first_visible >= max_first) {
          state->follow_tail = true;
        }
        return true;
      }
      if (event == Event::PageUp) {
        state->follow_tail = false;
        state->first_visible = std::max(0, state->first_visible - visible);
        return true;
      }
      if (event == Event::PageDown) {
        state->first_visible = std::min(state->first_visible + visible, max_first);
        if (state->first_visible >= max_first) {
          state->follow_tail = true;
        }
        return true;
      }
      if (event.is_mouse() && event.mouse().motion == Mouse::Pressed) {
        if (event.mouse().button == Mouse::WheelUp) {
          state->follow_tail = false;
          state->first_visible = std::max(0, state->first_visible - 3);
          return true;
        }
        if (event.mouse().button == Mouse::WheelDown) {
          state->first_visible = std::min(state->first_visible + 3, max_first);
          if (state->first_visible >= max_first) {
            state->follow_tail = true;
          }
          return true;
        }
      }
    }

    return false;
  });

  auto dispatch_console_keys = [app_mode, layout_state, shell](Event event) -> bool {
    if (!console_input_active(layout_state)) {
      return false;
    }
    if (debug_console_mode(app_mode)) {
      return false;
    }
    if (event_is_ctrl_c(event)) {
      if (shell != nullptr && shell->running()) {
        shell->send_interrupt();
        shell->drain_output();
      }
      return true;
    }
    if (event == Event::Escape) {
      if (layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }
    if (forward_pty_key(shell, event)) {
      shell->drain_output();
      return true;
    }
    if (!event.is_mouse()) {
      return true;
    }
    return false;
  };

  if (layout_state != nullptr) {
    layout_state->console_key_handler = dispatch_console_keys;
  }

  return Renderer(wrapped, [app_mode, model, shell, input_box, input_maybe, state,
                            layout_state] {
    state->input_placeholder = console_placeholder(app_mode);
    (void)input_maybe;

    const bool debug_mode = debug_console_mode(app_mode);

    if (!debug_mode) {
      int rows = visible_line_count(state->panel_box);
      int cols = visible_column_count(state->panel_box);
      if (rows <= 1 && state->panel_box.y_max > state->panel_box.y_min) {
        rows = std::max(1, visible_line_count(state->panel_box) - 2);
      }
      if (cols <= 1 && state->panel_box.x_max > state->panel_box.x_min) {
        cols = std::max(1, visible_column_count(state->panel_box) - 2);
      }
      state->last_visible_lines = rows;
      state->terminal_box = state->panel_box;
      sync_terminal_size(state.get(), shell, state->terminal_box);
      shell->drain_output();

      Element terminal_row = render_shell_terminal(shell, model, state.get(), layout_state);
      return MakePanel(console_panel_title(app_mode),
                       terminal_row | reflect(state->panel_box) | flex);
    }

    const std::vector<std::string>& terminal_lines = model->console_output;
    const int total = static_cast<int>(terminal_lines.size());

    int visible = visible_line_count(state->history_box);
    if (visible <= 1 && state->panel_box.y_max > state->panel_box.y_min) {
      visible = std::max(1, visible_line_count(state->panel_box) - 2);
    }
    state->last_visible_lines = visible;

    const std::size_t output_size = model->console_output.size();
    if (output_size > state->last_output_size) {
      if (state->follow_tail) {
        scroll_to_tail(state.get(), total, visible);
      }
      state->last_output_size = output_size;
    } else if (output_size < state->last_output_size) {
      state->last_output_size = output_size;
      scroll_to_tail(state.get(), total, visible);
    }

    clamp_scroll(state.get(), total, visible);

    const int end = std::min(total, state->first_visible + visible);
    Elements history;
    for (int i = state->first_visible; i < end; ++i) {
      const std::string& line = terminal_lines[static_cast<std::size_t>(i)];
      history.push_back(text(line.empty() ? " " : line) | color(theme::Header()));
    }
    if (history.empty()) {
      history.push_back(text("(sin salida GDB)") | color(theme::Muted()));
    }

    const int rendered_lines = static_cast<int>(history.size());
    Element history_row =
        hbox({vbox(std::move(history)) | flex | reflect(state->history_box) |
                  bgcolor(theme::PanelBg()),
              vertical_scrollbar(total, state->first_visible, visible, rendered_lines)}) |
        flex | bgcolor(theme::PanelBg());

    Element input_row;
    if (console_input_active(layout_state)) {
      input_row = input_box->Render() | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
                  color(theme::WatchInput());
    } else {
      const std::string preview =
          state->input.empty() ? console_placeholder(app_mode) + " (clic o Enter)"
                               : state->input;
      input_row = text(" " + preview + " ") | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
                  color(state->input.empty() ? theme::Muted() : theme::WatchInput());
    }
    input_row = input_row | reflect(state->input_box);

    return MakePanel(
        console_panel_title(app_mode),
        vbox({history_row | flex, separator() | size(HEIGHT, EQUAL, 1), input_row}) |
            reflect(state->panel_box) | flex);
  });
}

}  // namespace tgdb
