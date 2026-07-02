#include "build/build_environment_detector.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>

#include "build/docker_environment_detector.hpp"
#include "build/make_qp_parser.hpp"
#include "util/shell_utils.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr int kMakeQpTimeoutSeconds = 5;
constexpr int kEnvCaptureTimeoutSeconds = 8;
constexpr int kRecentArtifactMinutes = 30;

const char* kSetupScriptNames[] = {
    "envsetup.sh", "setenv.sh", "setup.sh", "activate", ".envrc", "config.mk",
};

bool has_makefile_at(const fs::path& dir) {
  std::error_code ec;
  return fs::is_regular_file(dir / "Makefile", ec) ||
         fs::is_regular_file(dir / "makefile", ec) ||
         fs::is_regular_file(dir / "GNUmakefile", ec);
}

std::string absolute_path_string(const fs::path& path) {
  std::error_code ec;
  return fs::absolute(path, ec).string();
}

BuildEnvironment profile_to_environment(const BuildEnvironmentProfile& profile,
                                          const std::string& workspace_root,
                                          BuildSystemKind system) {
  BuildEnvironment env;
  env.system = system;
  env.label = profile.label.empty() ? profile.id : profile.label;
  env.working_dir = profile.working_dir.empty() ? workspace_root : profile.working_dir;
  env.make_command = profile.make_command.empty() ? "make" : profile.make_command;
  env.setup_scripts = profile.setup_scripts;
  env.env_vars = profile.env_vars;
  env.docker_container = profile.docker_container;
  env.marker_paths = profile.marker_paths;
  if (!profile.id.empty()) {
    env.id = profile.id;
  } else {
    env.id = build_environment_id(env);
  }
  if (env.label.empty()) {
    env.label = env.id;
  }
  return env;
}

std::string run_make_qp(const std::string& working_dir, BuildEnvironmentState* state) {
  std::error_code ec;
  const fs::path makefile = fs::path(working_dir) / "Makefile";
  if (!has_makefile_at(fs::path(working_dir))) {
    return {};
  }

  if (state != nullptr && state->make_qp_cache_mtime > 0) {
    const auto mtime = fs::last_write_time(makefile, ec);
    if (!ec) {
      const auto seconds =
          std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();
      if (seconds == state->make_qp_cache_mtime && !state->make_qp_cache_text.empty()) {
        return state->make_qp_cache_text;
      }
    }
  }

  const std::string command = "cd " + shell_quote(working_dir) + " && make -qp 2>/dev/null";
  const std::string output = run_shell_capture(command, kMakeQpTimeoutSeconds);
  if (state != nullptr && !output.empty()) {
    const auto mtime = fs::last_write_time(makefile, ec);
    if (!ec) {
      state->make_qp_cache_mtime =
          std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();
      state->make_qp_cache_text = output;
    }
  }
  return output;
}

void add_variant_environments(const std::string& workspace_root, const MakeQpInfo& info,
                              BuildSystemKind system, std::vector<BuildEnvironment>* out) {
  if (out == nullptr) {
    return;
  }

  BuildEnvironment base;
  base.system = system;
  base.working_dir = workspace_root;
  base.make_command = "make";
  base.label = "default";
  base.fallback_compile_flags = info.compile_flags;
  base.marker_paths = discover_recent_artifact_paths(workspace_root, info.output_dirs);
  if (base.marker_paths.empty()) {
    base.marker_paths.push_back(workspace_root);
  }
  base.id = build_environment_id(base);
  out->push_back(base);

  static const char* kVariantTargets[] = {"debug", "release", "all"};
  for (const auto* target : kVariantTargets) {
    if (std::find(info.targets.begin(), info.targets.end(), target) == info.targets.end()) {
      continue;
    }
    BuildEnvironment variant = base;
    variant.make_command = std::string("make ") + target;
    variant.label = target;
    variant.id = build_environment_id(variant);
    out->push_back(std::move(variant));
  }

  const auto platform = info.variables.find("PLATFORM");
  if (platform != info.variables.end() && !platform->second.empty()) {
    BuildEnvironment variant = base;
    variant.make_command = "make PLATFORM=" + platform->second;
    variant.label = "PLATFORM=" + platform->second;
    variant.env_vars["PLATFORM"] = platform->second;
    variant.id = build_environment_id(variant);
    out->push_back(std::move(variant));
  }
}

void merge_unique_environments(std::vector<BuildEnvironment>* dest,
                               const std::vector<BuildEnvironment>& extra) {
  if (dest == nullptr) {
    return;
  }
  for (auto env : extra) {
    if (env.id.empty()) {
      env.id = build_environment_id(env);
    }
    const auto duplicate =
        std::find_if(dest->begin(), dest->end(),
                     [&](const BuildEnvironment& existing) { return existing.id == env.id; });
    if (duplicate == dest->end()) {
      dest->push_back(std::move(env));
    }
  }
}

bool is_recent_file(const fs::path& path, const fs::file_time_type& cutoff) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    return false;
  }
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return false;
  }
  return mtime >= cutoff;
}

void scan_recent_artifacts(const fs::path& root, const fs::path& current, int depth,
                           const fs::file_time_type& cutoff,
                           std::vector<std::string>* out, int max_results) {
  if (out == nullptr || static_cast<int>(out->size()) >= max_results || depth > 4) {
    return;
  }
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec || static_cast<int>(out->size()) >= max_results) {
      break;
    }
    if (entry.is_directory(ec)) {
      const auto name = entry.path().filename().string();
      if (name == ".git" || name == "node_modules") {
        continue;
      }
      scan_recent_artifacts(root, entry.path(), depth + 1, cutoff, out, max_results);
      continue;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const auto ext = entry.path().extension().string();
    if (ext != ".o" && ext != ".a" && entry.path().filename() != "config.h" &&
        entry.path().filename() != ".config") {
      continue;
    }
    if (is_recent_file(entry.path(), cutoff)) {
      out->push_back(absolute_path_string(entry.path()));
    }
  }
}

}  // namespace

std::vector<std::string> discover_setup_script_candidates(const std::string& workspace_root) {
  std::vector<std::string> scripts;
  if (workspace_root.empty()) {
    return scripts;
  }
  const fs::path root(workspace_root);
  std::error_code ec;
  for (const auto* name : kSetupScriptNames) {
    const fs::path candidate = root / name;
    if (fs::is_regular_file(candidate, ec)) {
      scripts.push_back(candidate.string());
    }
  }
  for (const auto& entry : fs::directory_iterator(root, ec)) {
    if (ec || !entry.is_regular_file(ec)) {
      break;
    }
    const auto filename = entry.path().filename().string();
    if (filename.rfind("setenv", 0) == 0 || filename.rfind("env", 0) == 0) {
      scripts.push_back(entry.path().string());
    }
  }
  return scripts;
}

std::map<std::string, std::string> capture_env_after_sourcing(
    const std::string& workspace_root, const std::string& script_path) {
  std::map<std::string, std::string> diff;
  if (workspace_root.empty() || script_path.empty()) {
    return diff;
  }

  const std::string bash_command =
      "cd " + shell_quote(workspace_root) + " && "
      "before=$(mktemp) && after=$(mktemp) && "
      "env >\"$before\" && "
      "set -a && source " + shell_quote(script_path) + " >/dev/null 2>&1; set +a; "
      "env >\"$after\"; "
      "for key in CC CXX CFLAGS CXXFLAGS CPPFLAGS PATH PKG_CONFIG_PATH CROSS_COMPILE SYSROOT "
      "PLATFORM OUT_DIR BUILD_DIR OBJDIR; do "
      "b=$(grep -m1 \"^$key=\" \"$before\" | cut -d= -f2-); "
      "a=$(grep -m1 \"^$key=\" \"$after\" | cut -d= -f2-); "
      "if [ -n \"$a\" ] && [ \"$a\" != \"$b\" ]; then echo \"$key=$a\"; fi; "
      "done; rm -f \"$before\" \"$after\"";

  const std::string output = run_shell_capture(bash_command, kEnvCaptureTimeoutSeconds);
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    const auto pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    diff[line.substr(0, pos)] = line.substr(pos + 1);
  }
  return diff;
}

std::vector<std::string> discover_recent_artifact_paths(const std::string& workspace_root,
                                                        const std::vector<std::string>& roots,
                                                        const int max_results) {
  std::vector<std::string> artifacts;
  if (workspace_root.empty()) {
    return artifacts;
  }
  const auto cutoff = fs::file_time_type::clock::now() - std::chrono::minutes(kRecentArtifactMinutes);
  std::set<std::string> scan_roots;
  scan_roots.insert(workspace_root);
  for (const auto& root : roots) {
    if (root.empty()) {
      continue;
    }
    const fs::path path =
        fs::path(root).is_absolute() ? fs::path(root) : fs::path(workspace_root) / root;
    scan_roots.insert(absolute_path_string(path));
  }
  static const char* kBuildDirs[] = {"build", "out", "obj", "output", "dist"};
  for (const auto* name : kBuildDirs) {
    scan_roots.insert(absolute_path_string(fs::path(workspace_root) / name));
  }

  for (const auto& root : scan_roots) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
      continue;
    }
    scan_recent_artifacts(fs::path(workspace_root), fs::path(root), 0, cutoff, &artifacts,
                          max_results);
    if (static_cast<int>(artifacts.size()) >= max_results) {
      break;
    }
  }
  return artifacts;
}

std::vector<BuildEnvironment> discover_build_environments(
    const std::string& workspace_root, const WorkspaceConfig& config,
    BuildEnvironmentState* state) {
  std::vector<BuildEnvironment> environments;
  if (workspace_root.empty()) {
    return environments;
  }

  const BuildSystemKind system = detect_build_system_kind(workspace_root);
  if (system != BuildSystemKind::kMakefile && system != BuildSystemKind::kHybrid) {
    return environments;
  }

  for (const auto& profile : config.build_environments.profiles) {
    environments.push_back(profile_to_environment(profile, workspace_root, system));
  }

  const std::string make_qp = run_make_qp(workspace_root, state);
  if (!make_qp.empty()) {
    const MakeQpInfo info = parse_make_qp_output(make_qp);
    add_variant_environments(workspace_root, info, system, &environments);
  } else if (environments.empty()) {
    BuildEnvironment fallback;
    fallback.system = system;
    fallback.working_dir = workspace_root;
    fallback.label = "default";
    fallback.marker_paths.push_back(workspace_root);
    fallback.id = build_environment_id(fallback);
    environments.push_back(std::move(fallback));
  }

  for (const auto& script : discover_setup_script_candidates(workspace_root)) {
    const auto env_diff = capture_env_after_sourcing(workspace_root, script);
    if (env_diff.empty()) {
      continue;
    }
    BuildEnvironment scripted;
    scripted.system = system;
    scripted.working_dir = workspace_root;
    scripted.setup_scripts.push_back(script);
    scripted.env_vars = env_diff;
    scripted.label = "script:" + fs::path(script).filename().string();
    scripted.marker_paths = discover_recent_artifact_paths(workspace_root, {});
    scripted.fallback_compile_flags = extract_compile_flags_from_make_vars(env_diff);
    scripted.id = build_environment_id(scripted);
    environments.push_back(std::move(scripted));
  }

  merge_unique_environments(&environments, discover_docker_environments(workspace_root));
  return environments;
}

}  // namespace tgdb
