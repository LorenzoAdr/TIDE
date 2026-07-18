#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "util/ui_activity_gate.hpp"

namespace tuide {

enum class UiPerfEventKind {
  kKeyboard = 0,
  kMouseClick,
  kMouseWheel,
  kMouseDrag,
  kMouseMove,
  kCustomTick,
  kPaint,
  kCount
};

struct UiPerfPhaseStats {
  std::string name;
  uint64_t samples = 0;
  uint64_t total_us = 0;
  uint64_t max_us = 0;
  uint64_t p95_us = 0;
};

struct UiPerfSnapshot {
  double paint_fps = 0.0;
  double tick_fps = 0.0;
  uint64_t paints_total = 0;
  uint64_t ticks_total = 0;
  uint64_t ticks_without_paint = 0;
  std::array<uint64_t, static_cast<std::size_t>(UiPerfEventKind::kCount)> event_counts{};
  UiActivityPhase activity_phase = UiActivityPhase::kGraceWindow;
  int64_t ms_in_phase = 0;
  double ticks_without_paint_ratio = 0.0;
  std::vector<UiPerfPhaseStats> tick_phases;
};

class UiPerfMonitor {
 public:
  void on_paint(int64_t now_ms);
  void on_custom_tick_begin(int64_t now_ms);
  void on_custom_tick_end(int64_t now_ms, uint64_t paint_count_before);
  void on_input_event(UiPerfEventKind kind);
  void on_tick_phase(std::string_view name, uint64_t duration_us);
  void set_activity_phase(UiActivityPhase phase, int64_t ms_in_phase);
  void publish_dump_phases();
  std::string dump_phases_line() const;

  const UiPerfSnapshot& snapshot() const { return snapshot_; }

 private:
  void refresh_fps(int64_t now_ms, bool is_paint);
  void refresh_phase_snapshot(std::size_t max_phases);

  UiPerfSnapshot snapshot_;
  int64_t last_paint_ms_ = 0;
  int64_t last_tick_ms_ = 0;
  bool tick_active_ = false;
  uint64_t tick_paint_before_ = 0;
  struct PhaseAccumulator {
    std::string name;
    std::vector<uint64_t> recent_us;
  };
  std::vector<PhaseAccumulator> phase_accumulators_;
  mutable std::mutex dump_mutex_;
  std::string dump_phases_line_;
};

class UiSyncPhaseScope {
 public:
  UiSyncPhaseScope(UiPerfMonitor* monitor, std::string name)
      : monitor_(monitor), name_(std::move(name)), start_(std::chrono::steady_clock::now()) {}
  ~UiSyncPhaseScope() {
    if (monitor_ == nullptr) {
      return;
    }
    const auto end = std::chrono::steady_clock::now();
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    monitor_->on_tick_phase(name_, static_cast<uint64_t>(std::max<int64_t>(0, us)));
  }

  UiSyncPhaseScope(const UiSyncPhaseScope&) = delete;
  UiSyncPhaseScope& operator=(const UiSyncPhaseScope&) = delete;

 private:
  UiPerfMonitor* monitor_;
  std::string name_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace tuide
