#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <sys/types.h>

#include "search/workspace_search.hpp"

namespace tuide {

class WorkspaceSearchRunner {
 public:
  WorkspaceSearchRunner();
  ~WorkspaceSearchRunner();

  WorkspaceSearchRunner(const WorkspaceSearchRunner&) = delete;
  WorkspaceSearchRunner& operator=(const WorkspaceSearchRunner&) = delete;

  void set_wake_callback(std::function<void()> callback);
  void start(WorkspaceSearchOptions opts);
  void cancel();
  bool running() const;

  // Devuelve true si hay resultados nuevos o cambió el estado (cancelado / terminado).
  bool poll(std::vector<WorkspaceSearchResult>* results, bool* cancelled, int* files_scanned,
            bool* used_rg = nullptr);

 private:
  struct Job {
    uint64_t generation = 0;
    WorkspaceSearchOptions opts;
  };

  void stop_worker();
  void worker_main();
  void run_job(uint64_t generation, WorkspaceSearchOptions opts);
  void search_inprocess(uint64_t generation, const WorkspaceSearchOptions& opts,
                        const std::vector<std::string>& files,
                        std::vector<WorkspaceSearchResult>* results, int* files_scanned,
                        bool* cancelled);
  void notify_wake();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<Job> pending_job_;
  std::atomic<bool> stop_{false};
  std::function<void()> wake_callback_;

  std::vector<WorkspaceSearchResult> results_;
  int files_scanned_ = 0;
  bool cancelled_ = false;
  bool finished_ = false;
  bool used_rg_ = false;
  uint64_t ready_generation_ = 0;

  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> generation_{0};
  std::atomic<bool> cancel_requested_{false};
  std::atomic<pid_t> child_pid_{-1};
};

}  // namespace tuide
