#include "editor/selection_occurrence_runner.hpp"

#include "util/thread_name.hpp"

namespace tgdb {

SelectionOccurrenceRunner::SelectionOccurrenceRunner() {
  worker_ = std::thread([this] { worker_main(); });
}

SelectionOccurrenceRunner::~SelectionOccurrenceRunner() {
  stop_worker();
}

void SelectionOccurrenceRunner::stop_worker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void SelectionOccurrenceRunner::start(uint64_t request_id, SelectionOccurrenceKey key,
                                      std::vector<std::string> lines, std::string needle,
                                      bool whole_word) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_request_id_ = request_id;
    pending_job_ = Job{request_id, std::move(key), std::move(lines), std::move(needle),
                       whole_word};
    has_pending_job_ = true;
    has_ready_result_ = false;
    running_ = true;
  }
  cv_.notify_one();
}

void SelectionOccurrenceRunner::cancel() {
  active_request_id_.fetch_add(1);
  std::lock_guard<std::mutex> lock(mutex_);
  has_pending_job_ = false;
  has_ready_result_ = false;
  running_ = false;
}

bool SelectionOccurrenceRunner::running() const {
  return running_.load();
}

bool SelectionOccurrenceRunner::poll(uint64_t request_id, const SelectionOccurrenceKey& key,
                                     std::vector<TextMatch>* matches) {
  if (matches == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_ready_result_ || ready_result_.request_id != request_id) {
    return false;
  }
  if (ready_result_.key != key) {
    has_ready_result_ = false;
    return false;
  }

  *matches = std::move(ready_result_.matches);
  has_ready_result_ = false;
  if (!has_pending_job_) {
    running_ = false;
  }
  return true;
}

void SelectionOccurrenceRunner::worker_main() {
  set_current_thread_name("sel-occ");

  while (true) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_.load() || has_pending_job_; });
      if (stop_.load()) {
        return;
      }
      job = std::move(pending_job_);
      has_pending_job_ = false;
    }

    if (active_request_id_.load() != job.request_id) {
      continue;
    }

    std::vector<TextMatch> matches = find_occurrences_in_lines(
        job.lines, job.needle, job.whole_word, &active_request_id_, job.request_id);

    std::lock_guard<std::mutex> lock(mutex_);
    if (active_request_id_.load() != job.request_id) {
      if (!has_pending_job_) {
        running_ = false;
      }
      continue;
    }

    ready_result_ = Result{job.request_id, job.key, std::move(matches)};
    has_ready_result_ = true;
    if (!has_pending_job_) {
      running_ = false;
    }
  }
}

}  // namespace tgdb
