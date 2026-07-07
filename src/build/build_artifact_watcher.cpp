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

namespace {

constexpr auto kChangeDebounce = std::chrono::milliseconds(2000);
constexpr auto kPollInterval = std::chrono::milliseconds(100);

}  // namespace

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
  bool debounce_pending = false;
  auto last_event_time = std::chrono::steady_clock::now();
  while (!stop_requested_.load(std::memory_order_acquire)) {
    const ssize_t bytes = read(fd, buffer.data(), buffer.size());
    const auto now = std::chrono::steady_clock::now();
    if (bytes > 0) {
      debounce_pending = true;
      last_event_time = now;
      pending_change_.store(true, std::memory_order_release);
      continue;
    }

    if (debounce_pending && now - last_event_time >= kChangeDebounce) {
      debounce_pending = false;
      pending_change_.store(false, std::memory_order_release);
      ChangeCallback callback;
      {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = callback_;
      }
      if (callback) {
        callback();
      }
    }

    std::this_thread::sleep_for(kPollInterval);
  }

  close(fd);
#endif
  running_.store(false, std::memory_order_release);
}

}  // namespace tgdb
