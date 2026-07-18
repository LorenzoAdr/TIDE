#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tuide {

class BuildArtifactWatcher {
 public:
  using ChangeCallback = std::function<void()>;

  void set_change_callback(ChangeCallback callback);
  void start(const std::string& workspace_root, const std::vector<std::string>& watch_dirs);
  void stop();
  bool drain_pending_change();

 private:
  void worker_loop();

  ChangeCallback callback_;
  std::string workspace_root_;
  std::vector<std::string> watch_dirs_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> pending_change_{false};
  std::mutex callback_mutex_;
};

}  // namespace tuide
