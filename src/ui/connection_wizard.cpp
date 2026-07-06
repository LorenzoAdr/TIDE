#include "ui/binary_symbols_panel.hpp"
#include "ui/connection_wizard.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"
#include "util/core_analyzer_support.hpp"
#include "util/nm_reader.hpp"
#include "util/path_normalize.hpp"
#include "util/shell_args.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

std::string step_title(WizardStep step, WizardMode mode) {
  switch (step) {
    case WizardStep::ChooseMode:
      return i18n::tr("wizard.connection.title");
    case WizardStep::PickBinary:
      return mode == WizardMode::AnalyzeSymbols
                 ? i18n::tr("wizard.connection.title.pick_binary_nm")
                 : i18n::tr("wizard.connection.title.pick_binary");
    case WizardStep::PickArgs:
      return i18n::tr("wizard.connection.title.pick_args");
    case WizardStep::PickProcess:
      return i18n::tr("wizard.connection.title.pick_process");
    case WizardStep::PickCoreFile:
      return i18n::tr("wizard.connection.title.pick_core");
    case WizardStep::ChooseCoreBackend:
      return i18n::tr("wizard.connection.title.core_backend");
  }
  return i18n::tr("wizard.connection.title.default");
}

std::string mode_label(WizardMode mode) {
  switch (mode) {
    case WizardMode::Launch:
      return i18n::tr("wizard.connection.mode.launch");
    case WizardMode::Attach:
      return i18n::tr("wizard.connection.mode.attach");
    case WizardMode::LoadCore:
      return i18n::tr("wizard.connection.mode.core");
    case WizardMode::AnalyzeSymbols:
      return i18n::tr("wizard.connection.mode.symbols");
  }
  return i18n::tr("wizard.connection.mode.debug");
}

bool update_f2_mode_hover(ConnectionWizardState* state, MainLayoutState* layout_state, int x,
                          int y) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  if (state->launch_mode_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::f2_mode(0));
  } else if (state->attach_mode_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::f2_mode(1));
  } else if (state->core_mode_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::f2_mode(2));
  } else if (state->symbols_mode_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::f2_mode(3));
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_f2_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    layout_state->request_ui_tick = true;
    return true;
  }
  return false;
}

bool update_f2_browser_hover(ConnectionWizardState* state, MainLayoutState* layout_state, int x,
                             int y) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const auto local = local_row_in_box(state->browser.browser_list_box, x, y);
  if (local.has_value()) {
    const int row = state->browser.browser_list_start + *local;
    if (row >= 0 && row < static_cast<int>(state->browser.entries.size())) {
      layout_state->clickable.set_hover(press_id::f2_browser_row(row));
    } else {
      layout_state->clickable.clear_hover_if(press_id::is_f2_hover);
    }
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_f2_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    layout_state->request_ui_tick = true;
    return true;
  }
  return false;
}

bool update_f2_process_hover(ConnectionWizardState* state, MainLayoutState* layout_state, int x,
                             int y) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const auto local = local_row_in_box(state->process_list_box, x, y);
  if (local.has_value()) {
    const int row = state->process_list_start + *local;
    if (row >= 0 && row < static_cast<int>(state->process_matches.size())) {
      layout_state->clickable.set_hover(press_id::f2_process_row(row));
    } else {
      layout_state->clickable.clear_hover_if(press_id::is_f2_hover);
    }
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_f2_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    layout_state->request_ui_tick = true;
    return true;
  }
  return false;
}

void refresh_args_completion(ConnectionWizardState* state);

void begin_pick_args(ConnectionWizardState* state) {
  if (state == nullptr || state->selected_program.empty()) {
    return;
  }
  std::error_code ec;
  state->launch_cwd =
      fs::weakly_canonical(fs::path(state->selected_program).parent_path(), ec).string();
  if (ec || state->launch_cwd.empty()) {
    state->launch_cwd = fs::path(state->selected_program).parent_path().string();
  }
  const std::string program_key = normalize_path(state->selected_program);
  const auto saved = state->launch_args_by_program.find(program_key);
  state->args_line = saved != state->launch_args_by_program.end() ? saved->second : "";
  state->args_completion_matches.clear();
  state->step = WizardStep::PickArgs;
  refresh_args_completion(state);
}

void refresh_args_completion(ConnectionWizardState* state) {
  if (state == nullptr) {
    return;
  }
  state->args_completion_matches =
      path_completions(state->launch_cwd, last_shell_token(state->args_line));
}

WizardMode mode_from_selected(int selected) {
  switch (selected) {
    case 0:
      return WizardMode::Launch;
    case 1:
      return WizardMode::Attach;
    case 2:
      return WizardMode::LoadCore;
    default:
      return WizardMode::AnalyzeSymbols;
  }
}

void activate_browser_row(ConnectionWizardState* state, MainLayoutState* layout_state, int row,
                          const std::function<void(const ConnectionResult&)>& finish) {
  if (row < 0 || row >= static_cast<int>(state->browser.entries.size())) {
    return;
  }
  trigger_press(layout_state, press_id::f2_browser_row(row));
  state->browser.selected = row;
  const auto& entry = state->browser.entries[static_cast<std::size_t>(row)];
  if (entry.is_directory) {
    state->browser.browser_path = entry.path;
    state->browser.reload_browser_entries(true);
    return;
  }
  if (!is_regular_file_path(entry.path)) {
    return;
  }
  if (state->mode == WizardMode::AnalyzeSymbols) {
    if (!is_nm_analyzable_path(entry.path)) {
      return;
    }
    state->open = false;
    request_binary_symbols_panel(layout_state, entry.path);
    return;
  }
  state->selected_program = entry.path;
  if (state->mode == WizardMode::Launch) {
    begin_pick_args(state);
  } else if (state->mode == WizardMode::LoadCore) {
    state->step = WizardStep::PickCoreFile;
    state->browser.browser_path =
        fs::path(entry.path).parent_path().string();
    state->browser.reload_browser_entries(true);
  } else {
    state->step = WizardStep::PickProcess;
    state->all_processes.clear();
    state->process_query.clear();
    state->process_selected = 0;
    state->refresh_process_matches();
  }
}

}  // namespace

void ConnectionWizardState::reset() {
  step = WizardStep::ChooseMode;
  mode = WizardMode::Attach;
  mode_selected = 0;
  browser.reset(workspace_root.empty() ? browser.launch_root : workspace_root);
  selected_program.clear();
  selected_core_path.clear();
  core_backend_selected = 0;
  launch_cwd.clear();
  args_line.clear();
  args_completion_matches.clear();
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
                                    DebugModel* model, MainLayoutState* layout_state,
                                    ConnectionCompleteCallback on_complete,
                                    std::function<void()> on_request_quit) {
  return Renderer(
      CatchEvent(main, [state, model, layout_state, on_complete, on_request_quit](Event event) {
        if (!state->open) {
          return false;
        }

        auto finish = [&](const ConnectionResult& result) {
          state->open = false;
          if (on_complete) {
            on_complete(result);
          }
        };

        auto finish_core_load = [&](CoreAnalysisMode analysis) {
          ConnectionResult result;
          result.mode = SessionMode::kCore;
          result.program = state->selected_program;
          result.workspace_root = state->workspace_root;
          result.core_path = state->selected_core_path;
          result.core_analysis = analysis;
          finish(result);
        };

        auto after_core_file_picked = [&]() {
          if (core_analyzer_supported()) {
            state->step = WizardStep::ChooseCoreBackend;
            state->core_backend_selected = 0;
          } else {
            finish_core_load(CoreAnalysisMode::kGdbOnly);
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
            case WizardStep::PickArgs:
              state->step = WizardStep::PickBinary;
              state->args_line.clear();
              state->args_completion_matches.clear();
              if (!state->selected_program.empty()) {
                state->browser.browser_path =
                    fs::path(state->selected_program).parent_path().string();
              }
              state->browser.browser_loaded_path.clear();
              state->browser.reload_browser_entries(true);
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
            case WizardStep::PickCoreFile:
              state->step = WizardStep::PickBinary;
              state->selected_core_path.clear();
              if (!state->selected_program.empty()) {
                state->browser.browser_path =
                    fs::path(state->selected_program).parent_path().string();
              }
              state->browser.browser_loaded_path.clear();
              state->browser.reload_browser_entries(true);
              break;
            case WizardStep::ChooseCoreBackend:
              state->step = WizardStep::PickCoreFile;
              break;
          }
        };

        if (event == Event::Escape) {
          go_back();
          return true;
        }

        if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
          if (state->step == WizardStep::ChooseMode) {
            update_f2_mode_hover(state, layout_state, event.mouse().x, event.mouse().y);
          } else if (state->step == WizardStep::PickBinary) {
            update_f2_browser_hover(state, layout_state, event.mouse().x, event.mouse().y);
          } else if (state->step == WizardStep::PickProcess) {
            update_f2_process_hover(state, layout_state, event.mouse().x, event.mouse().y);
          }
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
          if (event == Event::Character('3')) {
            state->mode = WizardMode::LoadCore;
            state->mode_selected = 2;
          }
          if (event == Event::Character('4')) {
            state->mode = WizardMode::AnalyzeSymbols;
            state->mode_selected = 3;
          }
          if (event == Event::ArrowDown || event == Event::Character('j')) {
            state->mode_selected = std::min(3, state->mode_selected + 1);
            state->mode = mode_from_selected(state->mode_selected);
            return true;
          }
          if (event == Event::ArrowUp || event == Event::Character('k')) {
            state->mode_selected = std::max(0, state->mode_selected - 1);
            state->mode = mode_from_selected(state->mode_selected);
            return true;
          }
          if (event == Event::Return) {
            trigger_press(layout_state, press_id::f2_mode(state->mode_selected));
            state->mode = mode_from_selected(state->mode_selected);
            state->step = WizardStep::PickBinary;
            state->browser.browser_path =
                canonical_browser_root(state->workspace_root.empty()
                                           ? state->browser.launch_root
                                           : state->workspace_root);
            state->browser.reload_browser_entries(true);
            return true;
          }
          if (event.is_mouse() && event.mouse().button == Mouse::Left &&
              event.mouse().motion == Mouse::Pressed) {
            const auto& m = event.mouse();
            if (state->launch_mode_box.Contain(m.x, m.y)) {
              state->mode_selected = 0;
              state->mode = WizardMode::Launch;
              trigger_press(layout_state, press_id::f2_mode(0));
              return true;
            }
            if (state->attach_mode_box.Contain(m.x, m.y)) {
              state->mode_selected = 1;
              state->mode = WizardMode::Attach;
              trigger_press(layout_state, press_id::f2_mode(1));
              return true;
            }
            if (state->core_mode_box.Contain(m.x, m.y)) {
              state->mode_selected = 2;
              state->mode = WizardMode::LoadCore;
              trigger_press(layout_state, press_id::f2_mode(2));
              return true;
            }
            if (state->symbols_mode_box.Contain(m.x, m.y)) {
              state->mode_selected = 3;
              state->mode = WizardMode::AnalyzeSymbols;
              trigger_press(layout_state, press_id::f2_mode(3));
              return true;
            }
          }
          return true;
        }

        if (state->step == WizardStep::PickCoreFile) {
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
          if (event == Event::Return) {
            if (state->browser.entries.empty()) {
              return true;
            }
            const auto& entry =
                state->browser.entries[static_cast<std::size_t>(state->browser.selected)];
            if (entry.is_directory || !is_regular_file_path(entry.path)) {
              return true;
            }
            state->selected_core_path = entry.path;
            after_core_file_picked();
            return true;
          }
          if (event.is_mouse() && event.mouse().button == Mouse::Left &&
              event.mouse().motion == Mouse::Pressed) {
            const auto& m = event.mouse();
            if (state->browser.browser_list_box.Contain(m.x, m.y)) {
              const int row = state->browser.browser_list_start +
                              (m.y - state->browser.browser_list_box.y_min);
              if (row >= 0 && row < static_cast<int>(state->browser.entries.size())) {
                const auto& entry = state->browser.entries[static_cast<std::size_t>(row)];
                if (!entry.is_directory && is_regular_file_path(entry.path)) {
                  state->selected_core_path = entry.path;
                  after_core_file_picked();
                }
              }
              return true;
            }
          }
          return true;
        }

        if (state->step == WizardStep::ChooseCoreBackend) {
          if (event == Event::Character('1')) {
            state->core_backend_selected = 0;
          }
          if (event == Event::Character('2') && core_analyzer_supported()) {
            state->core_backend_selected = 1;
          }
          if (event == Event::ArrowDown || event == Event::Character('j')) {
            if (core_analyzer_supported()) {
              state->core_backend_selected = 1;
            }
            return true;
          }
          if (event == Event::ArrowUp || event == Event::Character('k')) {
            state->core_backend_selected = 0;
            return true;
          }
          if (event == Event::Return) {
            const CoreAnalysisMode analysis =
                state->core_backend_selected == 1 && core_analyzer_supported()
                    ? CoreAnalysisMode::kCoreAnalyzer
                    : CoreAnalysisMode::kGdbOnly;
            finish_core_load(analysis);
            return true;
          }
          if (event.is_mouse() && event.mouse().button == Mouse::Left &&
              event.mouse().motion == Mouse::Pressed) {
            const auto& m = event.mouse();
            if (state->gdb_backend_box.Contain(m.x, m.y)) {
              state->core_backend_selected = 0;
              return true;
            }
            if (core_analyzer_supported() && state->ca_backend_box.Contain(m.x, m.y)) {
              state->core_backend_selected = 1;
              return true;
            }
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
            activate_browser_row(state, layout_state, state->browser.selected, finish);
            return true;
          }
          if (event.is_mouse() && event.mouse().button == Mouse::Left &&
              event.mouse().motion == Mouse::Pressed) {
            const auto& m = event.mouse();
            if (state->browser.browser_list_box.Contain(m.x, m.y)) {
              const int row = state->browser.browser_list_start +
                              (m.y - state->browser.browser_list_box.y_min);
              activate_browser_row(state, layout_state, row, finish);
              return true;
            }
          }
          return true;
        }

        if (state->step == WizardStep::PickArgs) {
          if (event == Event::Return) {
            ConnectionResult result;
            result.mode = SessionMode::kLaunch;
            result.program = state->selected_program;
            result.workspace_root = state->workspace_root;
            result.args_line = state->args_line;
            result.args = split_shell_args(state->args_line);
            result.packet_monitor_enabled = state->packet_monitor_enabled;
            result.packet_monitor_filter_src = state->packet_monitor_filter_src;
            result.packet_monitor_filter_dst = state->packet_monitor_filter_dst;
            finish(result);
            return true;
          }
          if (event == Event::Character('m')) {
            state->packet_monitor_enabled = !state->packet_monitor_enabled;
            return true;
          }
          if (event == Event::Tab || event == Event::CtrlI) {
            if (apply_path_tab_completion(&state->args_line, state->launch_cwd)) {
              refresh_args_completion(state);
            } else {
              refresh_args_completion(state);
            }
            return true;
          }
          if (event == Event::Backspace) {
            if (!state->args_line.empty()) {
              state->args_line.pop_back();
              refresh_args_completion(state);
            }
            return true;
          }
          if (event.is_character()) {
            const std::string ch = event.character();
            if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
                static_cast<unsigned char>(ch[0]) < 127) {
              state->args_line += ch;
              refresh_args_completion(state);
            }
            return true;
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
            trigger_press(layout_state, press_id::f2_process_row(state->process_selected));
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
          if (event.is_mouse() && event.mouse().button == Mouse::Left &&
              event.mouse().motion == Mouse::Pressed) {
            const auto& m = event.mouse();
            if (state->process_list_box.Contain(m.x, m.y)) {
              const int row =
                  state->process_list_start + (m.y - state->process_list_box.y_min);
              if (row >= 0 && row < static_cast<int>(state->process_matches.size())) {
                state->process_selected = row;
                trigger_press(layout_state, press_id::f2_process_row(row));
                const auto& proc = state->process_matches[static_cast<std::size_t>(row)];
                ConnectionResult result;
                result.mode = SessionMode::kAttach;
                result.program = state->selected_program;
                result.workspace_root = state->workspace_root;
                result.attach_pid = proc.pid;
                finish(result);
              }
              return true;
            }
          }
          return true;
        }

        return true;
      }),
      [main, state, model, layout_state] {
        Element base = main->Render();
        if (!state->open || binary_symbols_request_pending(layout_state)) {
          return base;
        }

        Elements body;
        std::string help;

        if (state->step == WizardStep::ChooseMode) {
          const bool launch_sel = state->mode_selected == 0;
          const bool launch_hovered =
              layout_state != nullptr && layout_state->clickable.is_hovered(press_id::f2_mode(0));
          const bool attach_hovered =
              layout_state != nullptr && layout_state->clickable.is_hovered(press_id::f2_mode(1));
          const bool core_hovered =
              layout_state != nullptr && layout_state->clickable.is_hovered(press_id::f2_mode(2));
          const bool symbols_hovered =
              layout_state != nullptr && layout_state->clickable.is_hovered(press_id::f2_mode(3));
          const bool launch_pressed =
              layout_state != nullptr && layout_state->clickable.is_pressed(press_id::f2_mode(0));
          const bool attach_pressed =
              layout_state != nullptr && layout_state->clickable.is_pressed(press_id::f2_mode(1));
          const bool core_pressed =
              layout_state != nullptr && layout_state->clickable.is_pressed(press_id::f2_mode(2));
          const bool symbols_pressed =
              layout_state != nullptr && layout_state->clickable.is_pressed(press_id::f2_mode(3));
          Element launch_row = StyleListRow(
              text(i18n::tr("wizard.connection.option.launch")) | color(theme::Header()), launch_sel,
              launch_hovered, launch_pressed);
          Element attach_row = StyleListRow(
              text(i18n::tr("wizard.connection.option.attach")) | color(theme::Header()),
              state->mode_selected == 1, attach_hovered, attach_pressed);
          const std::string core_label =
              core_analyzer_supported()
                  ? i18n::tr("wizard.connection.option.core")
                  : i18n::tr("wizard.connection.option.core_gdb_only");
          Element core_row = StyleListRow(
              text(core_label) | color(theme::Header()),
              state->mode_selected == 2, core_hovered, core_pressed);
          Element symbols_row = StyleListRow(
              text(i18n::tr("wizard.connection.option.symbols")) | color(theme::Header()),
              state->mode_selected == 3, symbols_hovered, symbols_pressed);
          body = {launch_row | reflect(state->launch_mode_box),
                  attach_row | reflect(state->attach_mode_box),
                  core_row | reflect(state->core_mode_box),
                  symbols_row | reflect(state->symbols_mode_box)};
          help = i18n::tr("wizard.connection.help.choose_mode");
        } else if (state->step == WizardStep::PickBinary) {
          state->browser.ensure_browser_entries();
          body.push_back(text(i18n::tr_fmt("common.workspace", {state->workspace_root})) |
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
            std::string prefix = row.is_directory ? i18n::tr("common.browser.dir_prefix")
                                                  : i18n::tr("common.browser.file_indent");
            const std::string row_id = press_id::f2_browser_row(i);
            const bool selected = i == state->browser.selected;
            const bool hovered =
                layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
            const bool pressed =
                layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
            Element line = text(prefix + row.name);
            if (row.is_directory) {
              line = line | color(theme::Accent());
            }
            line = StyleListRow(std::move(line), selected, hovered, pressed);
            list_rows.push_back(line);
          }
          if (list_rows.empty()) {
            list_rows.push_back(text(i18n::tr("common.empty")) | color(theme::Muted()));
          }
          body.push_back(vbox(std::move(list_rows)) |
                         reflect(state->browser.browser_list_box));
          help = state->mode == WizardMode::AnalyzeSymbols
                     ? i18n::tr("wizard.connection.help.pick_binary_nm")
                     : i18n::tr("wizard.connection.help.pick_binary");
        } else if (state->step == WizardStep::PickArgs) {
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.executable",
                                           {state->selected_program})) |
                         color(theme::Muted()));
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.cwd", {state->launch_cwd})) |
                         color(theme::Muted()));
          std::string query_line = state->args_line;
          query_line.push_back('_');
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.args", {query_line})) |
                         color(theme::WatchInput()));
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.packet_monitor",
                                           {state->packet_monitor_enabled
                                                ? i18n::tr("common.yes")
                                                : i18n::tr("common.no"),
                                            state->packet_monitor_filter_src.empty()
                                                ? "*"
                                                : state->packet_monitor_filter_src,
                                            state->packet_monitor_filter_dst.empty()
                                                ? "*"
                                                : state->packet_monitor_filter_dst})) |
                         color(theme::Muted()));
          if (!state->args_completion_matches.empty()) {
            body.push_back(separator());
            const int max_rows = 6;
            const int shown = std::min(max_rows, static_cast<int>(state->args_completion_matches.size()));
            for (int i = 0; i < shown; ++i) {
              body.push_back(text("  " + state->args_completion_matches[static_cast<std::size_t>(i)]) |
                             color(theme::Muted()));
            }
            if (static_cast<int>(state->args_completion_matches.size()) > max_rows) {
              body.push_back(text(i18n::tr("common.ellipsis")) | color(theme::Muted()));
            }
          }
          help = i18n::tr("wizard.connection.help.pick_args");
        } else if (state->step == WizardStep::PickProcess) {
          state->refresh_process_matches();
          body.push_back(text(i18n::tr_fmt("common.workspace", {state->workspace_root})) |
                         color(theme::Muted()));
          if (!state->selected_program.empty()) {
            body.push_back(text(i18n::tr_fmt("wizard.connection.label.binary",
                                             {state->selected_program})) |
                           color(theme::Muted()));
          }
          std::string query_line = state->process_query;
          query_line.push_back('_');
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.search", {query_line})) |
                         color(theme::WatchInput()));
          body.push_back(separator());
          const int max_rows = 10;
          state->process_list_start = std::max(
              0, std::min(state->process_selected,
                          std::max(0, static_cast<int>(state->process_matches.size()) -
                                            max_rows)));
          const int start = state->process_list_start;
          const int end = std::min(static_cast<int>(state->process_matches.size()),
                                   start + max_rows);
          Elements process_rows;
          for (int i = start; i < end; ++i) {
            const auto& proc = state->process_matches[static_cast<std::size_t>(i)];
            std::string line = i18n::tr_fmt("wizard.connection.process.row",
                                            {std::to_string(proc.pid), proc.name});
            if (!proc.cmdline.empty() && proc.cmdline != proc.name) {
              line += i18n::tr_fmt("wizard.connection.process.cmdline", {proc.cmdline});
              if (line.size() > 72) {
                line.resize(69);
                line += i18n::tr("common.truncation_suffix");
              }
            }
            const std::string row_id = press_id::f2_process_row(i);
            const bool selected = i == state->process_selected;
            const bool hovered =
                layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
            const bool pressed =
                layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
            Element row_el = StyleListRow(text(line) | color(theme::Header()), selected, hovered,
                                          pressed);
            process_rows.push_back(row_el);
          }
          if (process_rows.empty()) {
            process_rows.push_back(text(i18n::tr("common.no_matches")) | color(theme::Muted()));
          }
          body.push_back(vbox(std::move(process_rows)) | reflect(state->process_list_box));
          help = i18n::tr("wizard.connection.help.pick_process");
        } else if (state->step == WizardStep::PickCoreFile) {
          state->browser.ensure_browser_entries();
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.executable",
                                           {state->selected_program})) |
                         color(theme::Muted()));
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.core",
                                           {state->browser.browser_path})) |
                         color(theme::Muted()));
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
            std::string prefix = row.is_directory ? i18n::tr("common.browser.dir_prefix")
                                                  : i18n::tr("common.browser.file_indent");
            const std::string row_id = press_id::f2_browser_row(i);
            const bool selected = i == state->browser.selected;
            const bool hovered =
                layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
            const bool pressed =
                layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
            Element line = text(prefix + row.name);
            if (row.is_directory) {
              line = line | color(theme::Accent());
            }
            line = StyleListRow(std::move(line), selected, hovered, pressed);
            list_rows.push_back(line);
          }
          if (list_rows.empty()) {
            list_rows.push_back(text(i18n::tr("common.empty")) | color(theme::Muted()));
          }
          body.push_back(vbox(std::move(list_rows)) |
                         reflect(state->browser.browser_list_box));
          help = i18n::tr("wizard.connection.help.pick_core");
        } else if (state->step == WizardStep::ChooseCoreBackend) {
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.executable",
                                           {state->selected_program})) |
                         color(theme::Muted()));
          body.push_back(text(i18n::tr_fmt("wizard.connection.label.core",
                                           {state->selected_core_path})) |
                         color(theme::Muted()));
          body.push_back(separator());
          const bool gdb_sel = state->core_backend_selected == 0;
          Element gdb_row = StyleListRow(
              text(i18n::tr("wizard.connection.option.gdb_backend")) | color(theme::Header()),
              gdb_sel, false, false);
          Element ca_row;
          if (core_analyzer_supported()) {
            ca_row = StyleListRow(
                text(i18n::tr("wizard.connection.option.ca_backend")) | color(theme::Header()),
                !gdb_sel, false, false);
            body = {gdb_row | reflect(state->gdb_backend_box),
                    ca_row | reflect(state->ca_backend_box)};
            help = i18n::tr("wizard.connection.help.core_backend");
          } else {
            body = {gdb_row | reflect(state->gdb_backend_box),
                    text(i18n::tr("wizard.connection.ca_unavailable")) | color(theme::Muted())};
            help = i18n::tr("wizard.connection.help.core_backend_gdb_only");
          }
        }

        Element dialog = window(
            text(step_title(state->step, state->mode) +
                 i18n::tr_fmt("wizard.connection.badge", {mode_label(state->mode)})) |
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
