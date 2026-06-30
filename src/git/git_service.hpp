#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "git/git_diff.hpp"
#include "git/git_log.hpp"
#include "git/git_status.hpp"

namespace tgdb {

struct GitRepoInfo {
  bool valid = false;
  std::string root;
  std::string branch;
  std::string head;
  std::string last_error;
};

class GitService {
 public:
  GitService();
  ~GitService();

  void open(const std::string& workspace_root);
  void close();
  void invalidate(const std::string& path = {});
  void refresh_status();
  void refresh_status_and_diffs();
  void refresh_file_diff(const std::string& path, bool force = false);
  void refresh_file_head(const std::string& path);
  void refresh_log();
  void refresh_branches();

  void set_update_callback(std::function<void()> callback);
  uint64_t cache_revision() const;
  bool is_untracked(const std::string& path) const;
  bool has_file_diff_text(const std::string& path) const;

  bool is_repo() const;
  GitRepoInfo repo_info() const;
  GitStatusSnapshot status() const;
  GitFileDiff file_diff(const std::string& absolute_path) const;
  std::vector<GitCommitEntry> log_entries() const;
  std::vector<GitBranchEntry> branches() const;
  std::string file_diff_text(const std::string& absolute_path) const;
  std::string previous_line_content(const std::string& absolute_path, int line) const;
  bool busy() const;

  using CompletionCallback = std::function<void(bool success, const std::string& message)>;

  void stage_file(const std::string& path, CompletionCallback on_done);
  void unstage_file(const std::string& path, CompletionCallback on_done);
  void commit(const std::string& message, CompletionCallback on_done);
  void checkout_branch(const std::string& branch, CompletionCallback on_done);
  void push(CompletionCallback on_done);
  void pull(CompletionCallback on_done);

  void tick();

 private:
  std::string relative_path_unlocked(const std::string& absolute_path) const;
  std::string repo_relative_path_unlocked(const std::string& path) const;
  void enqueue(std::function<void()> task);
  void worker_main();
  void set_error_unlocked(const std::string& message);
  void detect_repo(const std::string& root);
  void load_status(const std::string& root);
  void load_file_diff_text(const std::string& root, const std::string& rel);
  void load_file_head(const std::string& root, const std::string& rel);
  void load_log(const std::string& root);
  void load_branches(const std::string& root);
  void notify_updated();
  void run_on_ui_thread(std::function<void()> task);
  void dispatch_completion(CompletionCallback on_done, bool success, const std::string& message);
  bool diff_text_cached_unlocked(const std::string& rel) const;

  mutable std::mutex mutex_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<std::function<void()>> queue_;
  std::thread worker_;
  std::string workspace_root_;
  GitRepoInfo repo_info_;
  GitStatusSnapshot status_;
  std::map<std::string, GitFileDiff> file_diffs_;
  std::map<std::string, std::string> file_diff_texts_;
  std::vector<GitCommitEntry> log_entries_;
  std::vector<GitBranchEntry> branches_;
  std::function<void()> update_callback_;
  std::mutex completion_mutex_;
  std::deque<std::function<void()>> pending_completions_;
  std::unordered_set<std::string> inflight_diffs_;
  std::unordered_set<std::string> inflight_heads_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> pending_ui_notify_{false};
  std::atomic<bool> inflight_status_{false};
  std::atomic<uint64_t> cache_revision_{0};
  std::atomic<int> pending_tasks_{0};
};

}  // namespace tgdb
