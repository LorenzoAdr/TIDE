#include "ui/ui_event_dispatcher.hpp"

#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ui/ui_event_trace.hpp"

namespace tgdb {

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

void UiEventDispatcher::enqueue(UiEvent event) {
  if (event.tag.empty()) {
    event.tag = "unnamed";
  }
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_events_.push_back(std::move(event));
  }
  post_custom_coalesced();
}

void UiEventDispatcher::post_custom_coalesced() {
  if (screen_ == nullptr) {
    return;
  }
  if (custom_pending_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  screen_->PostEvent(Event::Custom);
}

void UiEventDispatcher::emit(UiEvent event) {
  enqueue(std::move(event));
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
        plan.run_editor = true;
        plan.run_ui_tasks = true;
        plan.run_full_background = true;
        break;
    }
  }
  return plan;
}

UiEventTrace& UiEventDispatcher::trace() { return trace_; }

const UiEventTrace& UiEventDispatcher::trace() const { return trace_; }

}  // namespace tgdb
