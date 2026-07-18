#include "ui/symbol_picker_match_runner.hpp"

#include "util/fuzzy_catalog_filter.hpp"
#include "util/thread_name.hpp"

namespace tuide {

namespace {

constexpr std::size_t kMaxResults = 150;

std::vector<SymbolPickerMatch> search_catalog(std::string_view query_lower,
                                              const std::vector<SymbolCatalogEntry>& catalog) {
  std::vector<FuzzyCatalogEntryView> views;
  views.reserve(catalog.size());
  for (const SymbolCatalogEntry& entry : catalog) {
    views.push_back({entry.symbol.name, entry.name_lower});
  }

  const std::vector<FuzzyCatalogHit> hits =
      fuzzy_filter_catalog(views, query_lower, kMaxResults, kMaxResults);

  std::vector<SymbolPickerMatch> results;
  results.reserve(hits.size());
  for (const FuzzyCatalogHit& hit : hits) {
    results.push_back({catalog[hit.index].symbol, hit.score, hit.match_indices});
  }
  return results;
}

}  // namespace

SymbolPickerMatchRunner::SymbolPickerMatchRunner() {
  worker_ = std::thread([this] { worker_main(); });
}

SymbolPickerMatchRunner::~SymbolPickerMatchRunner() {
  stop_worker();
}

void SymbolPickerMatchRunner::stop_worker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void SymbolPickerMatchRunner::start(
    uint64_t request_id, std::string query_lower,
    std::shared_ptr<const std::vector<SymbolCatalogEntry>> catalog) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_request_id_ = request_id;
    pending_job_ = Job{request_id, std::move(query_lower), std::move(catalog)};
    has_pending_job_ = true;
    has_ready_result_ = false;
    running_ = true;
  }
  cv_.notify_one();
}

void SymbolPickerMatchRunner::cancel() {
  active_request_id_.fetch_add(1);
  std::lock_guard<std::mutex> lock(mutex_);
  has_pending_job_ = false;
  has_ready_result_ = false;
  running_ = false;
}

bool SymbolPickerMatchRunner::running() const {
  return running_.load();
}

bool SymbolPickerMatchRunner::poll(uint64_t request_id, std::vector<SymbolPickerMatch>* matches) {
  if (matches == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!has_ready_result_ || ready_result_.request_id != request_id) {
    return false;
  }

  *matches = std::move(ready_result_.matches);
  has_ready_result_ = false;
  if (!has_pending_job_) {
    running_ = false;
  }
  return true;
}

void SymbolPickerMatchRunner::worker_main() {
  set_current_thread_name("sym-pick");

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

    if (active_request_id_.load() != job.request_id || job.catalog == nullptr) {
      continue;
    }

    std::vector<SymbolPickerMatch> matches = search_catalog(job.query_lower, *job.catalog);

    std::lock_guard<std::mutex> lock(mutex_);
    if (active_request_id_.load() != job.request_id) {
      if (!has_pending_job_) {
        running_ = false;
      }
      continue;
    }

    ready_result_ = Result{job.request_id, std::move(matches)};
    has_ready_result_ = true;
    if (!has_pending_job_) {
      running_ = false;
    }
  }
}

}  // namespace tuide
