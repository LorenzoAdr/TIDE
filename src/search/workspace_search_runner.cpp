#include "search/workspace_search_runner.hpp"

#include <filesystem>
#include <fstream>

#include <signal.h>

#include "indexer/index_rules.hpp"
#include "indexer/workspace_indexer.hpp"
#include "search/workspace_search_rg.hpp"
#include "util/bundled_tools.hpp"
#include "util/monitor_log.hpp"
#include "util/thread_name.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr int kMaxResults = 2000;

std::string trim_preview(const std::string& line) {
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = line.find_last_not_of(" \t");
  return line.substr(start, end - start + 1);
}

void search_file(const WorkspaceSearchOptions& opts, const std::string& rel,
                 std::vector<WorkspaceSearchResult>* results, int* files_scanned) {
  if (results->size() >= static_cast<std::size_t>(kMaxResults)) {
    return;
  }
  if (is_probably_binary_path(rel)) {
    return;
  }

  std::error_code ec;
  const auto absolute = fs::path(opts.workspace_root) / rel;
  if (!fs::is_regular_file(absolute, ec)) {
    return;
  }

  std::ifstream input(absolute, std::ios::binary);
  if (!input) {
    return;
  }

  if (files_scanned != nullptr) {
    ++(*files_scanned);
  }

  std::string line;
  int line_num = 0;
  while (std::getline(input, line)) {
    if (line_num == 0 && text_looks_binary(line)) {
      break;
    }
    ++line_num;
    std::size_t pos = 0;
    while ((pos = line.find(opts.needle, pos)) != std::string::npos) {
      results->push_back({rel, line_num, static_cast<int>(pos) + 1, trim_preview(line)});
      if (results->size() >= static_cast<std::size_t>(kMaxResults)) {
        return;
      }
      pos += opts.needle.size();
    }
  }
}

}  // namespace

WorkspaceSearchRunner::WorkspaceSearchRunner() {
  worker_ = std::thread([this] { worker_main(); });
}

WorkspaceSearchRunner::~WorkspaceSearchRunner() {
  stop_worker();
}

void WorkspaceSearchRunner::stop_worker() {
  cancel();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    wake_callback_ = nullptr;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void WorkspaceSearchRunner::set_wake_callback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  wake_callback_ = std::move(callback);
}

void WorkspaceSearchRunner::notify_wake() {
  std::function<void()> wake;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wake = wake_callback_;
  }
  if (wake) {
    wake();
  }
}

void WorkspaceSearchRunner::start(WorkspaceSearchOptions opts) {
  // Invalidate any in-flight search without joining (worker is persistent).
  const pid_t pid = child_pid_.load();
  if (pid > 0) {
    kill(pid, SIGTERM);
  }
  const uint64_t gen = generation_.fetch_add(1) + 1;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    results_.clear();
    files_scanned_ = 0;
    cancelled_ = false;
    finished_ = false;
    used_rg_ = false;
    ready_generation_ = 0;
    pending_job_ = Job{gen, std::move(opts)};
    running_ = true;
  }
  cv_.notify_one();
}

void WorkspaceSearchRunner::cancel() {
  cancel_requested_ = true;
  generation_.fetch_add(1);
  const pid_t pid = child_pid_.load();
  if (pid > 0) {
    kill(pid, SIGTERM);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  pending_job_.reset();
}

bool WorkspaceSearchRunner::running() const {
  return running_.load();
}

bool WorkspaceSearchRunner::poll(std::vector<WorkspaceSearchResult>* results, bool* cancelled,
                                 int* files_scanned, bool* used_rg) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ready_generation_ == 0) {
    return false;
  }
  if (results != nullptr) {
    *results = results_;
  }
  if (cancelled != nullptr) {
    *cancelled = cancelled_;
  }
  if (files_scanned != nullptr) {
    *files_scanned = files_scanned_;
  }
  if (used_rg != nullptr) {
    *used_rg = used_rg_;
  }
  ready_generation_ = 0;
  return true;
}

void WorkspaceSearchRunner::search_inprocess(uint64_t generation,
                                             const WorkspaceSearchOptions& opts,
                                             const std::vector<std::string>& files,
                                             std::vector<WorkspaceSearchResult>* results,
                                             int* files_scanned, bool* cancelled) {
  if (results == nullptr || cancelled == nullptr) {
    return;
  }
  for (const auto& rel : files) {
    if (cancel_requested_.load() || generation_.load() != generation) {
      *cancelled = true;
      break;
    }
    if (!file_in_search_path(rel, opts.path_filter)) {
      continue;
    }
    if (!file_included(rel, opts.include_pattern)) {
      continue;
    }
    if (file_excluded(rel, opts.exclude_pattern)) {
      continue;
    }
    search_file(opts, rel, results, files_scanned);
  }
}

void WorkspaceSearchRunner::run_job(uint64_t generation, WorkspaceSearchOptions opts) {
  TUIDE_MON_SCOPE("idx", "workspace_search");
  std::vector<WorkspaceSearchResult> results;
  int files_scanned = 0;
  bool cancelled = false;
  bool used_rg = false;

  const auto should_cancel = [&]() {
    return cancel_requested_.load() || generation_.load() != generation;
  };

  auto ensure_files = [&]() -> const std::vector<std::string>& {
    const auto& existing = workspace_search_files(opts);
    if (!existing.empty()) {
      return existing;
    }
    opts.files = scan_workspace_files(opts.workspace_root);
    return opts.files;
  };

  if (const auto rg = resolve_rg(); rg.has_value()) {
    used_rg = true;
    const bool ok = search_workspace_rg(opts, rg->binary_path, should_cancel, &child_pid_,
                                        &results, &files_scanned);
    if (!ok) {
      results.clear();
      files_scanned = 0;
      used_rg = false;
      search_inprocess(generation, opts, ensure_files(), &results, &files_scanned, &cancelled);
    } else if (should_cancel()) {
      cancelled = true;
    }
  } else {
    search_inprocess(generation, opts, ensure_files(), &results, &files_scanned, &cancelled);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_.load() != generation) {
      if (!pending_job_.has_value()) {
        running_ = false;
      }
    } else {
      results_ = std::move(results);
      files_scanned_ = files_scanned;
      cancelled_ = cancelled;
      finished_ = true;
      used_rg_ = used_rg;
      ready_generation_ = generation;
      if (!pending_job_.has_value()) {
        running_ = false;
      }
    }
  }
  // Always wake outside the mutex: either results are ready or cancel finished.
  notify_wake();
}

void WorkspaceSearchRunner::worker_main() {
  set_current_thread_name("ws-search");

  while (true) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_.load() || pending_job_.has_value(); });
      if (stop_.load()) {
        return;
      }
      job = std::move(*pending_job_);
      pending_job_.reset();
    }

    if (generation_.load() != job.generation) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!pending_job_.has_value()) {
        running_ = false;
      }
      continue;
    }

    cancel_requested_ = false;
    run_job(job.generation, std::move(job.opts));
  }
}

}  // namespace tuide
