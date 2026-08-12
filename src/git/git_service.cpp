#include "git/git_service.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <unordered_set>
#include <utility>

#include "git/git_command.hpp"
#include "git/git_subrepos.hpp"
#include "util/monitor_log.hpp"
#include "util/path_normalize.hpp"
#include "i18n/tr.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string trim_newline(std::string value) {
  if (!value.empty() && value.back() == '\n') {
    value.pop_back();
  }
  return value;
}

std::string to_lower_ascii(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool looks_like_hash_query(const std::string& query) {
  if (query.size() < 4) {
    return false;
  }
  for (char ch : query) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

bool commit_matches_query(const GitCommitEntry& entry, const std::string& query,
                          const std::string& query_lower) {
  if (entry.short_hash.size() >= query.size()) {
    const std::string short_prefix =
        to_lower_ascii(entry.short_hash.substr(0, query.size()));
    if (short_prefix == query_lower) {
      return true;
    }
  }
  if (entry.hash.size() >= query.size()) {
    const std::string hash_prefix = to_lower_ascii(entry.hash.substr(0, query.size()));
    if (hash_prefix == query_lower) {
      return true;
    }
  }
  const std::string message_lower = to_lower_ascii(entry.message);
  return message_lower.find(query_lower) != std::string::npos;
}

void merge_log_entries(std::vector<GitCommitEntry>* merged,
                       const std::vector<GitCommitEntry>& entries,
                       std::unordered_set<std::string>* seen) {
  if (merged == nullptr || seen == nullptr) {
    return;
  }
  for (const auto& entry : entries) {
    if (seen->insert(entry.hash).second) {
      merged->push_back(entry);
    }
  }
}

}  // namespace

GitService::GitService() {
  worker_ = std::thread([this] { worker_main(); });
}

GitService::~GitService() {
  stop_ = true;
  queue_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void GitService::enqueue(std::function<void()> task) {
  if (stop_.load()) {
    return;
  }
  pending_tasks_.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.push_back(std::move(task));
  }
  queue_cv_.notify_one();
}

void GitService::worker_main() {
  while (!stop_.load()) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] { return stop_.load() || !queue_.empty(); });
      if (stop_.load() && queue_.empty()) {
        break;
      }
      if (queue_.empty()) {
        continue;
      }
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    if (task) {
      TUIDE_MON_SCOPE("git", "worker_task");
      task();
    }
    pending_tasks_.fetch_sub(1);
  }
}

bool GitService::busy() const { return pending_tasks_.load() > 0; }

void GitService::set_update_callback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  update_callback_ = std::move(callback);
}

uint64_t GitService::cache_revision() const { return cache_revision_.load(); }

void GitService::notify_updated() {
  cache_revision_.fetch_add(1, std::memory_order_relaxed);
  pending_ui_notify_.store(true, std::memory_order_release);
}

void GitService::run_on_ui_thread(std::function<void()> task) {
  if (!task) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(completion_mutex_);
    pending_completions_.push_back(std::move(task));
  }
  pending_ui_notify_.store(true, std::memory_order_release);
}

void GitService::dispatch_completion(CompletionCallback on_done, bool success,
                                     const std::string& message) {
  if (!on_done) {
    return;
  }
  run_on_ui_thread([on_done = std::move(on_done), success, message]() {
    on_done(success, message);
  });
}

bool GitService::has_file_diff_text(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string rel = repo_relative_path_unlocked(path);
  return diff_text_cached_unlocked(rel);
}

bool GitService::is_untracked(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string rel = repo_relative_path_unlocked(path);
  for (const auto& entry : status_.entries) {
    if (entry.path == rel) {
      return entry.unstaged == GitFileStatus::kUntracked;
    }
  }
  return false;
}

void GitService::open(const std::string& workspace_root) {
  const std::string root = normalize_path(workspace_root);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_root_ = root;
    main_repo_root_.clear();
    main_repo_valid_ = false;
    subrepos_.clear();
    context_repo_root_.clear();
    repo_info_ = {};
    status_ = {};
    file_diffs_.clear();
    file_diff_texts_.clear();
    log_entries_.clear();
    branches_.clear();
    log_search_query_.clear();
    log_search_results_.clear();
    timeline_path_.clear();
    file_timeline_.clear();
    timeline_diff_texts_.clear();
    commit_files_.clear();
    graph_lines_.clear();
    graph_loaded_ = false;
    if (!root.empty()) {
      repo_info_.root = root;
    }
  }
  if (root.empty()) {
    return;
  }
  enqueue([this, root] { discover_repos(root); });
}

void GitService::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_root_.clear();
    main_repo_root_.clear();
    main_repo_valid_ = false;
    subrepos_.clear();
    context_repo_root_.clear();
    repo_info_ = {};
    status_ = {};
    file_diffs_.clear();
    file_diff_texts_.clear();
    log_entries_.clear();
    branches_.clear();
    log_search_query_.clear();
    log_search_results_.clear();
    timeline_path_.clear();
    file_timeline_.clear();
    timeline_diff_texts_.clear();
    commit_files_.clear();
    graph_lines_.clear();
    graph_loaded_ = false;
  }
}

void GitService::discover_repos(const std::string& workspace_root) {
  if (stop_.load()) {
    return;
  }

  GitRepoInfo info;
  info.root = workspace_root;

  const std::string toplevel = git_toplevel(workspace_root);
  const bool main_valid = !toplevel.empty() && is_git_work_tree(workspace_root);
  const std::string main_root = main_valid ? toplevel : std::string{};

  std::vector<GitSubrepoInfo> subrepos;
  if (main_valid) {
    subrepos = discover_submodules(main_root);
    const auto nested = discover_nested_repos(workspace_root, main_root, subrepos);
    subrepos.insert(subrepos.end(), nested.begin(), nested.end());
  } else {
    subrepos = discover_nested_repos(workspace_root, {}, {});
  }

  if (!main_valid && subrepos.empty()) {
    info.valid = false;
    info.last_error = i18n::tr("git.not_repo");
    std::lock_guard<std::mutex> lock(mutex_);
    if (workspace_root_ != workspace_root) {
      return;
    }
    main_repo_valid_ = false;
    main_repo_root_.clear();
    subrepos_.clear();
    context_repo_root_.clear();
    repo_info_ = info;
    notify_updated();
    return;
  }

  info.valid = true;
  info.subrepo_count = static_cast<int>(subrepos.size());

  if (main_valid) {
    const auto branch = run_git(main_root, {"rev-parse", "--abbrev-ref", "HEAD"});
    if (branch.success()) {
      info.branch = trim_newline(branch.stdout_text);
    }
    const auto head = run_git(main_root, {"rev-parse", "HEAD"});
    if (head.success()) {
      info.head = trim_newline(head.stdout_text);
    }
    info.root = main_root;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (workspace_root_ != workspace_root) {
      return;
    }
    main_repo_valid_ = main_valid;
    main_repo_root_ = main_root;
    subrepos_ = std::move(subrepos);
    repo_info_ = info;
    context_repo_root_ =
        main_valid ? main_root : (subrepos_.empty() ? std::string{} : subrepos_.front().root);
  }
  notify_updated();
  load_all_status();
}

void GitService::invalidate(const std::string& path) {
  std::string root;
  std::string rel;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
    if (path.empty()) {
      file_diffs_.clear();
      file_diff_texts_.clear();
      commit_files_.clear();
      graph_lines_.clear();
      graph_loaded_ = false;
      log_search_query_.clear();
      log_search_results_.clear();
    } else {
      rel = repo_relative_path_unlocked(path);
      file_diff_texts_.erase(rel);
      const auto it = file_diffs_.find(rel);
      if (it != file_diffs_.end()) {
        it->second.line_changes.clear();
        it->second.previous_content_by_line.clear();
      }
    }
  }
  if (root.empty()) {
    return;
  }
  enqueue([this, root, rel, full = path.empty()]() {
    if (full) {
      load_all_status();
    } else if (!rel.empty()) {
      ResolvedGitPath resolved;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        resolved = resolve_path_unlocked(rel);
      }
      if (resolved.valid) {
        load_file_diff_text(resolved.repo_root, resolved.repo_rel, rel);
        load_file_head(resolved.repo_root, resolved.repo_rel, rel);
      }
      load_all_status();
    }
    notify_updated();
  });
}

void GitService::refresh_status() {
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
  }
  if (root.empty()) {
    return;
  }
  bool expected = false;
  if (!inflight_status_.compare_exchange_strong(expected, true)) {
    return;
  }
  enqueue([this, root]() {
    load_all_status();
    inflight_status_.store(false, std::memory_order_release);
    notify_updated();
  });
}

void GitService::refresh_status_and_diffs() {
  refresh_status();
}

void GitService::refresh_file_diff(const std::string& path, bool force) {
  if (path.empty()) {
    return;
  }
  std::string workspace_rel;
  ResolvedGitPath resolved;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_rel = repo_relative_path_unlocked(path);
    if (workspace_rel.empty()) {
      return;
    }
    if (!force &&
        (diff_text_cached_unlocked(workspace_rel) || inflight_diffs_.count(workspace_rel) > 0)) {
      return;
    }
    inflight_diffs_.insert(workspace_rel);
    resolved = resolve_path_unlocked(workspace_rel);
  }
  if (!resolved.valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_diffs_.erase(workspace_rel);
    return;
  }
  enqueue([this, resolved, workspace_rel]() {
    load_file_diff_text(resolved.repo_root, resolved.repo_rel, workspace_rel);
    load_file_head(resolved.repo_root, resolved.repo_rel, workspace_rel);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_diffs_.erase(workspace_rel);
    }
    notify_updated();
  });
}

void GitService::refresh_file_head(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::string workspace_rel;
  ResolvedGitPath resolved;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_rel = repo_relative_path_unlocked(path);
    if (workspace_rel.empty()) {
      return;
    }
    const auto it = file_diffs_.find(workspace_rel);
    if (it != file_diffs_.end() && it->second.loaded && !it->second.head_lines.empty()) {
      return;
    }
    if (inflight_heads_.count(workspace_rel) > 0) {
      return;
    }
    inflight_heads_.insert(workspace_rel);
    resolved = resolve_path_unlocked(workspace_rel);
  }
  if (!resolved.valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_heads_.erase(workspace_rel);
    return;
  }
  enqueue([this, resolved, workspace_rel]() {
    load_file_head(resolved.repo_root, resolved.repo_rel, workspace_rel);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_heads_.erase(workspace_rel);
    }
    notify_updated();
  });
}

void GitService::refresh_log() {
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
  }
  if (repo_root.empty()) {
    return;
  }
  enqueue([this, repo_root]() {
    load_log(repo_root);
    notify_updated();
  });
}

void GitService::refresh_log_search(const std::string& query) {
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
    if (query.empty()) {
      log_search_query_.clear();
      log_search_results_.clear();
      return;
    }
    if (log_search_query_ == query) {
      return;
    }
    if (inflight_log_searches_.count(query) > 0) {
      return;
    }
    inflight_log_searches_.insert(query);
  }
  if (repo_root.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_log_searches_.erase(query);
    return;
  }
  enqueue([this, repo_root, query]() {
    load_log_search(repo_root, query);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_log_searches_.erase(query);
    }
    notify_updated();
  });
}

void GitService::refresh_branches() {
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
  }
  if (repo_root.empty()) {
    return;
  }
  enqueue([this, repo_root]() {
    load_branches(repo_root);
    notify_updated();
  });
}

void GitService::refresh_file_timeline(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::string workspace_rel;
  ResolvedGitPath resolved;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_rel = repo_relative_path_unlocked(path);
    if (workspace_rel.empty()) {
      return;
    }
    if (timeline_path_ == workspace_rel) {
      return;
    }
    if (inflight_timelines_.count(workspace_rel) > 0) {
      return;
    }
    inflight_timelines_.insert(workspace_rel);
    resolved = resolve_path_unlocked(workspace_rel);
  }
  if (!resolved.valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_timelines_.erase(workspace_rel);
    return;
  }
  enqueue([this, resolved, workspace_rel]() {
    load_file_timeline(resolved.repo_root, resolved.repo_rel, workspace_rel);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_timelines_.erase(workspace_rel);
    }
    notify_updated();
  });
}

void GitService::refresh_timeline_diff(const std::string& path, const std::string& commit_hash) {
  if (path.empty() || commit_hash.empty()) {
    return;
  }
  std::string workspace_rel;
  ResolvedGitPath resolved;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_rel = repo_relative_path_unlocked(path);
    if (workspace_rel.empty()) {
      return;
    }
    if (timeline_diff_cached_unlocked(workspace_rel, commit_hash)) {
      return;
    }
    const std::string key = timeline_diff_key_unlocked(workspace_rel, commit_hash);
    if (inflight_timeline_diffs_.count(key) > 0) {
      return;
    }
    inflight_timeline_diffs_.insert(key);
    resolved = resolve_path_unlocked(workspace_rel);
  }
  if (!resolved.valid) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_timeline_diffs_.erase(timeline_diff_key_unlocked(workspace_rel, commit_hash));
    return;
  }
  enqueue([this, resolved, workspace_rel, commit_hash]() {
    load_timeline_diff(resolved.repo_root, resolved.repo_rel, workspace_rel, commit_hash);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_timeline_diffs_.erase(timeline_diff_key_unlocked(workspace_rel, commit_hash));
    }
    notify_updated();
  });
}

void GitService::refresh_commit_files(const std::string& commit_hash) {
  if (commit_hash.empty()) {
    return;
  }
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
    if (commit_files_cached_unlocked(commit_hash)) {
      return;
    }
    if (inflight_commit_files_.count(commit_hash) > 0) {
      return;
    }
    inflight_commit_files_.insert(commit_hash);
  }
  if (repo_root.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_commit_files_.erase(commit_hash);
    return;
  }
  enqueue([this, repo_root, commit_hash]() {
    load_commit_files(repo_root, commit_hash);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_commit_files_.erase(commit_hash);
    }
    notify_updated();
  });
}

void GitService::refresh_graph() {
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
    if (graph_loaded_) {
      return;
    }
    bool expected = false;
    if (!inflight_graph_.compare_exchange_strong(expected, true)) {
      return;
    }
  }
  if (repo_root.empty()) {
    inflight_graph_.store(false);
    return;
  }
  enqueue([this, repo_root]() {
    load_graph(repo_root);
    inflight_graph_.store(false);
    notify_updated();
  });
}

bool GitService::is_repo() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return repo_info_.valid;
}

bool GitService::has_subrepos() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !subrepos_.empty();
}

GitRepoInfo GitService::repo_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return repo_info_;
}

GitRepoInfo GitService::context_repo_info() const {
  std::string root;
  GitRepoInfo main_info;
  std::string sub_workspace_path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = context_repo_root_unlocked();
    main_info = repo_info_;
    if (root.empty()) {
      return {};
    }
    if (main_repo_valid_ && root == main_repo_root_) {
      return main_info;
    }
    for (const auto& sub : subrepos_) {
      if (sub.root == root) {
        sub_workspace_path = sub.workspace_path;
        break;
      }
    }
  }

  GitRepoInfo info;
  info.valid = true;
  info.root = root;
  const auto branch = run_git(root, {"rev-parse", "--abbrev-ref", "HEAD"});
  if (branch.success()) {
    info.branch = trim_newline(branch.stdout_text);
  }
  const auto head = run_git(root, {"rev-parse", "HEAD"});
  if (head.success()) {
    info.head = trim_newline(head.stdout_text);
  }
  if (!sub_workspace_path.empty()) {
    info.branch = sub_workspace_path + (info.branch.empty() ? "" : " @ " + info.branch);
  }
  return info;
}

void GitService::set_context_from_path(const std::string& workspace_rel_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  const ResolvedGitPath resolved = resolve_path_unlocked(workspace_rel_path);
  if (resolved.valid) {
    context_repo_root_ = resolved.repo_root;
  }
}

GitStatusSnapshot GitService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

GitFileDiff GitService::file_diff(const std::string& absolute_path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string rel = repo_relative_path_unlocked(absolute_path);
  const auto it = file_diffs_.find(rel);
  if (it != file_diffs_.end()) {
    return it->second;
  }
  const auto it2 = file_diffs_.find(absolute_path);
  if (it2 != file_diffs_.end()) {
    return it2->second;
  }
  return {};
}

std::vector<GitCommitEntry> GitService::log_entries() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return log_entries_;
}

std::vector<GitCommitEntry> GitService::log_search_results() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return log_search_results_;
}

bool GitService::log_search_ready(const std::string& query) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return query.empty() || log_search_query_ == query;
}

std::vector<GitBranchEntry> GitService::branches() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return branches_;
}

std::vector<GitCommitEntry> GitService::file_timeline(const std::string& absolute_path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string rel = repo_relative_path_unlocked(absolute_path);
  if (rel != timeline_path_) {
    return {};
  }
  return file_timeline_;
}

std::string GitService::timeline_diff_text(const std::string& absolute_path,
                                           const std::string& commit_hash) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string rel = repo_relative_path_unlocked(absolute_path);
  const auto it = timeline_diff_texts_.find(timeline_diff_key_unlocked(rel, commit_hash));
  if (it != timeline_diff_texts_.end()) {
    return it->second;
  }
  return {};
}

bool GitService::has_timeline_diff_text(const std::string& absolute_path,
                                        const std::string& commit_hash) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string rel = repo_relative_path_unlocked(absolute_path);
  return timeline_diff_cached_unlocked(rel, commit_hash);
}

std::vector<GitCommitFileEntry> GitService::commit_files(const std::string& commit_hash) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = commit_files_.find(commit_hash);
  if (it != commit_files_.end()) {
    return it->second;
  }
  return {};
}

bool GitService::has_commit_files(const std::string& commit_hash) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return commit_files_cached_unlocked(commit_hash);
}

std::vector<std::string> GitService::graph_lines() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return graph_lines_;
}

bool GitService::graph_loaded() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return graph_loaded_;
}

std::string GitService::file_diff_text(const std::string& absolute_path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string rel = repo_relative_path_unlocked(absolute_path);
  const auto it = file_diff_texts_.find(rel);
  if (it != file_diff_texts_.end()) {
    return it->second;
  }
  const auto it2 = file_diff_texts_.find(absolute_path);
  if (it2 != file_diff_texts_.end()) {
    return it2->second;
  }
  return {};
}

std::string GitService::previous_line_content(const std::string& absolute_path, int line) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!repo_info_.valid || line < 0) {
    return {};
  }
  const std::string rel = repo_relative_path_unlocked(absolute_path);
  const auto diff_it = file_diffs_.find(rel);
  if (diff_it == file_diffs_.end()) {
    return {};
  }
  const GitFileDiff& diff = diff_it->second;
  const auto prev_it = diff.previous_content_by_line.find(line);
  if (prev_it != diff.previous_content_by_line.end()) {
    return prev_it->second;
  }
  const auto change_it = diff.line_changes.find(line);
  if (change_it != diff.line_changes.end() && change_it->second == GitLineChange::kAdded) {
    return i18n::tr("git.new_line");
  }
  if (line >= 0 && static_cast<std::size_t>(line) < diff.head_lines.size()) {
    return diff.head_lines[static_cast<std::size_t>(line)];
  }
  return {};
}

void GitService::stage_file(const std::string& path, CompletionCallback on_done) {
  stage_files({path}, std::move(on_done));
}

void GitService::unstage_file(const std::string& path, CompletionCallback on_done) {
  unstage_files({path}, std::move(on_done));
}

void GitService::stage_files(const std::vector<std::string>& paths, CompletionCallback on_done) {
  if (paths.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  struct ResolvedItem {
    std::string repo_root;
    std::string repo_rel;
  };
  std::vector<ResolvedItem> items;
  items.reserve(paths.size());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::string& path : paths) {
      const ResolvedGitPath resolved = resolve_path_unlocked(path);
      if (!resolved.valid) {
        continue;
      }
      context_repo_root_ = resolved.repo_root;
      items.push_back({resolved.repo_root, resolved.repo_rel});
    }
  }
  if (items.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  enqueue([this, items = std::move(items), on_done]() {
    std::map<std::string, std::vector<std::string>> by_root;
    for (const ResolvedItem& item : items) {
      by_root[item.repo_root].push_back(item.repo_rel);
    }
    for (auto& [repo_root, rels] : by_root) {
      std::vector<std::string> args = {"add", "--"};
      args.insert(args.end(), rels.begin(), rels.end());
      const auto result = run_git(repo_root, args);
      if (!result.success()) {
        dispatch_completion(on_done, false, result.stderr_text);
        return;
      }
    }
    invalidate();
    dispatch_completion(on_done, true, {});
  });
}

void GitService::unstage_files(const std::vector<std::string>& paths, CompletionCallback on_done) {
  if (paths.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  struct ResolvedItem {
    std::string repo_root;
    std::string repo_rel;
  };
  std::vector<ResolvedItem> items;
  items.reserve(paths.size());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::string& path : paths) {
      const ResolvedGitPath resolved = resolve_path_unlocked(path);
      if (!resolved.valid) {
        continue;
      }
      context_repo_root_ = resolved.repo_root;
      items.push_back({resolved.repo_root, resolved.repo_rel});
    }
  }
  if (items.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  enqueue([this, items = std::move(items), on_done]() {
    std::map<std::string, std::vector<std::string>> by_root;
    for (const ResolvedItem& item : items) {
      by_root[item.repo_root].push_back(item.repo_rel);
    }
    for (auto& [repo_root, rels] : by_root) {
      std::vector<std::string> args = {"reset", "HEAD", "--"};
      args.insert(args.end(), rels.begin(), rels.end());
      const auto result = run_git(repo_root, args);
      if (!result.success()) {
        dispatch_completion(on_done, false, result.stderr_text);
        return;
      }
    }
    invalidate();
    dispatch_completion(on_done, true, {});
  });
}

void GitService::discard_files(const std::vector<std::string>& paths, CompletionCallback on_done) {
  if (paths.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  struct ResolvedItem {
    std::string repo_root;
    std::string repo_rel;
    bool untracked = false;
  };
  std::vector<ResolvedItem> items;
  items.reserve(paths.size());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::string& path : paths) {
      const ResolvedGitPath resolved = resolve_path_unlocked(path);
      if (!resolved.valid) {
        continue;
      }
      context_repo_root_ = resolved.repo_root;
      bool untracked = false;
      for (const GitStatusEntry& entry : status_.entries) {
        if (entry.path == path || entry.path == resolved.workspace_rel) {
          untracked = entry.unstaged == GitFileStatus::kUntracked ||
                      entry.staged == GitFileStatus::kUntracked;
          break;
        }
      }
      items.push_back({resolved.repo_root, resolved.repo_rel, untracked});
    }
  }
  if (items.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  enqueue([this, items = std::move(items), on_done]() {
    std::map<std::string, std::vector<std::string>> restore_by_root;
    std::map<std::string, std::vector<std::string>> clean_by_root;
    for (const ResolvedItem& item : items) {
      if (item.untracked) {
        clean_by_root[item.repo_root].push_back(item.repo_rel);
      } else {
        restore_by_root[item.repo_root].push_back(item.repo_rel);
      }
    }
    for (auto& [repo_root, rels] : restore_by_root) {
      std::vector<std::string> args = {"restore", "--source=HEAD", "--staged", "--worktree", "--"};
      args.insert(args.end(), rels.begin(), rels.end());
      const auto result = run_git(repo_root, args);
      if (!result.success()) {
        dispatch_completion(on_done, false, result.stderr_text);
        return;
      }
    }
    for (auto& [repo_root, rels] : clean_by_root) {
      std::vector<std::string> args = {"clean", "-f", "--"};
      args.insert(args.end(), rels.begin(), rels.end());
      const auto result = run_git(repo_root, args);
      if (!result.success()) {
        dispatch_completion(on_done, false, result.stderr_text);
        return;
      }
    }
    invalidate();
    dispatch_completion(on_done, true, {});
  });
}

void GitService::commit(const std::string& message, CompletionCallback on_done) {
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
  }
  if (repo_root.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  enqueue([this, repo_root, message, on_done]() {
    const auto result = run_git(repo_root, {"commit", "-m", message});
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, result.stdout_text);
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::checkout_branch(const std::string& branch, CompletionCallback on_done) {
  std::string repo_root;
  std::string workspace;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
    workspace = workspace_root_;
  }
  if (repo_root.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  enqueue([this, repo_root, workspace, branch, on_done]() {
    const auto result = run_git(repo_root, {"switch", branch});
    if (result.success()) {
      open(workspace);
      dispatch_completion(on_done, true, {});
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::push(CompletionCallback on_done) {
  push(GitCredentials{}, std::move(on_done));
}

void GitService::push(const GitCredentials& credentials, CompletionCallback on_done) {
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
  }
  if (repo_root.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  enqueue([this, repo_root, credentials, on_done]() {
    const auto result =
        credentials.password.empty() && credentials.username.empty()
            ? run_git(repo_root, {"push"})
            : run_git_with_credentials(repo_root, {"push"}, credentials);
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, result.stdout_text);
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::pull(CompletionCallback on_done) {
  pull(GitCredentials{}, std::move(on_done));
}

void GitService::pull(const GitCredentials& credentials, CompletionCallback on_done) {
  std::string repo_root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    repo_root = context_repo_root_unlocked();
  }
  if (repo_root.empty()) {
    dispatch_completion(on_done, false, i18n::tr("git.not_repo"));
    return;
  }
  enqueue([this, repo_root, credentials, on_done]() {
    const auto result =
        credentials.password.empty() && credentials.username.empty()
            ? run_git(repo_root, {"pull"})
            : run_git_with_credentials(repo_root, {"pull"}, credentials);
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, result.stdout_text);
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::tick() {
  TUIDE_MON_SCOPE("git", "tick");
  std::deque<std::function<void()>> completions;
  {
    std::lock_guard<std::mutex> lock(completion_mutex_);
    completions.swap(pending_completions_);
  }
  for (auto& task : completions) {
    if (task) {
      task();
    }
  }

  if (pending_ui_notify_.exchange(false, std::memory_order_acq_rel)) {
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = update_callback_;
    }
    if (callback) {
      callback();
    }
  }

  if (!is_repo()) {
    return;
  }
  static thread_local int counter = 0;
  if (++counter % 300 == 0) {
    refresh_status();
  }
}

std::string GitService::repo_relative_path_unlocked(const std::string& path) const {
  if (path.empty()) {
    return {};
  }
  if (path[0] == '/') {
    return relative_path_unlocked(path);
  }
  return path;
}

std::string GitService::relative_path_unlocked(const std::string& absolute_path) const {
  if (workspace_root_.empty() || absolute_path.empty()) {
    return absolute_path;
  }
  std::error_code ec;
  const fs::path rel = fs::relative(fs::path(absolute_path), fs::path(workspace_root_), ec);
  if (ec) {
    return absolute_path;
  }
  return rel.string();
}

void GitService::set_error_unlocked(const std::string& message) {
  repo_info_.last_error = message;
}

bool GitService::diff_text_cached_unlocked(const std::string& rel) const {
  return file_diff_texts_.find(rel) != file_diff_texts_.end();
}

void GitService::load_all_status() {
  if (stop_.load()) {
    return;
  }

  std::string workspace;
  std::string main_root;
  bool main_valid = false;
  std::vector<GitSubrepoInfo> subrepos;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace = workspace_root_;
    main_root = main_repo_root_;
    main_valid = main_repo_valid_;
    subrepos = subrepos_;
  }
  if (workspace.empty()) {
    return;
  }

  GitStatusSnapshot merged;
  if (main_valid && !main_root.empty()) {
    const auto result = run_git(main_root, {"status", "--porcelain"});
    if (result.success()) {
      const auto parsed = parse_git_status_porcelain(result.stdout_text);
      for (GitStatusEntry entry : parsed.entries) {
        if (main_root != workspace) {
          const std::string ws_rel =
              repo_to_workspace_rel(workspace, main_root, entry.path);
          if (ws_rel.empty()) {
            continue;
          }
          entry.path = ws_rel;
        }
        if (entry.staged != GitFileStatus::kUnmodified) {
          ++merged.staged_count;
        }
        if (entry.unstaged == GitFileStatus::kUntracked) {
          ++merged.untracked_count;
        } else if (entry.unstaged != GitFileStatus::kUnmodified) {
          ++merged.unstaged_count;
        }
        merged.entries.push_back(std::move(entry));
      }
    }
  }

  for (const auto& sub : subrepos) {
    const auto result = run_git(sub.root, {"status", "--porcelain"});
    if (!result.success()) {
      continue;
    }
    const auto parsed = parse_git_status_porcelain(result.stdout_text);
    for (GitStatusEntry entry : parsed.entries) {
      entry.path = join_workspace_path(sub.workspace_path, entry.path);
      entry.repo_prefix = sub.workspace_path;
      if (entry.staged != GitFileStatus::kUnmodified) {
        ++merged.staged_count;
      }
      if (entry.unstaged == GitFileStatus::kUntracked) {
        ++merged.untracked_count;
      } else if (entry.unstaged != GitFileStatus::kUnmodified) {
        ++merged.unstaged_count;
      }
      merged.entries.push_back(std::move(entry));
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_ != workspace) {
    return;
  }
  status_ = std::move(merged);
}

void GitService::load_file_diff_text(const std::string& repo_root, const std::string& repo_rel,
                                     const std::string& workspace_rel) {
  if (stop_.load() || repo_rel.empty() || workspace_rel.empty()) {
    return;
  }

  const auto diff_result = run_git(repo_root, {"diff", "HEAD", "--", repo_rel});
  GitFileDiff parsed = parse_unified_diff(workspace_rel, diff_result.stdout_text);
  parsed.path = workspace_rel;

  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }

  parsed.untracked = false;
  for (const auto& entry : status_.entries) {
    if (entry.path == workspace_rel && entry.unstaged == GitFileStatus::kUntracked) {
      parsed.untracked = true;
      break;
    }
  }

  auto& existing = file_diffs_[workspace_rel];
  parsed.head_lines = existing.head_lines;
  // Baseline is complete only with HEAD content or confirmed untracked. Having
  // unified hunks alone must not set loaded — that let Myers run against empty HEAD.
  parsed.loaded = parsed.untracked || !parsed.head_lines.empty();
  existing = std::move(parsed);
  file_diff_texts_[workspace_rel] = diff_result.stdout_text;
}

void GitService::load_file_head(const std::string& repo_root, const std::string& repo_rel,
                                const std::string& workspace_rel) {
  if (stop_.load() || repo_rel.empty() || workspace_rel.empty()) {
    return;
  }

  const auto head_result = run_git(repo_root, {"show", "HEAD:" + repo_rel});

  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }

  GitFileDiff& diff = file_diffs_[workspace_rel];
  diff.path = workspace_rel;
  const auto text_it = file_diff_texts_.find(workspace_rel);
  if (text_it != file_diff_texts_.end()) {
    diff = parse_unified_diff(workspace_rel, text_it->second);
    diff.path = workspace_rel;
  }

  if (head_result.success()) {
    diff.head_lines = parse_file_lines(head_result.stdout_text);
    diff.loaded = true;
    diff.untracked = false;
  } else {
    // Only treat as untracked when status confirms it. A failed/slow `git show`
    // on a tracked file must leave the baseline pending (not all-green).
    bool untracked = false;
    for (const auto& entry : status_.entries) {
      if (entry.path == workspace_rel && entry.unstaged == GitFileStatus::kUntracked) {
        untracked = true;
        break;
      }
    }
    diff.head_lines.clear();
    diff.untracked = untracked;
    diff.loaded = untracked;
  }
  file_diffs_[workspace_rel] = diff;
}

void GitService::load_log(const std::string& root) {
  if (stop_.load()) {
    return;
  }
  const auto result =
      run_git(root, {"log", "-n", "50", "--decorate=short", "--format=%H|%h|%s"});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }
  if (result.success()) {
    log_entries_ = parse_git_log_list(result.stdout_text);
    commit_files_.clear();
    log_search_query_.clear();
    log_search_results_.clear();
  }
}

void GitService::load_log_search(const std::string& root, const std::string& query) {
  if (stop_.load() || query.empty()) {
    return;
  }

  std::vector<GitCommitEntry> merged;
  std::unordered_set<std::string> seen;
  const std::string query_lower = to_lower_ascii(query);

  const auto grep_result = run_git(
      root, {"log", "-n", "200", "--format=%H|%h|%s", "--fixed-strings", "--grep=" + query, "-i"});
  if (grep_result.success()) {
    merge_log_entries(&merged, parse_git_log_list(grep_result.stdout_text), &seen);
  }

  if (looks_like_hash_query(query)) {
    const auto rev_result =
        run_git(root, {"log", "-n", "20", "--format=%H|%h|%s", "--all", query});
    if (rev_result.success()) {
      merge_log_entries(&merged, parse_git_log_list(rev_result.stdout_text), &seen);
    }

    const auto scan_result =
        run_git(root, {"log", "-n", "500", "--format=%H|%h|%s", "--all"});
    if (scan_result.success()) {
      const auto scanned = parse_git_log_list(scan_result.stdout_text);
      std::vector<GitCommitEntry> filtered;
      filtered.reserve(scanned.size());
      for (const auto& entry : scanned) {
        if (commit_matches_query(entry, query, query_lower)) {
          filtered.push_back(entry);
        }
      }
      merge_log_entries(&merged, filtered, &seen);
    }
  } else {
    const auto scan_result =
        run_git(root, {"log", "-n", "500", "--format=%H|%h|%s", "--all"});
    if (scan_result.success()) {
      const auto scanned = parse_git_log_list(scan_result.stdout_text);
      std::vector<GitCommitEntry> filtered;
      for (const auto& entry : scanned) {
        if (commit_matches_query(entry, query, query_lower)) {
          filtered.push_back(entry);
        }
      }
      merge_log_entries(&merged, filtered, &seen);
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }
  log_search_query_ = query;
  log_search_results_ = std::move(merged);
}

void GitService::load_branches(const std::string& root) {
  if (stop_.load()) {
    return;
  }
  const auto result = run_git(root, {"branch", "-a", "--no-color"});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }
  if (result.success()) {
    branches_ = parse_git_branches(result.stdout_text);
  }
}

std::string GitService::timeline_diff_key_unlocked(const std::string& rel,
                                                  const std::string& commit_hash) const {
  return rel + '\0' + commit_hash;
}

bool GitService::timeline_diff_cached_unlocked(const std::string& rel,
                                               const std::string& commit_hash) const {
  return timeline_diff_texts_.find(timeline_diff_key_unlocked(rel, commit_hash)) !=
         timeline_diff_texts_.end();
}

void GitService::load_file_timeline(const std::string& repo_root, const std::string& repo_rel,
                                    const std::string& workspace_rel) {
  if (stop_.load() || repo_rel.empty() || workspace_rel.empty()) {
    return;
  }
  const auto result = run_git(
      repo_root, {"log", "--follow", "--format=%H|%h|%s|%an|%ar", "-n", "100", "--", repo_rel});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }
  timeline_path_ = workspace_rel;
  timeline_diff_texts_.clear();
  if (result.success()) {
    file_timeline_ = parse_git_log_timeline(result.stdout_text);
  } else {
    file_timeline_.clear();
  }
}

void GitService::load_timeline_diff(const std::string& repo_root, const std::string& repo_rel,
                                    const std::string& workspace_rel,
                                    const std::string& commit_hash) {
  if (stop_.load() || repo_rel.empty() || workspace_rel.empty() || commit_hash.empty()) {
    return;
  }
  const auto result = run_git(repo_root, {"show", commit_hash, "--", repo_rel});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }
  timeline_diff_texts_[timeline_diff_key_unlocked(workspace_rel, commit_hash)] = result.stdout_text;
}

bool GitService::commit_files_cached_unlocked(const std::string& commit_hash) const {
  return commit_files_.find(commit_hash) != commit_files_.end();
}

void GitService::load_commit_files(const std::string& root, const std::string& commit_hash) {
  if (stop_.load() || commit_hash.empty()) {
    return;
  }
  const auto result =
      run_git(root, {"diff-tree", "--no-commit-id", "--name-status", "-r", commit_hash});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }
  if (result.success()) {
    commit_files_[commit_hash] = parse_commit_name_status(result.stdout_text);
  } else {
    commit_files_[commit_hash] = {};
  }
}

void GitService::load_graph(const std::string& root) {
  if (stop_.load()) {
    return;
  }
  const auto result =
      run_git(root, {"log", "--graph", "--oneline", "--all", "--decorate", "-n", "80"});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_.empty()) {
    return;
  }
  graph_loaded_ = true;
  if (result.success()) {
    graph_lines_ = split_git_graph_lines(result.stdout_text);
  } else {
    graph_lines_.clear();
  }
}

ResolvedGitPath GitService::resolve_path_unlocked(const std::string& path) const {
  return resolve_git_path(workspace_root_, main_repo_root_, main_repo_valid_, subrepos_, path);
}

std::string GitService::context_repo_root_unlocked() const {
  if (!context_repo_root_.empty()) {
    return context_repo_root_;
  }
  if (main_repo_valid_) {
    return main_repo_root_;
  }
  if (!subrepos_.empty()) {
    return subrepos_.front().root;
  }
  return {};
}

}  // namespace tuide
