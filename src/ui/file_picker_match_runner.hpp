#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "indexer/workspace_indexer.hpp"

namespace tuide {

struct FilePickerMatch {
  std::string path;
  std::string display_label;
  int score = 0;
  std::vector<std::size_t> match_indices;
};

struct FilePickerSearchParams {
  std::string ref_dir;
  std::vector<FilePickerCatalogEntry> open_tabs;
};

class FilePickerMatchRunner {
 public:
  FilePickerMatchRunner();
  ~FilePickerMatchRunner();

  FilePickerMatchRunner(const FilePickerMatchRunner&) = delete;
  FilePickerMatchRunner& operator=(const FilePickerMatchRunner&) = delete;

  void start(uint64_t request_id, std::string query_lower,
             std::shared_ptr<const std::vector<FilePickerCatalogEntry>> catalog,
             FilePickerSearchParams params);

  void cancel();

  bool running() const;

  bool poll(uint64_t request_id, std::vector<FilePickerMatch>* matches);

 private:
  struct Job {
    uint64_t request_id = 0;
    std::string query_lower;
    std::shared_ptr<const std::vector<FilePickerCatalogEntry>> catalog;
    FilePickerSearchParams params;
  };

  struct Result {
    uint64_t request_id = 0;
    std::vector<FilePickerMatch> matches;
  };

  void worker_main();
  void stop_worker();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> active_request_id_{0};

  bool has_pending_job_ = false;
  Job pending_job_;
  bool has_ready_result_ = false;
  Result ready_result_;
};

}  // namespace tuide
