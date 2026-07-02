#include "build/build_artifact_watcher.hpp"

#include <array>
#include <chrono>
#include <filesystem>

#include "util/thread_name.hpp"

#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace tgdb {

void BuildArtifactWatcher::set_change_callback(ChangeCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  callback_ = std::move(callback);
}

void BuildArtifactWatcher::start(const std::string& workspace_root,
                                 const std::vector<std::string>& watch_dirs) {
  stop();
  if (workspace_root.empty()) {
    return;
  }
  workspace_root_ = workspace_root;
  watch_dirs_ = watch_dirs;
  stop_requested_.store(false, std::memory_order_release);
  pending_change_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  worker_ = std::thread([this] {
    set_current_thread_name("build-watch");
    worker_loop();
  });
}

void BuildArtifactWatcher::stop() {
  stop_requested_.store(true, std::memory_order_release);
  running_.store(false, std::memory_order_release);
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool BuildArtifactWatcher::drain_pending_change() {
  return pending_change_.exchange(false, std::memory_order_acq_rel);
}

void BuildArtifactWatcher::worker_loop() {
#if defined(__linux__)
  const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (fd < 0) {
    running_.store(false, std::memory_order_release);
    return;
  }

  constexpr int kMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_CLOSE_WRITE;
  std::vector<std::string> unique_dirs;
  unique_dirs.push_back((fs::path(workspace_root_) / ".tgdb" / "environments").string());
  for (const auto& dir : watch_dirs_) {
    if (!dir.empty()) {
      unique_dirs.push_back(dir);
    }
  }
  static const char* kDefaults[] = {"build", "out", "obj", "output", "dist"};
  for (const auto* name : kDefaults) {
    unique_dirs.push_back((fs::path(workspace_root_) / name).string());
  }

  for (const auto& dir : unique_dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
      continue;
    }
    inotify_add_watch(fd, dir.c_str(), kMask);
  }

  std::array<char, 4096> buffer{};
  while (!stop_requested_.load(std::memory_order_acquire)) {
    const ssize_t bytes = read(fd, buffer.data(), buffer.size());
    if (bytes <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }
    pending_change_.store(true, std::memory_order_release);
    ChangeCallback callback;
    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback = callback_;
    }
    if (callback) {
      callback();
    }
  }

  close(fd);
#endif
  running_.store(false, std::memory_order_release);
}

}  // namespace tgdb
