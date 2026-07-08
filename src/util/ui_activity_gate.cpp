#include "util/ui_activity_gate.hpp"

#include <algorithm>

namespace tgdb {

namespace {

constexpr int64_t kSustainedIdleMs = 1500;

int64_t default_grace_end(int64_t now_ms, int64_t grace_ms) {
  return now_ms + std::max<int64_t>(1, grace_ms);
}

}  // namespace

void UiActivityGate::set_grace_window_ms(int64_t ms) {
  grace_window_ms_.store(std::max<int64_t>(1, ms), std::memory_order_relaxed);
}

void UiActivityGate::on_significant_input(int64_t now_ms) {
  if (!passive_enabled_.load(std::memory_order_relaxed)) {
    phase_.store(UiActivityPhase::kGraceWindow, std::memory_order_release);
    grace_end_ms_.store(0, std::memory_order_relaxed);
    return;
  }
  last_input_ms_.store(now_ms, std::memory_order_relaxed);
  const auto prev = phase_.load(std::memory_order_relaxed);
  if (prev != UiActivityPhase::kInteractive) {
    phase_entered_ms_.store(now_ms, std::memory_order_relaxed);
  }
  phase_.store(UiActivityPhase::kInteractive, std::memory_order_release);
  grace_end_ms_.store(0, std::memory_order_relaxed);
}

void UiActivityGate::on_debug_critical(int64_t now_ms) {
  on_significant_input(now_ms);
}

void UiActivityGate::tick(int64_t now_ms) {
  if (!passive_enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  const auto current = phase_.load(std::memory_order_acquire);
  if (current == UiActivityPhase::kInteractive) {
    const int64_t last_input = last_input_ms_.load(std::memory_order_relaxed);
    if (last_input > 0 && now_ms - last_input >= kSustainedIdleMs) {
      phase_.store(UiActivityPhase::kGraceWindow, std::memory_order_release);
      grace_end_ms_.store(
          default_grace_end(now_ms, grace_window_ms_.load(std::memory_order_relaxed)),
          std::memory_order_relaxed);
      phase_entered_ms_.store(now_ms, std::memory_order_relaxed);
    }
    return;
  }
  if (current != UiActivityPhase::kGraceWindow) {
    return;
  }
  const int64_t grace_end = grace_end_ms_.load(std::memory_order_relaxed);
  if (grace_end > 0 && now_ms >= grace_end) {
    phase_.store(UiActivityPhase::kInhibited, std::memory_order_release);
    phase_entered_ms_.store(now_ms, std::memory_order_relaxed);
  }
}

bool UiActivityGate::is_inhibited() const {
  if (!passive_enabled_.load(std::memory_order_relaxed)) {
    return false;
  }
  return phase_.load(std::memory_order_acquire) == UiActivityPhase::kInhibited;
}

bool UiActivityGate::is_interactive() const {
  if (!passive_enabled_.load(std::memory_order_relaxed)) {
    return false;
  }
  return phase_.load(std::memory_order_acquire) == UiActivityPhase::kInteractive;
}

bool UiActivityGate::allows_periodic_tick() const { return !is_inhibited(); }

bool UiActivityGate::allows_lsp_ui() const { return !is_inhibited(); }

bool UiActivityGate::allows_hover_chrome() const { return !is_inhibited(); }

bool UiActivityGate::allows_cursor_blink() const { return !is_inhibited(); }

int64_t UiActivityGate::ms_in_current_phase(int64_t now_ms) const {
  const int64_t entered = phase_entered_ms_.load(std::memory_order_relaxed);
  if (entered <= 0) {
    return 0;
  }
  return std::max<int64_t>(0, now_ms - entered);
}

const char* ui_activity_phase_label(UiActivityPhase phase) {
  switch (phase) {
    case UiActivityPhase::kInteractive:
      return "interactive";
    case UiActivityPhase::kGraceWindow:
      return "grace";
    case UiActivityPhase::kInhibited:
      return "inhibited";
  }
  return "unknown";
}

}  // namespace tgdb
