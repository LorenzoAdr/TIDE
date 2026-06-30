#include "util/clangd_workspace_setup.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr const char* kClangdMarker = "# tgdb: extra include paths (auto-generated)";

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

  for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied,
                                           ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_directory(ec)) {
      flags->insert("-I" + it->path().string());
    }
  }
}

bool clangd_file_is_tgdb_managed(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }
  std::string line;
  if (!std::getline(input, line)) {
    return false;
  }
  return line == kClangdMarker;
}

void remove_tgdb_clangd_file(const fs::path& clangd_path) {
  std::error_code ec;
  if (!fs::is_regular_file(clangd_path, ec)) {
    return;
  }
  if (!clangd_file_is_tgdb_managed(clangd_path.string())) {
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
  if (config.clangd_extra_include_paths.empty()) {
    remove_tgdb_clangd_file(clangd_path);
    return;
  }

  const std::vector<std::string> flags =
      expand_recursive_include_flags(config.clangd_extra_include_paths);
  if (flags.empty()) {
    remove_tgdb_clangd_file(clangd_path);
    return;
  }

  std::ostringstream out;
  out << kClangdMarker << '\n';
  out << "CompileFlags:\n";
  out << "  Add:\n";
  for (const auto& flag : flags) {
    out << "    - " << flag << '\n';
  }

  std::ofstream output(clangd_path);
  if (!output) {
    return;
  }
  output << out.str();
}

}  // namespace tgdb
