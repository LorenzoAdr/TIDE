#include "indexer/workspace_watcher.hpp"

#include <chrono>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "util/thread_name.hpp"

#include "indexer/index_rules.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

#if defined(__linux__)

#include <sys/inotify.h>
#include <unistd.h>

constexpr int kWatchMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                           IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;

void add_directory_watch(int fd, const fs::path& dir,
                         std::unordered_map<int, fs::path>* watch_dirs,
                         std::vector<FileIndexChange>* pending) {
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

void scan_directories(int fd, const fs::path& root, const fs::path& current,
                      const IndexFilterOptions& options,
                      std::unordered_map<int, fs::path>* watch_dirs,
                      std::vector<FileIndexChange>* pending) {
  add_directory_watch(fd, current, watch_dirs, pending);
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
    scan_directories(fd, root, entry.path(), options, watch_dirs, pending);
  }
}

#endif

}  // namespace

struct WorkspaceWatcher::Impl {
  std::string workspace_root;
  IndexFilterOptions filter_options;
  std::thread worker;
  std::atomic<bool> running{false};
  std::atomic<bool> stop_requested{false};
  std::mutex mutex;
  std::vector<FileIndexChange> pending;

#if defined(__linux__)
  int inotify_fd = -1;
#endif

  void push_change(FileIndexChange change) {
    if (change.relative_path.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    pending.push_back(std::move(change));
  }

#if defined(__linux__)
  void worker_main() {
    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
      running = false;
      return;
    }

    std::unordered_map<int, fs::path> watch_dirs;
    const fs::path root(workspace_root);
    scan_directories(inotify_fd, root, root, filter_options, &watch_dirs, &pending);

    std::vector<char> buffer(64 * 1024);
    while (!stop_requested.load()) {
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
            add_directory_watch(inotify_fd, entry_path, &watch_dirs, &pending);
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
        push_change(std::move(change));
      }
    }

    if (inotify_fd >= 0) {
      close(inotify_fd);
      inotify_fd = -1;
    }
    running = false;
  }
#else
  void worker_main() { running = false; }
#endif
};

WorkspaceWatcher::WorkspaceWatcher() : impl_(std::make_unique<Impl>()) {}

WorkspaceWatcher::~WorkspaceWatcher() {
  stop();
}

void WorkspaceWatcher::start(const std::string& workspace_root,
                               const IndexFilterOptions& filter_options) {
  stop();
  if (workspace_root.empty()) {
    return;
  }
  impl_->workspace_root = workspace_root;
  impl_->filter_options = filter_options;
  impl_->stop_requested = false;
  impl_->running = true;
  impl_->worker = std::thread([this] {
    set_current_thread_name("idx-watch");
    impl_->worker_main();
  });
}

void WorkspaceWatcher::stop() {
  impl_->stop_requested = true;
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  impl_->running = false;
#if defined(__linux__)
  if (impl_->inotify_fd >= 0) {
    close(impl_->inotify_fd);
    impl_->inotify_fd = -1;
  }
#endif
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->pending.clear();
}

std::vector<FileIndexChange> WorkspaceWatcher::drain_changes() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<FileIndexChange> out = std::move(impl_->pending);
  impl_->pending.clear();
  return out;
}

}  // namespace tgdb
