#include "ui/connection_wizard.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

std::string step_title(WizardStep step) {
  switch (step) {
    case WizardStep::ChooseMode:
      return "Conectar depurador";
    case WizardStep::PickBinary:
      return "Elegir ejecutable";
    case WizardStep::PickProcess:
      return "Elegir proceso (PID)";
  }
  return "Conectar";
}

std::string mode_label(WizardMode mode) {
  return mode == WizardMode::Launch ? "Launch" : "Attach";
}

}  // namespace

void ConnectionWizardState::reset() {
  step = WizardStep::ChooseMode;
  mode = WizardMode::Attach;
  mode_selected = 0;
  browser.reset(workspace_root.empty() ? browser.launch_root : workspace_root);
  selected_program.clear();
  process_query.clear();
  all_processes.clear();
  process_matches.clear();
  process_selected = 0;
}

void ConnectionWizardState::refresh_process_matches() {
  if (all_processes.empty()) {
    all_processes = list_processes();
  }
  process_matches = filter_processes(all_processes, process_query);
  if (process_selected >= static_cast<int>(process_matches.size())) {
    process_selected = std::max(0, static_cast<int>(process_matches.size()) - 1);
  }
}

Component MakeConnectionWizardOverlay(Component main, ConnectionWizardState* state,
                                    DebugModel* model,
                                    ConnectionCompleteCallback on_complete,
                                    std::function<void()> on_request_quit) {
  return Renderer(
      CatchEvent(main, [state, model, on_complete, on_request_quit](Event event) {
        if (!state->open) {
          return false;
        }

        auto finish = [&](const ConnectionResult& result) {
          state->open = false;
          if (on_complete) {
            on_complete(result);
          }
        };

        auto go_back = [&]() {
          switch (state->step) {
            case WizardStep::ChooseMode:
              state->open = false;
              break;
            case WizardStep::PickBinary:
              state->step = WizardStep::ChooseMode;
              break;
            case WizardStep::PickProcess:
              state->step = WizardStep::PickBinary;
              if (!state->selected_program.empty()) {
                state->browser.browser_path =
                    fs::path(state->selected_program).parent_path().string();
              } else {
                state->browser.browser_path =
                    canonical_browser_root(state->browser.launch_root);
              }
              state->browser.browser_loaded_path.clear();
              state->browser.reload_browser_entries(true);
              break;
          }
        };

        if (event == Event::Escape) {
          go_back();
          return true;
        }

        if (state->step == WizardStep::ChooseMode) {
          if (event == Event::Character('q')) {
            state->open = false;
            return true;
          }
          if (event == Event::Character('1')) {
            state->mode = WizardMode::Launch;
            state->mode_selected = 0;
          }
          if (event == Event::Character('2')) {
            state->mode = WizardMode::Attach;
            state->mode_selected = 1;
          }
          if (event == Event::ArrowDown || event == Event::Character('j')) {
            state->mode_selected = 1;
            state->mode = WizardMode::Attach;
            return true;
          }
          if (event == Event::ArrowUp || event == Event::Character('k')) {
            state->mode_selected = 0;
            state->mode = WizardMode::Launch;
            return true;
          }
          if (event == Event::Return) {
            state->mode =
                state->mode_selected == 0 ? WizardMode::Launch : WizardMode::Attach;
            state->step = WizardStep::PickBinary;
            state->browser.browser_path =
                canonical_browser_root(state->workspace_root.empty()
                                           ? state->browser.launch_root
                                           : state->workspace_root);
            state->browser.reload_browser_entries(true);
            return true;
          }
          return true;
        }

        if (state->step == WizardStep::PickBinary) {
          state->browser.ensure_browser_entries();

          if (event == Event::ArrowDown || event == Event::Character('j')) {
            state->browser.selected = std::min(
                state->browser.selected + 1,
                std::max(0, static_cast<int>(state->browser.entries.size()) - 1));
            return true;
          }
          if (event == Event::ArrowUp || event == Event::Character('k')) {
            state->browser.selected = std::max(0, state->browser.selected - 1);
            return true;
          }
          if (event == Event::PageDown) {
            state->browser.selected = std::min(
                state->browser.selected + 12,
                std::max(0, static_cast<int>(state->browser.entries.size()) - 1));
            return true;
          }
          if (event == Event::PageUp) {
            state->browser.selected = std::max(0, state->browser.selected - 12);
            return true;
          }
          if (event == Event::Return) {
            if (state->browser.entries.empty()) {
              return true;
            }
            const auto& row = state->browser.entries[state->browser.selected];
            if (row.is_directory) {
              state->browser.browser_path = row.path;
              state->browser.reload_browser_entries(true);
              return true;
            }
            if (is_regular_file_path(row.path)) {
              state->selected_program = row.path;
              if (state->mode == WizardMode::Launch) {
                ConnectionResult result;
                result.mode = SessionMode::kLaunch;
                result.program = state->selected_program;
                result.workspace_root = state->workspace_root;
                finish(result);
              } else {
                state->step = WizardStep::PickProcess;
                state->all_processes.clear();
                state->process_query.clear();
                state->process_selected = 0;
                state->refresh_process_matches();
              }
            }
            return true;
          }
          if (event.is_mouse() && event.mouse().button == Mouse::Left &&
              event.mouse().motion == Mouse::Pressed) {
            const auto& m = event.mouse();
            if (state->browser.browser_list_box.Contain(m.x, m.y)) {
              const int row = state->browser.browser_list_start +
                              (m.y - state->browser.browser_list_box.y_min);
              if (row >= 0 && row < static_cast<int>(state->browser.entries.size())) {
                state->browser.selected = row;
                const auto& entry = state->browser.entries[row];
                if (entry.is_directory) {
                  state->browser.browser_path = entry.path;
                  state->browser.reload_browser_entries(true);
                } else if (is_regular_file_path(entry.path)) {
                  state->selected_program = entry.path;
                  if (state->mode == WizardMode::Launch) {
                    ConnectionResult result;
                    result.mode = SessionMode::kLaunch;
                    result.program = state->selected_program;
                    result.workspace_root = state->workspace_root;
                    finish(result);
                  } else {
                    state->step = WizardStep::PickProcess;
                    state->all_processes.clear();
                    state->process_query.clear();
                    state->process_selected = 0;
                    state->refresh_process_matches();
                  }
                }
              }
              return true;
            }
          }
          return true;
        }

        if (state->step == WizardStep::PickProcess) {
          state->refresh_process_matches();

          if (event == Event::ArrowDown || event == Event::Character('j')) {
            if (!state->process_matches.empty()) {
              state->process_selected = std::min(
                  state->process_selected + 1,
                  static_cast<int>(state->process_matches.size()) - 1);
            }
            return true;
          }
          if (event == Event::ArrowUp || event == Event::Character('k')) {
            state->process_selected = std::max(0, state->process_selected - 1);
            return true;
          }
          if (event == Event::Return) {
            if (state->process_matches.empty()) {
              return true;
            }
            const auto& proc = state->process_matches[state->process_selected];
            ConnectionResult result;
            result.mode = SessionMode::kAttach;
            result.program = state->selected_program;
            result.workspace_root = state->workspace_root;
            result.attach_pid = proc.pid;
            finish(result);
            return true;
          }
          if (event == Event::Backspace) {
            if (!state->process_query.empty()) {
              state->process_query.pop_back();
              state->process_selected = 0;
              state->refresh_process_matches();
            }
            return true;
          }
          if (event.is_character()) {
            const std::string ch = event.character();
            if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
                static_cast<unsigned char>(ch[0]) < 127) {
              state->process_query += ch;
              state->process_selected = 0;
              state->refresh_process_matches();
            }
            return true;
          }
          return true;
        }

        return true;
      }),
      [main, state, model] {
        Element base = main->Render();
        if (!state->open) {
          return base;
        }

        Elements body;
        std::string help;

        if (state->step == WizardStep::ChooseMode) {
          const bool launch_sel = state->mode_selected == 0;
          Element launch_row =
              text(" 1  Launch — lanzar ejecutable") | color(theme::Header());
          Element attach_row =
              text(" 2  Attach — adjuntar a proceso en ejecución") |
              color(theme::Header());
          if (launch_sel) {
            launch_row = launch_row | inverted | bold;
          } else {
            attach_row = attach_row | inverted | bold;
          }
          body = {launch_row, attach_row};
          help = "1/2 o j/k  Enter  Esc cancelar";
        } else if (state->step == WizardStep::PickBinary) {
          state->browser.ensure_browser_entries();
          body.push_back(text("workspace: " + state->workspace_root) |
                         color(theme::Muted()));
          body.push_back(text(state->browser.browser_path) | color(theme::Muted()));
          body.push_back(separator());
          const int max_rows = 12;
          state->browser.browser_list_start = std::max(
              0, std::min(state->browser.selected,
                          std::max(0, static_cast<int>(state->browser.entries.size()) -
                                            max_rows)));
          const int start = state->browser.browser_list_start;
          const int end =
              std::min(static_cast<int>(state->browser.entries.size()), start + max_rows);
          Elements list_rows;
          for (int i = start; i < end; ++i) {
            const auto& row = state->browser.entries[i];
            std::string prefix = row.is_directory ? "[dir] " : "      ";
            Element line = text(prefix + row.name);
            if (row.is_directory) {
              line = line | color(theme::Accent());
            }
            if (i == state->browser.selected) {
              line = line | inverted | bold;
            }
            list_rows.push_back(line);
          }
          if (list_rows.empty()) {
            list_rows.push_back(text("(vacío)") | color(theme::Muted()));
          }
          body.push_back(vbox(std::move(list_rows)) |
                         reflect(state->browser.browser_list_box));
          help = "j/k  Enter ejecutable  clic  Esc atrás";
        } else if (state->step == WizardStep::PickProcess) {
          state->refresh_process_matches();
          body.push_back(text("workspace: " + state->workspace_root) |
                         color(theme::Muted()));
          if (!state->selected_program.empty()) {
            body.push_back(text("binario: " + state->selected_program) |
                           color(theme::Muted()));
          }
          std::string query_line = state->process_query;
          query_line.push_back('_');
          body.push_back(text("buscar: " + query_line) | color(theme::WatchInput()));
          body.push_back(separator());
          const int max_rows = 10;
          const int start = std::max(
              0, std::min(state->process_selected,
                          std::max(0, static_cast<int>(state->process_matches.size()) -
                                            max_rows)));
          const int end = std::min(static_cast<int>(state->process_matches.size()),
                                   start + max_rows);
          for (int i = start; i < end; ++i) {
            const auto& proc = state->process_matches[i];
            std::string line = std::to_string(proc.pid) + "  " + proc.name;
            if (!proc.cmdline.empty() && proc.cmdline != proc.name) {
              line += "  — " + proc.cmdline;
              if (line.size() > 72) {
                line.resize(69);
                line += "...";
              }
            }
            Element row_el = text(line) | color(theme::Header());
            if (i == state->process_selected) {
              row_el = row_el | inverted | bold;
            }
            body.push_back(row_el);
          }
          if (state->process_matches.empty()) {
            body.push_back(text("(sin coincidencias)") | color(theme::Muted()));
          }
          help = "escribe para filtrar  j/k  Enter  Esc atrás";
        }

        Element dialog = window(
            text(step_title(state->step) + "  [" + mode_label(state->mode) + "]") |
                color(theme::Accent()),
            vbox({
                vbox(std::move(body)) | flex | bgcolor(theme::PanelBg()),
                separator(),
                text(help) | color(theme::Muted()),
            }))
            | size(WIDTH, GREATER_THAN, 60)
            | size(HEIGHT, GREATER_THAN, 10)
            | bgcolor(theme::PanelBg());

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
