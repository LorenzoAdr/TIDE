#include "backend/dap_backend.hpp"

#include <filesystem>
#include <utility>

#include "dap/gdb_protocol.hpp"
#include "dap/protocol.h"
#include "dap/session.h"
#include "util/path_normalize.hpp"

namespace tgdb {

DapBackend::DapBackend(ThreadSafeQueue<UiCommand>& commands,
                       ThreadSafeQueue<DebugEvent>& events)
    : commands_(commands), events_(events) {}

DapBackend::~DapBackend() {
  stop();
}

void DapBackend::start() {
  if (running_.exchange(true)) {
    return;
  }
  worker_ = std::thread([this] { worker_main(); });
}

void DapBackend::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  commands_.close();

  if (worker_.joinable()) {
    worker_.join();
  }

  if (gdb_) {
    gdb_->stop(true);
  }
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_.reset();
  }

  gdb_.reset();
}

void DapBackend::submit(const UiCommand& command) {
  commands_.push(command);
}

void DapBackend::push_event(DebugEvent event) {
  events_.push(std::move(event));
}

void DapBackend::push_error(const std::string& message) {
  DebugEvent event;
  event.kind = DebugEventKind::kError;
  event.text = message;
  push_event(std::move(event));
}

void DapBackend::setup_session() {
  session_ = dap::Session::create();

  session_->registerHandler([&](const dap::InitializedEvent&) {
    DebugEvent event;
    event.kind = DebugEventKind::kSessionReady;
    event.text = "GDB DAP listo";
    push_event(std::move(event));
  });

  session_->registerHandler([&](const dap::StoppedEvent& e) {
    if (e.threadId.has_value()) {
      active_thread_id_ = static_cast<int>(*e.threadId);
    }

    // Tras continue, GDB puede entregar tarde el StoppedEvent del attach/pausa
    // inicial; ignorarlo evita que la UI vuelva a "Detenido: attach".
    const bool believed_running = !inferior_stopped_.load(std::memory_order_acquire);
    if (believed_running && inferior_attached_) {
      if (expecting_interrupt_for_breakpoints_) {
        expecting_interrupt_for_breakpoints_ = false;
      } else if (e.reason == "attach" || e.reason == "pause") {
        return;
      }
    }

    DebugEvent event;
    event.kind = DebugEventKind::kStopped;
    event.thread_id = active_thread_id_;
    event.stop_reason = e.reason;
    if (e.description.has_value()) {
      event.text = *e.description;
    } else if (e.text.has_value()) {
      event.text = *e.text;
    }
    push_event(std::move(event));

    inferior_stopped_.store(true, std::memory_order_release);

    if (breakpoints_pending_sync_) {
      UiCommand sync;
      sync.kind = UiCommandKind::kSyncBreakpoints;
      commands_.push(sync);
    }

    // No llamar session_->send() aquí: este handler corre en el hilo lector DAP
    // y provocaría deadlock. Delegar al worker.
    UiCommand refresh;
    refresh.kind = UiCommandKind::kRefreshStack;
    refresh.thread_id = active_thread_id_;
    commands_.push(refresh);
  });

  session_->registerHandler([&](const dap::ContinuedEvent& e) {
    inferior_stopped_.store(false, std::memory_order_release);
    DebugEvent event;
    event.kind = DebugEventKind::kContinued;
    event.thread_id = static_cast<int>(e.threadId);
    push_event(std::move(event));
  });

  session_->registerHandler([&](const dap::TerminatedEvent&) {
    DebugEvent event;
    event.kind = DebugEventKind::kTerminated;
    push_event(std::move(event));
  });

  session_->registerHandler([&](const dap::OutputEvent& e) {
    DebugEvent event;
    event.kind = DebugEventKind::kOutput;
    event.text = e.output;
    push_event(std::move(event));
  });

  session_->registerHandler([&](const dap::BreakpointEvent& e) {
    if (!e.breakpoint.verified) {
      return;
    }
    DebugEvent event;
    event.kind = DebugEventKind::kBreakpointsUpdated;
    BreakpointInfo info;
    if (e.breakpoint.source.has_value() &&
        e.breakpoint.source->path.has_value()) {
      info.file = *e.breakpoint.source->path;
    }
    if (e.breakpoint.line.has_value()) {
      info.line = static_cast<int>(*e.breakpoint.line);
    }
    info.verified = true;
    event.breakpoints.push_back(std::move(info));
    push_event(std::move(event));
  });

  session_->registerHandler([&](const dap::ThreadEvent& e) {
    active_thread_id_ = static_cast<int>(e.threadId);
  });
  session_->registerHandler([&](const dap::ExitedEvent&) {});

  // GDB envía estos eventos durante launch/attach; ignorarlos evita ruido en consola.
  session_->registerHandler([&](const dap::ModuleEvent&) {});
  session_->registerHandler([&](const dap::LoadedSourceEvent&) {});
  session_->registerHandler([&](const dap::CapabilitiesEvent&) {});
  session_->registerHandler([&](const dap::ProcessEvent&) {});
  session_->registerHandler([&](const dap::ProgressStartEvent&) {});
  session_->registerHandler([&](const dap::ProgressUpdateEvent&) {});
  session_->registerHandler([&](const dap::ProgressEndEvent&) {});
  session_->registerHandler([&](const dap::InvalidatedEvent&) {});
  session_->registerHandler([&](const dap::MemoryEvent&) {});

  session_->onError([&](const char* message) {
    push_error(std::string("DAP: ") + message);
  });
}

bool DapBackend::initialize_session() {
  dap::InitializeRequest request;
  request.adapterID = "gdb";
  request.linesStartAt1 = true;
  request.columnsStartAt1 = true;

  const auto response = session_->send(request).get();
  if (response.error) {
    push_error("initialize falló: " + response.error.message);
    return false;
  }
  return true;
}

void DapBackend::refresh_stack(int thread_id) {
  int top_frame_id = 0;
  bool have_frames = false;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }

    dap::StackTraceRequest request;
    request.threadId = thread_id;
    request.startFrame = 0;
    request.levels = 50;

    auto response = session_->send(request).get();
    if (response.error &&
        response.error.message.find("not stopped") != std::string::npos) {
      if (pause_inferior_locked()) {
        inferior_stopped_.store(true, std::memory_order_release);
        response = session_->send(request).get();
      }
    }
    if (response.error) {
      push_error("stackTrace falló: " + response.error.message);
      return;
    }

    DebugEvent event;
    event.kind = DebugEventKind::kStackUpdated;
    event.thread_id = thread_id;
    for (const auto& frame : response.response.stackFrames) {
      StackFrameInfo info;
      info.id = static_cast<int>(frame.id);
      info.name = frame.name;
      info.line = static_cast<int>(frame.line);
      if (frame.source.has_value() && frame.source->path.has_value()) {
        info.file = *frame.source->path;
      }
      event.stack_frames.push_back(std::move(info));
    }
    push_event(std::move(event));

    if (!response.response.stackFrames.empty()) {
      have_frames = true;
      top_frame_id =
          static_cast<int>(response.response.stackFrames.front().id);
    }
  }

  if (have_frames) {
    refresh_variables(top_frame_id);
  }
}

void DapBackend::refresh_variables(int frame_id) {
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }

  dap::ScopesRequest scopes_request;
  scopes_request.frameId = frame_id;
  const auto scopes_response = session_->send(scopes_request).get();
  if (scopes_response.error) {
    push_error("scopes falló: " + scopes_response.error.message);
    return;
  }

  std::vector<VariableInfo> variables;
  for (const auto& scope : scopes_response.response.scopes) {
    if (scope.variablesReference <= 0) {
      continue;
    }

    dap::VariablesRequest variables_request;
    variables_request.variablesReference = scope.variablesReference;
    const auto variables_response = session_->send(variables_request).get();
    if (variables_response.error) {
      continue;
    }

    for (const auto& variable : variables_response.response.variables) {
      VariableInfo info;
      info.name = variable.name;
      info.expression = variable.name;
      info.value = variable.value;
      info.depth = 0;
      if (variable.type.has_value()) {
        info.type = *variable.type;
      }
      if (variable.variablesReference > 0) {
        info.variables_reference = static_cast<int>(variable.variablesReference);
      }
      variables.push_back(std::move(info));
    }
  }

  DebugEvent event;
  event.kind = DebugEventKind::kVariablesUpdated;
  event.stack_frame_id = frame_id;
  event.variables = std::move(variables);
  push_event(std::move(event));
}

namespace {

std::string child_expression(const std::string& parent,
                             const std::string& name) {
  if (parent.empty()) {
    return name;
  }
  if (!name.empty() && (name[0] == '[' || name[0] == '.')) {
    return parent + name;
  }
  return parent + "." + name;
}

}  // namespace

void DapBackend::fetch_variable_children(int variables_reference,
                                           const std::string& parent_expression,
                                           int parent_depth) {
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }

  dap::VariablesRequest variables_request;
  variables_request.variablesReference = variables_reference;
  const auto variables_response = session_->send(variables_request).get();
  if (variables_response.error) {
    push_error("variables (hijos) falló: " + variables_response.error.message);
    return;
  }

  std::vector<VariableInfo> variables;
  for (const auto& variable : variables_response.response.variables) {
    VariableInfo info;
    info.name = variable.name;
    info.expression = child_expression(parent_expression, variable.name);
    info.value = variable.value;
    info.depth = parent_depth + 1;
    if (variable.type.has_value()) {
      info.type = *variable.type;
    }
    if (variable.variablesReference > 0) {
      info.variables_reference = static_cast<int>(variable.variablesReference);
    }
    variables.push_back(std::move(info));
  }

  DebugEvent event;
  event.kind = DebugEventKind::kVariableChildrenUpdated;
  event.parent_expression = parent_expression;
  event.variables = std::move(variables);
  push_event(std::move(event));
}

bool DapBackend::pause_inferior_locked() {
  dap::PauseRequest pause;
  pause.threadId = active_thread_id_ > 0 ? active_thread_id_ : 1;
  const auto pause_response = session_->send(pause).get();
  if (!pause_response.error) {
    return true;
  }

  dap::EvaluateRequest interrupt;
  interrupt.expression = "interrupt";
  interrupt.context = "repl";
  const auto interrupt_response = session_->send(interrupt).get();
  return !interrupt_response.error;
}

void DapBackend::notify_stopped(const std::string& reason, int thread_id) {
  inferior_stopped_.store(true, std::memory_order_release);
  DebugEvent event;
  event.kind = DebugEventKind::kStopped;
  event.stop_reason = reason;
  event.thread_id = thread_id > 0 ? thread_id : active_thread_id_;
  push_event(std::move(event));
}

void DapBackend::notify_continued(int thread_id) {
  inferior_stopped_.store(false, std::memory_order_release);
  DebugEvent event;
  event.kind = DebugEventKind::kContinued;
  event.thread_id = thread_id > 0 ? thread_id : active_thread_id_;
  push_event(std::move(event));
}

bool DapBackend::continue_inferior_locked() {
  dap::ContinueRequest request;
  request.threadId = active_thread_id_ > 0 ? active_thread_id_ : 1;
  const auto response = session_->send(request).get();
  if (response.error) {
    return false;
  }
  notify_continued(request.threadId);
  return true;
}

bool DapBackend::verify_inferior_attached_locked() {
  dap::EvaluateRequest request;
  request.expression = "info proc";
  request.context = "repl";
  const auto response = session_->send(request).get();
  if (!response.error) {
    return true;
  }

  push_error(
      "Attach no controla el proceso (bloqueo ptrace de Linux; "
      "yama.ptrace_scope suele ser 1 en Ubuntu).\n"
      "  • Recompila hello y vuelve a lanzarlo: ./tools/launch_hello.sh\n"
      "  • O depura con launch: ./tools/launch.sh ./build/hello\n"
      "  • O temporal: sudo sysctl kernel.yama.ptrace_scope=0");
  return false;
}

void DapBackend::refresh_active_thread_locked() {
  dap::ThreadsRequest request;
  const auto response = session_->send(request).get();
  if (response.error || response.response.threads.empty()) {
    return;
  }
  active_thread_id_ = static_cast<int>(response.response.threads.front().id);
}

void DapBackend::on_inferior_attached() {
  inferior_attached_ = true;
  std::lock_guard<std::mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }

  refresh_active_thread_locked();

  bool stopped = pause_inferior_locked();
  if (!stopped) {
    dap::EvaluateRequest interrupt;
    interrupt.expression = "-exec interrupt";
    interrupt.context = "repl";
    const auto interrupt_response = session_->send(interrupt).get();
    stopped = !interrupt_response.error;
  }

  if (!stopped) {
    inferior_stopped_.store(false, std::memory_order_release);
    push_error(
        "El proceso sigue en ejecución tras attach. "
        "Pausa manualmente (⏸) para inspeccionar el stack.");
    notify_continued();
    return;
  }

  inferior_stopped_.store(true, std::memory_order_release);
  for (const auto& [file, lines] : breakpoints_by_file_) {
    send_breakpoints_locked(file, lines);
  }
  notify_stopped("attach");

  UiCommand refresh;
  refresh.kind = UiCommandKind::kRefreshStack;
  refresh.thread_id = active_thread_id_;
  commands_.push(refresh);
}

void DapBackend::apply_pending_breakpoints_locked() {
  if (!inferior_stopped_.load(std::memory_order_acquire)) {
    return;
  }
  for (const auto& [file, lines] : breakpoints_by_file_) {
    send_breakpoints_locked(file, lines);
  }
  breakpoints_pending_sync_ = false;
  const bool resume = resume_after_breakpoint_sync_;
  resume_after_breakpoint_sync_ = false;
  if (resume) {
    continue_inferior_locked();
  }
}

void DapBackend::send_breakpoints_locked(const std::string& normalized_file,
                                         const std::vector<int>& lines) {
  if (!inferior_stopped_.load(std::memory_order_acquire)) {
    push_error("setBreakpoints: el inferior no está detenido");
    return;
  }

  dap::SetBreakpointsRequest request;
  request.source.path = normalized_file;
  request.source.name =
      std::filesystem::path(normalized_file).filename().string();

  dap::array<dap::SourceBreakpoint> breakpoints;
  for (int line : lines) {
    dap::SourceBreakpoint bp;
    bp.line = line;
    breakpoints.push_back(bp);
  }
  request.breakpoints = breakpoints;

  const auto response = session_->send(request).get();
  if (response.error) {
    push_error("setBreakpoints falló: " + response.error.message);
    return;
  }

  DebugEvent event;
  event.kind = DebugEventKind::kBreakpointsUpdated;
  for (const auto& bp : response.response.breakpoints) {
    BreakpointInfo info;
    info.file = normalized_file;
    if (bp.line.has_value()) {
      info.line = static_cast<int>(*bp.line);
    }
    info.verified = bp.verified;
    if (!bp.verified) {
      if (bp.message.has_value()) {
        info.message = *bp.message;
      } else if (bp.reason.has_value()) {
        info.message = *bp.reason;
      }
    }
    event.breakpoints.push_back(std::move(info));

    DebugEvent log;
    log.kind = DebugEventKind::kOutput;
    if (bp.verified && bp.line.has_value()) {
      log.text = "[breakpoint] instalado en " + normalized_file + ":" +
                 std::to_string(static_cast<int>(*bp.line));
    } else if (bp.line.has_value()) {
      log.text = "[breakpoint] NO verificado en " + normalized_file + ":" +
                 std::to_string(static_cast<int>(*bp.line));
      if (info.message.empty()) {
        log.text += " (¿ruta de fuente distinta a la del binario?)";
      } else {
        log.text += " — " + info.message;
      }
    }
    if (!log.text.empty()) {
      push_event(std::move(log));
    }
  }
  push_event(std::move(event));
}

void DapBackend::update_breakpoints(const std::string& file,
                                    const std::vector<int>& lines) {
  const std::string normalized = normalize_path(file);
  if (lines.empty()) {
    breakpoints_by_file_.erase(normalized);
  } else {
    breakpoints_by_file_[normalized] = lines;
  }

  if (!inferior_attached_) {
    return;
  }

  std::lock_guard<std::mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }

  if (inferior_stopped_.load(std::memory_order_acquire)) {
    breakpoints_pending_sync_ = false;
    resume_after_breakpoint_sync_ = false;
    send_breakpoints_locked(normalized, lines);
    return;
  }

  breakpoints_pending_sync_ = true;
  resume_after_breakpoint_sync_ = true;
  expecting_interrupt_for_breakpoints_ = true;
  if (!pause_inferior_locked()) {
    expecting_interrupt_for_breakpoints_ = false;
    breakpoints_pending_sync_ = false;
    resume_after_breakpoint_sync_ = false;
    push_error(
        "No se pudo interrumpir el proceso para instalar breakpoints. "
        "Pausa manualmente (⏸) e inténtalo de nuevo.");
  }
}

bool DapBackend::send_configuration_done() {
  dap::ConfigurationDoneRequest done;
  const auto done_response = session_->send(done).get();
  if (done_response.error) {
    push_error("configurationDone falló: " + done_response.error.message);
    return false;
  }
  return true;
}

void DapBackend::handle_command(const UiCommand& command) {
  if (command.kind == UiCommandKind::kSetBreakpoints) {
    update_breakpoints(command.breakpoint_file, command.breakpoint_lines);
    return;
  }
  if (command.kind == UiCommandKind::kSyncBreakpoints) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (session_) {
      apply_pending_breakpoints_locked();
    }
    return;
  }
  if (command.kind == UiCommandKind::kRefreshStack) {
    refresh_stack(command.thread_id > 0 ? command.thread_id : active_thread_id_);
    return;
  }
  if (command.kind == UiCommandKind::kFetchVariables) {
    refresh_variables(command.frame_id);
    return;
  }
  if (command.kind == UiCommandKind::kFetchVariableChildren) {
    fetch_variable_children(command.variables_reference, command.parent_expression,
                            command.variable_depth);
    return;
  }
  if (command.kind == UiCommandKind::kAddWatch) {
    UiCommand eval = command;
    eval.kind = UiCommandKind::kEvaluate;
    eval.evaluate_context = EvaluateContext::kWatch;
    handle_command(eval);
    return;
  }

  if (command.kind == UiCommandKind::kLaunch) {
    dap::GdbLaunchRequest launch;
    launch.program = command.launch.program;
    launch.cwd = command.launch.cwd;
    launch.stopAtBeginningOfMainSubprogram =
        dap::boolean(command.launch.stop_at_main);
    if (!command.launch.args.empty()) {
      launch.args = command.launch.args;
    }

    std::unique_lock<std::mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }
    auto launch_future = session_->send(launch);
    if (!send_configuration_done()) {
      return;
    }
    lock.unlock();

    const auto response = launch_future.get();
    bool inferior_started = false;
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      if (!session_) {
        return;
      }
      if (response.error) {
        push_error("launch falló: " + response.error.message);
      } else {
        inferior_started = true;
      }
    }
    if (inferior_started) {
      inferior_launched_ = true;
      on_inferior_attached();
    }
    return;
  }

  if (command.kind == UiCommandKind::kAttach) {
    dap::GdbAttachRequest attach;
    attach.program = command.attach.program;
    if (command.attach.pid > 0) {
      attach.pid = command.attach.pid;
    }
    if (!command.attach.target.empty()) {
      attach.target = command.attach.target;
    }

    std::unique_lock<std::mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }
    auto attach_future = session_->send(attach);
    if (!send_configuration_done()) {
      return;
    }
    lock.unlock();

    const auto attach_response = attach_future.get();
    bool attach_started = false;
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      if (!session_) {
        return;
      }
      if (attach_response.error) {
        push_error("attach falló: " + attach_response.error.message);
      } else if (!verify_inferior_attached_locked()) {
        // ptrace bloqueado: GDB DAP devuelve attach OK pero sin inferior real.
      } else {
        attach_started = true;
      }
    }
    if (attach_started) {
      inferior_launched_ = false;
      on_inferior_attached();
    }
    return;
  }

  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }

    switch (command.kind) {
    case UiCommandKind::kContinue: {
      if (!continue_inferior_locked()) {
        push_error("continue falló");
      }
      break;
    }
    case UiCommandKind::kPause: {
      if (pause_inferior_locked()) {
        notify_stopped("pause");
      } else {
        push_error("pause falló");
      }
      break;
    }
    case UiCommandKind::kNext: {
      dap::NextRequest request;
      request.threadId = command.thread_id > 0 ? command.thread_id
                                               : active_thread_id_;
      const auto response = session_->send(request).get();
      if (response.error) {
        push_error("next falló: " + response.error.message);
      }
      break;
    }
    case UiCommandKind::kStepIn: {
      dap::StepInRequest request;
      request.threadId = command.thread_id > 0 ? command.thread_id
                                               : active_thread_id_;
      const auto response = session_->send(request).get();
      if (response.error) {
        push_error("stepIn falló: " + response.error.message);
      }
      break;
    }
    case UiCommandKind::kStepOut: {
      dap::StepOutRequest request;
      request.threadId = command.thread_id > 0 ? command.thread_id
                                               : active_thread_id_;
      const auto response = session_->send(request).get();
      if (response.error) {
        push_error("stepOut falló: " + response.error.message);
      }
      break;
    }
    case UiCommandKind::kEvaluate: {
      dap::EvaluateRequest request;
      request.expression = command.expression;
      if (command.frame_id >= 0) {
        request.frameId = command.frame_id;
      }
      // GDB DAP: context "repl" trata "x"/"y" como comandos GDB; "watch"+frameId
      // evalúa expresiones del programa (variables locales).
      if (command.evaluate_context == EvaluateContext::kWatch) {
        request.context = "watch";
      } else {
        request.context = "repl";
      }
      const auto response = session_->send(request).get();
      if (response.error) {
        if (command.evaluate_context == EvaluateContext::kWatch) {
          DebugEvent event;
          event.kind = DebugEventKind::kWatchUpdated;
          event.watch_expression = command.expression;
          event.watch_value = "[error] " + response.error.message;
          push_event(std::move(event));
        } else {
          push_error("evaluate falló: " + response.error.message);
        }
        break;
      }
      DebugEvent event;
      if (command.evaluate_context == EvaluateContext::kWatch) {
        event.kind = DebugEventKind::kWatchUpdated;
        event.watch_expression = command.expression;
        event.watch_value = response.response.result;
      } else {
        event.kind = DebugEventKind::kEvaluateResult;
        event.text = response.response.result;
      }
      push_event(std::move(event));
      break;
    }
    case UiCommandKind::kSetWatchValue: {
      dap::SetExpressionRequest request;
      request.expression = command.expression;
      request.value = command.assign_value;
      if (command.frame_id >= 0) {
        request.frameId = command.frame_id;
      }
      const auto response = session_->send(request).get();
      if (response.error) {
        dap::EvaluateRequest fallback;
        fallback.expression =
            "set variable " + command.expression + " = " + command.assign_value;
        fallback.context = "repl";
        if (command.frame_id >= 0) {
          fallback.frameId = command.frame_id;
        }
        const auto fb = session_->send(fallback).get();
        if (fb.error) {
          DebugEvent event;
          event.kind = DebugEventKind::kWatchUpdated;
          event.watch_expression = command.expression;
          event.watch_value = "[error] " + fb.error.message;
          push_event(std::move(event));
          break;
        }
        DebugEvent event;
        event.kind = DebugEventKind::kWatchUpdated;
        event.watch_expression = command.expression;
        event.watch_value = command.assign_value;
        push_event(std::move(event));
      } else {
        DebugEvent event;
        event.kind = DebugEventKind::kWatchUpdated;
        event.watch_expression = command.expression;
        event.watch_value = response.response.value;
        push_event(std::move(event));
      }
      if (command.frame_id >= 0) {
        UiCommand refresh;
        refresh.kind = UiCommandKind::kFetchVariables;
        refresh.frame_id = command.frame_id;
        commands_.push(refresh);
      }
      break;
    }
    case UiCommandKind::kDisconnect: {
      dap::DisconnectRequest request;
      request.terminateDebuggee = true;
      session_->send(request).get();
      inferior_attached_ = false;
      break;
    }
    case UiCommandKind::kDetach: {
      dap::DisconnectRequest request;
      request.terminateDebuggee = false;
      session_->send(request).get();
      inferior_attached_ = false;
      inferior_stopped_.store(false, std::memory_order_release);
      break;
    }
    default:
      break;
  }
  }
}

void DapBackend::worker_main() {
  gdb_ = std::make_unique<GdbProcess>();
  if (!gdb_->start()) {
    push_error("No se pudo iniciar gdb -i=dap");
    running_ = false;
    return;
  }

  setup_session();
  session_->bind(gdb_->reader(), gdb_->writer(), [&] {
    DebugEvent event;
    event.kind = DebugEventKind::kTerminated;
    event.text = "Conexión DAP cerrada";
    push_event(std::move(event));
  });

  if (!initialize_session()) {
    gdb_->stop();
    running_ = false;
    return;
  }

  while (true) {
    auto command = commands_.wait_pop();
    if (!command.has_value()) {
      break;
    }
    if (command->kind == UiCommandKind::kQuit) {
      break;
    }
    handle_command(*command);
  }

  if (session_ && gdb_ && gdb_->running() && inferior_attached_) {
    dap::DisconnectRequest disconnect;
    disconnect.terminateDebuggee = inferior_launched_;
    session_->send(disconnect).get();
    inferior_attached_ = false;
  }
  if (gdb_) {
    gdb_->stop();
  }
  session_.reset();
  gdb_.reset();
}

}  // namespace tgdb
