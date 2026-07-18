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
#include "git/git_subrepos.hpp"

namespace tuide {

struct GitRepoInfo {
  bool valid = false;
  std::string root;
  std::string branch;
  std::string head;
  std::string last_error;
  int subrepo_count = 0;
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
  void refresh_log_search(const std::string& query);
  void refresh_file_timeline(const std::string& path);
  void refresh_timeline_diff(const std::string& path, const std::string& commit_hash);
  void refresh_commit_files(const std::string& commit_hash);
  void refresh_graph();

  void set_update_callback(std::function<void()> callback);
  uint64_t cache_revision() const;
  bool is_untracked(const std::string& path) const;
  bool has_file_diff_text(const std::string& path) const;

  bool is_repo() const;
  bool has_subrepos() const;
  GitRepoInfo repo_info() const;
  GitRepoInfo context_repo_info() const;
  void set_context_from_path(const std::string& workspace_rel_path);
  GitStatusSnapshot status() const;
  GitFileDiff file_diff(const std::string& absolute_path) const;
  std::vector<GitCommitEntry> log_entries() const;
  std::vector<GitCommitEntry> log_search_results() const;
  bool log_search_ready(const std::string& query) const;
  std::vector<GitBranchEntry> branches() const;
  std::vector<GitCommitEntry> file_timeline(const std::string& absolute_path) const;
  std::string timeline_diff_text(const std::string& absolute_path,
                                 const std::string& commit_hash) const;
  bool has_timeline_diff_text(const std::string& absolute_path,
                              const std::string& commit_hash) const;
  std::vector<GitCommitFileEntry> commit_files(const std::string& commit_hash) const;
  bool has_commit_files(const std::string& commit_hash) const;
  std::vector<std::string> graph_lines() const;
  bool graph_loaded() const;
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
  ResolvedGitPath resolve_path_unlocked(const std::string& path) const;
  std::string context_repo_root_unlocked() const;
  void discover_repos(const std::string& workspace_root);
  void enqueue(std::function<void()> task);
  void worker_main();
  void set_error_unlocked(const std::string& message);
  void load_all_status();
  void merge_status_snapshot(GitStatusSnapshot* merged, const GitStatusSnapshot& part,
                             const std::string& repo_prefix) const;
  void load_file_diff_text(const std::string& repo_root, const std::string& repo_rel,
                           const std::string& workspace_rel);
  void load_file_head(const std::string& repo_root, const std::string& repo_rel,
                      const std::string& workspace_rel);
  void load_log(const std::string& root);
  void load_log_search(const std::string& root, const std::string& query);
  void load_branches(const std::string& root);
  void load_file_timeline(const std::string& repo_root, const std::string& repo_rel,
                          const std::string& workspace_rel);
  void load_timeline_diff(const std::string& repo_root, const std::string& repo_rel,
                          const std::string& workspace_rel, const std::string& commit_hash);
  void load_commit_files(const std::string& root, const std::string& commit_hash);
  void load_graph(const std::string& root);
  bool commit_files_cached_unlocked(const std::string& commit_hash) const;
  std::string timeline_diff_key_unlocked(const std::string& rel,
                                         const std::string& commit_hash) const;
  bool timeline_diff_cached_unlocked(const std::string& rel,
                                     const std::string& commit_hash) const;
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
  std::string main_repo_root_;
  bool main_repo_valid_ = false;
  std::vector<GitSubrepoInfo> subrepos_;
  std::string context_repo_root_;
  GitRepoInfo repo_info_;
  GitStatusSnapshot status_;
  std::map<std::string, GitFileDiff> file_diffs_;
  std::map<std::string, std::string> file_diff_texts_;
  std::vector<GitCommitEntry> log_entries_;
  std::string log_search_query_;
  std::vector<GitCommitEntry> log_search_results_;
  std::unordered_set<std::string> inflight_log_searches_;
  std::vector<GitBranchEntry> branches_;
  std::string timeline_path_;
  std::vector<GitCommitEntry> file_timeline_;
  std::map<std::string, std::string> timeline_diff_texts_;
  std::map<std::string, std::vector<GitCommitFileEntry>> commit_files_;
  std::vector<std::string> graph_lines_;
  bool graph_loaded_ = false;
  std::unordered_set<std::string> inflight_timelines_;
  std::unordered_set<std::string> inflight_timeline_diffs_;
  std::unordered_set<std::string> inflight_commit_files_;
  std::atomic<bool> inflight_graph_{false};
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

}  // namespace tuide
