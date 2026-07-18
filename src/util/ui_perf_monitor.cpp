#include "util/ui_perf_monitor.hpp"

#include <algorithm>
#include <cmath>

namespace tuide {

namespace {

constexpr std::size_t kMaxPhaseSamples = 128;
constexpr std::size_t kMaxPhases = 32;

uint64_t percentile_p95(std::vector<uint64_t> values) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t index =
      std::min(values.size() - 1, static_cast<std::size_t>(std::ceil(values.size() * 0.95) - 1));
  return values[index];
}

void update_ema(double& current, double sample) {
  if (current <= 0.0) {
    current = sample;
    return;
  }
  current = current * 0.8 + sample * 0.2;
}

}  // namespace

void UiPerfMonitor::on_paint(int64_t now_ms) {
  ++snapshot_.paints_total;
  on_input_event(UiPerfEventKind::kPaint);
  refresh_fps(now_ms, true);
  last_paint_ms_ = now_ms;
}

void UiPerfMonitor::on_custom_tick_begin(int64_t now_ms) {
  ++snapshot_.ticks_total;
  on_input_event(UiPerfEventKind::kCustomTick);
  tick_active_ = true;
  tick_paint_before_ = snapshot_.paints_total;
  refresh_fps(now_ms, false);
  last_tick_ms_ = now_ms;
}

void UiPerfMonitor::on_custom_tick_end(int64_t /*now_ms*/, uint64_t paint_count_before) {
  if (!tick_active_) {
    return;
  }
  tick_active_ = false;
  if (snapshot_.paints_total == paint_count_before) {
    ++snapshot_.ticks_without_paint;
  }
  const uint64_t total_ticks = snapshot_.ticks_total;
  snapshot_.ticks_without_paint_ratio =
      total_ticks > 0 ? static_cast<double>(snapshot_.ticks_without_paint) / total_ticks : 0.0;
}

void UiPerfMonitor::on_input_event(UiPerfEventKind kind) {
  const auto index = static_cast<std::size_t>(kind);
  if (index < snapshot_.event_counts.size()) {
    ++snapshot_.event_counts[index];
  }
}

void UiPerfMonitor::on_tick_phase(std::string_view name, uint64_t duration_us) {
  if (name.empty()) {
    return;
  }
  PhaseAccumulator* target = nullptr;
  for (PhaseAccumulator& phase : phase_accumulators_) {
    if (phase.name == name) {
      target = &phase;
      break;
    }
  }
  if (target == nullptr) {
    if (phase_accumulators_.size() >= kMaxPhases) {
      return;
    }
    phase_accumulators_.push_back({std::string(name), {}});
    target = &phase_accumulators_.back();
  }
  target->recent_us.push_back(duration_us);
  if (target->recent_us.size() > kMaxPhaseSamples) {
    target->recent_us.erase(target->recent_us.begin());
  }
}

void UiPerfMonitor::refresh_phase_snapshot(std::size_t max_phases) {
  snapshot_.tick_phases.clear();
  for (const PhaseAccumulator& phase_acc : phase_accumulators_) {
    if (phase_acc.recent_us.empty()) {
      continue;
    }
    UiPerfPhaseStats stats;
    stats.name = phase_acc.name;
    stats.samples = phase_acc.recent_us.size();
    for (uint64_t us : phase_acc.recent_us) {
      stats.total_us += us;
      stats.max_us = std::max(stats.max_us, us);
    }
    stats.p95_us = percentile_p95(phase_acc.recent_us);
    snapshot_.tick_phases.push_back(std::move(stats));
  }
  std::sort(snapshot_.tick_phases.begin(), snapshot_.tick_phases.end(),
            [](const UiPerfPhaseStats& a, const UiPerfPhaseStats& b) {
              return a.p95_us > b.p95_us;
            });
  if (snapshot_.tick_phases.size() > max_phases) {
    snapshot_.tick_phases.resize(max_phases);
  }
}

void UiPerfMonitor::set_activity_phase(UiActivityPhase phase, int64_t ms_in_phase) {
  snapshot_.activity_phase = phase;
  snapshot_.ms_in_phase = ms_in_phase;
  refresh_phase_snapshot(8);
}

void UiPerfMonitor::publish_dump_phases() {
  std::vector<UiPerfPhaseStats> ranked;
  ranked.reserve(phase_accumulators_.size());
  for (const PhaseAccumulator& phase_acc : phase_accumulators_) {
    if (phase_acc.recent_us.empty()) {
      continue;
    }
    UiPerfPhaseStats stats;
    stats.name = phase_acc.name;
    stats.samples = phase_acc.recent_us.size();
    for (uint64_t us : phase_acc.recent_us) {
      stats.total_us += us;
      stats.max_us = std::max(stats.max_us, us);
    }
    stats.p95_us = percentile_p95(phase_acc.recent_us);
    ranked.push_back(std::move(stats));
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const UiPerfPhaseStats& a, const UiPerfPhaseStats& b) {
              return a.p95_us > b.p95_us;
            });
  if (ranked.size() > 6) {
    ranked.resize(6);
  }

  std::string line;
  for (std::size_t i = 0; i < ranked.size(); ++i) {
    if (i > 0) {
      line += ',';
    }
    line += ranked[i].name;
    line += '@';
    line += std::to_string(ranked[i].p95_us);
    line += "us";
  }

  refresh_phase_snapshot(8);

  std::lock_guard<std::mutex> lock(dump_mutex_);
  dump_phases_line_ = std::move(line);
}

std::string UiPerfMonitor::dump_phases_line() const {
  std::lock_guard<std::mutex> lock(dump_mutex_);
  return dump_phases_line_;
}

void UiPerfMonitor::refresh_fps(int64_t now_ms, bool is_paint) {
  if (is_paint) {
    if (last_paint_ms_ > 0) {
      const double interval_ms = static_cast<double>(now_ms - last_paint_ms_);
      if (interval_ms > 0.0) {
        update_ema(snapshot_.paint_fps, 1000.0 / interval_ms);
      }
    }
    return;
  }
  if (last_tick_ms_ > 0) {
    const double interval_ms = static_cast<double>(now_ms - last_tick_ms_);
    if (interval_ms > 0.0) {
      update_ema(snapshot_.tick_fps, 1000.0 / interval_ms);
    }
  }
}

}  // namespace tuide
