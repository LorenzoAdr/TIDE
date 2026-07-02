#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "app/workspace_config.hpp"
#include "build/build_environment.hpp"
#include "build/build_environment_state.hpp"
#include "util/compile_commands_remap.hpp"

namespace tgdb {

class BuildEnvironmentService {
 public:
  using EnvironmentChangedCallback = std::function<void()>;

  void set_environment_changed_callback(EnvironmentChangedCallback callback);

  CompileCommandsSetupResult resolve_compile_commands(const std::string& workspace_root,
                                                      const WorkspaceConfig& config,
                                                      const EnvironmentSelectionHints& hints = {});

  const BuildEnvironment& active_environment() const;
  std::string active_environment_fingerprint() const;

  void schedule_background_generation(const std::string& workspace_root,
                                      const WorkspaceConfig& config,
                                      const EnvironmentSelectionHints& hints = {});

  void notify_artifacts_changed(const std::string& workspace_root,
                                const WorkspaceConfig& config,
                                const EnvironmentSelectionHints& hints = {});

  void shutdown();

 private:
  void run_background_generation(std::string workspace_root, WorkspaceConfig config,
                                 EnvironmentSelectionHints hints);
  void update_active_environment(const std::string& workspace_root, const WorkspaceConfig& config,
                                 const EnvironmentSelectionHints& hints, bool allow_generation);

  mutable std::mutex mutex_;
  BuildEnvironment active_environment_;
  BuildEnvironmentState state_;
  std::string fingerprint_;
  EnvironmentChangedCallback on_environment_changed_;
  std::thread background_thread_;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> generation_in_progress_{false};
};

BuildEnvironmentService& global_build_environment_service();

}  // namespace tgdb
