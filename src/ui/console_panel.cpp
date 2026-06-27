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
  Box content_box;
  Box history_box;
  Box terminal_box;
  Box input_box;
  int last_visible_lines = 1;
  int first_visible = 0;
  std::size_t last_output_size = 0;
  bool follow_tail = true;
  int last_terminal_cols = 0;
  int last_terminal_rows = 0;
  int pending_terminal_cols = 80;
  int pending_terminal_rows = 24;
  int applied_terminal_cols = 0;
  int applied_terminal_rows = 0;
  bool layout_measured = false;
  bool shell_start_requested = false;
  bool shell_start_failed = false;
  std::string last_workspace_root;
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

std::string console_panel_title(AppMode* app_mode, bool shell_running) {
  if (debug_console_mode(app_mode)) {
    return "GDB";
  }
  if (shell_running) {
    return "Terminal — escribe aquí (teclas al shell)";
  }
  return "Terminal";
}

constexpr int kMaxTermCols = 160;
constexpr int kMaxTermRows = 48;

int clamp_terminal_cols(int cols) {
  return std::max(8, std::min(cols, kMaxTermCols));
}

int clamp_terminal_rows(int rows) {
  return std::max(2, std::min(rows, kMaxTermRows));
}

bool terminal_box_valid(const Box& box) {
  if (box.y_max <= box.y_min || box.x_max <= box.x_min) {
    return false;
  }
  const int rows = box.y_max - box.y_min + 1;
  const int cols = box.x_max - box.x_min + 1;
  return rows >= 1 && cols >= 4;
}

void measure_terminal_layout(ConsolePanelState* state) {
  if (state == nullptr) {
    return;
  }
  if (!terminal_box_valid(state->content_box)) {
    return;
  }
  state->pending_terminal_cols = clamp_terminal_cols(visible_column_count(state->content_box));
  state->pending_terminal_rows = clamp_terminal_rows(visible_line_count(state->content_box));
  state->layout_measured = true;
}

void reset_terminal_session_state(ConsolePanelState* state, const std::string& workspace_root) {
  if (state == nullptr) {
    return;
  }
  if (state->last_workspace_root == workspace_root) {
    return;
  }
  state->last_workspace_root = workspace_root;
  state->shell_start_requested = false;
  state->shell_start_failed = false;
  state->applied_terminal_cols = 0;
  state->applied_terminal_rows = 0;
}

void tick_terminal_shell(ConsolePanelState* state, ShellSession* shell, DebugModel* model) {
  if (state == nullptr || shell == nullptr || model == nullptr) {
    return;
  }
  if (model->workspace_root.empty()) {
    return;
  }

  reset_terminal_session_state(state, model->workspace_root);

  const int cols =
      state->layout_measured ? state->pending_terminal_cols : state->pending_terminal_cols;
  const int rows =
      state->layout_measured ? state->pending_terminal_rows : state->pending_terminal_rows;

  if (!shell->running()) {
    if (shell->starting()) {
      return;
    }
    if (shell->start_failed()) {
      state->shell_start_failed = true;
      return;
    }
    if (!state->shell_start_requested || state->shell_start_failed) {
      return;
    }
    shell->request_start(model->workspace_root, cols, rows);
    return;
  }

  state->shell_start_failed = false;
  if (state->applied_terminal_cols == 0 && state->applied_terminal_rows == 0) {
    state->applied_terminal_cols = cols;
    state->applied_terminal_rows = rows;
  }

  if (!state->layout_measured) {
    return;
  }

  if (cols != state->applied_terminal_cols || rows != state->applied_terminal_rows) {
    shell->resize(cols, rows);
    state->applied_terminal_cols = cols;
    state->applied_terminal_rows = rows;
  }
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
                          FocusManagerState* focus, ShellSession* shell,
                          ConsolePanelState* state) {
  activate_console_input(layout_state, focus, nullptr);
  if (state != nullptr) {
    state->shell_start_requested = true;
    state->shell_start_failed = false;
  }
  tick_terminal_shell(state, shell, model);
}

Element render_shell_terminal(ShellSession* shell, DebugModel* model, ConsolePanelState* state) {
  if (model->workspace_root.empty()) {
    return text("(selecciona workspace con F3)") | color(theme::Muted()) | center | flex |
           bgcolor(theme::PanelBg());
  }

  if (shell == nullptr || !shell->running()) {
    if (shell != nullptr && shell->starting()) {
      return text("(iniciando terminal...)") | color(theme::Muted()) | center | flex |
             bgcolor(theme::PanelBg());
    }
    if (state != nullptr && state->shell_start_failed) {
      return text("(shell no disponible)") | color(theme::Muted()) | center | flex |
             bgcolor(theme::PanelBg());
    }
    return text("(F4 o clic para abrir la terminal)") | color(theme::Muted()) | center | flex |
           bgcolor(theme::PanelBg());
  }

  return shell->render_terminal() | flex | bgcolor(theme::CodeBg());
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

    if (!debug_mode && event == Event::Custom) {
      if (layout_state != nullptr && layout_state->terminal_start_requested) {
        state->shell_start_requested = true;
        state->shell_start_failed = false;
      }
      tick_terminal_shell(state.get(), shell, model);
      return false;
    }

    const int visible = state->last_visible_lines;
    const int total = static_cast<int>(model->console_output.size());
    const int max_first = max_first_visible(total, visible);

    if (console_input_active(layout_state) && !debug_mode && event_is_ctrl_c(event)) {
      if (shell != nullptr && shell->running()) {
        shell->send_interrupt();
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
      if (!debug_mode && state->content_box.Contain(m.x, m.y)) {
        activate_shell_input(model, layout_state, focus, shell, state.get());
        return true;
      }
      if (debug_mode && state->input_box.Contain(m.x, m.y)) {
        activate_console_input(layout_state, focus, input_box);
        return false;
      }
    }

    if (console_input_active(layout_state) && !debug_mode) {
      if (forward_pty_key(shell, event)) {
        return true;
      }
      if (event == Event::Escape) {
        return true;
      }
      return false;
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
      activate_shell_input(model, layout_state, focus, shell, state.get());
      if (forward_pty_key(shell, event)) {
        return true;
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
      }
      return true;
    }
    if (event == Event::Escape) {
      if (layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }
    return forward_pty_key(shell, event);
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
      if (layout_state != nullptr && layout_state->terminal_start_requested) {
        state->shell_start_requested = true;
        state->shell_start_failed = false;
      }
      measure_terminal_layout(state.get());
      tick_terminal_shell(state.get(), shell, model);
      if (shell != nullptr && shell->running()) {
        shell->drain_output(8192);
      }
      state->last_visible_lines =
          state->layout_measured ? state->pending_terminal_rows : state->last_visible_lines;
      state->terminal_box = state->content_box;
      const bool shell_running = shell != nullptr && shell->running();
      Element body = render_shell_terminal(shell, model, state.get());
      return MakePanel(console_panel_title(app_mode, shell_running),
                       body | reflect(state->content_box) | flex);
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
        console_panel_title(app_mode, false),
        vbox({history_row | flex, separator() | size(HEIGHT, EQUAL, 1), input_row}) |
            reflect(state->panel_box) | flex);
  });
}

}  // namespace tgdb
