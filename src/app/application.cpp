#include "app/application.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ui/file_picker.hpp"
#include "ui/main_layout.hpp"

namespace fs = std::filesystem;

namespace tgdb {

using namespace ftxui;

Application::Application(AppConfig config) : config_(std::move(config)) {
  namespace fs = std::filesystem;
  std::error_code ec;

  if (config_.workspace_root.empty()) {
    config_.workspace_root = fs::current_path().string();
  } else {
    config_.workspace_root = fs::absolute(config_.workspace_root, ec).string();
  }

  if (!config_.program.empty()) {
    config_.program = fs::absolute(config_.program, ec).string();
  }

  if (config_.workspace_root.empty()) {
    config_.workspace_root = fs::current_path().string();
  }
  model_.workspace_root = config_.workspace_root;
  model_.program = config_.program;
  model_.program_args = config_.args;
  model_.status_message = "Conectando con GDB DAP...";

  backend_ = std::make_unique<DapBackend>(command_queue_, event_queue_);
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
      if (debugging_started_) {
        break;
      }
      debugging_started_ = true;

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
              "Attach PID " + std::to_string(config_.attach_pid);
        } else if (!config_.attach_target.empty()) {
          model_.status_message = "Attach " + config_.attach_target;
        }
      } else if (!config_.program.empty()) {
        UiCommand launch;
        launch.kind = UiCommandKind::kLaunch;
        launch.launch.program = config_.program;
        launch.launch.cwd = config_.workspace_root;
        launch.launch.args = config_.args;
        launch.launch.stop_at_main = true;
        submit_command(launch);
        model_.status_message = "Lanzando " + config_.program;
      }
      break;
    case DebugEventKind::kOutput:
      model_.append_console(event.text);
      break;
    case DebugEventKind::kStopped:
      model_.set_stopped(event.thread_id, event.stop_reason);
      if (event.stop_reason == "breakpoint") {
        model_.append_console("[stopped] breakpoint alcanzado");
      }
      refresh_all_watches();
      break;
    case DebugEventKind::kContinued:
      model_.set_running();
      break;
    case DebugEventKind::kTerminated:
      model_.set_terminated();
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
          std::string msg =
              "[breakpoint] no verificado: " + bp.file + ":" +
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

  auto root = CatchEvent(with_picker, [this](const Event& event) {
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
    if (event == Event::Special({24})) {  // Shift+F11 (terminal dependent)
      command.kind = UiCommandKind::kStepOut;
      submit_command(command);
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
