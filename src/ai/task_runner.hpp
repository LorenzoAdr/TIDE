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

inline constexpr int kTaskRunnerDefaultTimeoutMs = 120000;   // 2 min: comandos sueltos
inline constexpr int kTaskRunnerBuildTimeoutMs = 900000;     // 15 min: build/test/launch

// Enrutado Docker resuelto por util/docker_shell.hpp (resolve_shell_launch_config).
// container vacío = ejecutar en el host, como hasta ahora.
struct ShellDockerRoute {
  std::string container;
  std::string cwd;  // -w dentro del contenedor; vacío = WORKDIR de la imagen
  bool active() const { return !container.empty(); }
};

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
  // Vacío (container="") vuelve a ejecutar en el host. El workspace puede
  // resolverse a un contenedor Docker (util/docker_shell.hpp); sin esto, todo
  // lo que lanza el harness de IA corre en el host aunque el proyecto viva
  // dentro del contenedor.
  void set_docker_route(ShellDockerRoute route);
  ShellDockerRoute docker_route() const;

  bool is_whitelisted(const std::string& name_or_command) const;
  const std::vector<AiTaskSpec>& tasks() const { return tasks_; }

  // Runs named task or raw command if whitelist allows. Blocks the calling
  // thread; stream lines via on_line. Prefer AiController::run_task (async) from UI.
  // timeout_ms <= 0 desactiva el auto-kill (no recomendado; usar solo para tests).
  TaskRunnerResult run(const std::string& name_or_command, const std::string& cwd,
                       const LineCallback& on_line, int timeout_ms = kTaskRunnerDefaultTimeoutMs);

  // Soft-cancel: SIGTERM the process group so popen-style reads unblock.
  void cancel();
  bool busy() const { return busy_.load(); }

 private:
  bool matches_whitelist(const std::vector<std::string>& argv) const;
  TaskRunnerResult deny(const std::string& reason) const;

  mutable std::mutex mu_;
  std::vector<std::string> whitelist_;
  std::vector<AiTaskSpec> tasks_;
  ShellDockerRoute docker_route_;
  std::atomic<bool> busy_{false};
  std::atomic<bool> cancel_{false};
  std::atomic<pid_t> child_pid_{-1};
};

}  // namespace tuide
