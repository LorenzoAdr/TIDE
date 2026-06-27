#include "ui/console_panel.hpp"

#include <memory>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct ConsolePanelState {
  std::string input;
  Box panel_box;
  Box history_box;
  Box input_box;
  int last_visible_lines = 1;
  int first_visible = 0;
  std::size_t last_output_size = 0;
  bool follow_tail = true;
};

void handle_console_command(const std::string& line, DebugModel* model,
                            CommandCallback on_command) {
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

  const int thumb_height =
      std::max(1, visible_lines * bar_height / total_lines);
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

}  // namespace

Component MakeConsolePanel(DebugModel* model, CommandCallback on_command,
                           MainLayoutState* layout_state) {
  auto state = std::make_shared<ConsolePanelState>();
  InputOption input_opt = InputOption::Default();
  input_opt.multiline = false;
  auto input_box = Input(&state->input, "Comando GDB o -exec ...", input_opt);
  auto input_maybe = Maybe(
      input_box, [layout_state] { return console_input_active(layout_state); });

  auto component = Container::Vertical({input_maybe});

  auto wrapped = CatchEvent(component, [model, on_command, state, layout_state,
                                        input_box](Event event) {
    const int total = static_cast<int>(model->console_output.size());
    const int visible = state->last_visible_lines;
    const int max_first = max_first_visible(total, visible);

    if (event == Event::Escape) {
      if (layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      if (state->input_box.Contain(m.x, m.y)) {
        if (layout_state) {
          layout_state->text_input_focus = TextInputFocus::Console;
        }
        input_box->TakeFocus();
        return false;
      }
    }

    if (event == Event::Return) {
      if (!console_input_active(layout_state)) {
        if (layout_state) {
          layout_state->text_input_focus = TextInputFocus::Console;
        }
        input_box->TakeFocus();
        return true;
      }
      const std::string line = state->input;
      model->append_console("> " + line);
      handle_console_command(line, model, on_command);
      state->input.clear();
      scroll_to_tail(state.get(), static_cast<int>(model->console_output.size()),
                     visible);
      return true;
    }

    if (console_input_active(layout_state)) {
      return false;
    }

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
        state->first_visible =
            std::min(state->first_visible + 3, max_first);
        if (state->first_visible >= max_first) {
          state->follow_tail = true;
        }
        return true;
      }
    }
    return false;
  });

  return Renderer(wrapped, [model, input_box, input_maybe, state, layout_state] {
    const int total = static_cast<int>(model->console_output.size());

    int visible = visible_line_count(state->history_box);
    if (visible <= 1 && state->panel_box.y_max > state->panel_box.y_min) {
      visible = std::max(1, visible_line_count(state->panel_box) - 2);
    }
    state->last_visible_lines = visible;

    if (model->console_output.size() > state->last_output_size) {
      if (state->follow_tail) {
        scroll_to_tail(state.get(), total, visible);
      }
      state->last_output_size = model->console_output.size();
    } else if (model->console_output.size() < state->last_output_size) {
      state->last_output_size = model->console_output.size();
      scroll_to_tail(state.get(), total, visible);
    }

    clamp_scroll(state.get(), total, visible);

    const int end = std::min(total, state->first_visible + visible);
    Elements history;
    for (int i = state->first_visible; i < end; ++i) {
      history.push_back(text(model->console_output[i]) | color(theme::Header()));
    }
    if (history.empty()) {
      history.push_back(text("(sin salida)") | color(theme::Muted()));
    }

    const int rendered_lines = static_cast<int>(history.size());
    Element history_row =
        hbox({vbox(std::move(history)) | flex | reflect(state->history_box) |
                  bgcolor(theme::PanelBg()),
              vertical_scrollbar(total, state->first_visible, visible,
                                 rendered_lines)}) |
        flex | bgcolor(theme::PanelBg());

    Element input_row;
    if (console_input_active(layout_state)) {
      input_row = input_box->Render() | size(HEIGHT, EQUAL, 1) |
                  bgcolor(theme::TabIdle()) | color(theme::WatchInput());
    } else {
      const std::string preview =
          state->input.empty() ? "Comando GDB o -exec ... (clic o Enter)" : state->input;
      input_row = text(" " + preview + " ") | size(HEIGHT, EQUAL, 1) |
                  bgcolor(theme::TabIdle()) |
                  color(state->input.empty() ? theme::Muted() : theme::WatchInput());
    }
    input_row = input_row | reflect(state->input_box);

    return MakePanel(
        "Terminal",
        vbox({history_row | flex, separator() | size(HEIGHT, EQUAL, 1), input_row}) |
            reflect(state->panel_box) | flex);
  });
}

}  // namespace tgdb
