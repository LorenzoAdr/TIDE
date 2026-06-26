#include "app/application.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ui/connection_wizard.hpp"
#include "ui/file_picker.hpp"
#include "ui/main_layout.hpp"

namespace fs = std::filesystem;

namespace tgdb {

using namespace ftxui;

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

  model_.workspace_root = config_.workspace_root;
  model_.program = config_.program;
  model_.program_args = config_.args;

  connection_wizard_state_.launch_root = config_.launch_directory;
  if (connection_wizard_state_.launch_root.empty()) {
    connection_wizard_state_.launch_root = config_.workspace_root;
  }

  if (config_.use_connection_wizard) {
    model_.status_message = "Configurar conexión...";
    connection_wizard_state_.open = true;
    connection_wizard_state_.reset();
    if (!config_.program.empty()) {
      connection_wizard_state_.selected_program = config_.program;
      connection_wizard_state_.step = WizardStep::PickWorkspace;
      connection_wizard_state_.browser_path =
          connection_wizard_state_.launch_root;
      connection_wizard_state_.browser_loaded_path.clear();
      if (config_.mode == SessionMode::kAttach) {
        connection_wizard_state_.mode = WizardMode::Attach;
        connection_wizard_state_.mode_selected = 1;
      } else {
        connection_wizard_state_.mode = WizardMode::Launch;
        connection_wizard_state_.mode_selected = 0;
      }
    }
  } else {
    model_.status_message = "Conectando con GDB DAP...";
  }

  backend_ = std::make_unique<DapBackend>(command_queue_, event_queue_);
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

  if (config_.mode == SessionMode::kAttach) {
    UiCommand attach;
    attach.kind = UiCommandKind::kAttach;
    attach.attach.program = config_.program;
    attach.attach.cwd = config_.workspace_root;
    attach.attach.pid = config_.attach_pid;
    attach.attach.target = config_.attach_target;
    submit_command(attach);
    if (config_.attach_pid > 0) {
      model_.status_message =
          "Adjuntando PID " + std::to_string(config_.attach_pid);
    } else if (!config_.attach_target.empty()) {
      model_.status_message = "Adjuntando " + config_.attach_target;
    }
  } else {
    UiCommand launch;
    launch.kind = UiCommandKind::kLaunch;
    launch.launch.program = config_.program;
    launch.launch.cwd = config_.workspace_root;
    launch.launch.args = config_.args;
    launch.launch.stop_at_main = true;
    submit_command(launch);
    model_.status_message = "Lanzando " + config_.program;
  }
}

void Application::on_connection_complete(const ConnectionResult& result) {
  config_.mode = result.mode;
  config_.program = result.program;
  config_.workspace_root = result.workspace_root;
  config_.attach_pid = result.attach_pid;
  config_.attach_target.clear();
  config_.use_connection_wizard = false;

  model_.program = config_.program;
  model_.workspace_root = config_.workspace_root;
  model_.program_args = config_.args;
  model_.status_message = "Conectando con GDB DAP...";

  apply_connection_and_start();
}

void Application::open_connection_wizard() {
  if (connection_wizard_state_.open) {
    return;
  }

  if (debugging_started_) {
    if (config_.mode == SessionMode::kAttach) {
      submit_command(UiCommand{UiCommandKind::kDetach});
    } else {
      submit_command(UiCommand{UiCommandKind::kDisconnect});
    }
    debugging_started_ = false;
  }

  model_.stack_frames.clear();
  model_.locals.clear();
  model_.variable_children.clear();
  model_.watches.clear();
  model_.breakpoints_by_file.clear();
  model_.disabled_breakpoints.clear();
  model_.console_output.clear();
  model_.state = DebugState::kDisconnected;
  model_.status_message = "Configurar conexión...";

  connection_wizard_state_.reset();
  connection_wizard_state_.open = true;
}

void Application::submit_command(const UiCommand& command) {
  backend_->submit(command);
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
        model_.status_message = connection_wizard_state_.open
                                    ? "Configurar conexión..."
                                    : model_.status_message;
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

void Application::schedule_poll() {
  // Polling is wired from run() via screen.Post.
}

int Application::run() {
  backend_->start();

  auto screen = ScreenInteractive::Fullscreen();
  std::atomic<bool> poll_events{true};
  std::thread event_poller([&screen, &poll_events] {
    while (poll_events.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      screen.Post(Event::Custom);
    }
  });

  CommandCallback on_command = [this](const UiCommand& command) {
    submit_command(command);
  };

  auto layout = MakeMainLayout(
      &model_, &source_state_, on_command, &layout_state_,
      [this] { drain_events(); });

  auto with_picker =
      MakeFilePickerOverlay(layout, &model_, &file_picker_state_);

  auto with_wizard = MakeConnectionWizardOverlay(
      with_picker, &connection_wizard_state_, &model_,
      [this](const ConnectionResult& result) { on_connection_complete(result); },
      [&screen] { screen.ExitLoopClosure()(); });

  auto root = CatchEvent(with_wizard, [this](const Event& event) {
    if (connection_wizard_state_.open) {
      return false;
    }

    UiCommand command;
    bool handled = true;

    if (event == Event::Character('q')) {
      command.kind = UiCommandKind::kQuit;
      submit_command(command);
      return true;
    }
    if (event == Event::F5) {
      command.kind = UiCommandKind::kContinue;
      submit_command(command);
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
    if (event == Event::F2) {
      open_connection_wizard();
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
        file_picker_state_.ensure_indexed(model_.workspace_root);
        file_picker_state_.refresh_matches();
      }
      return true;
    }
    if (event == Event::CtrlT) {
      layout_state_.console_visible = !layout_state_.console_visible;
      return true;
    }
    if (event == Event::Escape) {
      layout_state_.text_input_focus = TextInputFocus::None;
      return true;
    }

    handled = false;
    return handled;
  });

  screen.Loop(root);

  poll_events = false;
  if (event_poller.joinable()) {
    event_poller.join();
  }

  submit_command(UiCommand{UiCommandKind::kDisconnect});
  backend_->stop();
  return 0;
}

}  // namespace tgdb
