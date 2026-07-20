#include "ui/ui_event_dispatcher.hpp"

#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ui/ui_event_trace.hpp"

namespace tuide {

using namespace ftxui;

void UiEventDispatcher::set_screen(ScreenInteractive* screen) { screen_ = screen; }

uint64_t UiEventDispatcher::begin_input_correlation() {
  active_correlation_id_ = next_correlation_id_++;
  return active_correlation_id_;
}

uint64_t UiEventDispatcher::current_correlation_id() const { return active_correlation_id_; }

bool UiEventDispatcher::correlation_still_valid(uint64_t id) const {
  return id != 0 && id == active_correlation_id_;
}

void UiEventDispatcher::post_custom_coalesced(bool urgent) {
  if (screen_ == nullptr) {
    return;
  }
  (void)urgent;
  if (custom_pending_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  screen_->PostEvent(Event::Custom);
}

void UiEventDispatcher::enqueue(UiEvent event, bool urgent) {
  if (event.tag.empty()) {
    event.tag = "unnamed";
  }
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_events_.push_back(std::move(event));
  }
  post_custom_coalesced(urgent);
}

void UiEventDispatcher::enqueue(UiEvent event) { enqueue(std::move(event), false); }

void UiEventDispatcher::emit(UiEvent event) { enqueue(std::move(event), false); }

void UiEventDispatcher::emit_urgent(UiEvent event) { enqueue(std::move(event), true); }

void UiEventDispatcher::post_repaint_urgent() {
  if (screen_ == nullptr) {
    return;
  }
  UiEvent event;
  event.kind = UiEventKind::InputCorrelated;
  event.tag = "ui.repaint_urgent";
  event.src_file = __FILE__;
  event.src_line = __LINE__;
  enqueue(std::move(event), true);
  request_animation_frame();
}

void UiEventDispatcher::emit_correlated_if_valid(uint64_t correlation_id, std::string tag,
                                                 std::function<void()> pre_paint,
                                                 const char* src_file, int src_line) {
  if (!correlation_still_valid(correlation_id)) {
    if (pre_paint) {
      pre_paint();
    }
    return;
  }
  UiEvent event;
  event.kind = UiEventKind::InputCorrelated;
  event.correlation_id = correlation_id;
  event.tag = std::move(tag);
  event.pre_paint = std::move(pre_paint);
  event.src_file = src_file;
  event.src_line = src_line;
  emit(std::move(event));
}

void UiEventDispatcher::emit_terminal(std::string tag, std::function<void()> pre_paint,
                                        const char* src_file, int src_line) {
  UiEvent event;
  event.kind = UiEventKind::TerminalOutput;
  event.tag = std::move(tag);
  event.pre_paint = std::move(pre_paint);
  event.src_file = src_file;
  event.src_line = src_line;
  emit(std::move(event));
  // Same as emit_debug: coalesced Custom alone may not schedule a frame, so PTY
  // echoes and streaming compile output stay invisible until the next key/click
  // (one-character lag / frozen scrollback).
  request_animation_frame();
}

void UiEventDispatcher::emit_debug(std::string tag, std::function<void()> pre_paint,
                                     const char* src_file, int src_line) {
  UiEvent event;
  event.kind = UiEventKind::DebugCritical;
  event.tag = std::move(tag);
  event.pre_paint = std::move(pre_paint);
  event.src_file = src_file;
  event.src_line = src_line;
  emit(std::move(event));
  // DebugCritical mutates layout (mode, panels). Coalesced Custom alone may not
  // schedule a frame; without this the UI stays stale until the next key/click.
  request_animation_frame();
}

void UiEventDispatcher::post_repaint_custom() {
  if (screen_ == nullptr) {
    return;
  }
  screen_->PostEvent(Event::Custom);
}

void UiEventDispatcher::post_on_main(std::function<void()> fn) {
  if (!fn) {
    return;
  }
  if (screen_ == nullptr) {
    fn();
    return;
  }
  screen_->Post(std::move(fn));
}

void UiEventDispatcher::request_animation_frame() {
  if (screen_ != nullptr) {
    screen_->RequestAnimationFrame();
  }
}

UiEventDrainPlan UiEventDispatcher::drain_pending(int64_t now_ms) {
  (void)now_ms;
  custom_pending_.store(false, std::memory_order_release);

  std::vector<UiEvent> events;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    events.swap(pending_events_);
  }

  UiEventDrainPlan plan;
  for (const UiEvent& event : events) {
    trace_.record(event, events.size() > 1, now_ms);
    if (event.pre_paint) {
      event.pre_paint();
    }
    const bool is_lsp_diag = event.tag == "lsp.diagnostics";
    const bool is_lsp_completion = event.tag == "lsp.completion";
    switch (event.kind) {
      case UiEventKind::TerminalOutput:
        plan.run_terminal = true;
        break;
      case UiEventKind::DebugCritical:
        plan.run_debug = true;
        plan.run_editor = true;
        plan.run_ui_tasks = true;
        plan.run_full_background = true;
        break;
      case UiEventKind::UserInput:
      case UiEventKind::InputCorrelated:
        if (!is_lsp_diag && !is_lsp_completion) {
          plan.run_editor = true;
          plan.run_ui_tasks = true;
          plan.run_full_background = true;
        }
        break;
    }
  }
  if (!events.empty()) {
    plan.lsp_diag_only = true;
    plan.lsp_completion_only = true;
    for (const UiEvent& event : events) {
      if (event.tag != "lsp.diagnostics") {
        plan.lsp_diag_only = false;
      }
      if (event.tag != "lsp.completion") {
        plan.lsp_completion_only = false;
      }
    }
  }
  bool repost = false;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    repost = !pending_events_.empty();
  }
  if (repost) {
    post_custom_coalesced(false);
  }
  return plan;
}

UiEventTrace& UiEventDispatcher::trace() { return trace_; }

const UiEventTrace& UiEventDispatcher::trace() const { return trace_; }

}  // namespace tuide
