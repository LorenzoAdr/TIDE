#include "ui/file_picker_match_runner.hpp"

#include <algorithm>
#include <unordered_set>

#include "util/fuzzy_catalog_filter.hpp"
#include "util/fuzzy_match.hpp"
#include "util/thread_name.hpp"

namespace tgdb {

namespace {

constexpr int kOpenTabScoreBonus = 1000;
constexpr int kSameDirectoryBonus = 400;
constexpr int kSharedPathComponentBonus = 80;
constexpr std::size_t kMaxResults = 500;

int count_shared_path_components(std::string_view a, std::string_view b) {
  int shared = 0;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < a.size() && j < b.size()) {
    while (i < a.size() && (a[i] == '/' || a[i] == '\\')) {
      ++i;
    }
    while (j < b.size() && (b[j] == '/' || b[j] == '\\')) {
      ++j;
    }
    if (i >= a.size() || j >= b.size()) {
      break;
    }

    const std::size_t end_a = a.find_first_of("/\\", i);
    const std::size_t end_b = b.find_first_of("/\\", j);
    const std::size_t seg_end_a = end_a == std::string::npos ? a.size() : end_a;
    const std::size_t seg_end_b = end_b == std::string::npos ? b.size() : end_b;
    if (a.substr(i, seg_end_a - i) != b.substr(j, seg_end_b - j)) {
      break;
    }
    ++shared;
    i = seg_end_a;
    j = seg_end_b;
  }
  return shared;
}

int path_proximity_bonus(std::string_view ref_dir, std::string_view candidate_dir) {
  if (ref_dir.empty() || candidate_dir.empty()) {
    return 0;
  }
  if (ref_dir == candidate_dir) {
    return kSameDirectoryBonus;
  }
  return count_shared_path_components(ref_dir, candidate_dir) * kSharedPathComponentBonus;
}

std::size_t filename_offset_in_label(std::string_view label) {
  const std::size_t slash = label.find_last_of("/\\");
  return slash == std::string::npos ? 0 : slash + 1;
}

std::vector<std::size_t> label_match_indices(std::string_view label,
                                             const std::vector<std::size_t>& filename_indices) {
  const std::size_t offset = filename_offset_in_label(label);
  std::vector<std::size_t> indices;
  indices.reserve(filename_indices.size());
  for (const std::size_t index : filename_indices) {
    indices.push_back(index + offset);
  }
  return indices;
}

void try_add_catalog_hit(const FilePickerCatalogEntry& entry, int bonus, std::string_view ref_dir,
                         std::string_view query_lower, std::unordered_set<std::string>* seen,
                         std::vector<FilePickerMatch>* out) {
  const FuzzyMatchResult result =
      fuzzy_match_cached(entry.filename, entry.filename_lower, query_lower);
  if (!result.matched) {
    return;
  }
  if (seen->count(entry.path) != 0) {
    return;
  }
  seen->insert(entry.path);

  const int proximity = path_proximity_bonus(ref_dir, entry.dir_label);
  out->push_back({entry.path, entry.display_label, result.score + bonus + proximity,
                  label_match_indices(entry.display_label, result.indices)});
}

std::vector<FilePickerMatch> search_files(
    std::string_view query_lower, const std::vector<FilePickerCatalogEntry>& catalog,
    const FilePickerSearchParams& params) {
  std::unordered_set<std::string> seen;
  std::vector<FilePickerMatch> results;
  results.reserve(kMaxResults);

  if (query_lower.empty()) {
    return results;
  }

  for (const FilePickerCatalogEntry& entry : params.open_tabs) {
    try_add_catalog_hit(entry, kOpenTabScoreBonus, params.ref_dir, query_lower, &seen, &results);
  }

  std::vector<FuzzyCatalogEntryView> views;
  views.reserve(catalog.size());
  for (const FilePickerCatalogEntry& entry : catalog) {
    views.push_back({entry.filename, entry.filename_lower});
  }

  const std::vector<FuzzyCatalogHit> hits =
      fuzzy_filter_catalog(views, query_lower, kMaxResults, kMaxResults);

  for (const FuzzyCatalogHit& hit : hits) {
    const FilePickerCatalogEntry& entry = catalog[hit.index];
    if (seen.count(entry.path) != 0) {
      continue;
    }
    seen.insert(entry.path);
    const int proximity = path_proximity_bonus(params.ref_dir, entry.dir_label);
    results.push_back({entry.path, entry.display_label, hit.score + proximity,
                       label_match_indices(entry.display_label, hit.match_indices)});
  }

  std::sort(results.begin(), results.end(), [](const FilePickerMatch& a, const FilePickerMatch& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    if (a.display_label.size() != b.display_label.size()) {
      return a.display_label.size() < b.display_label.size();
    }
    return a.display_label < b.display_label;
  });

  if (results.size() > kMaxResults) {
    results.resize(kMaxResults);
  }
  return results;
}

}  // namespace

FilePickerMatchRunner::FilePickerMatchRunner() {
  worker_ = std::thread([this] { worker_main(); });
}

FilePickerMatchRunner::~FilePickerMatchRunner() {
  stop_worker();
}

void FilePickerMatchRunner::stop_worker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void FilePickerMatchRunner::start(
    uint64_t request_id, std::string query_lower,
    std::shared_ptr<const std::vector<FilePickerCatalogEntry>> catalog,
    FilePickerSearchParams params) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_request_id_ = request_id;
    pending_job_ =
        Job{request_id, std::move(query_lower), std::move(catalog), std::move(params)};
    has_pending_job_ = true;
    has_ready_result_ = false;
    running_ = true;
  }
  cv_.notify_one();
}

void FilePickerMatchRunner::cancel() {
  active_request_id_.fetch_add(1);
  std::lock_guard<std::mutex> lock(mutex_);
  has_pending_job_ = false;
  has_ready_result_ = false;
  running_ = false;
}

bool FilePickerMatchRunner::running() const {
  return running_.load();
}

bool FilePickerMatchRunner::poll(uint64_t request_id, std::vector<FilePickerMatch>* matches) {
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

void FilePickerMatchRunner::worker_main() {
  set_current_thread_name("file-pick");

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

    std::vector<FilePickerMatch> matches =
        search_files(job.query_lower, *job.catalog, job.params);

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

}  // namespace tgdb
