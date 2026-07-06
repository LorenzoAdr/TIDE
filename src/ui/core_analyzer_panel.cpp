#include "ui/core_analyzer_panel.hpp"

#include <algorithm>
#include <sstream>

#include "backend/idebug_backend.hpp"
#include "core_analyzer/output_parser.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/focus_manager.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/text_input_style.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

using CoreAnalyzerFocus = MainLayoutState::CoreAnalyzerFocus;

struct CoreAnalyzerPanelState {
  std::string command_input;
  std::string search_input;
  int log_first_visible = 0;
  int instances_first_visible = 0;
  int last_log_visible = 1;
  int last_instances_visible = 1;
  bool follow_log_tail = true;
  Box left_column_box;
  Box log_box;
  Box command_input_box;
  Box search_input_box;
  Box instances_box;
};

int visible_lines(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int max_first_visible(int total, int visible) {
  return std::max(0, total - visible);
}

void assign_watch_frame(UiCommand* command, DebugModel* model) {
  if (command == nullptr || model == nullptr) {
    return;
  }
  if (model->variables_frame_id >= 0) {
    command->frame_id = model->variables_frame_id;
  } else if (!model->stack_frames.empty()) {
    command->frame_id = model->stack_frames[model->selected_frame].id;
  }
}

void clamp_instance_scroll(CoreAnalyzerPanelState* state, int total, int visible, int selected) {
  if (state == nullptr || total <= 0 || selected < 0) {
    return;
  }
  if (selected < state->instances_first_visible) {
    state->instances_first_visible = selected;
  } else if (selected >= state->instances_first_visible + visible) {
    state->instances_first_visible = selected - visible + 1;
  }
  state->instances_first_visible =
      std::max(0, std::min(state->instances_first_visible, max_first_visible(total, visible)));
}

void set_selected_instance(int index, DebugModel* model, CoreAnalyzerPanelState* state,
                           int visible) {
  if (model == nullptr || state == nullptr) {
    return;
  }
  const int total = static_cast<int>(model->core_analyzer_instances.size());
  if (total == 0) {
    model->core_analyzer_selected_instance = -1;
    return;
  }
  model->core_analyzer_selected_instance = std::max(0, std::min(index, total - 1));
  clamp_instance_scroll(state, total, visible, model->core_analyzer_selected_instance);
}

void inspect_selected_instance(DebugModel* model, MainLayoutState* layout_state,
                               CommandCallback on_command) {
  if (model == nullptr || layout_state == nullptr || !on_command) {
    return;
  }
  const int selected = model->core_analyzer_selected_instance;
  if (selected < 0 || selected >= static_cast<int>(model->core_analyzer_instances.size())) {
    return;
  }
  add_watch_for_core_instance(
      model->core_analyzer_instances[static_cast<std::size_t>(selected)],
      model->core_analyzer_search_query, model, on_command);
  layout_state->pending_watches_focus = true;
}

CoreAnalyzerFocus next_focus(CoreAnalyzerFocus focus) {
  switch (focus) {
    case CoreAnalyzerFocus::kCommand:
      return CoreAnalyzerFocus::kSearch;
    case CoreAnalyzerFocus::kSearch:
      return CoreAnalyzerFocus::kInstances;
    case CoreAnalyzerFocus::kInstances:
      return CoreAnalyzerFocus::kCommand;
  }
  return CoreAnalyzerFocus::kCommand;
}

bool core_analyzer_panel_active(MainLayoutState* layout_state, FocusManagerState* focus) {
  if (focus == nullptr || focus->region != FocusRegion::Terminal) {
    return false;
  }
  if (layout_state == nullptr ||
      layout_state->console_tabs.selected_tab != ConsolePanelTabs::kCoreAnalyzer) {
    return false;
  }
  if (layout_state->text_input_focus != TextInputFocus::Console &&
      layout_state->text_input_focus != TextInputFocus::None) {
    return false;
  }
  return true;
}

bool handle_instance_navigation(Event event, DebugModel* model, CoreAnalyzerPanelState* state,
                                int visible) {
  if (model == nullptr || state == nullptr) {
    return false;
  }
  const int total = static_cast<int>(model->core_analyzer_instances.size());
  if (total == 0) {
    return false;
  }
  int selected = model->core_analyzer_selected_instance;
  if (selected < 0) {
    selected = 0;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    set_selected_instance(selected + 1, model, state, visible);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    set_selected_instance(selected - 1, model, state, visible);
    return true;
  }
  return false;
}

bool handle_core_analyzer_mouse(Event event, DebugModel* model, CoreAnalyzerPanelState* state,
                                MainLayoutState* layout_state, FocusManagerState* focus,
                                CommandCallback on_command) {
  if (!event.is_mouse() || layout_state == nullptr || state == nullptr) {
    return false;
  }
  const auto& m = event.mouse();
  const int inst_visible = std::max(1, state->last_instances_visible);
  const int inst_total = model != nullptr ? static_cast<int>(model->core_analyzer_instances.size())
                                          : 0;

  if (m.motion == Mouse::Moved && hover_effects_enabled()) {
    if (state->instances_box.Contain(m.x, m.y) && inst_total > 0) {
      const int visual_row = m.y - state->instances_box.y_min;
      const int row = state->instances_first_visible + visual_row;
      if (row >= 0 && row < inst_total) {
        layout_state->clickable.set_hover(press_id::core_analyzer_instance(row));
        layout_state->request_ui_tick = true;
      }
    } else {
      const std::string_view before = layout_state->clickable.hovered_id();
      layout_state->clickable.clear_hover_if(press_id::is_core_analyzer_hover);
      apply_hover_repaint(layout_state, before);
    }
  }

  if (state->instances_box.Contain(m.x, m.y) &&
      (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown)) {
    const int delta = m.button == Mouse::WheelUp ? -1 : 1;
    state->instances_first_visible =
        std::max(0, std::min(state->instances_first_visible + delta,
                             max_first_visible(inst_total, inst_visible)));
    layout_state->request_ui_tick = true;
    return true;
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed) {
    return false;
  }

  if (focus != nullptr) {
    focus->region = FocusRegion::Terminal;
  }
  layout_state->text_input_focus = TextInputFocus::Console;

  if (state->command_input_box.Contain(m.x, m.y)) {
    layout_state->core_analyzer_focus = CoreAnalyzerFocus::kCommand;
    cursor_blink::show();
    layout_state->request_ui_tick = true;
    return true;
  }
  if (state->search_input_box.Contain(m.x, m.y)) {
    layout_state->core_analyzer_focus = CoreAnalyzerFocus::kSearch;
    cursor_blink::show();
    layout_state->request_ui_tick = true;
    return true;
  }
  if (state->instances_box.Contain(m.x, m.y) && inst_total > 0) {
    layout_state->core_analyzer_focus = CoreAnalyzerFocus::kInstances;
    const int visual_row = m.y - state->instances_box.y_min;
    const int row = state->instances_first_visible + visual_row;
    if (row < 0 || row >= inst_total) {
      return true;
    }
    set_selected_instance(row, model, state, inst_visible);
    layout_state->request_ui_tick = true;
    return true;
  }
  return false;
}

bool handle_core_analyzer_keyboard(Event event, DebugModel* model, CoreAnalyzerPanelState* state,
                                   MainLayoutState* layout_state, CommandCallback on_command) {
  if (layout_state == nullptr || state == nullptr) {
    return false;
  }

  const CoreAnalyzerFocus focus = layout_state->core_analyzer_focus;
  const int inst_visible = std::max(1, state->last_instances_visible);

  if (event == Event::Tab || event == Event::TabReverse) {
    layout_state->core_analyzer_focus =
        event == Event::TabReverse ? CoreAnalyzerFocus::kCommand : next_focus(focus);
    if (layout_state->core_analyzer_focus != CoreAnalyzerFocus::kInstances) {
      cursor_blink::show();
    }
    return true;
  }

  if (focus == CoreAnalyzerFocus::kInstances) {
    if (handle_instance_navigation(event, model, state, inst_visible)) {
      return true;
    }
    if (event == Event::Return) {
      inspect_selected_instance(model, layout_state, on_command);
      return true;
    }
    if (event == Event::Character('w')) {
      inspect_selected_instance(model, layout_state, on_command);
      return true;
    }
    if (event == Event::Character('r') && model != nullptr) {
      const int selected = model->core_analyzer_selected_instance;
      if (selected >= 0 &&
          selected < static_cast<int>(model->core_analyzer_instances.size())) {
        const auto& inst =
            model->core_analyzer_instances[static_cast<std::size_t>(selected)];
        submit_core_analyzer_command("ref " + inst.address_hex, model, on_command);
      }
      return true;
    }
    if (event == Event::Escape) {
      layout_state->core_analyzer_focus = CoreAnalyzerFocus::kCommand;
      cursor_blink::show();
      return true;
    }
    return false;
  }

  if (focus == CoreAnalyzerFocus::kSearch) {
    if (event == Event::Return) {
      submit_core_analyzer_class_search(state->search_input, model, on_command);
      layout_state->core_analyzer_focus = CoreAnalyzerFocus::kInstances;
      return true;
    }
    if (event == Event::Backspace) {
      if (!state->search_input.empty()) {
        state->search_input.pop_back();
        cursor_blink::show();
      }
      return true;
    }
    if (event.is_character()) {
      const std::string ch = event.character();
      if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32) {
        state->search_input += ch;
        cursor_blink::show();
      }
      return true;
    }
    if (event == Event::ArrowDown) {
      layout_state->core_analyzer_focus = CoreAnalyzerFocus::kInstances;
      if (model != nullptr && !model->core_analyzer_instances.empty() &&
          model->core_analyzer_selected_instance < 0) {
        set_selected_instance(0, model, state, inst_visible);
      }
      return true;
    }
    if (event == Event::Escape) {
      state->search_input.clear();
      layout_state->core_analyzer_focus = CoreAnalyzerFocus::kCommand;
      cursor_blink::show();
      return true;
    }
    return false;
  }

  // Command focus (default).
  if (event == Event::Return) {
    submit_core_analyzer_command(state->command_input, model, on_command);
    state->command_input.clear();
    state->follow_log_tail = true;
    return true;
  }
  if (event == Event::Backspace) {
    if (!state->command_input.empty()) {
      state->command_input.pop_back();
      cursor_blink::show();
    }
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32) {
      state->command_input += ch;
      cursor_blink::show();
    }
    return true;
  }
  if (event == Event::ArrowUp && model != nullptr && !model->core_analyzer_instances.empty()) {
    layout_state->core_analyzer_focus = CoreAnalyzerFocus::kInstances;
    if (model->core_analyzer_selected_instance < 0) {
      set_selected_instance(0, model, state, inst_visible);
    }
    return true;
  }
  if (event == Event::Escape) {
    state->command_input.clear();
    return true;
  }
  return false;
}

}  // namespace

void submit_core_analyzer_command(const std::string& line, DebugModel* model,
                                  CommandCallback on_command) {
  if (line.empty() || model == nullptr || !on_command) {
    return;
  }
  model->append_core_analyzer_log("> " + line);
  UiCommand command;
  command.kind = UiCommandKind::kEvaluate;
  command.expression = line;
  command.evaluate_context = EvaluateContext::kCoreAnalyzer;
  assign_watch_frame(&command, model);
  on_command(command);
}

void submit_core_analyzer_class_search(const std::string& type_query, DebugModel* model,
                                       CommandCallback on_command) {
  if (model == nullptr) {
    return;
  }
  model->core_analyzer_search_query = type_query;
  const std::string cmd = build_obj_search_command(type_query);
  submit_core_analyzer_command(cmd, model, on_command);
}

void apply_core_analyzer_search_result(DebugModel* model, const std::string& output,
                                       const std::string& type_query) {
  if (model == nullptr) {
    return;
  }
  CoreAnalyzerParseResult parsed = parse_obj_command_output(output);
  model->core_analyzer_instances = std::move(parsed.instances);
  model->core_analyzer_selected_instance = model->core_analyzer_instances.empty() ? -1 : 0;
  if (!type_query.empty()) {
    for (auto& instance : model->core_analyzer_instances) {
      if (instance.type_name.empty()) {
        instance.type_name = type_query;
      }
    }
  }
}

void add_watch_for_core_instance(const CoreAnalyzerInstance& instance,
                                 const std::string& type_query, DebugModel* model,
                                 CommandCallback on_command) {
  if (model == nullptr || instance.address_hex.empty() || !on_command) {
    return;
  }
  std::string type = type_query;
  if (type.empty()) {
    type = instance.type_name;
  }
  if (type.empty()) {
    type = "void";
  }
  if (type.find('*') == std::string::npos) {
    type += "*";
  }
  const std::string expr = "*(" + type + ")" + instance.address_hex;
  model->add_watch(expr);
  UiCommand command;
  command.kind = UiCommandKind::kAddWatch;
  command.expression = expr;
  assign_watch_frame(&command, model);
  on_command(command);
}

Component MakeCoreAnalyzerPanel(DebugModel* model, CommandCallback on_command,
                                MainLayoutState* layout_state, FocusManagerState* focus) {
  auto state = std::make_shared<CoreAnalyzerPanelState>();

  auto panel = CatchEvent(
      Renderer([model, state, layout_state] {
        int log_visible = visible_lines(state->log_box);
        if (log_visible <= 1 && state->left_column_box.y_max > state->left_column_box.y_min) {
          log_visible = std::max(1, visible_lines(state->left_column_box) - 3);
        }
        state->last_log_visible = std::max(1, log_visible);

        Elements log_lines;
        const int log_total = static_cast<int>(model->core_analyzer_log.size());
        if (state->follow_log_tail) {
          state->log_first_visible = max_first_visible(log_total, log_visible);
        }
        state->log_first_visible =
            std::max(0, std::min(state->log_first_visible, max_first_visible(log_total, log_visible)));
        const int log_end =
            std::min(log_total, state->log_first_visible + log_visible);
        for (int i = state->log_first_visible; i < log_end; ++i) {
          log_lines.push_back(text(model->core_analyzer_log[static_cast<std::size_t>(i)]) |
                             color(theme::Header()));
        }
        if (log_lines.empty()) {
          log_lines.push_back(text(i18n::tr("panel.core_analyzer.commands_hint")) |
                             color(theme::Muted()));
        }

        const int rendered_log_lines = static_cast<int>(log_lines.size());
        Element log_row =
            hbox({vbox(std::move(log_lines)) | flex | reflect(state->log_box) |
                      bgcolor(theme::PanelBg()),
                  vertical_scrollbar(log_total, state->log_first_visible, log_visible,
                                     rendered_log_lines)}) |
            flex | bgcolor(theme::PanelBg());

        const bool command_focused =
            layout_state != nullptr &&
            layout_state->core_analyzer_focus == CoreAnalyzerFocus::kCommand;
        Element cmd_row =
            hbox({
                text("> ") | color(theme::WatchInput()),
                RenderBlinkInputLine(state->command_input,
                                     static_cast<int>(state->command_input.size()),
                                     command_focused),
            }) |
            size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) | reflect(state->command_input_box);

        Element left_column = vbox({
                               log_row | flex,
                               separator() | size(HEIGHT, EQUAL, 1),
                               std::move(cmd_row),
                           }) |
                           flex | border | reflect(state->left_column_box);

        int inst_visible = visible_lines(state->instances_box);
        if (inst_visible <= 1) {
          inst_visible = std::max(1, state->last_instances_visible);
        }
        state->last_instances_visible = std::max(1, inst_visible);

        const int inst_total = static_cast<int>(model->core_analyzer_instances.size());
        if (model->core_analyzer_selected_instance >= 0) {
          clamp_instance_scroll(state.get(), inst_total, inst_visible,
                                model->core_analyzer_selected_instance);
        }
        state->instances_first_visible = std::max(
            0, std::min(state->instances_first_visible, max_first_visible(inst_total, inst_visible)));
        const int inst_end =
            std::min(inst_total, state->instances_first_visible + inst_visible);

        Elements instance_lines;
        for (int i = state->instances_first_visible; i < inst_end; ++i) {
          const auto& inst = model->core_analyzer_instances[static_cast<std::size_t>(i)];
          const bool selected = i == model->core_analyzer_selected_instance;
          std::ostringstream line;
          line << inst.address_hex;
          if (inst.size > 0) {
            line << "  " << inst.size << " B";
          }
          if (!inst.reference_summary.empty()) {
            line << "  " << inst.reference_summary;
          }
          const bool hovered =
              layout_state != nullptr &&
              layout_state->clickable.is_hovered(press_id::core_analyzer_instance(i));
          instance_lines.push_back(
              StyleListRow(text(line.str()) | color(theme::Header()), selected, hovered, false));
        }
        if (model->core_analyzer_instances.empty()) {
          instance_lines.push_back(text(i18n::tr("common.no_results")) | color(theme::Muted()));
        }

        const int rendered_instance_lines = static_cast<int>(instance_lines.size());
        Element instances_row =
            hbox({vbox(std::move(instance_lines)) | flex | reflect(state->instances_box) |
                      bgcolor(theme::PanelBg()),
                  vertical_scrollbar(inst_total, state->instances_first_visible, inst_visible,
                                     rendered_instance_lines)}) |
            flex | bgcolor(theme::PanelBg());

        const bool search_focused =
            layout_state != nullptr &&
            layout_state->core_analyzer_focus == CoreAnalyzerFocus::kSearch;
        Element right_column =
            vbox({
                text(i18n::tr("panel.core_analyzer.search_class")) | bold | color(theme::Accent()) |
                    size(HEIGHT, EQUAL, 1),
                RenderBlinkInputLine(state->search_input,
                                     static_cast<int>(state->search_input.size()), search_focused) |
                    size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
                    reflect(state->search_input_box),
                separator() | size(HEIGHT, EQUAL, 1),
                instances_row | flex,
                separator() | size(HEIGHT, EQUAL, 1),
                text(i18n::tr("panel.core_analyzer.footer")) | color(theme::Muted()) |
                    size(HEIGHT, EQUAL, 1),
            }) |
            size(WIDTH, EQUAL, 36) | flex | border;

        return hbox({
            std::move(left_column),
            separator(),
            std::move(right_column),
        }) | flex;
      }),
      [model, state, layout_state, focus, on_command](Event event) {
        if (!core_analyzer_panel_active(layout_state, focus)) {
          return false;
        }
        if (event.is_mouse()) {
          return handle_core_analyzer_mouse(event, model, state.get(), layout_state, focus,
                                            on_command);
        }
        if (handle_core_analyzer_keyboard(event, model, state.get(), layout_state, on_command)) {
          if (layout_state != nullptr) {
            layout_state->request_ui_tick = true;
          }
          return true;
        }
        return false;
      });

  if (layout_state != nullptr) {
    layout_state->core_analyzer_key_handler =
        [panel, layout_state, focus](Event event) {
          if (!core_analyzer_tab_active(layout_state)) {
            return false;
          }
          if (!core_analyzer_panel_active(layout_state, focus)) {
            return false;
          }
          return panel->OnEvent(event);
        };
  }

  return panel;
}

}  // namespace tgdb
