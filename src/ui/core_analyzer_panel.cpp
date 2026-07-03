#include "ui/core_analyzer_panel.hpp"

#include <algorithm>
#include <sstream>

#include "backend/idebug_backend.hpp"
#include "core_analyzer/output_parser.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/focus_manager.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct CoreAnalyzerPanelState {
  std::string command_input;
  std::string search_input;
  int selected_instance = 0;
  int log_first_visible = 0;
  int instances_first_visible = 0;
  int last_log_visible = 1;
  int last_instances_visible = 1;
  bool follow_log_tail = true;
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

  return CatchEvent(
      Renderer([model, state, layout_state, focus] {
        Elements left_lines;
        const int log_visible = std::max(1, state->last_log_visible);
        const int log_total = static_cast<int>(model->core_analyzer_log.size());
        if (state->follow_log_tail) {
          state->log_first_visible = max_first_visible(log_total, log_visible);
        }
        state->log_first_visible =
            std::max(0, std::min(state->log_first_visible, max_first_visible(log_total, log_visible)));
        const int log_end =
            std::min(log_total, state->log_first_visible + log_visible);
        for (int i = state->log_first_visible; i < log_end; ++i) {
          left_lines.push_back(text(model->core_analyzer_log[static_cast<std::size_t>(i)]) |
                             color(theme::Header()));
        }
        if (left_lines.empty()) {
          left_lines.push_back(text("(comandos: obj, ref, heap, pattern, shrobj)") |
                             color(theme::Muted()));
        }

        std::string cmd_line = state->command_input;
        cmd_line.push_back('_');
        left_lines.push_back(separator());
        left_lines.push_back(text("> " + cmd_line) | color(theme::WatchInput()));

        Elements right_lines;
        right_lines.push_back(text("Buscar clase") | bold | color(theme::Accent()));
        std::string search_line = state->search_input;
        search_line.push_back('_');
        right_lines.push_back(text(search_line) | color(theme::WatchInput()));
        right_lines.push_back(separator());

        const int inst_visible = std::max(1, state->last_instances_visible);
        const int inst_total = static_cast<int>(model->core_analyzer_instances.size());
        state->instances_first_visible = std::max(
            0, std::min(state->instances_first_visible, max_first_visible(inst_total, inst_visible)));
        const int inst_end =
            std::min(inst_total, state->instances_first_visible + inst_visible);
        for (int i = state->instances_first_visible; i < inst_end; ++i) {
          const auto& inst = model->core_analyzer_instances[static_cast<std::size_t>(i)];
          const bool selected = i == state->selected_instance;
          std::ostringstream line;
          line << inst.address_hex;
          if (inst.size > 0) {
            line << "  " << inst.size << " B";
          }
          if (!inst.reference_summary.empty()) {
            line << "  " << inst.reference_summary;
          }
          Element row = text(line.str()) | color(selected ? theme::Accent() : theme::Header());
          if (selected) {
            row = row | bold;
          }
          right_lines.push_back(row);
        }
        if (model->core_analyzer_instances.empty()) {
          right_lines.push_back(text("(sin resultados)") | color(theme::Muted()));
        }
        right_lines.push_back(separator());
        right_lines.push_back(text("Enter: buscar  w: watch  r: ref  j/k: navegar") |
                           color(theme::Muted()));

        return hbox({
            vbox(std::move(left_lines)) | flex | border,
            separator(),
            vbox(std::move(right_lines)) | size(WIDTH, EQUAL, 36) | border,
        });
      }),
      [model, state, layout_state, focus, on_command](Event event) {
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

        const bool search_focus = layout_state->core_analyzer_search_focus;

        if (event == Event::Tab) {
          layout_state->core_analyzer_search_focus = !layout_state->core_analyzer_search_focus;
          return true;
        }

        if (search_focus) {
          if (event == Event::Return) {
            submit_core_analyzer_class_search(state->search_input, model, on_command);
            return true;
          }
          if (event == Event::Backspace) {
            if (!state->search_input.empty()) {
              state->search_input.pop_back();
            }
            return true;
          }
          if (event.is_character()) {
            const std::string ch = event.character();
            if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32) {
              state->search_input += ch;
            }
            return true;
          }
          if (event == Event::ArrowDown || event == Event::Character('j')) {
            if (!model->core_analyzer_instances.empty()) {
              state->selected_instance = std::min(
                  state->selected_instance + 1,
                  static_cast<int>(model->core_analyzer_instances.size()) - 1);
            }
            return true;
          }
          if (event == Event::ArrowUp || event == Event::Character('k')) {
            state->selected_instance = std::max(0, state->selected_instance - 1);
            return true;
          }
          if (event == Event::Character('w') &&
              state->selected_instance >= 0 &&
              state->selected_instance < static_cast<int>(model->core_analyzer_instances.size())) {
            add_watch_for_core_instance(
                model->core_analyzer_instances[static_cast<std::size_t>(state->selected_instance)],
                model->core_analyzer_search_query, model, on_command);
            layout_state->pending_watches_focus = true;
            return true;
          }
          if (event == Event::Character('r') &&
              state->selected_instance >= 0 &&
              state->selected_instance < static_cast<int>(model->core_analyzer_instances.size())) {
            const auto& inst =
                model->core_analyzer_instances[static_cast<std::size_t>(state->selected_instance)];
            submit_core_analyzer_command("ref " + inst.address_hex, model, on_command);
            return true;
          }
          return true;
        }

        if (event == Event::Return) {
          submit_core_analyzer_command(state->command_input, model, on_command);
          state->command_input.clear();
          state->follow_log_tail = true;
          return true;
        }
        if (event == Event::Backspace) {
          if (!state->command_input.empty()) {
            state->command_input.pop_back();
          }
          return true;
        }
        if (event.is_character()) {
          const std::string ch = event.character();
          if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32) {
            state->command_input += ch;
          }
          return true;
        }
        return false;
      });
}

}  // namespace tgdb
