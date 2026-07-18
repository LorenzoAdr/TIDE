#include "build/build_environment_service.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

#include "build/build_environment_detector.hpp"
#include "build/build_environment_selector.hpp"
#include "build/compile_commands_generator.hpp"
#include "util/clangd_workspace_setup.hpp"
#include "util/compile_commands_setup.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string fingerprint_for_environment(const std::string& workspace_root,
                                      const BuildEnvironment& environment) {
  if (environment.id.empty()) {
    return {};
  }
  const fs::path db_path = fs::path(environment_compile_commands_path(workspace_root, environment.id));
  std::error_code ec;
  if (!fs::is_regular_file(db_path, ec)) {
    return environment.id + ":missing";
  }
  const auto mtime = fs::last_write_time(db_path, ec);
  if (ec) {
    return environment.id + ":unknown";
  }
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();
  return environment.id + ':' + std::to_string(seconds);
}

WorkspaceConfig apply_fallback_flags(WorkspaceConfig config,
                                     const std::vector<std::string>& fallback_flags) {
  for (const auto& flag : fallback_flags) {
    if (flag.rfind("-I", 0) == 0 && flag.size() > 2) {
      config.clangd_extra_include_paths.push_back(flag.substr(2));
    }
  }
  return config;
}

}  // namespace

BuildEnvironmentService& global_build_environment_service() {
  static BuildEnvironmentService service;
  return service;
}

void BuildEnvironmentService::set_environment_changed_callback(
    EnvironmentChangedCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_environment_changed_ = std::move(callback);
}

const BuildEnvironment& BuildEnvironmentService::active_environment() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_environment_;
}

std::string BuildEnvironmentService::active_environment_fingerprint() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return fingerprint_;
}

void BuildEnvironmentService::update_active_environment(const std::string& workspace_root,
                                                        const WorkspaceConfig& config,
                                                        const EnvironmentSelectionHints& hints,
                                                        const bool allow_generation) {
  BuildEnvironmentState state = BuildEnvironmentStateStore::load(workspace_root);
  std::vector<BuildEnvironment> candidates =
      discover_build_environments(workspace_root, config, &state);
  if (candidates.empty()) {
    return;
  }

  const auto selection = select_active_environment(candidates, config.build_environments.active_environment_id,
                                                 hints, state.last_active_environment_id);
  CompileCommandsGenerationResult generation;
  if (allow_generation) {
    generation = generate_compile_commands(workspace_root, selection.environment, config);
  } else if (compile_commands_exists(
                 environment_compile_dir(workspace_root, selection.environment.id))) {
    generation.success = true;
    generation.compile_dir =
        environment_compile_dir(workspace_root, selection.environment.id);
    generation.method = "cache";
  }

  state.discovered_environments = std::move(candidates);
  state.last_active_environment_id = selection.environment.id;
  BuildEnvironmentStateStore::save(workspace_root, state);

  WorkspaceConfig clangd_config =
      apply_fallback_flags(config, generation.fallback_compile_flags.empty()
                                      ? selection.environment.fallback_compile_flags
                                      : generation.fallback_compile_flags);
  apply_clangd_workspace_config(workspace_root, clangd_config);

  const std::string new_fingerprint =
      fingerprint_for_environment(workspace_root, selection.environment);
  EnvironmentChangedCallback callback;
  bool fingerprint_changed = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    fingerprint_changed = new_fingerprint != fingerprint_;
    fingerprint_ = new_fingerprint;
    active_environment_ = selection.environment;
    state_ = state;
    callback = on_environment_changed_;
  }

  if (fingerprint_changed && callback) {
    callback();
  }
}

CompileCommandsSetupResult BuildEnvironmentService::resolve_compile_commands(
    const std::string& workspace_root, const WorkspaceConfig& config,
    const EnvironmentSelectionHints& hints) {
  CompileCommandsSetupResult result;
  if (workspace_root.empty()) {
    return result;
  }

  const BuildSystemKind system = detect_build_system_kind(workspace_root);
  if (system != BuildSystemKind::kMakefile && system != BuildSystemKind::kHybrid) {
    return result;
  }

  update_active_environment(workspace_root, config, hints, true);

  BuildEnvironment active;
  std::string fingerprint;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active = active_environment_;
    fingerprint = fingerprint_;
  }

  if (active.id.empty()) {
    return result;
  }

  const std::string compile_dir = environment_compile_dir(workspace_root, active.id);
  if (compile_commands_exists(compile_dir)) {
    result.compile_dir = compile_dir;
    if (!active.docker_container.empty()) {
      result.status_note = "entorno " + active.label;
    } else if (!active.label.empty()) {
      result.status_note = "entorno " + active.label;
    }
    (void)fingerprint;
    return result;
  }

  result.status_note = "generando compile_commands (" + active.label + ")";
  schedule_background_generation(workspace_root, config, hints);
  return result;
}

void BuildEnvironmentService::schedule_background_generation(const std::string& workspace_root,
                                                             const WorkspaceConfig& config,
                                                             const EnvironmentSelectionHints& hints) {
  if (shutdown_requested_.load(std::memory_order_acquire)) {
    return;
  }
  if (generation_in_progress_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  if (background_thread_.joinable()) {
    background_thread_.join();
  }
  background_thread_ = std::thread([this, workspace_root, config, hints] {
    run_background_generation(workspace_root, config, hints);
  });
}

void BuildEnvironmentService::run_background_generation(std::string workspace_root,
                                                        WorkspaceConfig config,
                                                        EnvironmentSelectionHints hints) {
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  if (!shutdown_requested_.load(std::memory_order_acquire)) {
    update_active_environment(workspace_root, config, hints, true);
  }
  generation_in_progress_.store(false, std::memory_order_release);
}

void BuildEnvironmentService::notify_artifacts_changed(const std::string& workspace_root,
                                                         const WorkspaceConfig& config,
                                                         const EnvironmentSelectionHints& hints) {
  schedule_background_generation(workspace_root, config, hints);
}

void BuildEnvironmentService::shutdown() {
  shutdown_requested_.store(true, std::memory_order_release);
  if (background_thread_.joinable()) {
    background_thread_.join();
  }
}

}  // namespace tuide
