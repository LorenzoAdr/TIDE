#include "ui/connection_wizard.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

std::string canonical_browser_root(const std::string& path) {
  if (path.empty()) {
    std::error_code ec;
    return fs::current_path(ec).string();
  }
  std::error_code ec;
  const auto canonical = fs::weakly_canonical(fs::path(path), ec);
  return ec ? path : canonical.string();
}

std::string step_title(WizardStep step) {
  switch (step) {
    case WizardStep::ChooseMode:
      return "Conectar depurador";
    case WizardStep::PickBinary:
      return "Elegir ejecutable";
    case WizardStep::PickWorkspace:
      return "Elegir directorio de fuentes";
    case WizardStep::PickProcess:
      return "Elegir proceso (PID)";
  }
  return "Conectar";
}

std::string mode_label(WizardMode mode) {
  return mode == WizardMode::Launch ? "Launch" : "Attach";
}

bool is_regular_file_path(const std::string& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

bool is_directory_path(const std::string& path) {
  std::error_code ec;
  return fs::is_directory(path, ec);
}

void activate_browser_row(ConnectionWizardState* state) {
  if (state->entries.empty()) {
    return;
  }
  const auto& row = state->entries[state->selected];
  if (row.is_directory) {
    state->browser_path = row.path;
    state->reload_browser_entries(true);
    return;
  }
  if (state->step == WizardStep::PickBinary && is_regular_file_path(row.path)) {
    state->selected_program = row.path;
    state->step = WizardStep::PickWorkspace;
    state->browser_path = fs::path(row.path).parent_path().string();
    if (state->browser_path.empty()) {
      state->browser_path = canonical_browser_root(state->launch_root);
    }
    state->reload_browser_entries(true);
  }
}

}  // namespace

void ConnectionWizardState::reset() {
  step = WizardStep::ChooseMode;
  mode = WizardMode::Attach;
  mode_selected = 0;
  browser_path = canonical_browser_root(launch_root);
  browser_loaded_path.clear();
  entries.clear();
  selected = 0;
  browser_list_start = 0;
  selected_program.clear();
  selected_workspace.clear();
  process_query.clear();
  all_processes.clear();
  process_matches.clear();
  process_selected = 0;
}

void ConnectionWizardState::reload_browser_entries(bool reset_selection) {
  entries.clear();
  if (reset_selection) {
    selected = 0;
    browser_list_start = 0;
  }
  std::error_code ec;
  fs::path current(browser_path);
  if (!fs::exists(current, ec)) {
    browser_path = canonical_browser_root(launch_root);
    current = fs::path(browser_path);
  }
  browser_path = fs::weakly_canonical(current, ec).string();
  if (ec) {
    browser_path = current.string();
  }
  browser_loaded_path = browser_path;

  if (current.has_parent_path()) {
    BrowserEntry parent;
    parent.name = "..";
    parent.path = current.parent_path().string();
    parent.is_directory = true;
    parent.is_parent = true;
    entries.push_back(std::move(parent));
  }

  std::vector<BrowserEntry> dirs;
  std::vector<BrowserEntry> files;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    BrowserEntry row;
    row.name = entry.path().filename().string();
    if (row.name.empty() || row.name[0] == '.') {
      continue;
    }
    row.path = entry.path().string();
    if (entry.is_directory(ec)) {
      row.is_directory = true;
      dirs.push_back(std::move(row));
    } else if (entry.is_regular_file(ec)) {
      row.is_directory = false;
      files.push_back(std::move(row));
    }
  }

  auto by_name = [](const BrowserEntry& a, const BrowserEntry& b) {
    return a.name < b.name;
  };
  std::sort(dirs.begin(), dirs.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);
  entries.insert(entries.end(), dirs.begin(), dirs.end());
  entries.insert(entries.end(), files.begin(), files.end());

  selected = std::max(
      0, std::min(selected, std::max(0, static_cast<int>(entries.size()) - 1)));
}

void ConnectionWizardState::ensure_browser_entries() {
  if (browser_loaded_path == browser_path && !entries.empty()) {
    return;
  }
  reload_browser_entries(true);
}

void ConnectionWizardState::refresh_process_matches() {
  if (all_processes.empty()) {
    all_processes = list_processes();
  }
  process_matches = filter_processes(all_processes, process_query);
  if (process_selected >= static_cast<int>(process_matches.size())) {
    process_selected =
        std::max(0, static_cast<int>(process_matches.size()) - 1);
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
          on_complete(result);
        };

        auto go_back = [&]() {
          switch (state->step) {
            case WizardStep::ChooseMode:
              if (on_request_quit) {
                on_request_quit();
              }
              break;
            case WizardStep::PickBinary:
              state->step = WizardStep::ChooseMode;
              break;
            case WizardStep::PickWorkspace:
              state->step = WizardStep::PickBinary;
              state->browser_path = canonical_browser_root(state->launch_root);
              state->browser_loaded_path.clear();
              state->reload_browser_entries(true);
              break;
            case WizardStep::PickProcess:
              state->step = WizardStep::PickWorkspace;
              if (!state->selected_program.empty()) {
                state->browser_path =
                    fs::path(state->selected_program).parent_path().string();
              } else {
                state->browser_path = canonical_browser_root(state->launch_root);
              }
              state->browser_loaded_path.clear();
              state->reload_browser_entries(true);
              break;
          }
        };

        if (event == Event::Escape) {
          go_back();
          return true;
        }

        if (state->step == WizardStep::ChooseMode) {
          if (event == Event::Character('q')) {
            if (on_request_quit) {
              on_request_quit();
            }
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
            state->browser_path = canonical_browser_root(state->launch_root);
            state->reload_browser_entries(true);
            return true;
          }
          return true;
        }

        if (state->step == WizardStep::PickBinary ||
            state->step == WizardStep::PickWorkspace) {
          state->ensure_browser_entries();

          if (state->step == WizardStep::PickWorkspace &&
              (event == Event::Character('a') || event == Event::Character('A'))) {
            if (!is_directory_path(state->browser_path)) {
              return true;
            }
            state->selected_workspace = state->browser_path;
            if (state->mode == WizardMode::Launch) {
              ConnectionResult result;
              result.mode = SessionMode::kLaunch;
              result.program = state->selected_program;
              result.workspace_root = state->selected_workspace;
              finish(result);
            } else {
              state->step = WizardStep::PickProcess;
              state->all_processes.clear();
              state->process_query.clear();
              state->process_selected = 0;
              state->refresh_process_matches();
            }
            return true;
          }

          if (event == Event::ArrowDown || event == Event::Character('j')) {
            state->selected = std::min(
                state->selected + 1,
                std::max(0, static_cast<int>(state->entries.size()) - 1));
            return true;
          }
          if (event == Event::ArrowUp || event == Event::Character('k')) {
            state->selected = std::max(0, state->selected - 1);
            return true;
          }
          if (event == Event::PageDown) {
            state->selected = std::min(
                state->selected + 12,
                std::max(0, static_cast<int>(state->entries.size()) - 1));
            return true;
          }
          if (event == Event::PageUp) {
            state->selected = std::max(0, state->selected - 12);
            return true;
          }
          if (event == Event::Return) {
            activate_browser_row(state);
            return true;
          }
          if (event.is_mouse() && event.mouse().button == Mouse::Left &&
              event.mouse().motion == Mouse::Pressed) {
            const auto& m = event.mouse();
            if (state->browser_list_box.Contain(m.x, m.y)) {
              const int row =
                  state->browser_list_start + (m.y - state->browser_list_box.y_min);
              if (row >= 0 && row < static_cast<int>(state->entries.size())) {
                state->selected = row;
                activate_browser_row(state);
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
            const auto& proc =
                state->process_matches[state->process_selected];
            ConnectionResult result;
            result.mode = SessionMode::kAttach;
            result.program = state->selected_program;
            result.workspace_root = state->selected_workspace;
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
            if (ch.size() == 1 &&
                static_cast<unsigned char>(ch[0]) >= 32 &&
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
          help = "1/2 o j/k  Enter  Esc/q salir";
        } else if (state->step == WizardStep::PickBinary ||
                   state->step == WizardStep::PickWorkspace) {
          state->ensure_browser_entries();
          body.push_back(text(state->browser_path) | color(theme::Muted()));
          body.push_back(separator());
          const int max_rows = 12;
          state->browser_list_start = std::max(
              0, std::min(state->selected,
                          std::max(0, static_cast<int>(state->entries.size()) -
                                            max_rows)));
          const int start = state->browser_list_start;
          const int end =
              std::min(static_cast<int>(state->entries.size()), start + max_rows);
          Elements list_rows;
          for (int i = start; i < end; ++i) {
            const auto& row = state->entries[i];
            std::string prefix = row.is_directory ? "[dir] " : "      ";
            Element line = text(prefix + row.name);
            if (row.is_directory) {
              line = line | color(theme::Accent());
            }
            if (i == state->selected) {
              line = line | inverted | bold;
            }
            list_rows.push_back(line);
          }
          if (list_rows.empty()) {
            list_rows.push_back(text("(vacío)") | color(theme::Muted()));
          }
          body.push_back(vbox(std::move(list_rows)) | reflect(state->browser_list_box));
          if (state->step == WizardStep::PickBinary) {
            help = "j/k  Enter archivo  clic  Esc atrás";
          } else {
            help = "j/k  Enter carpeta  a=usar carpeta  clic  Esc atrás";
          }
        } else if (state->step == WizardStep::PickProcess) {
          state->refresh_process_matches();
          std::string query_line = state->process_query;
          query_line.push_back('_');
          body.push_back(text("buscar: " + query_line) |
                         color(theme::WatchInput()));
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

        if (!state->selected_program.empty() &&
            state->step != WizardStep::ChooseMode) {
          body.insert(body.begin(),
                      text("binario: " + state->selected_program) |
                          color(theme::Muted()));
        }
        if (!state->selected_workspace.empty() &&
            state->step == WizardStep::PickProcess) {
          body.insert(body.begin(),
                      text("workspace: " + state->selected_workspace) |
                          color(theme::Muted()));
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

        return dbox({base | bgcolor(theme::PanelBg()) | dim,
                     std::move(dialog) | center});
      });
}

}  // namespace tgdb
