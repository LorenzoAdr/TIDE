#include "ui/file_picker_match_runner.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

#include "util/fuzzy_match.hpp"
#include "util/path_normalize.hpp"
#include "util/thread_name.hpp"

namespace tgdb {

namespace {

constexpr int kOpenTabScoreBonus = 1000;
constexpr int kSameDirectoryBonus = 400;
constexpr int kSharedPathComponentBonus = 80;
constexpr std::size_t kMaxResults = 500;

std::string picker_directory_label(std::string_view label) {
  const std::size_t slash = label.find_last_of("/\\");
  if (slash == std::string::npos) {
    return {};
  }
  return std::string(label.substr(0, slash));
}

std::string picker_filename(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) {
    return std::string(path);
  }
  return std::string(path.substr(slash + 1));
}

std::size_t picker_filename_offset(std::string_view label) {
  const std::size_t slash = label.find_last_of("/\\");
  return slash == std::string::npos ? 0 : slash + 1;
}

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

int path_proximity_bonus(std::string_view ref_dir, std::string_view candidate_label) {
  if (ref_dir.empty()) {
    return 0;
  }
  const std::string candidate_dir = picker_directory_label(candidate_label);
  if (candidate_dir.empty()) {
    return 0;
  }
  if (ref_dir == candidate_dir) {
    return kSameDirectoryBonus;
  }
  return count_shared_path_components(ref_dir, candidate_dir) * kSharedPathComponentBonus;
}

std::string picker_display_path(const std::string& path, const std::string& workspace_root) {
  if (path.empty()) {
    return path;
  }
  std::error_code ec;
  const std::filesystem::path absolute = std::filesystem::path(path).is_absolute()
                                             ? std::filesystem::path(path)
                                             : std::filesystem::path(workspace_root) / path;
  if (!workspace_root.empty()) {
    const auto relative =
        std::filesystem::relative(absolute, std::filesystem::path(workspace_root), ec);
    if (!ec && !relative.empty()) {
      return relative.string();
    }
  }
  return absolute.filename().string();
}

std::filesystem::path picker_absolute_path(const std::string& match,
                                           const std::string& workspace_root) {
  std::error_code ec;
  const std::filesystem::path match_path(match);
  if (match_path.is_absolute()) {
    return std::filesystem::absolute(match_path, ec);
  }
  return std::filesystem::absolute(std::filesystem::path(workspace_root) / match, ec);
}

std::shared_ptr<const std::vector<FilePickerCatalogEntry>> build_catalog(
    const IndexSnapshot& snapshot) {
  auto catalog = std::make_shared<std::vector<FilePickerCatalogEntry>>();
  catalog->reserve(snapshot.files.size());
  for (const std::string& path : snapshot.files) {
    const std::string filename = picker_filename(path);
    catalog->push_back({path, filename, fuzzy_to_lower(filename)});
  }
  return catalog;
}

std::vector<FilePickerMatch> search_files(std::string_view query_lower,
                                          const std::vector<FilePickerCatalogEntry>& catalog,
                                          const FilePickerSearchParams& params) {
  struct Candidate {
    std::string path;
    std::string label;
    int score = 0;
    std::vector<std::size_t> match_indices;
  };

  std::vector<Candidate> candidates;
  std::unordered_set<std::string> seen;
  const std::string& workspace_root = params.workspace_root;

  const auto try_add = [&](const std::string& path, std::string_view filename,
                           std::string_view filename_lower, int bonus) {
    const FuzzyMatchResult result = fuzzy_match_cached(filename, filename_lower, query_lower);
    if (!result.matched) {
      return;
    }
    const auto absolute = picker_absolute_path(path, workspace_root);
    const std::string normalized = normalize_path(absolute.string());
    if (seen.count(normalized) != 0) {
      return;
    }
    seen.insert(normalized);

    const std::string label = picker_display_path(path, workspace_root);
    const std::size_t filename_offset = picker_filename_offset(label);
    std::vector<std::size_t> label_indices;
    label_indices.reserve(result.indices.size());
    for (const std::size_t index : result.indices) {
      label_indices.push_back(index + filename_offset);
    }

    const int proximity = path_proximity_bonus(params.ref_dir, label);
    candidates.push_back(
        {path, label, result.score + bonus + proximity, std::move(label_indices)});
  };

  for (const std::string& path : params.open_tabs) {
    const std::string filename = picker_filename(picker_display_path(path, workspace_root));
    try_add(path, filename, fuzzy_to_lower(filename), kOpenTabScoreBonus);
  }

  for (const FilePickerCatalogEntry& entry : catalog) {
    try_add(entry.path, entry.filename, entry.filename_lower, 0);
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) {
              if (a.score != b.score) {
                return a.score > b.score;
              }
              if (a.label.size() != b.label.size()) {
                return a.label.size() < b.label.size();
              }
              return a.label < b.label;
            });

  const std::size_t limit = std::min(candidates.size(), kMaxResults);
  std::vector<FilePickerMatch> results;
  results.reserve(limit);
  for (std::size_t i = 0; i < limit; ++i) {
    const Candidate& candidate = candidates[i];
    results.push_back({candidate.path, candidate.score, candidate.match_indices});
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

void FilePickerMatchRunner::start(uint64_t request_id, std::string query_lower,
                                  std::shared_ptr<const IndexSnapshot> snapshot,
                                  FilePickerSearchParams params) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_request_id_ = request_id;
    pending_job_ = Job{request_id, std::move(query_lower), std::move(snapshot),
                       std::move(params)};
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

    if (active_request_id_.load() != job.request_id || job.snapshot == nullptr) {
      continue;
    }

    if (cached_snapshot_.get() != job.snapshot.get()) {
      cached_catalog_ = build_catalog(*job.snapshot);
      cached_snapshot_ = job.snapshot;
    }

    if (active_request_id_.load() != job.request_id) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!has_pending_job_) {
        running_ = false;
      }
      continue;
    }

    std::vector<FilePickerMatch> matches =
        search_files(job.query_lower, *cached_catalog_, job.params);

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
