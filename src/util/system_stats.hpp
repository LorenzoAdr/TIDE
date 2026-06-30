#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

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
  const PerformanceSnapshot& snapshot() const { return snapshot_; }

 private:
  FpsCounter fps_counter_;
  PerformanceSnapshot snapshot_;
  bool has_prev_ = false;
  std::chrono::steady_clock::time_point last_sample_time_{};
  std::unique_ptr<SamplerStatsState> stats_state_;
};

}  // namespace tgdb
