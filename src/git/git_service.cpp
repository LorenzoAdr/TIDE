#include "git/git_service.hpp"

#include <filesystem>
#include <utility>

#include "git/git_command.hpp"
#include "util/monitor_log.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string trim_newline(std::string value) {
  if (!value.empty() && value.back() == '\n') {
    value.pop_back();
  }
  return value;
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
      TGDB_MON_SCOPE("git", "worker_task");
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
    repo_info_ = {};
    status_ = {};
    file_diffs_.clear();
    file_diff_texts_.clear();
    log_entries_.clear();
    branches_.clear();
    if (!root.empty()) {
      repo_info_.root = root;
    }
  }
  if (root.empty()) {
    return;
  }
  enqueue([this, root] { detect_repo(root); });
}

void GitService::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_root_.clear();
    repo_info_ = {};
    status_ = {};
    file_diffs_.clear();
    file_diff_texts_.clear();
    log_entries_.clear();
    branches_.clear();
  }
}

void GitService::detect_repo(const std::string& root) {
  if (stop_.load()) {
    return;
  }

  GitRepoInfo info;
  info.root = root;

  const auto tree = run_git(root, {"rev-parse", "--is-inside-work-tree"});
  if (!tree.success() || tree.stdout_text.find("true") == std::string::npos) {
    info.valid = false;
    info.last_error = "no es un repositorio git";
    std::lock_guard<std::mutex> lock(mutex_);
    if (workspace_root_ != root) {
      return;
    }
    repo_info_ = info;
    notify_updated();
    return;
  }

  info.valid = true;
  const auto branch = run_git(root, {"rev-parse", "--abbrev-ref", "HEAD"});
  if (branch.success()) {
    info.branch = trim_newline(branch.stdout_text);
  }
  const auto head = run_git(root, {"rev-parse", "HEAD"});
  if (head.success()) {
    info.head = trim_newline(head.stdout_text);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (workspace_root_ != root) {
      return;
    }
    repo_info_ = info;
  }
  notify_updated();
  load_status(root);
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
      load_status(root);
    } else if (!rel.empty()) {
      load_file_diff_text(root, rel);
      load_file_head(root, rel);
      load_status(root);
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
    load_status(root);
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
  std::string root;
  std::string rel;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
    rel = repo_relative_path_unlocked(path);
    if (!force && (diff_text_cached_unlocked(rel) || inflight_diffs_.count(rel) > 0)) {
      return;
    }
    inflight_diffs_.insert(rel);
  }
  if (root.empty() || rel.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_diffs_.erase(rel);
    return;
  }
  enqueue([this, root, rel]() {
    load_file_diff_text(root, rel);
    load_file_head(root, rel);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_diffs_.erase(rel);
    }
    notify_updated();
  });
}

void GitService::refresh_file_head(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::string root;
  std::string rel;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
    rel = repo_relative_path_unlocked(path);
    const auto it = file_diffs_.find(rel);
    if (it != file_diffs_.end() && it->second.loaded && !it->second.head_lines.empty()) {
      return;
    }
    if (inflight_heads_.count(rel) > 0) {
      return;
    }
    inflight_heads_.insert(rel);
  }
  if (root.empty() || rel.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    inflight_heads_.erase(rel);
    return;
  }
  enqueue([this, root, rel]() {
    load_file_head(root, rel);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      inflight_heads_.erase(rel);
    }
    notify_updated();
  });
}

void GitService::refresh_log() {
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
  }
  if (root.empty()) {
    return;
  }
  enqueue([this, root]() {
    load_log(root);
    notify_updated();
  });
}

void GitService::refresh_branches() {
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
  }
  if (root.empty()) {
    return;
  }
  enqueue([this, root]() {
    load_branches(root);
    notify_updated();
  });
}

bool GitService::is_repo() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return repo_info_.valid;
}

GitRepoInfo GitService::repo_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return repo_info_;
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

std::vector<GitBranchEntry> GitService::branches() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return branches_;
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
    return "(línea nueva)";
  }
  if (line >= 0 && static_cast<std::size_t>(line) < diff.head_lines.size()) {
    return diff.head_lines[static_cast<std::size_t>(line)];
  }
  return {};
}

void GitService::stage_file(const std::string& path, CompletionCallback on_done) {
  std::string rel;
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
    rel = repo_relative_path_unlocked(path);
  }
  enqueue([this, root, rel, on_done]() {
    const auto result = run_git(root, {"add", "--", rel});
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, {});
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::unstage_file(const std::string& path, CompletionCallback on_done) {
  std::string rel;
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
    rel = repo_relative_path_unlocked(path);
  }
  enqueue([this, root, rel, on_done]() {
    const auto result = run_git(root, {"reset", "HEAD", "--", rel});
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, {});
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::commit(const std::string& message, CompletionCallback on_done) {
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
  }
  enqueue([this, root, message, on_done]() {
    const auto result = run_git(root, {"commit", "-m", message});
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, result.stdout_text);
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::checkout_branch(const std::string& branch, CompletionCallback on_done) {
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
  }
  enqueue([this, root, branch, on_done]() {
    const auto result = run_git(root, {"switch", branch});
    if (result.success()) {
      open(root);
      dispatch_completion(on_done, true, {});
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::push(CompletionCallback on_done) {
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
  }
  enqueue([this, root, on_done]() {
    const auto result = run_git(root, {"push"});
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, result.stdout_text);
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::pull(CompletionCallback on_done) {
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    root = workspace_root_;
  }
  enqueue([this, root, on_done]() {
    const auto result = run_git(root, {"pull"});
    if (result.success()) {
      invalidate();
      dispatch_completion(on_done, true, result.stdout_text);
    } else {
      dispatch_completion(on_done, false, result.stderr_text);
    }
  });
}

void GitService::tick() {
  TGDB_MON_SCOPE("git", "tick");
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

void GitService::load_status(const std::string& root) {
  if (stop_.load()) {
    return;
  }
  const auto result = run_git(root, {"status", "--porcelain"});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_ != root) {
    return;
  }
  if (result.success()) {
    status_ = parse_git_status_porcelain(result.stdout_text);
  } else {
    set_error_unlocked(result.stderr_text);
  }
}

void GitService::load_file_diff_text(const std::string& root, const std::string& rel) {
  if (stop_.load() || rel.empty()) {
    return;
  }

  const auto diff_result = run_git(root, {"diff", "HEAD", "--", rel});
  GitFileDiff parsed = parse_unified_diff(rel, diff_result.stdout_text);
  parsed.path = rel;

  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_ != root) {
    return;
  }

  parsed.untracked = false;
  for (const auto& entry : status_.entries) {
    if (entry.path == rel && entry.unstaged == GitFileStatus::kUntracked) {
      parsed.untracked = true;
      break;
    }
  }

  auto& existing = file_diffs_[rel];
  parsed.head_lines = existing.head_lines;
  parsed.loaded = existing.loaded || !parsed.line_changes.empty() || parsed.untracked;
  existing = std::move(parsed);
  file_diff_texts_[rel] = diff_result.stdout_text;
}

void GitService::load_file_head(const std::string& root, const std::string& rel) {
  if (stop_.load() || rel.empty()) {
    return;
  }

  const auto head_result = run_git(root, {"show", "HEAD:" + rel});

  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_ != root) {
    return;
  }

  GitFileDiff& diff = file_diffs_[rel];
  diff.path = rel;
  const auto text_it = file_diff_texts_.find(rel);
  if (text_it != file_diff_texts_.end()) {
    diff = parse_unified_diff(rel, text_it->second);
    diff.path = rel;
  }

  if (head_result.success()) {
    diff.head_lines = parse_file_lines(head_result.stdout_text);
    diff.loaded = true;
    diff.untracked = false;
  } else {
    diff.head_lines.clear();
    diff.loaded = true;
    diff.untracked = true;
  }
  file_diffs_[rel] = diff;
}

void GitService::load_log(const std::string& root) {
  if (stop_.load()) {
    return;
  }
  const auto result =
      run_git(root, {"log", "--oneline", "-n", "50", "--decorate=short"});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_ != root) {
    return;
  }
  if (result.success()) {
    log_entries_ = parse_git_log_oneline(result.stdout_text);
  }
}

void GitService::load_branches(const std::string& root) {
  if (stop_.load()) {
    return;
  }
  const auto result = run_git(root, {"branch", "-a", "--no-color"});
  std::lock_guard<std::mutex> lock(mutex_);
  if (workspace_root_ != root) {
    return;
  }
  if (result.success()) {
    branches_ = parse_git_branches(result.stdout_text);
  }
}

}  // namespace tgdb
