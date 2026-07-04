#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "util/csv_viewer.hpp"

namespace tgdb {

enum class TabularFileState { kIdle, kIndexing, kReady, kError };

class TabularFileStore {
 public:
  TabularFileStore();
  ~TabularFileStore();

  void open_async(const std::string& path);
  void reset();

  TabularFileState state() const;
  bool ready() const;
  bool indexing() const;
  bool loading_more() const;
  bool has_more() const;
  std::string error_message() const;

  int row_count() const;
  int max_data_total() const;
  bool request_load_at_end(int data_scroll, int visible_rows);
  std::string row_at(int index);
  TabularDelimiter delimiter() const;
  const TabularTableLayout& layout() const;

  void ensure_viewport(int data_scroll, int visible_rows);

 private:
  void open_worker(std::string path);
  void load_more_worker();
  void update_layout_locked(const std::vector<std::string>& sample_rows);
  std::string read_row_unlocked(int index) const;
  void ensure_cache_window_locked(int data_scroll, int visible_rows);
  void evict_cache_outside_locked(int start, int end);

  mutable std::mutex mutex_;
  std::thread worker_;
  std::thread load_more_thread_;
  std::atomic<bool> loading_more_{false};
  std::string path_;
  std::vector<std::uint64_t> offsets_;
  std::uint64_t file_read_pos_ = 0;
  int row_count_ = 0;
  bool has_more_ = false;
  TabularDelimiter delimiter_ = TabularDelimiter::kComma;
  TabularTableLayout layout_;
  std::unordered_map<int, std::string> row_cache_;
  int cache_start_ = -1;
  int cache_end_ = -1;
  int layout_cache_start_ = -1;
  int layout_cache_end_ = -1;
  TabularFileState state_ = TabularFileState::kIdle;
  std::string error_;
};

}  // namespace tgdb
