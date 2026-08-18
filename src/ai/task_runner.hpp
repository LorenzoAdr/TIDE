#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "ai/ai_types.hpp"

namespace tuide {

struct AiTaskSpec {
  std::string name;
  std::string command;
};

struct TaskRunnerResult {
  bool allowed = false;
  bool started = false;
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
  std::string deny_reason;
};

class TaskRunner {
 public:
  using LineCallback = std::function<void(const std::string& line)>;
  using DoneCallback = std::function<void(const TaskRunnerResult& result)>;

  void set_whitelist(std::vector<std::string> whitelist);
  void set_tasks(std::vector<AiTaskSpec> tasks);
  void ensure_default_tasks(const std::string& workspace_root);

  bool is_whitelisted(const std::string& name_or_command) const;
  const std::vector<AiTaskSpec>& tasks() const { return tasks_; }

  // Runs named task or raw command if whitelist allows. Blocks the calling
  // thread; stream lines via on_line. Prefer AiController::run_task (async) from UI.
  TaskRunnerResult run(const std::string& name_or_command, const std::string& cwd,
                       const LineCallback& on_line);

  // Soft-cancel: SIGTERM the process group so popen-style reads unblock.
  void cancel();
  bool busy() const { return busy_.load(); }

 private:
  bool matches_whitelist(const std::vector<std::string>& argv) const;
  TaskRunnerResult deny(const std::string& reason) const;

  mutable std::mutex mu_;
  std::vector<std::string> whitelist_;
  std::vector<AiTaskSpec> tasks_;
  std::atomic<bool> busy_{false};
  std::atomic<bool> cancel_{false};
  std::atomic<pid_t> child_pid_{-1};
};

}  // namespace tuide
