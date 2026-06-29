#include "app/application.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <thread>

#include "backend/idebug_backend.hpp"
#include "dap/gdb_launcher.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ui/context_menu.hpp"
#include "ui/connection_wizard.hpp"
#include "ui/file_picker.hpp"
#include "ui/quit_confirm.hpp"
#include "ui/settings_modal.hpp"
#include "ui/symbol_picker.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/key_bindings.hpp"
#include "ui/main_layout.hpp"
#include "ui/press_ids.hpp"
#include "ui/terminal_keyboard.hpp"
#include "util/crash_handler.hpp"

namespace fs = std::filesystem;

namespace tgdb {

using namespace ftxui;

namespace {

bool event_is_alt_up(const Event& event) {
  return event == Event::Special("\x1B[1;3A");
}

bool event_is_alt_down(const Event& event) {
  return event == Event::Special("\x1B[1;3B");
}

class EventPoller {
 public:
  explicit EventPoller(ScreenInteractive* screen) : screen_(screen) {
    thread_ = std::thread([this] {
      while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (screen_ != nullptr) {
          screen_->Post(Event::Custom);
        }
      }
    });
  }

  ~EventPoller() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  EventPoller(const EventPoller&) = delete;
  EventPoller& operator=(const EventPoller&) = delete;

 private:
  ScreenInteractive* screen_;
  std::atomic<bool> running_{true};
  std::thread thread_;
};

// FTXUI CatchEvent invoca el handler antes que el hijo; RequestUiTickGuard en el
// handler se destruía con request_ui_tick aún en false. Este wrapper postea Custom
// después de que todo el árbol haya procesado el evento.
class UiTickPostWrapper : public ComponentBase {
 public:
  UiTickPostWrapper(MainLayoutState* layout, ScreenInteractive* screen)
      : layout_(layout), screen_(screen) {}

  bool OnEvent(Event event) override {
    const bool handled = ComponentBase::OnEvent(std::move(event));
    post_pending_tick();
    return handled;
  }

 private:
  void post_pending_tick() {
    if (layout_ == nullptr || screen_ == nullptr || !layout_->request_ui_tick) {
      return;
    }
    layout_->request_ui_tick = false;
    screen_->Post(Event::Custom);
  }

  MainLayoutState* layout_;
  ScreenInteractive* screen_;
};

Component WrapUiTickPost(Component child, MainLayoutState* layout, ScreenInteractive* screen) {
  auto wrapper = Make<UiTickPostWrapper>(layout, screen);
  wrapper->Add(std::move(child));
  return wrapper;
}

}  // namespace

Application::Application(AppConfig config) : config_(std::move(config)) {
  std::error_code ec;

  if (config_.workspace_root.empty()) {
    config_.workspace_root = fs::current_path().string();
  } else {
    config_.workspace_root = fs::absolute(config_.workspace_root, ec).string();
  }

  if (!config_.program.empty()) {
    config_.program = fs::absolute(config_.program, ec).string();
  }

  if (!config_.initial_file.empty()) {
    config_.initial_file = fs::absolute(config_.initial_file, ec).string();
  }

  model_.workspace_root = config_.workspace_root;
  model_.program = config_.program;
  model_.program_args = config_.args;
  workspace_.root = config_.workspace_root;

  connection_wizard_state_.browser.launch_root = config_.launch_directory;
  if (connection_wizard_state_.browser.launch_root.empty()) {
    connection_wizard_state_.browser.launch_root = config_.workspace_root;
  }
  workspace_wizard_state_.launch_root = connection_wizard_state_.browser.launch_root;

  symbol_provider_ = std::make_shared<LspSymbolProvider>();
  app_settings_ = AppSettings::load();
  layout_state_.app_settings = &app_settings_;
  symbol_provider_->set_lsp_enabled(app_settings_.lsp_enabled);
  debug_available_ = gdb_supports_dap();

  if (config_.use_workspace_wizard) {
    workspace_.status_message = "Selecciona un directorio de trabajo...";
    workspace_wizard_state_.open = true;
    workspace_wizard_state_.reset();
    if (!config_.workspace_root.empty()) {
      model_.workspace_root = config_.workspace_root;
    }
  } else {
    set_workspace(config_.workspace_root);
    if (!config_.initial_file.empty()) {
      workspace_.open_file(config_.initial_file);
    }
    if (config_.auto_debug && connection_config_complete()) {
      if (debug_available_) {
        app_mode_ = AppMode::kDebug;
        layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kDebug;
        layout_state_.console_visible = true;
        layout_state_.terminal_start_requested = true;
      } else {
        set_workspace_status(
            "Depuración no disponible (GDB 14+ con DAP requerido)");
      }
    }
  }

  backend_ = std::make_unique<DapBackend>(command_queue_, event_queue_);
}

Application::~Application() {
  shell_session_.stop();
  workspace_watcher_.stop();
  if (symbol_provider_) {
    symbol_provider_->on_workspace_closed();
  }
  symbol_indexer_.stop();
  indexer_.stop();
  if (backend_) {
    backend_->stop();
  }
}

void Application::request_terminal_autostart() {
  if (model_.workspace_root.empty()) {
    return;
  }
  layout_state_.console_visible = true;
  layout_state_.terminal_start_requested = true;
}

void Application::set_workspace(const std::string& workspace_root) {
  std::error_code ec;
  const auto absolute = fs::absolute(workspace_root, ec).string();
  config_.workspace_root = absolute;
  model_.workspace_root = absolute;
  workspace_.root = absolute;
  workspace_.cursor_history.clear();
  workspace_.clear_tabs();
  workspace_.status_message = "Workspace: " + fs::path(absolute).filename().string();
  file_picker_state_.indexed_root.clear();
  file_picker_state_.all_files.clear();
  shell_session_.stop();
  model_.console_output.clear();
  request_terminal_autostart();
  if (symbol_provider_) {
    symbol_provider_->on_workspace_opened(absolute);
  }
  indexer_.start_scan(absolute);
  symbol_indexer_.start_scan(absolute, symbol_provider_, &indexer_);
  workspace_watcher_.start(absolute);
}

void Application::on_workspace_complete(const std::string& workspace_root) {
  set_workspace(workspace_root);
  config_.use_workspace_wizard = false;
  workspace_wizard_state_.open = false;
}

void Application::open_workspace_wizard() {
  if (workspace_wizard_state_.open || connection_wizard_state_.open) {
    return;
  }
  if (debugging_started_) {
    exit_debug_mode();
  }
  workspace_wizard_state_.launch_root =
      workspace_.root.empty() ? connection_wizard_state_.browser.launch_root
                              : workspace_.root;
  workspace_wizard_state_.reset();
  workspace_wizard_state_.open = true;
}

bool Application::connection_config_complete() const {
  if (config_.program.empty()) {
    return false;
  }
  if (config_.mode == SessionMode::kAttach) {
    return config_.attach_pid > 0 || !config_.attach_target.empty();
  }
  return true;
}

void Application::ensure_backend_started() {
  if (backend_started_ || !debug_available_) {
    return;
  }
  backend_->start();
  backend_started_ = true;
}

void Application::set_status(const std::string& message) {
  model_.status_message = message;
  workspace_.status_message = message;
}

void Application::set_workspace_status(const std::string& message) {
  workspace_.status_message = message;
  if (app_mode_ == AppMode::kNormal) {
    model_.status_message = message;
  }
}

void Application::exit_debug_mode() {
  if (debugging_started_) {
    if (config_.mode == SessionMode::kAttach) {
      submit_command(UiCommand{UiCommandKind::kDetach});
    } else {
      submit_command(UiCommand{UiCommandKind::kDisconnect});
    }
    debugging_started_ = false;
    session_ready_ = false;
  }
  if (backend_started_) {
    backend_->stop();
    backend_started_ = false;
    backend_ = std::make_unique<DapBackend>(command_queue_, event_queue_);
  }

  model_.stack_frames.clear();
  model_.locals.clear();
  model_.variable_children.clear();
  model_.watches.clear();
  model_.breakpoints_by_file.clear();
  model_.disabled_breakpoints.clear();
  model_.console_output.clear();
  model_.state = DebugState::kDisconnected;

  app_mode_ = AppMode::kNormal;
  layout_state_.text_input_focus = TextInputFocus::None;
  layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kTerminal;
  set_workspace_status("Modo edición");
  request_terminal_autostart();
}

void Application::apply_connection_and_start() {
  if (!session_ready_ || debugging_started_) {
    return;
  }
  if (!connection_config_complete()) {
    return;
  }

  debugging_started_ = true;
  model_.program = config_.program;
  model_.workspace_root = config_.workspace_root;
  model_.program_args = config_.args;

  if (!workspace_.buffer.path.empty()) {
    model_.active_file = workspace_.buffer.path;
    model_.active_line = workspace_.buffer.primary_line() + 1;
    model_.view_token++;
  }

  app_mode_ = AppMode::kDebug;
  focus_state_.region = FocusRegion::RightPanel;
  layout_state_.text_input_focus = TextInputFocus::None;
  layout_state_.right_panel_active_section = 1;
  layout_state_.pending_watches_focus = true;
  layout_state_.focus_sync_needed = true;
  layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kDebug;
  layout_state_.console_visible = true;
  layout_state_.terminal_start_requested = true;
  model_.console_output.clear();

  if (config_.mode == SessionMode::kAttach) {
    UiCommand attach;
    attach.kind = UiCommandKind::kAttach;
    attach.attach.program = config_.program;
    attach.attach.cwd = config_.workspace_root;
    attach.attach.pid = config_.attach_pid;
    attach.attach.target = config_.attach_target;
    submit_command(attach);
    if (config_.attach_pid > 0) {
      set_status("Adjuntando PID " + std::to_string(config_.attach_pid));
    } else if (!config_.attach_target.empty()) {
      set_status("Adjuntando " + config_.attach_target);
    }
  } else {
    UiCommand launch;
    launch.kind = UiCommandKind::kLaunch;
    launch.launch.program = config_.program;
    launch.launch.cwd = config_.workspace_root;
    launch.launch.args = config_.args;
    launch.launch.stop_at_main = true;
    submit_command(launch);
    set_status("Lanzando " + config_.program);
  }
}

void Application::on_connection_complete(const ConnectionResult& result) {
  pending_connection_ = result;
}

void Application::apply_pending_connection() {
  if (!pending_connection_.has_value()) {
    return;
  }

  if (workspace_.buffer.dirty) {
    set_status("Guarda los cambios (Ctrl+S) antes de depurar.");
    return;
  }

  const ConnectionResult result = *pending_connection_;
  pending_connection_.reset();

  config_.mode = result.mode;
  config_.program = result.program;
  config_.attach_pid = result.attach_pid;
  config_.attach_target.clear();

  model_.program = config_.program;
  model_.program_args = config_.args;

  set_status("Conectando con GDB DAP...");
  ensure_backend_started();
}

void Application::open_connection_wizard() {
  if (connection_wizard_state_.open || workspace_wizard_state_.open) {
    return;
  }
  if (!debug_available_) {
    set_status("Depuración no disponible: GDB 14+ con DAP requerido");
    return;
  }

  if (debugging_started_) {
    exit_debug_mode();
  } else if (app_mode_ == AppMode::kDebug) {
    app_mode_ = AppMode::kNormal;
    layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kTerminal;
  }

  connection_wizard_state_.workspace_root = workspace_.root;
  connection_wizard_state_.browser.launch_root =
      connection_wizard_state_.browser.launch_root.empty()
          ? workspace_.root
          : connection_wizard_state_.browser.launch_root;
  connection_wizard_state_.reset();
  connection_wizard_state_.open = true;
  set_status("Configurar depuración...");
}

void Application::submit_command(const UiCommand& command) {
  if (backend_started_) {
    backend_->submit(command);
  }
}

void Application::refresh_all_watches() {
  for (const auto& watch : model_.watches) {
    UiCommand command;
    command.kind = UiCommandKind::kAddWatch;
    command.expression = watch.expression;
    if (model_.variables_frame_id >= 0) {
      command.frame_id = model_.variables_frame_id;
    } else if (!model_.stack_frames.empty()) {
      command.frame_id = model_.stack_frames[model_.selected_frame].id;
    }
    submit_command(command);
  }
}

void Application::apply_event(const DebugEvent& event) {
  switch (event.kind) {
    case DebugEventKind::kSessionReady:
      session_ready_ = true;
      model_.status_message = event.text;
      if (connection_wizard_state_.open || !connection_config_complete()) {
        break;
      }
      apply_connection_and_start();
      break;
    case DebugEventKind::kOutput:
      model_.append_console(event.text);
      break;
    case DebugEventKind::kStopped:
      model_.set_stopped(event.thread_id, event.stop_reason);
      if (!event.text.empty() && event.stop_reason != "attach" &&
          event.stop_reason != "pause") {
        model_.append_console("[stopped] " + event.text);
      } else if (event.stop_reason == "breakpoint") {
        model_.append_console("[stopped] breakpoint alcanzado");
      }
      refresh_all_watches();
      break;
    case DebugEventKind::kContinued:
      model_.set_running();
      break;
    case DebugEventKind::kTerminated:
      model_.set_terminated();
      debugging_started_ = false;
      if (!event.text.empty()) {
        model_.status_message = event.text;
      }
      break;
    case DebugEventKind::kStackUpdated:
      model_.stack_frames = event.stack_frames;
      if (!model_.stack_frames.empty()) {
        model_.selected_frame = 0;
        model_.active_file = model_.stack_frames.front().file;
        model_.active_line = model_.stack_frames.front().line;
        model_.view_token++;
      }
      break;
    case DebugEventKind::kVariablesUpdated:
      model_.locals = event.variables;
      model_.variable_children.clear();
      if (event.stack_frame_id >= 0) {
        model_.variables_frame_id = event.stack_frame_id;
      }
      break;
    case DebugEventKind::kVariableChildrenUpdated:
      model_.variable_children[event.parent_expression] = event.variables;
      break;
    case DebugEventKind::kEvaluateResult:
      if (!event.text.empty()) {
        model_.append_console(event.text);
      }
      break;
    case DebugEventKind::kWatchUpdated:
      for (auto& watch : model_.watches) {
        if (watch.expression == event.watch_expression) {
          watch.value = event.watch_value;
        }
      }
      break;
    case DebugEventKind::kBreakpointsUpdated:
      for (const auto& bp : event.breakpoints) {
        if (bp.verified) {
          model_.breakpoints_by_file[bp.file].insert(bp.line);
        } else if (bp.line > 0 && !bp.file.empty()) {
          auto& lines = model_.breakpoints_by_file[bp.file];
          lines.erase(bp.line);
          if (lines.empty()) {
            model_.breakpoints_by_file.erase(bp.file);
          }
          std::string msg = "[breakpoint] no verificado: " + bp.file + ":" +
                            std::to_string(bp.line);
          if (!bp.message.empty()) {
            msg += " — " + bp.message;
          }
          model_.append_console(msg);
        }
      }
      break;
    case DebugEventKind::kError:
      model_.append_console("[error] " + event.text);
      model_.status_message = event.text;
      break;
    default:
      break;
  }
}

void Application::drain_events() {
  while (auto event = event_queue_.try_pop()) {
    apply_event(*event);
  }
}

void Application::process_index_changes() {
  if (workspace_.root.empty()) {
    return;
  }
  for (const auto& change : workspace_watcher_.drain_changes()) {
    if (change.kind == FileIndexChangeKind::Remove) {
      indexer_.remove_file(workspace_.root, change.relative_path);
      symbol_indexer_.remove_file(workspace_.root, change.relative_path);
    } else {
      indexer_.upsert_file(workspace_.root, change.relative_path, change.absolute_path);
      symbol_indexer_.reindex_file(workspace_.root, change.relative_path,
                                   change.absolute_path);
    }
  }
}

bool Application::any_modal_open() const {
  return workspace_wizard_state_.open || connection_wizard_state_.open ||
         file_picker_state_.open || symbol_picker_state_.open ||
         shortcuts_modal_state_.open || settings_modal_state_.open ||
         quit_confirm_state_.open || context_menu_active(&layout_state_.context_menu);
}

void Application::apply_app_settings() {
  if (symbol_provider_) {
    symbol_provider_->set_lsp_enabled(app_settings_.lsp_enabled);
  }
  if (!app_settings_.secondary_panel_enabled &&
      focus_state_.region == FocusRegion::RightPanel) {
    focus_state_.region = FocusRegion::Editor;
    layout_state_.text_input_focus = TextInputFocus::None;
    layout_state_.focus_sync_needed = true;
  }
  workspace_.buffer.view_token++;
  layout_state_.request_ui_tick = true;
}

bool Application::handle_focus_shortcuts(const Event& event) {
  auto mark_focus_sync = [this] { layout_state_.focus_sync_needed = true; };

  if (event == Event::CtrlA) {
    focus_state_.region = FocusRegion::Explorer;
    mark_focus_sync();
    return true;
  }
  if (event == Event::CtrlE) {
    focus_state_.region = FocusRegion::Editor;
    layout_state_.text_input_focus = TextInputFocus::None;
    mark_focus_sync();
    return true;
  }
  if (event_is_open_outline_panel(event)) {
    if (!app_settings_.secondary_panel_enabled) {
      return true;
    }
    focus_state_.region = FocusRegion::RightPanel;
    layout_state_.right_sidebar.selected_tab = 0;
    layout_state_.right_panel_active_section = 0;
    layout_state_.text_input_focus = TextInputFocus::None;
    mark_focus_sync();
    return true;
  }
  if (event_is_open_search_panel(event)) {
    if (!app_settings_.secondary_panel_enabled) {
      return true;
    }
    focus_state_.region = FocusRegion::RightPanel;
    layout_state_.right_sidebar.selected_tab = RightSidebarTabs::kSearch;
    layout_state_.right_panel_active_section = 0;
    layout_state_.right_sidebar.pending_focus_search = true;
    layout_state_.text_input_focus = TextInputFocus::SearchQuery;
    mark_focus_sync();
    return true;
  }
  if (event == Event::F4) {
    focus_state_.region = FocusRegion::Terminal;
    layout_state_.console_visible = true;
    layout_state_.console_tabs.selected_tab = ConsolePanelTabs::kTerminal;
    layout_state_.text_input_focus = TextInputFocus::Console;
    layout_state_.terminal_start_requested = true;
    mark_focus_sync();
    return true;
  }
  if (focus_state_.region != FocusRegion::Editor) {
    if (event_is_alt_left(event)) {
      if (focus_state_.region == FocusRegion::RightPanel &&
          !app_settings_.secondary_panel_enabled) {
        focus_state_.region = FocusRegion::Editor;
      } else {
        focus_state_.move_left();
      }
      mark_focus_sync();
      return true;
    }
    if (event_is_alt_right(event)) {
      if (focus_state_.region == FocusRegion::Editor &&
          !app_settings_.secondary_panel_enabled) {
        return true;
      }
      focus_state_.move_right();
      mark_focus_sync();
      return true;
    }
    if (event_is_alt_down(event)) {
      focus_state_.move_down();
      layout_state_.console_visible = true;
      layout_state_.terminal_start_requested = true;
      if (app_mode_ != AppMode::kDebug ||
          layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kTerminal) {
        layout_state_.text_input_focus = TextInputFocus::Console;
      }
      mark_focus_sync();
      return true;
    }
    if (event_is_alt_up(event)) {
      focus_state_.move_up();
      mark_focus_sync();
      return true;
    }
  }
  return false;
}

int Application::run() {
  if (config_.auto_debug && connection_config_complete() && !config_.use_workspace_wizard &&
      debug_available_) {
    ensure_backend_started();
  }

  const bool ui_smoke = std::getenv("TGDB_UI_SMOKE") != nullptr;
  auto screen = ui_smoke ? ScreenInteractive::TerminalOutput()
                         : ScreenInteractive::Fullscreen();
  if (!ui_smoke) {
    screen.ForceHandleCtrlC(false);
    screen.ForceHandleCtrlZ(false);
    enable_extended_key_reporting();
  }

  std::unique_ptr<EventPoller> event_poller;
  if (!ui_smoke) {
    event_poller = std::make_unique<EventPoller>(&screen);
  } else {
    auto exit_loop = screen.ExitLoopClosure();
    std::thread([exit_loop] {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      exit_loop();
    }).detach();
  }

  CommandCallback on_command = [this](const UiCommand& command) {
    submit_command(command);
  };

  StopDebugCallback on_stop_debug = [this] { exit_debug_mode(); };

  auto build_ui = [&]() {
    return MakeMainLayout(&app_mode_, &model_, &workspace_, &source_state_,
                          &focus_state_, symbol_provider_, on_command,
                          &layout_state_, on_stop_debug, &shell_session_, &indexer_,
                          &symbol_indexer_);
  };

  auto layout = build_ui();

  layout_state_.terminal_width = [&screen]() { return screen.dimx(); };

  auto with_picker = MakeFilePickerOverlay(
      layout, &model_, &workspace_, &file_picker_state_, &focus_state_, &indexer_);

  auto with_symbol_picker = MakeSymbolPickerOverlay(
      with_picker, &workspace_, &symbol_picker_state_, &focus_state_, symbol_provider_);

  auto with_debug_wizard = MakeConnectionWizardOverlay(
      with_symbol_picker, &connection_wizard_state_, &model_, &layout_state_,
      [this](const ConnectionResult& result) { on_connection_complete(result); },
      [&screen] { screen.ExitLoopClosure()(); });

  auto with_workspace_wizard = MakeWorkspaceWizardOverlay(
      with_debug_wizard, &workspace_wizard_state_, &layout_state_,
      [this](const std::string& root) { on_workspace_complete(root); },
      [&screen] { screen.ExitLoopClosure()(); });

  auto with_shortcuts = MakeShortcutsModalOverlay(with_workspace_wizard,
                                                  &shortcuts_modal_state_);

  SettingsApplyCallback on_settings_apply = [this](const AppSettings&) { apply_app_settings(); };
  auto with_settings = MakeSettingsModalOverlay(with_shortcuts, &settings_modal_state_,
                                                &app_settings_, on_settings_apply);

  auto with_quit_confirm = MakeQuitConfirmOverlay(
      with_settings, &quit_confirm_state_, &layout_state_, [this, &screen] {
        submit_command(UiCommand{UiCommandKind::kQuit});
        screen.ExitLoopClosure()();
      });

  auto with_context_menu = MakeContextMenuOverlay(
      with_quit_confirm, &layout_state_.context_menu, &workspace_, &model_, &focus_state_,
      &layout_state_, symbol_provider_, &indexer_, &symbol_indexer_,
      [this]() {
        if (layout_state_.editor_visible_line_count) {
          return layout_state_.editor_visible_line_count();
        }
        return 24;
      });

  auto root = CatchEvent(with_context_menu, [this, &screen, on_command](const Event& event) {
    try {
      if (event == Event::Custom) {
        if (!any_modal_open()) {
          apply_pending_connection();
        }
        process_index_changes();
        drain_events();
        cursor_blink::tick();
        layout_state_.clickable.tick();
        if (layout_state_.console_visible && layout_state_.terminal_tick_callback) {
          layout_state_.terminal_tick_callback();
        }
        if (symbol_provider_ && symbol_provider_->drain_async_results()) {
          layout_state_.request_ui_tick = true;
        }
        if (layout_state_.editor_tick_callback) {
          layout_state_.editor_tick_callback();
        }
        if (layout_state_.outline_tick_callback) {
          layout_state_.outline_tick_callback();
        }
        if (layout_state_.right_sidebar.pending_call_hierarchy &&
            layout_state_.call_hierarchy_key_handler) {
          layout_state_.call_hierarchy_key_handler(event);
        }
        return false;
      }

      if (event == Event::F1) {
        if (shortcuts_modal_state_.open) {
          shortcuts_modal_state_.open = false;
          shortcuts_modal_state_.first_visible = 0;
        } else if (!any_modal_open()) {
          shortcuts_modal_state_.open = true;
          shortcuts_modal_state_.first_visible = 0;
        }
        return true;
      }

      if (app_mode_ == AppMode::kNormal && event == Event::F10) {
        if (settings_modal_state_.open) {
          close_settings_modal(
              &settings_modal_state_, &app_settings_,
              [this](const AppSettings&) { apply_app_settings(); });
        } else if (!any_modal_open()) {
          open_settings_modal(&settings_modal_state_, app_settings_);
        }
        return true;
      }

      if (any_modal_open()) {
        return false;
      }

      if (layout_state_.editor_modifier_handler) {
        Event mod_event = event;
        layout_state_.editor_modifier_handler(mod_event);
      }

      if (app_mode_ == AppMode::kNormal && event.is_mouse() &&
          layout_state_.explorer_mouse_handler &&
          layout_state_.explorer_mouse_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }

      if (app_mode_ == AppMode::kNormal && event.is_mouse() &&
          layout_state_.editor_chrome_mouse_handler &&
          layout_state_.editor_chrome_mouse_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }

      if (handle_focus_shortcuts(event)) {
        screen.Post(Event::Custom);
        return true;
      }

      if (layout_state_.console_visible && layout_state_.console_mouse_handler &&
          layout_state_.console_mouse_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }

      if (app_mode_ == AppMode::kNormal && event.is_mouse() &&
          layout_state_.editor_mouse_handler &&
          layout_state_.editor_mouse_handler(event)) {
        screen.Post(Event::Custom);
        layout_state_.focus_sync_needed = true;
        return true;
      }

      // Intercept console keys before editor (FTXUI focus may still be on editor).
      const bool terminal_tab =
          app_mode_ != AppMode::kDebug ||
          layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kTerminal;
      const bool shell_terminal_focus =
          terminal_tab && focus_state_.region == FocusRegion::Terminal &&
          shell_session_.running();
      if ((layout_state_.text_input_focus == TextInputFocus::Console || shell_terminal_focus) &&
          layout_state_.console_key_handler &&
          layout_state_.console_key_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }
      if (app_mode_ == AppMode::kNormal && event_is_ctrl_f(event) &&
          focus_state_.region == FocusRegion::Editor &&
          layout_state_.editor_key_handler &&
          layout_state_.editor_key_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }
      if (is_search_input_focus(layout_state_.text_input_focus) &&
          layout_state_.search_key_handler &&
          layout_state_.search_key_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }
      if (focus_state_.region == FocusRegion::RightPanel &&
          layout_state_.right_sidebar.selected_tab == RightSidebarTabs::kCallHierarchy &&
          layout_state_.call_hierarchy_key_handler &&
          layout_state_.call_hierarchy_key_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }
      if (layout_state_.text_input_focus == TextInputFocus::EditorFind &&
          focus_state_.region == FocusRegion::Editor &&
          app_mode_ == AppMode::kNormal && layout_state_.editor_key_handler &&
          layout_state_.editor_key_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }
      if (focus_state_.region == FocusRegion::Editor &&
          app_mode_ == AppMode::kNormal &&
          !is_search_input_focus(layout_state_.text_input_focus) &&
          !is_watch_input_focus(layout_state_.text_input_focus) &&
          layout_state_.text_input_focus != TextInputFocus::EditorFind &&
          layout_state_.text_input_focus != TextInputFocus::Console &&
          layout_state_.editor_key_handler && layout_state_.editor_key_handler(event)) {
        screen.Post(Event::Custom);
        return true;
      }

      // Tab nunca cambia foco entre paneles; en terminal va al shell, en editor indenta.
      if (event == Event::Tab || event == Event::TabReverse) {
        const bool terminal_tab =
            app_mode_ != AppMode::kDebug ||
            layout_state_.console_tabs.selected_tab == ConsolePanelTabs::kTerminal;
        if (terminal_tab &&
            (layout_state_.text_input_focus == TextInputFocus::Console ||
             (focus_state_.region == FocusRegion::Terminal && shell_session_.running())) &&
            shell_session_.running()) {
          if (event == Event::Tab) {
            shell_session_.write_raw("\t");
          } else {
            shell_session_.write_raw("\x1b[Z");
          }
          screen.Post(Event::Custom);
          return true;
        }
        return false;
      }

      if (event == Event::CtrlQ) {
        quit_confirm_state_.open = true;
        quit_confirm_state_.selected = 0;
        return true;
      }

      if (app_mode_ == AppMode::kDebug) {
        UiCommand command;
        if (event == Event::CtrlB) {
          if (!model_.active_file.empty() && model_.active_line > 0) {
            ToggleBreakpointAtLine(&model_, model_.active_line, on_command);
          }
          return true;
        }
        if (event == Event::F5) {
          command.kind = UiCommandKind::kContinue;
          submit_command(command);
          layout_state_.clickable.trigger_press(press_id::kWatchesPlay);
          layout_state_.request_ui_tick = true;
          return true;
        }
        if (event == Event::F10) {
          command.kind = UiCommandKind::kNext;
          submit_command(command);
          return true;
        }
        if (event == Event::F11) {
          command.kind = UiCommandKind::kStepIn;
          submit_command(command);
          return true;
        }
        if (event == Event::Special({24})) {
          command.kind = UiCommandKind::kStepOut;
          submit_command(command);
          return true;
        }
      }

      if (event == Event::F2) {
        open_connection_wizard();
        return true;
      }
      if (event == Event::F3) {
        open_workspace_wizard();
        return true;
      }
      if (event == Event::CtrlP) {
        if (file_picker_state_.open && !file_picker_state_.matches.empty()) {
          file_picker_state_.selected =
              (file_picker_state_.selected + 1) %
              static_cast<int>(file_picker_state_.matches.size());
          return true;
        }
        file_picker_state_.open = !file_picker_state_.open;
        if (file_picker_state_.open) {
          file_picker_state_.query.clear();
          file_picker_state_.selected = 0;
          file_picker_state_.sync_index(indexer_.snapshot(), model_.workspace_root);
          file_picker_state_.refresh_matches(&workspace_);
        }
        return true;
      }
      if (event == Event::CtrlO) {
        if (symbol_picker_state_.open && !symbol_picker_state_.matches.empty()) {
          symbol_picker_state_.selected =
              (symbol_picker_state_.selected + 1) %
              static_cast<int>(symbol_picker_state_.matches.size());
          return true;
        }
        symbol_picker_state_.open = true;
        symbol_picker_state_.query.clear();
        symbol_picker_state_.selected = 0;
        symbol_picker_state_.loaded_file.clear();
        return true;
      }
      if (event == Event::F9) {
        layout_state_.diagnostics_panel_visible = !layout_state_.diagnostics_panel_visible;
        return true;
      }
      if (event == Event::CtrlT) {
        layout_state_.console_visible = !layout_state_.console_visible;
        return true;
      }
      if (event == Event::Escape) {
        if (focus_state_.region == FocusRegion::Editor) {
          return false;
        }
        layout_state_.text_input_focus = TextInputFocus::None;
        return true;
      }

      return false;
    } catch (const std::exception& e) {
      model_.append_console(std::string("[crash] ") + e.what());
      set_status(std::string("Error: ") + e.what());
      print_current_backtrace(e.what());
      return true;
    }
  });

  layout_state_.schedule_ui_tick = [&screen]() { screen.Post(Event::Custom); };

  screen.Loop(WrapUiTickPost(root, &layout_state_, &screen));

  if (!ui_smoke) {
    disable_extended_key_reporting();
  }

  if (backend_) {
    backend_->stop();
    backend_started_ = false;
  }
  return 0;
}

}  // namespace tgdb
