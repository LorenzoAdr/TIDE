#include "backend/dap_backend.hpp"

#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <regex>
#include <sstream>
#include <utility>

#include "dap/gdb_protocol.hpp"
#include "dap/debug_adapter_process.hpp"
#include "dap/debug_adapter_spec.hpp"
#include "dap/protocol.h"
#include "dap/session.h"
#include "i18n/tr.hpp"
#include "packet_monitor/pkt_preload_path.hpp"
#include "util/path_normalize.hpp"
#include "util/monitor_log.hpp"
#include "util/shell_utils.hpp"
#include "util/thread_name.hpp"
#include "util/bundled_tools.hpp"

#include <array>

namespace tuide {

namespace {


const char* ui_command_kind_name(UiCommandKind kind) {
  switch (kind) {
    case UiCommandKind::kConnect:
      return "connect";
    case UiCommandKind::kLaunch:
      return "launch";
    case UiCommandKind::kAttach:
      return "attach";
    case UiCommandKind::kLoadCore:
      return "load_core";
    case UiCommandKind::kContinue:
      return "continue";
    case UiCommandKind::kPause:
      return "pause";
    case UiCommandKind::kNext:
      return "next";
    case UiCommandKind::kStepIn:
      return "step_in";
    case UiCommandKind::kStepOut:
      return "step_out";
    case UiCommandKind::kEvaluate:
      return "evaluate";
    case UiCommandKind::kSetBreakpoints:
      return "set_breakpoints";
    case UiCommandKind::kSyncBreakpoints:
      return "sync_breakpoints";
    case UiCommandKind::kRefreshStack:
      return "refresh_stack";
    case UiCommandKind::kFetchVariables:
      return "fetch_variables";
    case UiCommandKind::kFetchVariableChildren:
      return "fetch_variable_children";
    case UiCommandKind::kAddWatch:
      return "add_watch";
    case UiCommandKind::kSetWatchValue:
      return "set_watch_value";
    case UiCommandKind::kAddHardwareWatch:
      return "add_hardware_watch";
    case UiCommandKind::kRemoveHardwareWatch:
      return "remove_hardware_watch";
    case UiCommandKind::kSetHardwareWatchEnabled:
      return "set_hardware_watch_enabled";
    case UiCommandKind::kDisconnect:
      return "disconnect";
    case UiCommandKind::kDetach:
      return "detach";
    case UiCommandKind::kSetSourceSubstitutePath:
      return "set_source_substitute_path";
    case UiCommandKind::kQuit:
      return "quit";
  }
  return "unknown";
}

std::string glibc_debug_directory_paths() {
  static constexpr const char* kCandidates[] = {
      "/usr/lib/debug",
      "/lib/debug",
  };
  std::string paths;
  for (const char* candidate : kCandidates) {
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec) || ec) {
      continue;
    }
    if (!paths.empty()) {
      paths += ':';
    }
    paths += candidate;
  }
  return paths;
}

std::optional<int> parse_watchpoint_number(const std::string& output) {
  static const std::regex kPattern(R"((?:Hardware\s+)?[Ww]atchpoint\s+(\d+))");
  std::smatch match;
  if (std::regex_search(output, match, kPattern) && match.size() > 1) {
    return std::stoi(match[1].str());
  }
  return std::nullopt;
}

// GDB 15 (Ubuntu) runs the inferior on the launch request itself. GDB 16+ / our
// bundled patch defers run/start until configurationDone.
int probe_gdb_major_version(const std::string& gdb_path) {
  if (gdb_path.empty()) {
    return 0;
  }
  const std::string cmd = shell_quote(gdb_path) + " --version 2>/dev/null";
  FILE* pipe = ::popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return 0;
  }
  std::array<char, 256> buffer{};
  std::string first_line;
  if (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    first_line = buffer.data();
  }
  ::pclose(pipe);
  static const std::regex kVersion(R"((\d+)\.\d+)");
  std::smatch match;
  if (std::regex_search(first_line, match, kVersion) && match.size() > 1) {
    return std::stoi(match[1].str());
  }
  return 0;
}

bool gdb_defers_launch_until_configuration_done() {
  const auto location = resolve_gdb();
  if (!location.has_value()) {
    return false;
  }
  if (location->source == GdbLocation::Source::Bundled) {
    return true;
  }
  return probe_gdb_major_version(location->binary_path) >= 16;
}

}  // namespace

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
  worker_ = std::thread([this] {
    set_current_thread_name("dap-wrk");
    worker_main();
  });
}

void DapBackend::stop() {
  // Kill the adapter *before* joining the worker. Otherwise the UI thread
  // deadlocks: join waits for session_->send(...).get(), which waits for the
  // adapter, which is only killed after join.
  {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    wake_callback_ = nullptr;
  }
  // Invalidate bind onClose before tearing pipes so a late callback cannot
  // publish kTerminated into a newly constructed DapBackend at the same address.
  if (session_close_guard_) {
    session_close_guard_->store(false, std::memory_order_release);
  }
  running_.store(false, std::memory_order_release);
  dap_initialized_cv_.notify_all();

  // Only close the shared command queue once per live worker/adapter. A second
  // stop() from ~DapBackend after Application already reset() the queue must not
  // flip closed_ back to true.
  const bool close_commands = worker_.joinable() || static_cast<bool>(adapter_);

  if (adapter_) {
    adapter_->stop(true);
  }

  if (close_commands) {
    commands_.close();
  }

  if (worker_.joinable()) {
    worker_.join();
  }

  {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    session_.reset();
  }
  adapter_.reset();
  session_close_guard_.reset();
}

void DapBackend::submit(const UiCommand& command) {
  commands_.push(command);
}

void DapBackend::set_wake_callback(DebugWakeCallback callback) {
  std::lock_guard<std::mutex> lock(wake_mutex_);
  wake_callback_ = std::move(callback);
}

void DapBackend::push_event(DebugEvent event) {
  event.backend_epoch = backend_epoch_.load(std::memory_order_acquire);
  const DebugEventKind kind = event.kind;
  events_.push(std::move(event));
  // kContinued must wake the UI so drain_events can apply set_running() and
  // clear the stopped-line highlight / play→pause toolbar state.
  // kStackUpdated must also wake: stackTrace is async and usually arrives after
  // the kStopped drain/paint cycle; without a wake the editor keeps a stale
  // active_line (no ► / highlight) until the next user keypress.
  if (kind == DebugEventKind::kStopped || kind == DebugEventKind::kTerminated ||
      kind == DebugEventKind::kSessionReady || kind == DebugEventKind::kLaunchConfigured ||
      kind == DebugEventKind::kContinued || kind == DebugEventKind::kError ||
      kind == DebugEventKind::kStackUpdated) {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    if (wake_callback_) {
      wake_callback_(kind);
    }
  }
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
    {
      std::lock_guard<std::mutex> lock(dap_initialized_mutex_);
      dap_initialized_event_ = true;
    }
    dap_initialized_cv_.notify_all();

    // GDB sends initialized after initialize; debugpy/bashdb send it after launch
    // (SessionReady for those is emitted from initialize_session instead).
    if (adapter_is_gdb()) {
      DebugEvent event;
      event.kind = DebugEventKind::kSessionReady;
      event.text = i18n::tr("debug.dap.session_ready");
      push_event(std::move(event));
    }
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
        if (!expecting_stop_after_pause_) {
          return;
        }
        expecting_stop_after_pause_ = false;
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

  session_->registerHandler([&](const dap::ExitedEvent& e) {
    inferior_attached_ = false;
    inferior_launched_ = false;
    inferior_stopped_.store(false, std::memory_order_release);
    last_exit_code_ = static_cast<int>(e.exitCode);
  });

  session_->registerHandler([&](const dap::GdbTerminatedEvent&) {
    inferior_attached_ = false;
    inferior_launched_ = false;
    inferior_stopped_.store(false, std::memory_order_release);
    DebugEvent event;
    event.kind = DebugEventKind::kTerminated;
    if (last_exit_code_ >= 0) {
      event.text = i18n::tr_fmt("debug.dap.process_exited",
                                {std::to_string(last_exit_code_)});
      last_exit_code_ = -1;
    }
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

  // GDB envía estos eventos durante launch/attach; ignorarlos evita ruido en consola.
  session_->registerHandler([&](const dap::ModuleEvent&) {});
  session_->registerHandler([&](const dap::LoadedSourceEvent&) {});
  session_->registerHandler([&](const dap::CapabilitiesEvent&) {});
  session_->registerHandler([&](const dap::ProcessEvent& e) {
    if (e.systemProcessId.has_value()) {
      emit_inferior_pid(static_cast<int>(*e.systemProcessId));
    }
  });
  session_->registerHandler([&](const dap::ProgressStartEvent&) {});
  session_->registerHandler([&](const dap::ProgressUpdateEvent&) {});
  session_->registerHandler([&](const dap::ProgressEndEvent&) {});
  session_->registerHandler([&](const dap::InvalidatedEvent&) {});
  session_->registerHandler([&](const dap::MemoryEvent&) {});
  session_->registerHandler([&](const dap::DebugpyWaitingForServerEvent&) {});

  session_->onError([&](const char* message) {
    push_error(i18n::tr_fmt("debug.dap.error",
                            {message != nullptr ? message : "unknown"}));
  });
}

bool DapBackend::adapter_is_gdb() const {
  return adapter_ != nullptr && adapter_->kind() == DebugAdapterKind::kGdb;
}

bool DapBackend::adapter_is_debugpy() const {
  return adapter_ != nullptr && adapter_->kind() == DebugAdapterKind::kDebugpy;
}

bool DapBackend::adapter_is_bashdb() const {
  return adapter_ != nullptr && adapter_->kind() == DebugAdapterKind::kBashdb;
}

void DapBackend::set_preferred_adapter(DebugAdapterKind kind) {
  preferred_adapter_ = kind;
}

bool DapBackend::initialize_session() {
  dap::InitializeRequest request;
  request.adapterID = adapter_ ? adapter_->adapter_id() : std::string(kDapAdapterGdb);
  request.clientID = "tuide";
  request.clientName = "tuide";
  request.linesStartAt1 = true;
  request.columnsStartAt1 = true;
  request.pathFormat = "path";
  request.supportsRunInTerminalRequest = dap::boolean(false);


  // Never use bare .get(): if the adapter dies (e.g. missing node_modules) the
  // future never completes and cancel deadlocks on worker join.
  auto init_future = session_->send(request);
  constexpr int kTimeoutMs = 8000;
  constexpr int kSliceMs = 100;
  int waited = 0;
  while (init_future.wait_for(std::chrono::milliseconds(kSliceMs)) != dap::future_status::ready) {
    waited += kSliceMs;
    if (!running_.load(std::memory_order_acquire)) {
      return false;
    }
    if (adapter_ && !adapter_->process_alive()) {
      if (preferred_adapter_ == DebugAdapterKind::kBashdb) {
        push_error(i18n::tr("debug.dap.bashdb_start_failed"));
      } else if (preferred_adapter_ == DebugAdapterKind::kDebugpy) {
        push_error(i18n::tr("debug.dap.debugpy_start_failed"));
      } else {
        push_error(i18n::tr("debug.dap.gdb_start_failed"));
      }
      return false;
    }
    if (waited >= kTimeoutMs) {
      push_error(i18n::tr_fmt("debug.dap.initialize_failed", {"timeout"}));
      return false;
    }
  }

  const auto response = init_future.get();
  if (response.error) {
    push_error(i18n::tr_fmt("debug.dap.initialize_failed",
                            {response.error.message}));
    return false;
  }

  // debugpy and bashdb send DAP "initialized" only after receiving launch/attach
  // (non-spec; see debugpy adapter clients.py / vscode-bash-debug). Waiting for
  // that event before launch deadlocks. GDB still signals SessionReady from the
  // InitializedEvent handler.
  if (!adapter_is_gdb()) {
    DebugEvent event;
    event.kind = DebugEventKind::kSessionReady;
    event.text = i18n::tr("debug.dap.session_ready");
    push_event(std::move(event));
  }
  return true;
}

void DapBackend::refresh_stack(int thread_id) {
  int top_frame_id = 0;
  bool have_frames = false;
  {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }
    // debugpy may answer stackTrace while running, but scopes/variables often
    // hang. Skip the whole refresh if we already resumed.
    if (!inferior_stopped_.load(std::memory_order_acquire)) {
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
      if (response.error.message.find("not stopped") != std::string::npos) {
        return;
      }
      push_error(i18n::tr_fmt("debug.dap.stack_trace_failed",
                              {response.error.message}));
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

  if (have_frames && inferior_stopped_.load(std::memory_order_acquire)) {
    refresh_variables(top_frame_id);
  }
}

void DapBackend::refresh_variables(int frame_id) {
  std::lock_guard<std::recursive_mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }
  if (!inferior_stopped_.load(std::memory_order_acquire)) {
    return;
  }

  dap::ScopesRequest scopes_request;
  scopes_request.frameId = frame_id;
  const auto scopes_response = session_->send(scopes_request).get();
  if (scopes_response.error) {
    push_error(i18n::tr_fmt("debug.dap.scopes_failed",
                            {scopes_response.error.message}));
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
  std::lock_guard<std::recursive_mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }

  dap::VariablesRequest variables_request;
  variables_request.variablesReference = variables_reference;
  const auto variables_response = session_->send(variables_request).get();
  if (variables_response.error) {
    push_error(i18n::tr_fmt("debug.dap.variables_children_failed",
                            {variables_response.error.message}));
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
  if (!session_) {
    return false;
  }
  dap::ContinueRequest request;
  request.threadId = active_thread_id_ > 0 ? active_thread_id_ : 1;
  // Mark running before waiting so a queued RefreshStack/FetchVariables that
  // races after Play does not block the worker on hung scopes (debugpy).
  inferior_stopped_.store(false, std::memory_order_release);
  const auto response = session_->send(request).get();
  if (response.error) {
    // GDB responde notStopped si el inferior ya está en ejecución.
    if (response.error.message.find("notStopped") != std::string::npos) {
      notify_continued(request.threadId);
      return true;
    }
    inferior_stopped_.store(true, std::memory_order_release);
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

  push_error(i18n::tr("debug.dap.attach_ptrace_blocked"));
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

void DapBackend::on_inferior_launched() {
  inferior_attached_ = true;
  std::vector<std::pair<std::string, std::vector<int>>> pending_bps;
  {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }

    refresh_active_thread_locked();

    // "info proc" is GDB-only; debugpy gets the PID from ProcessEvent.
    if (adapter_is_gdb() && reported_inferior_pid_.load(std::memory_order_acquire) <= 0) {
      emit_inferior_pid(fetch_inferior_pid_locked(true));
    }

    // debugpy/bashdb/GDB launch install breakpoints before configurationDone.
    if (!adapter_is_gdb()) {
      return;
    }

    if (inferior_stopped_.load(std::memory_order_acquire)) {
      pending_bps.assign(breakpoints_by_file_.begin(), breakpoints_by_file_.end());
    } else if (!breakpoints_by_file_.empty() && !configuration_done_) {
      // Only defer when BPs were not part of the pre-run DAP configuration.
      breakpoints_pending_sync_ = true;
    }
  }
  for (const auto& [file, lines] : pending_bps) {
    send_breakpoints_locked(file, lines);
  }
}

void DapBackend::on_inferior_attached() {
  inferior_attached_ = true;
  std::vector<std::pair<std::string, std::vector<int>>> pending_bps;
  {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }

    refresh_active_thread_locked();

    if (reported_inferior_pid_.load(std::memory_order_acquire) <= 0) {
      emit_inferior_pid(fetch_inferior_pid_locked(true));
    }

    expecting_stop_after_pause_ = true;
    bool stopped = pause_inferior_locked();
    if (!stopped) {
      dap::EvaluateRequest interrupt;
      interrupt.expression = "-exec interrupt";
      interrupt.context = "repl";
      const auto interrupt_response = session_->send(interrupt).get();
      stopped = !interrupt_response.error;
    }
    if (!stopped) {
      expecting_stop_after_pause_ = false;
      inferior_stopped_.store(false, std::memory_order_release);
      push_error(i18n::tr("debug.dap.attach_still_running"));
      notify_continued();
      return;
    }

    inferior_stopped_.store(true, std::memory_order_release);
    pending_bps.assign(breakpoints_by_file_.begin(), breakpoints_by_file_.end());
    notify_stopped("attach");
  }
  for (const auto& [file, lines] : pending_bps) {
    send_breakpoints_locked(file, lines);
  }
}

void DapBackend::apply_pending_breakpoints_locked() {
  if (!inferior_stopped_.load(std::memory_order_acquire)) {
    return;
  }
  std::vector<std::pair<std::string, std::vector<int>>> pending(
      breakpoints_by_file_.begin(), breakpoints_by_file_.end());
  breakpoints_pending_sync_ = false;
  const bool resume = resume_after_breakpoint_sync_;
  resume_after_breakpoint_sync_ = false;
  // Caller may hold session_mutex_; unlock is the caller's responsibility before
  // this if they need send(). Prefer copying then sending without the lock.
  for (const auto& [file, lines] : pending) {
    send_breakpoints_locked(file, lines);
  }
  if (resume) {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (session_) {
      continue_inferior_locked();
    }
  }
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

  bool send_now = false;
  {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }

    if (inferior_stopped_.load(std::memory_order_acquire)) {
      breakpoints_pending_sync_ = false;
      resume_after_breakpoint_sync_ = false;
      send_now = true;
    } else {
      breakpoints_pending_sync_ = true;
      resume_after_breakpoint_sync_ = true;
      expecting_interrupt_for_breakpoints_ = true;
      if (!pause_inferior_locked()) {
        expecting_interrupt_for_breakpoints_ = false;
        breakpoints_pending_sync_ = false;
        resume_after_breakpoint_sync_ = false;
        push_error(i18n::tr("debug.dap.interrupt_for_breakpoints_failed"));
      }
    }
  }
  if (send_now) {
    send_breakpoints_locked(normalized, lines);
  }
}

void DapBackend::send_breakpoints_locked(const std::string& normalized_file,
                                         const std::vector<int>& lines,
                                         bool require_stopped) {
  if (require_stopped && !inferior_stopped_.load(std::memory_order_acquire)) {
    push_error(i18n::tr("debug.dap.set_breakpoints_not_stopped"));
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

  // Must not hold session_mutex_ across .get(): bash setBreakpoints waits on
  // debugger I/O and may emit events that also take the mutex.
  std::unique_lock<std::recursive_mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }
  auto response_future = session_->send(request);
  lock.unlock();
  const auto response = response_future.get();
  if (response.error) {
    push_error(i18n::tr_fmt("debug.dap.set_breakpoints_failed",
                            {response.error.message}));
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

    DebugEvent log;
    log.kind = DebugEventKind::kOutput;
    if (bp.verified && bp.line.has_value()) {
      log.text = i18n::tr_fmt("debug.dap.breakpoint_installed",
                              {normalized_file,
                               std::to_string(static_cast<int>(*bp.line))});
    } else if (bp.line.has_value()) {
      log.text = i18n::tr_fmt("debug.dap.breakpoint_not_verified",
                              {normalized_file,
                               std::to_string(static_cast<int>(*bp.line))});
      if (info.message.empty()) {
        log.text += i18n::tr("debug.dap.breakpoint_source_path_hint");
      } else {
        log.text += i18n::tr_fmt("debug.dap.breakpoint_message_suffix",
                                 {info.message});
      }
    }
    if (!log.text.empty()) {
      push_event(std::move(log));
    }
    event.breakpoints.push_back(std::move(info));
  }
  push_event(std::move(event));
}

bool DapBackend::wait_for_initialized_event(int timeout_ms) {
  std::unique_lock<std::mutex> lock(dap_initialized_mutex_);
  return dap_initialized_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
    return dap_initialized_event_ || !running_.load(std::memory_order_acquire);
  }) && dap_initialized_event_;
}

bool DapBackend::finish_late_configuration_unlocked(bool send_configuration_done) {
  // Caller must NOT hold session_mutex_. Late DAP sequence for debugpy/bashdb.
  std::vector<std::pair<std::string, std::vector<int>>> pending;
  {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return false;
    }
    pending.assign(breakpoints_by_file_.begin(), breakpoints_by_file_.end());
  }
  for (const auto& [file, lines] : pending) {
    // Do not hold session_mutex_ across send().get(): bash setBreakpoints is
    // async and may emit events whose handlers also need the mutex.
    {
      std::lock_guard<std::recursive_mutex> lock(session_mutex_);
      if (!session_) {
        return false;
      }
    }
    send_breakpoints_locked(file, lines, /*require_stopped=*/false);
  }

  if (!send_configuration_done) {
    // vscode-bash-debug: supportsConfigurationDoneRequest = false.
    breakpoints_pending_sync_ = false;
    return true;
  }

  // configurationDone must not run while session_mutex_ is held: debugpy responds
  // to the pending launch and emits process/thread/stopped in the same turn.
  std::unique_lock<std::recursive_mutex> lock(session_mutex_);
  if (!session_) {
    return false;
  }
  if (configuration_done_) {
    return true;
  }
  breakpoints_pending_sync_ = false;
  dap::ConfigurationDoneRequest done;
  auto done_future = session_->send(done);
  lock.unlock();

  const auto done_response = done_future.get();
  if (done_response.error) {
    push_error(i18n::tr_fmt("debug.dap.configuration_done_failed",
                            {done_response.error.message}));
    return false;
  }
  {
    std::lock_guard<std::recursive_mutex> done_lock(session_mutex_);
    configuration_done_ = true;
  }
  return true;
}

bool DapBackend::launch_debugpy(const UiCommand& command) {
  // debugpy sequence (non-spec): initialize → launch → initialized event →
  // setBreakpoints → configurationDone → launch response.
  {
    std::lock_guard<std::mutex> lock(dap_initialized_mutex_);
    dap_initialized_event_ = false;
  }
  configuration_done_ = false;

  dap::GdbLaunchRequest launch;
  launch.program = command.launch.program;
  launch.cwd = command.launch.cwd;
  if (!command.launch.args.empty()) {
    launch.args = command.launch.args;
  }
  // Avoid runInTerminal reverse-requests (tuide has no handler); keep DAP stdio clean.
  launch.console = dap::string("internalConsole");
  if (command.launch.stop_at_main) {
    launch.stopOnEntry = dap::boolean(true);
  }

  std::unique_lock<std::recursive_mutex> lock(session_mutex_);
  if (!session_) {
    return false;
  }
  auto launch_future = session_->send(launch);
  lock.unlock();

  if (!wait_for_initialized_event(15000)) {
    push_error(i18n::tr("debug.dap.initialize_failed"));
    return false;
  }

  if (!finish_late_configuration_unlocked(/*send_configuration_done=*/true)) {
    return false;
  }

  const auto response = launch_future.get();
  if (response.error) {
    push_error(i18n::tr_fmt("debug.dap.launch_failed", {response.error.message}));
    return false;
  }
  inferior_launched_ = true;
  on_inferior_launched();
  if (!inferior_stopped_.load(std::memory_order_acquire)) {
    notify_continued();
  }
  {
    DebugEvent configured;
    configured.kind = DebugEventKind::kLaunchConfigured;
    configured.text = i18n::tr("debug.dap.session_ready");
    push_event(std::move(configured));
  }
  return true;
}

bool DapBackend::launch_bashdb(const UiCommand& command) {
  // vscode-bash-debug: initialize → launch → (launch response) → InitializedEvent.
  // Unlike debugpy, supportsConfigurationDoneRequest is false.
  const auto bash_loc = resolve_bash_debug_adapter();
  if (!bash_loc.has_value()) {
    push_error(i18n::tr("debug.dap.bashdb_start_failed"));
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(dap_initialized_mutex_);
    dap_initialized_event_ = false;
  }
  configuration_done_ = false;

  dap::BashdbLaunchRequest launch;
  launch.program = command.launch.program;
  launch.cwd = command.launch.cwd;
  // extension.ts resolveDebugConfiguration defaults — Object.keys(env) crashes if missing.
  launch.args = command.launch.args;
  launch.argsString = "";
  launch.env = dap::object{};
  launch.pathBash = bash_loc->bash_path;
  launch.pathBashdb = bash_loc->bashdb_path;
  launch.pathBashdbLib = bash_loc->bashdb_lib_path;
  launch.pathCat = "/bin/cat";
  launch.pathMkfifo = "/usr/bin/mkfifo";
  launch.pathPkill = "/usr/bin/pkill";
  launch.terminalKind = "debugConsole";

  std::unique_lock<std::recursive_mutex> lock(session_mutex_);
  if (!session_) {
    return false;
  }
  auto launch_future = session_->send(launch);
  lock.unlock();

  if (!wait_for_initialized_event(15000)) {
    if (!running_.load(std::memory_order_acquire)) {
      return false;
    }
    push_error(i18n::tr("debug.dap.initialize_failed"));
    return false;
  }

  // Breakpoints only — do not send configurationDone (unsupported by bash-debug).
  if (!finish_late_configuration_unlocked(/*send_configuration_done=*/false)) {
    return false;
  }

  const auto response = launch_future.get();
  if (response.error) {
    push_error(i18n::tr_fmt("debug.dap.launch_failed", {response.error.message}));
    return false;
  }
  inferior_launched_ = true;
  on_inferior_launched();
  {
    DebugEvent configured;
    configured.kind = DebugEventKind::kLaunchConfigured;
    configured.text = i18n::tr("debug.dap.session_ready");
    push_event(std::move(configured));
  }
  return true;
}

bool DapBackend::send_configuration_done() {
  if (configuration_done_) {
    return true;
  }
  if (!session_) {
    return false;
  }
  dap::ConfigurationDoneRequest done;
  const auto done_response = session_->send(done).get();
  if (done_response.error) {
    push_error(i18n::tr_fmt("debug.dap.configuration_done_failed",
                            {done_response.error.message}));
    return false;
  }
  configuration_done_ = true;
  return true;
}

bool DapBackend::exec_repl_locked(const std::string& gdb_command, bool emit_output) {
  std::string output;
  if (!exec_repl_capture_locked(gdb_command, &output)) {
    return false;
  }
  if (emit_output && !output.empty()) {
    DebugEvent event;
    event.kind = DebugEventKind::kOutput;
    event.text = output;
    push_event(std::move(event));
  }
  return true;
}

bool DapBackend::exec_repl_capture_locked(const std::string& gdb_command, std::string* output,
                                          bool silent) {
  dap::EvaluateRequest request;
  request.expression = gdb_command;
  request.context = "repl";
  const auto response = session_->send(request).get();
  if (response.error) {
    if (!silent) {
      push_error(i18n::tr_fmt("debug.dap.gdb", {response.error.message}));
    }
    return false;
  }
  if (output != nullptr) {
    *output = response.response.result;
  }
  return true;
}

void DapBackend::emit_inferior_pid(int pid) {
  if (pid <= 0) {
    return;
  }
  const int previous = reported_inferior_pid_.exchange(pid, std::memory_order_acq_rel);
  if (previous == pid) {
    return;
  }
  DebugEvent event;
  event.kind = DebugEventKind::kInferiorPid;
  event.inferior_pid = pid;
  push_event(std::move(event));
}

bool DapBackend::configure_packet_monitor_env_locked(const LaunchConfig& launch) {
  if (!launch.packet_monitor_enabled) {
    exec_repl_locked("unset environment LD_PRELOAD", false);
    exec_repl_locked("unset environment TUIDE_PKT_FILTER_SRC", false);
    exec_repl_locked("unset environment TUIDE_PKT_FILTER_DST", false);
    return true;
  }

  const std::string preload_path = packet_monitor::resolve_preload_library_path();
  if (preload_path.empty()) {
    push_error(i18n::tr("packet_monitor.preload_missing"));
    return false;
  }

  if (!exec_repl_locked("set environment LD_PRELOAD " + preload_path, false)) {
    return false;
  }
  if (!launch.packet_monitor_filter_src.empty()) {
    if (!exec_repl_locked("set environment TUIDE_PKT_FILTER_SRC " +
                              launch.packet_monitor_filter_src,
                          false)) {
      return false;
    }
  } else {
    exec_repl_locked("unset environment TUIDE_PKT_FILTER_SRC", false);
  }
  if (!launch.packet_monitor_filter_dst.empty()) {
    if (!exec_repl_locked("set environment TUIDE_PKT_FILTER_DST " +
                              launch.packet_monitor_filter_dst,
                          false)) {
      return false;
    }
  } else {
    exec_repl_locked("unset environment TUIDE_PKT_FILTER_DST", false);
  }
  return true;
}

int DapBackend::fetch_inferior_pid_locked(bool silent) {
  std::string output;
  if (!exec_repl_capture_locked("info proc", &output, silent)) {
    return 0;
  }
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream row(line);
    std::string token;
    row >> token;
    if (token == "process" || token == "proceso") {
      int pid = 0;
      if (row >> pid) {
        return pid;
      }
    }
  }
  return 0;
}

bool DapBackend::configure_glibc_debug_symbols_locked() {
  const std::string paths = glibc_debug_directory_paths();
  if (paths.empty()) {
    return true;
  }
  return exec_repl_locked("set debug-file-directory " + paths, false);
}

void DapBackend::on_inferior_core_loaded() {
  inferior_attached_ = true;
  inferior_launched_ = false;
  inferior_stopped_.store(true, std::memory_order_release);
  std::lock_guard<std::recursive_mutex> lock(session_mutex_);
  if (!session_) {
    return;
  }
  refresh_active_thread_locked();
  notify_stopped("core");
  UiCommand refresh;
  refresh.kind = UiCommandKind::kRefreshStack;
  refresh.thread_id = active_thread_id_;
  commands_.push(refresh);
}

void DapBackend::handle_command(const UiCommand& command) {
  std::ostringstream scope_name;
  scope_name << "handle_command kind=" << ui_command_kind_name(command.kind);
  monitor_log::MonitorScope command_scope("dap", scope_name.str());

  if (command.kind == UiCommandKind::kSetBreakpoints) {
    update_breakpoints(command.breakpoint_file, command.breakpoint_lines);
    return;
  }
  if (command.kind == UiCommandKind::kSyncBreakpoints) {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (session_) {
      apply_pending_breakpoints_locked();
    }
    return;
  }
  if (command.kind == UiCommandKind::kSetSourceSubstitutePath) {
    const std::string from = normalize_path(command.substitute_from);
    const std::string to = normalize_path(command.substitute_to);
    if (from.empty() || to.empty()) {
      push_error(i18n::tr("debug.source_substitute.invalid_paths"));
      return;
    }
    std::vector<std::pair<std::string, std::vector<int>>> pending_bps;
    {
      std::lock_guard<std::recursive_mutex> lock(session_mutex_);
      if (!session_) {
        push_error(i18n::tr("debug.source_substitute.no_session"));
        return;
      }
      const std::string gdb_cmd =
          "set substitute-path " + shell_quote(from) + " " + shell_quote(to);
      if (!exec_repl_locked(gdb_cmd, true)) {
        push_error(i18n::tr("debug.source_substitute.failed"));
        return;
      }
      DebugEvent applied;
      applied.kind = DebugEventKind::kOutput;
      applied.text = i18n::tr_fmt("debug.source_substitute.applied", {from, to});
      push_event(std::move(applied));
      if (inferior_attached_ && inferior_stopped_.load(std::memory_order_acquire)) {
        pending_bps.assign(breakpoints_by_file_.begin(), breakpoints_by_file_.end());
      }
    }
    for (const auto& [file, lines] : pending_bps) {
      send_breakpoints_locked(file, lines);
    }
    if (!pending_bps.empty()) {
      refresh_stack(active_thread_id_);
    }
    return;
  }
  if (command.kind == UiCommandKind::kRefreshStack) {
    refresh_stack(command.thread_id > 0 ? command.thread_id : active_thread_id_);
    return;
  }
  if (command.kind == UiCommandKind::kFetchVariables) {
    if (!inferior_stopped_.load(std::memory_order_acquire)) {
      return;
    }
    refresh_variables(command.frame_id);
    return;
  }
  if (command.kind == UiCommandKind::kFetchVariableChildren) {
    if (!inferior_stopped_.load(std::memory_order_acquire)) {
      return;
    }
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
  if (command.kind == UiCommandKind::kAddHardwareWatch ||
      command.kind == UiCommandKind::kRemoveHardwareWatch ||
      command.kind == UiCommandKind::kSetHardwareWatchEnabled) {
    if (!adapter_is_gdb()) {
      push_error(i18n::tr("debug.dap.hardware_watch_gdb_only"));
      return;
    }
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }
    if (command.kind == UiCommandKind::kAddHardwareWatch) {
      if (!inferior_stopped_.load(std::memory_order_acquire)) {
        push_error(i18n::tr("debug.dap.hardware_watch_requires_stop"));
        return;
      }
      std::string output;
      const std::string gdb_cmd = "watch " + command.expression;
      if (!exec_repl_capture_locked(gdb_cmd, &output)) {
        return;
      }
      if (!output.empty()) {
        DebugEvent out;
        out.kind = DebugEventKind::kOutput;
        out.text = output;
        push_event(std::move(out));
      }
      DebugEvent event;
      event.kind = DebugEventKind::kHardwareWatchUpdated;
      event.hardware_watch_index = command.hardware_watch_index;
      if (const auto num = parse_watchpoint_number(output)) {
        event.hardware_watch_gdb_number = *num;
      }
      push_event(std::move(event));
      return;
    }
    if (command.kind == UiCommandKind::kRemoveHardwareWatch) {
      if (command.hardware_watch_gdb_number > 0) {
        exec_repl_locked("delete " + std::to_string(command.hardware_watch_gdb_number), true);
      }
      return;
    }
    if (command.hardware_watch_gdb_number > 0) {
      const std::string action =
          command.hardware_watch_enabled ? "enable" : "disable";
      exec_repl_locked(action + " " + std::to_string(command.hardware_watch_gdb_number),
                       true);
    }
    return;
  }

  if (command.kind == UiCommandKind::kLaunch) {
    // Route by the *running* adapter, not preferred_adapter_ alone — a stale
    // preferred flag must not send GDB's early configurationDone to debugpy.
    if (adapter_is_debugpy()) {
      launch_debugpy(command);
      return;
    }
    if (adapter_is_bashdb()) {
      launch_bashdb(command);
      return;
    }

    dap::GdbLaunchRequest launch;
    launch.program = command.launch.program;
    launch.cwd = command.launch.cwd;
    const bool stop_at_main = command.launch.stop_at_main;
    const bool deferred_launch = gdb_defers_launch_until_configuration_done();
    if (adapter_is_gdb()) {
      launch.stopAtBeginningOfMainSubprogram = dap::boolean(stop_at_main);
    }
    if (!command.launch.args.empty()) {
      launch.args = command.launch.args;
    }

    // Three GDB launch shapes:
    // 1) Deferred (GDB 16+/bundled): launch → setBreakpoints → configurationDone
    //    (configurationDone actually starts the inferior).
    // 2) Immediate + stop_at_main: launch (tbreak+run) → configurationDone; BPs
    //    after the synthetic stop.
    // 3) Immediate + !stop_at_main: setBreakpoints → configurationDone → launch
    //    so `run` already has editor BPs (otherwise the process races off and
    //    exits before any stop, leaving the UI "ok" with a blank Play button).
    auto complete_gdb_launch =
        [this](dap::future<dap::ResponseOrError<dap::LaunchResponse>> launch_future) {
          const auto response = launch_future.get();
          bool inferior_started = false;
          {
            std::lock_guard<std::recursive_mutex> response_lock(session_mutex_);
            if (!session_) {
              return;
            }
            if (response.error) {
              push_error(i18n::tr_fmt("debug.dap.launch_failed",
                                      {response.error.message}));
            } else {
              inferior_started = true;
            }
          }
          if (!inferior_started) {
            return;
          }
          inferior_launched_ = true;
          on_inferior_launched();
          // Without a stop (no stop-at-main / not yet on a BP), mark running so
          // the toolbar Play/Pause state is not left blank (Disconnected).
          if (!inferior_stopped_.load(std::memory_order_acquire)) {
            notify_continued();
          }
          DebugEvent configured;
          configured.kind = DebugEventKind::kLaunchConfigured;
          configured.text = i18n::tr("debug.dap.session_ready");
          push_event(std::move(configured));
        };

    if (deferred_launch) {
      std::unique_lock<std::recursive_mutex> lock(session_mutex_);
      if (!session_) {
        return;
      }
      if (adapter_is_gdb() && !configure_packet_monitor_env_locked(command.launch)) {
        return;
      }
      configuration_done_ = false;
      auto launch_future = session_->send(launch);
      lock.unlock();
      if (!finish_late_configuration_unlocked(/*send_configuration_done=*/true)) {
        return;
      }
      complete_gdb_launch(std::move(launch_future));
      return;
    }

    if (!stop_at_main) {
      {
        std::lock_guard<std::recursive_mutex> lock(session_mutex_);
        if (!session_) {
          return;
        }
        if (adapter_is_gdb() && !configure_packet_monitor_env_locked(command.launch)) {
          return;
        }
        configuration_done_ = false;
      }
      if (!finish_late_configuration_unlocked(/*send_configuration_done=*/false)) {
        return;
      }
      std::unique_lock<std::recursive_mutex> lock(session_mutex_);
      if (!session_) {
        return;
      }
      if (!send_configuration_done()) {
        return;
      }
      auto launch_future = session_->send(launch);
      lock.unlock();
      complete_gdb_launch(std::move(launch_future));
      return;
    }

    {
      std::unique_lock<std::recursive_mutex> lock(session_mutex_);
      if (!session_) {
        return;
      }
      if (adapter_is_gdb() && !configure_packet_monitor_env_locked(command.launch)) {
        return;
      }
      configuration_done_ = false;
      auto launch_future = session_->send(launch);
      if (!send_configuration_done()) {
        return;
      }
      lock.unlock();
      complete_gdb_launch(std::move(launch_future));
    }
    return;
  }

  if (command.kind == UiCommandKind::kAttach) {
    if (!adapter_is_gdb()) {
      // debugpy attach needs processId/listen/connect + late configurationDone;
      // GdbAttachRequest (pid/target) is GDB-only and triggers
      // debugpyWaitingForServer + rejected configurationDone.
      push_error(i18n::tr("debug.dap.attach_gdb_only"));
      return;
    }
    dap::GdbAttachRequest attach;
    attach.program = command.attach.program;
    if (command.attach.pid > 0) {
      attach.pid = command.attach.pid;
    }
    if (!command.attach.target.empty()) {
      attach.target = command.attach.target;
    }

    std::unique_lock<std::recursive_mutex> lock(session_mutex_);
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
      std::lock_guard<std::recursive_mutex> lock(session_mutex_);
      if (!session_) {
        return;
      }
      if (attach_response.error) {
        push_error(i18n::tr_fmt("debug.dap.attach_failed",
                                {attach_response.error.message}));
      } else if (!verify_inferior_attached_locked()) {
        // ptrace bloqueado: GDB DAP devuelve attach OK pero sin inferior real.
      } else {
        attach_started = true;
      }
    }
    if (attach_started) {
      inferior_launched_ = false;
      on_inferior_attached();
      DebugEvent configured;
      configured.kind = DebugEventKind::kLaunchConfigured;
      configured.text = i18n::tr("debug.dap.session_ready");
      push_event(std::move(configured));
    }
    return;
  }

  if (command.kind == UiCommandKind::kLoadCore) {
    if (command.core.core_path.empty()) {
      push_error(i18n::tr("debug.dap.core_file_empty_path"));
      return;
    }

    dap::GdbAttachRequest attach;
    attach.coreFile = command.core.core_path;
    if (!command.core.program.empty()) {
      attach.program = command.core.program;
    }

    std::unique_lock<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }
    if (!configure_glibc_debug_symbols_locked()) {
      return;
    }
    auto attach_future = session_->send(attach);
    if (!send_configuration_done()) {
      return;
    }
    lock.unlock();

    const auto attach_response = attach_future.get();
    bool loaded = false;
    {
      std::lock_guard<std::recursive_mutex> lock(session_mutex_);
      if (!session_) {
        return;
      }
      if (attach_response.error) {
        push_error(i18n::tr_fmt("debug.dap.attach_failed",
                                {attach_response.error.message}));
      } else {
        loaded = true;
      }
    }
    if (loaded) {
      on_inferior_core_loaded();
      DebugEvent configured;
      configured.kind = DebugEventKind::kLaunchConfigured;
      configured.text = i18n::tr("debug.dap.session_ready");
      push_event(std::move(configured));
    }
    return;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(session_mutex_);
    if (!session_) {
      return;
    }

    switch (command.kind) {
    case UiCommandKind::kContinue: {
      if (!inferior_attached_) {
        break;
      }
      if (!continue_inferior_locked()) {
        push_error(i18n::tr("debug.dap.continue_failed"));
      }
      break;
    }
    case UiCommandKind::kPause: {
      if (pause_inferior_locked()) {
        notify_stopped("pause");
      } else {
        push_error(i18n::tr("debug.dap.pause_failed"));
      }
      break;
    }
    case UiCommandKind::kNext: {
      dap::NextRequest request;
      request.threadId = command.thread_id > 0
                             ? command.thread_id
                             : (active_thread_id_ > 0 ? active_thread_id_ : 1);
      request.granularity = dap::SteppingGranularity("line");
      // Mark running before waiting so a queued RefreshStack that races after
      // step-over does not block the worker on hung scopes (same as continue).
      inferior_stopped_.store(false, std::memory_order_release);
      const auto response = session_->send(request).get();
      if (response.error) {
        inferior_stopped_.store(true, std::memory_order_release);
        push_error(i18n::tr_fmt("debug.dap.next_failed",
                                {response.error.message}));
      } else {
        notify_continued(request.threadId);
      }
      break;
    }
    case UiCommandKind::kStepIn: {
      dap::StepInRequest request;
      request.threadId = command.thread_id > 0
                             ? command.thread_id
                             : (active_thread_id_ > 0 ? active_thread_id_ : 1);
      request.granularity = dap::SteppingGranularity("line");
      inferior_stopped_.store(false, std::memory_order_release);
      const auto response = session_->send(request).get();
      if (response.error) {
        inferior_stopped_.store(true, std::memory_order_release);
        push_error(i18n::tr_fmt("debug.dap.step_in_failed",
                                {response.error.message}));
      } else {
        notify_continued(request.threadId);
      }
      break;
    }
    case UiCommandKind::kStepOut: {
      dap::StepOutRequest request;
      request.threadId = command.thread_id > 0
                             ? command.thread_id
                             : (active_thread_id_ > 0 ? active_thread_id_ : 1);
      request.granularity = dap::SteppingGranularity("line");
      inferior_stopped_.store(false, std::memory_order_release);
      const auto response = session_->send(request).get();
      if (response.error) {
        inferior_stopped_.store(true, std::memory_order_release);
        push_error(i18n::tr_fmt("debug.dap.step_out_failed",
                                {response.error.message}));
      } else {
        notify_continued(request.threadId);
      }
      break;
    }
    case UiCommandKind::kEvaluate: {
      if ((command.evaluate_context == EvaluateContext::kWatch ||
           command.evaluate_context == EvaluateContext::kHover) &&
          !inferior_stopped_.load(std::memory_order_acquire)) {
        break;
      }
      dap::EvaluateRequest request;
      request.expression = command.expression;
      if (command.frame_id >= 0) {
        request.frameId = command.frame_id;
      }
      // GDB DAP: context "repl" trata "x"/"y" como comandos GDB; "watch"+frameId
      // evalúa expresiones del programa (variables locales).
      if (command.evaluate_context == EvaluateContext::kWatch) {
        request.context = "watch";
      } else if (command.evaluate_context == EvaluateContext::kHover) {
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
          event.watch_value = i18n::tr_fmt("debug.dap.error_prefix",
                                           {response.error.message});
          push_event(std::move(event));
        } else if (command.evaluate_context == EvaluateContext::kHover) {
          DebugEvent event;
          event.kind = DebugEventKind::kHoverValue;
          event.hover_key = command.correlation_id;
          event.hover_expression = command.expression;
          event.hover_value = i18n::tr_fmt("debug.dap.error_prefix",
                                         {response.error.message});
          push_event(std::move(event));
        } else if (command.evaluate_context == EvaluateContext::kCoreAnalyzer) {
          DebugEvent event;
          event.kind = DebugEventKind::kCoreAnalyzerResult;
          event.text = i18n::tr_fmt("debug.dap.error_prefix",
                                    {response.error.message});
          push_event(std::move(event));
        } else {
          push_error(i18n::tr_fmt("debug.dap.evaluate_failed",
                                  {response.error.message}));
        }
        break;
      }
      DebugEvent event;
      if (command.evaluate_context == EvaluateContext::kWatch) {
        event.kind = DebugEventKind::kWatchUpdated;
        event.watch_expression = command.expression;
        event.watch_value = response.response.result;
      } else if (command.evaluate_context == EvaluateContext::kHover) {
        event.kind = DebugEventKind::kHoverValue;
        event.hover_key = command.correlation_id;
        event.hover_expression = command.expression;
        event.hover_value = response.response.result;
      } else if (command.evaluate_context == EvaluateContext::kCoreAnalyzer) {
        event.kind = DebugEventKind::kCoreAnalyzerResult;
        event.text = response.response.result;
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
          event.watch_value = i18n::tr_fmt("debug.dap.error_prefix",
                                           {fb.error.message});
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
  adapter_ = create_debug_adapter_process(preferred_adapter_);
  if (!adapter_ || !adapter_->start()) {
    if (preferred_adapter_ == DebugAdapterKind::kDebugpy) {
      push_error(i18n::tr("debug.dap.debugpy_start_failed"));
    } else if (preferred_adapter_ == DebugAdapterKind::kBashdb) {
      push_error(i18n::tr("debug.dap.bashdb_start_failed"));
    } else {
      push_error(i18n::tr("debug.dap.gdb_start_failed"));
    }
    running_ = false;
    return;
  }

  setup_session();
  session_close_guard_ = std::make_shared<std::atomic<bool>>(true);
  const auto close_guard = session_close_guard_;
  session_->bind(adapter_->reader(), adapter_->writer(), [this, close_guard] {
    if (!close_guard || !close_guard->load(std::memory_order_acquire) ||
        !running_.load(std::memory_order_acquire)) {
      return;
    }
    DebugEvent event;
    event.kind = DebugEventKind::kTerminated;
    event.text = i18n::tr("debug.dap.connection_closed");
    push_event(std::move(event));
  });

  if (!initialize_session()) {
    adapter_->stop();
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
    try {
      handle_command(*command);
    } catch (const std::exception& ex) {
      push_error(std::string("DAP worker exception: ") + ex.what());
    } catch (...) {
      push_error("DAP worker unknown exception");
    }
  }

  if (running_.load(std::memory_order_acquire) && session_ && adapter_ &&
      adapter_->running() && inferior_attached_) {
    dap::DisconnectRequest disconnect;
    disconnect.terminateDebuggee = inferior_launched_;
    session_->send(disconnect).get();
    inferior_attached_ = false;
  }
  if (adapter_) {
    adapter_->stop();
  }
  session_.reset();
  adapter_.reset();
  running_.store(false);
}

}  // namespace tuide
