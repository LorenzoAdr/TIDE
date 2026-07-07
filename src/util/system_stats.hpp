#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/ui_activity_gate.hpp"

namespace tgdb {

struct ThreadSample {
  int tid = 0;
  std::string comm;
  double cpu_percent = 0.0;
  std::size_t rss_kb = 0;
  int child_thread_count = 0;
  bool is_child_process = false;
};

struct ProcessSample {
  double cpu_percent = 0.0;
  std::size_t rss_kb = 0;
  std::size_t vms_kb = 0;
  int thread_count = 0;
  std::vector<ThreadSample> threads;
};

struct CpuCoreSample {
  std::string name;
  double usage_percent = 0.0;
};

struct SystemSample {
  double cpu_total_percent = 0.0;
  int core_count = 0;
  std::vector<CpuCoreSample> cores;
  std::size_t mem_total_kb = 0;
  std::size_t mem_used_kb = 0;
  std::size_t swap_total_kb = 0;
  std::size_t swap_used_kb = 0;
  bool available = false;
};

struct PerformanceSnapshot {
  double fps = 0.0;
  ProcessSample process;
  SystemSample system;
  bool stats_available = false;
};

struct SamplerStatsState;

class FpsCounter {
 public:
  void on_frame();

  double fps() const { return fps_; }

 private:
  bool has_sample_ = false;
  std::chrono::steady_clock::time_point last_frame_{};
  double fps_ = 0.0;
};

class PerformanceSampler {
 public:
  PerformanceSampler();
  ~PerformanceSampler();
  void on_frame(bool sample_workers = false);
  void set_worker_sampling_enabled(bool enabled);
  void set_file_dump_enabled(bool enabled);
  bool file_dump_enabled() const;
  void set_dump_hooks(const UiActivityGate* activity_gate,
                      const std::atomic<uint64_t>* ui_paint_count,
                      const std::atomic<uint64_t>* ui_custom_tick);
  PerformanceSnapshot snapshot() const;
  std::string dump_file_path() const;

 private:
  void sampler_loop();
  void open_dump_file();
  void append_dump_line(const PerformanceSnapshot& snap, double elapsed_sec);

  FpsCounter fps_counter_;
  mutable std::mutex snapshot_mutex_;
  PerformanceSnapshot snapshot_;
  std::unique_ptr<SamplerStatsState> stats_state_;
  std::atomic<bool> sampling_enabled_{false};
  std::atomic<bool> file_dump_enabled_{false};
  std::atomic<bool> sampler_running_{false};
  std::thread sampler_thread_;
  std::string dump_path_;
  const UiActivityGate* activity_gate_ = nullptr;
  const std::atomic<uint64_t>* ui_paint_count_ = nullptr;
  const std::atomic<uint64_t>* ui_custom_tick_ = nullptr;
};

}  // namespace tgdb
