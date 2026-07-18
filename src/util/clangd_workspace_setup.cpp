#include "util/clangd_workspace_setup.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr const char* kClangdMarker = "# tuide: workspace config (auto-generated)";
constexpr const char* kLegacyClangdMarker = "# tgdb: workspace config (auto-generated)";

bool path_is_directory(const std::string& path) {
  std::error_code ec;
  return fs::is_directory(path, ec);
}

void collect_recursive_include_flags(const std::string& root,
                                     std::set<std::string>* flags) {
  if (flags == nullptr || root.empty() || !path_is_directory(root)) {
    return;
  }

  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(fs::path(root), ec);
  const std::string base = ec ? root : canonical.string();
  flags->insert("-I" + base);

  // One level of immediate child directories covers #include "local.h" when the
  // header lives in a direct subfolder. Avoid fs::recursive_directory_iterator:
  // large trees (/usr/include, sysroot mounts) produced millions of -I flags and
  // OOM/crashes when saving workspace settings.
  for (const auto& entry : fs::directory_iterator(base, fs::directory_options::skip_permission_denied,
                                                  ec)) {
    if (ec) {
      break;
    }
    if (entry.is_directory(ec)) {
      flags->insert("-I" + entry.path().string());
    }
  }
}

bool clangd_file_is_tuide_managed(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }
  std::string line;
  if (!std::getline(input, line)) {
    return false;
  }
  return line == kClangdMarker || line == kLegacyClangdMarker;
}

void remove_tuide_clangd_file(const fs::path& clangd_path) {
  std::error_code ec;
  if (!fs::is_regular_file(clangd_path, ec)) {
    return;
  }
  if (!clangd_file_is_tuide_managed(clangd_path.string())) {
    return;
  }
  fs::remove(clangd_path, ec);
}

}  // namespace

std::vector<std::string> expand_recursive_include_flags(
    const std::vector<std::string>& root_paths) {
  std::set<std::string> flags;
  for (const auto& root : root_paths) {
    collect_recursive_include_flags(root, &flags);
  }
  return {flags.begin(), flags.end()};
}

void apply_clangd_workspace_config(const std::string& workspace_root,
                                   const WorkspaceConfig& config) {
  if (workspace_root.empty()) {
    return;
  }

  const fs::path clangd_path = fs::path(workspace_root) / ".clangd";
  const bool skip_background = !config.clangd_background_index;
  const std::vector<std::string> include_flags =
      config.clangd_extra_include_paths.empty()
          ? std::vector<std::string>{}
          : expand_recursive_include_flags(config.clangd_extra_include_paths);
  const bool needs_file = skip_background || !include_flags.empty();

  if (!needs_file) {
    remove_tuide_clangd_file(clangd_path);
    return;
  }

  std::error_code ec;
  if (fs::is_regular_file(clangd_path, ec) &&
      !clangd_file_is_tuide_managed(clangd_path.string())) {
    return;
  }

  std::ostringstream out;
  out << kClangdMarker << '\n';
  if (skip_background) {
    out << "Index:\n  Background: Skip\n";
  }
  if (!include_flags.empty()) {
    out << "CompileFlags:\n  Add:\n";
    for (const auto& flag : include_flags) {
      out << "    - " << flag << '\n';
    }
  }

  std::ofstream output(clangd_path);
  if (!output) {
    return;
  }
  output << out.str();
}

}  // namespace tuide
