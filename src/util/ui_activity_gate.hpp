#pragma once

#include <atomic>
#include <cstdint>

namespace tgdb {

enum class UiActivityPhase { kInteractive, kGraceWindow, kInhibited };

class UiActivityGate {
 public:
  void set_passive_enabled(bool enabled) { passive_enabled_.store(enabled); }
  void set_grace_window_ms(int64_t ms);

  void on_significant_input(int64_t now_ms);
  void on_debug_critical(int64_t now_ms);
  void on_mouse_move() {}

  void tick(int64_t now_ms);

  UiActivityPhase phase() const { return phase_.load(std::memory_order_acquire); }
  bool is_inhibited() const;
  bool allows_periodic_tick() const;
  bool allows_lsp_ui() const;
  bool allows_hover_chrome() const;
  bool allows_cursor_blink() const;

  int64_t ms_in_current_phase(int64_t now_ms) const;
  int64_t grace_window_ms() const { return grace_window_ms_.load(std::memory_order_relaxed); }

 private:
  std::atomic<bool> passive_enabled_{true};
  std::atomic<int64_t> grace_window_ms_{1000};
  std::atomic<UiActivityPhase> phase_{UiActivityPhase::kGraceWindow};
  std::atomic<int64_t> grace_end_ms_{0};
  std::atomic<int64_t> phase_entered_ms_{0};
};

const char* ui_activity_phase_label(UiActivityPhase phase);

}  // namespace tgdb
