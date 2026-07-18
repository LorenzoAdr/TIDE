#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "editor/text_search.hpp"

namespace tuide {

struct FindMatchKey {
  std::string query;
  std::string path;
  uint64_t view_token = 0;

  bool operator==(const FindMatchKey& other) const {
    return query == other.query && path == other.path && view_token == other.view_token;
  }

  bool operator!=(const FindMatchKey& other) const { return !(*this == other); }
};

class FindMatchRunner {
 public:
  FindMatchRunner();
  ~FindMatchRunner();

  FindMatchRunner(const FindMatchRunner&) = delete;
  FindMatchRunner& operator=(const FindMatchRunner&) = delete;

  void start(uint64_t request_id, FindMatchKey key, std::vector<std::string> lines,
             std::string needle);

  void cancel();

  bool running() const;

  bool poll(uint64_t request_id, const FindMatchKey& key, std::vector<TextMatch>* matches);

 private:
  struct Job {
    uint64_t request_id = 0;
    FindMatchKey key;
    std::vector<std::string> lines;
    std::string needle;
  };

  struct Result {
    uint64_t request_id = 0;
    FindMatchKey key;
    std::vector<TextMatch> matches;
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
