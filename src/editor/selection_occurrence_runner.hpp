#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "editor/text_search.hpp"

namespace tgdb {

struct SelectionOccurrenceKey {
  int start_line = -1;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
  std::string path;
  uint64_t view_token = 0;

  bool operator==(const SelectionOccurrenceKey &other) const {
    return start_line == other.start_line && start_col == other.start_col &&
           end_line == other.end_line && end_col == other.end_col &&
           path == other.path && view_token == other.view_token;
  }

  bool operator!=(const SelectionOccurrenceKey &other) const {
    return !(*this == other);
  }
};

class SelectionOccurrenceRunner {
public:
  SelectionOccurrenceRunner();
  ~SelectionOccurrenceRunner();

  SelectionOccurrenceRunner(const SelectionOccurrenceRunner &) = delete;
  SelectionOccurrenceRunner &
  operator=(const SelectionOccurrenceRunner &) = delete;

  void start(uint64_t request_id, SelectionOccurrenceKey key,
             std::vector<std::string> lines, std::string needle,
             bool whole_word);

  void cancel();

  bool running() const;

  bool poll(uint64_t request_id, const SelectionOccurrenceKey &key,
            std::vector<TextMatch> *matches);

private:
  struct Job {
    uint64_t request_id = 0;
    SelectionOccurrenceKey key;
    std::vector<std::string> lines;
    std::string needle;
    bool whole_word = false;
  };

  struct Result {
    uint64_t request_id = 0;
    SelectionOccurrenceKey key;
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

} // namespace tgdb