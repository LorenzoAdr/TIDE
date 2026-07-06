#include "indexer/workspace_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <unordered_map>

#include "indexer/index_rules.hpp"
#include "indexer/workspace_indexer_rg.hpp"
#include "util/fuzzy_match.hpp"
#include "util/monitor_log.hpp"
#include "util/thread_name.hpp"

#if defined(__linux__)
#include <signal.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace tgdb {

namespace {

void scan_dir(const fs::path& root, const fs::path& current, const IndexFilterOptions& options,
              std::vector<std::string>* out) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (entry.is_directory(ec)) {
      if (should_skip_dir_name(name, options)) {
        continue;
      }
      scan_dir(root, entry.path(), options, out);
    } else if (entry.is_regular_file(ec)) {
      std::error_code rel_ec;
      const auto rel = fs::relative(entry.path(), root, rel_ec);
      if (!rel_ec && should_list_workspace_path(rel.generic_string(), options)) {
        out->push_back(rel.generic_string());
      }
    }
  }
}

#if defined(__linux__)

constexpr int kWatchMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                           IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;

void add_directory_watch(int fd, const fs::path& dir,
                         std::unordered_map<int, fs::path>* watch_dirs) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return;
  }
  const int wd = inotify_add_watch(fd, dir.c_str(), kWatchMask);
  if (wd < 0) {
    return;
  }
  (*watch_dirs)[wd] = dir;
}

void scan_directories_for_watch(int fd, const fs::path& root, const fs::path& current,
                                const IndexFilterOptions& options,
                                std::unordered_map<int, fs::path>* watch_dirs) {
  add_directory_watch(fd, current, watch_dirs);
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_directory(ec)) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (should_skip_dir_name(name, options)) {
      continue;
    }
    scan_directories_for_watch(fd, root, entry.path(), options, watch_dirs);
  }
}

void run_inotify_loop(const std::string& workspace_root, const IndexFilterOptions& filter_options,
                      int inotify_fd, std::atomic<bool>* stop_requested,
                      std::mutex* changes_mutex, std::vector<FileIndexChange>* pending_changes) {
  std::unordered_map<int, fs::path> watch_dirs;
  const fs::path root(workspace_root);
  scan_directories_for_watch(inotify_fd, root, root, filter_options, &watch_dirs);

  std::vector<char> buffer(64 * 1024);
  while (!stop_requested->load()) {
    const ssize_t length =
        read(inotify_fd, buffer.data(), static_cast<ssize_t>(buffer.size()));
    if (length < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    ssize_t offset = 0;
    while (offset < length) {
      const auto* event =
          reinterpret_cast<const inotify_event*>(buffer.data() + offset);
      offset += sizeof(inotify_event) + event->len;

      const auto it = watch_dirs.find(event->wd);
      if (it == watch_dirs.end()) {
        continue;
      }

      const fs::path dir = it->second;
      if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
        watch_dirs.erase(it);
        inotify_rm_watch(inotify_fd, event->wd);
        continue;
      }

      if (event->len == 0) {
        continue;
      }

      const fs::path entry_path = dir / event->name;
      std::error_code ec;
      const fs::path rel = fs::relative(entry_path, root, ec);
      if (ec || rel.empty()) {
        continue;
      }
      const std::string rel_str = rel.generic_string();

      if (event->mask & IN_ISDIR) {
        if ((event->mask & (IN_CREATE | IN_MOVED_TO)) &&
            !should_skip_dir_name(event->name, filter_options)) {
          add_directory_watch(inotify_fd, entry_path, &watch_dirs);
        }
        continue;
      }

      if (!should_list_workspace_path(rel_str, filter_options)) {
        continue;
      }

      FileIndexChange change;
      change.relative_path = rel_str;
      if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
        change.kind = FileIndexChangeKind::Remove;
        change.absolute_path.clear();
      } else {
        change.kind = FileIndexChangeKind::Upsert;
        change.absolute_path = entry_path.string();
      }
      std::lock_guard<std::mutex> lock(*changes_mutex);
      pending_changes->push_back(std::move(change));
    }
  }
}

#endif

}  // namespace

void rebuild_index_files_lower(IndexSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->files_lower.resize(snapshot->files.size());
  for (std::size_t i = 0; i < snapshot->files.size(); ++i) {
    snapshot->files_lower[i] = fuzzy_to_lower(snapshot->files[i]);
  }
}

std::vector<std::string> scan_workspace_files(const std::string& workspace_root,
                                                const IndexFilterOptions& filter_options) {
  std::vector<std::string> files;
  if (list_workspace_files_rg(workspace_root, filter_options, &files)) {
    return files;
  }

  std::error_code ec;
  const fs::path root(workspace_root);
  if (!fs::is_directory(root, ec)) {
    return files;
  }
  scan_dir(root, root, filter_options, &files);
  std::sort(files.begin(), files.end());
  return files;
}

WorkspaceIndexer::WorkspaceIndexer() {
  snapshot_ = std::make_shared<IndexSnapshot>();
}

WorkspaceIndexer::~WorkspaceIndexer() {
  stop();
}

void WorkspaceIndexer::start_scan(const std::string& workspace_root,
                                  const IndexFilterOptions& filter_options) {
  stop();
  stop_requested_ = false;
  scanning_ = true;
  {
    auto snap = std::make_shared<IndexSnapshot>();
    snap->workspace_root = workspace_root;
    snap->filter_options = filter_options;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  {
    std::lock_guard<std::mutex> lock(changes_mutex_);
    pending_changes_.clear();
  }
  worker_ = std::thread([this, workspace_root, filter_options] {
    set_current_thread_name("idx-work");
    worker_main(workspace_root, filter_options);
  });
}

void WorkspaceIndexer::stop() {
  stop_requested_ = true;
#if defined(__linux__)
  if (inotify_fd_ >= 0) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }
#endif
  const pid_t rg_pid = rg_child_pid_.exchange(-1);
  if (rg_pid > 0) {
    kill(rg_pid, SIGTERM);
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  scanning_ = false;
#if defined(__linux__)
  inotify_fd_ = -1;
#endif
  {
    std::lock_guard<std::mutex> lock(changes_mutex_);
    pending_changes_.clear();
  }
}

std::shared_ptr<const IndexSnapshot> WorkspaceIndexer::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool WorkspaceIndexer::scanning() const {
  return scanning_.load();
}

std::vector<FileIndexChange> WorkspaceIndexer::drain_changes() {
  std::lock_guard<std::mutex> lock(changes_mutex_);
  std::vector<FileIndexChange> out = std::move(pending_changes_);
  pending_changes_.clear();
  return out;
}

void WorkspaceIndexer::worker_main(std::string workspace_root,
                                     IndexFilterOptions filter_options) {
  TGDB_MON_SCOPE("idx", "workspace_indexer.scan");
  auto snap = std::make_shared<IndexSnapshot>();
  snap->workspace_root = workspace_root;
  snap->filter_options = filter_options;

  const auto should_cancel = [this]() { return stop_requested_.load(); };
  if (!list_workspace_files_rg(workspace_root, filter_options, &snap->files, should_cancel,
                               &rg_child_pid_)) {
    std::error_code ec;
    const fs::path root(workspace_root);
    if (fs::is_directory(root, ec)) {
      scan_dir(root, root, filter_options, &snap->files);
      std::sort(snap->files.begin(), snap->files.end());
    }
  }
  rebuild_index_files_lower(snap.get());
  TGDB_MON("idx", "workspace_indexer.files=" + std::to_string(snap->files.size()));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  scanning_ = false;

  if (stop_requested_.load()) {
    return;
  }

#if defined(__linux__)
  inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotify_fd_ < 0) {
    return;
  }
  run_inotify_loop(workspace_root, filter_options, inotify_fd_, &stop_requested_, &changes_mutex_,
                   &pending_changes_);
  if (inotify_fd_ >= 0) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }
#endif
}

void WorkspaceIndexer::upsert_file(const std::string& workspace_root,
                                   const std::string& relative_file,
                                   const std::string& absolute_path) {
  IndexFilterOptions options;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_) {
      options = snapshot_->filter_options;
    }
  }
  if (!should_list_workspace_path(relative_file, options)) {
    remove_file(workspace_root, relative_file);
    return;
  }
  std::error_code ec;
  if (!fs::is_regular_file(absolute_path, ec)) {
    remove_file(workspace_root, relative_file);
    return;
  }

  auto updated = std::make_shared<IndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->filter_options = snapshot_->filter_options;
    updated->files = snapshot_->files;
  }

  auto& files = updated->files;
  files.erase(std::remove(files.begin(), files.end(), relative_file), files.end());
  files.push_back(relative_file);
  std::sort(files.begin(), files.end());
  rebuild_index_files_lower(updated.get());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void WorkspaceIndexer::remove_file(const std::string& workspace_root,
                                   const std::string& relative_file) {
  if (relative_file.empty()) {
    return;
  }

  auto updated = std::make_shared<IndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->filter_options = snapshot_->filter_options;
    updated->files = snapshot_->files;
  }

  auto& files = updated->files;
  files.erase(std::remove(files.begin(), files.end(), relative_file), files.end());
  rebuild_index_files_lower(updated.get());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

}  // namespace tgdb
