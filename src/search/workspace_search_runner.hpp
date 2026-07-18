#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
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

  void start(const WorkspaceSearchOptions& opts);
  void cancel();
  bool running() const;

  // Devuelve true si hay resultados nuevos o cambió el estado (cancelado / terminado).
  bool poll(std::vector<WorkspaceSearchResult>* results, bool* cancelled, int* files_scanned,
            bool* used_rg = nullptr);

 private:
  void worker_main(uint64_t generation, WorkspaceSearchOptions opts);
  void search_inprocess(uint64_t generation, const WorkspaceSearchOptions& opts,
                        std::vector<WorkspaceSearchResult>* results, int* files_scanned,
                        bool* cancelled);

  mutable std::mutex mutex_;
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
