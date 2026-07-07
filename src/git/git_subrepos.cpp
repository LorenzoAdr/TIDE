#include "git/git_subrepos.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <unordered_set>

#include "git/git_command.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string trim_newline(std::string value) {
  if (!value.empty() && value.back() == '\n') {
    value.pop_back();
  }
  return value;
}

bool path_is_prefix(const std::string& prefix, const std::string& path) {
  if (prefix.empty()) {
    return true;
  }
  if (path.size() < prefix.size()) {
    return false;
  }
  if (path.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  return path.size() == prefix.size() || path[prefix.size()] == '/';
}

std::string strip_prefix(const std::string& prefix, const std::string& path) {
  if (prefix.empty()) {
    return path;
  }
  if (path.size() == prefix.size()) {
    return {};
  }
  return path.substr(prefix.size() + 1);
}

}  // namespace

bool is_git_work_tree(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  const auto result = run_git(path, {"rev-parse", "--is-inside-work-tree"});
  return result.success() && result.stdout_text.find("true") != std::string::npos;
}

std::string git_toplevel(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const auto result = run_git(path, {"rev-parse", "--show-toplevel"});
  if (!result.success()) {
    return {};
  }
  return normalize_path(trim_newline(result.stdout_text));
}

std::vector<GitSubrepoInfo> discover_submodules(const std::string& main_repo_root) {
  std::vector<GitSubrepoInfo> subrepos;
  if (main_repo_root.empty() || !is_git_work_tree(main_repo_root)) {
    return subrepos;
  }

  const auto result = run_git(main_repo_root, {"submodule", "status", "--recursive"});
  if (!result.success()) {
    return subrepos;
  }

  std::size_t pos = 0;
  while (pos < result.stdout_text.size()) {
    const std::size_t end = result.stdout_text.find('\n', pos);
    const std::string line =
        end == std::string::npos ? result.stdout_text.substr(pos)
                                 : result.stdout_text.substr(pos, end - pos);
    pos = end == std::string::npos ? result.stdout_text.size() : end + 1;

    if (line.size() < 3) {
      continue;
    }

    std::size_t idx = 0;
    if (line[0] == ' ' || line[0] == '+' || line[0] == '-' || line[0] == 'U') {
      idx = 1;
    }
    while (idx < line.size() && line[idx] == ' ') {
      ++idx;
    }
    const std::size_t hash_start = idx;
    while (idx < line.size() && line[idx] != ' ') {
      ++idx;
    }
    if (idx == hash_start || idx >= line.size()) {
      continue;
    }
    while (idx < line.size() && line[idx] == ' ') {
      ++idx;
    }
    const std::size_t path_start = idx;
    while (idx < line.size() && line[idx] != ' ' && line[idx] != '(') {
      ++idx;
    }
    if (idx == path_start) {
      continue;
    }

    const std::string rel_path = line.substr(path_start, idx - path_start);
    const std::string abs_root = normalize_path((fs::path(main_repo_root) / rel_path).string());
    if (!is_git_work_tree(abs_root)) {
      continue;
    }

    GitSubrepoInfo info;
    info.root = git_toplevel(abs_root);
    if (info.root.empty()) {
      info.root = abs_root;
    }
    info.workspace_path = rel_path;
    info.is_submodule = true;
    subrepos.push_back(std::move(info));
  }

  return subrepos;
}

std::vector<GitSubrepoInfo> discover_nested_repos(
    const std::string& workspace_root, const std::string& main_repo_root,
    const std::vector<GitSubrepoInfo>& known) {
  std::vector<GitSubrepoInfo> nested;
  if (workspace_root.empty()) {
    return nested;
  }

  std::unordered_set<std::string> known_paths;
  for (const auto& info : known) {
    known_paths.insert(info.workspace_path);
  }
  if (!main_repo_root.empty()) {
    std::error_code ec;
    const fs::path rel = fs::relative(fs::path(main_repo_root), fs::path(workspace_root), ec);
    if (!ec) {
      known_paths.insert(rel.string());
    }
  }

  std::function<void(const fs::path&, int)> walk;
  walk = [&](const fs::path& dir, int depth) {
    if (depth > 10) {
      return;
    }
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
      return;
    }

    const std::string abs = normalize_path(dir.string());
    if (fs::exists(dir / ".git", ec)) {
      std::string ws_rel;
      const fs::path relative = fs::relative(dir, fs::path(workspace_root), ec);
      if (!ec) {
        ws_rel = relative.string();
      }
      if (!ws_rel.empty() && known_paths.find(ws_rel) == known_paths.end() && is_git_work_tree(abs)) {
        GitSubrepoInfo info;
        info.root = git_toplevel(abs);
        if (info.root.empty()) {
          info.root = abs;
        }
        info.workspace_path = ws_rel;
        info.is_submodule = false;
        nested.push_back(std::move(info));
        known_paths.insert(ws_rel);
      }
      return;
    }

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (ec) {
        break;
      }
      if (!entry.is_directory()) {
        continue;
      }
      const std::string name = entry.path().filename().string();
      if (name == ".git") {
        continue;
      }
      if (!name.empty() && name[0] == '.' && name != ".") {
        continue;
      }
      if (name == "node_modules" || name == "build" || name == "out" || name == "_deps") {
        continue;
      }
      walk(entry.path(), depth + 1);
    }
  };

  walk(fs::path(workspace_root), 0);
  return nested;
}

std::string join_workspace_path(const std::string& prefix, const std::string& path) {
  if (prefix.empty()) {
    return path;
  }
  if (path.empty()) {
    return prefix;
  }
  return prefix + "/" + path;
}

std::string workspace_to_repo_rel(const std::string& workspace_root, const std::string& repo_root,
                                  const std::string& workspace_rel) {
  if (workspace_root.empty() || repo_root.empty() || workspace_rel.empty()) {
    return {};
  }
  std::error_code ec;
  const fs::path abs = fs::path(workspace_root) / workspace_rel;
  const fs::path rel = fs::relative(abs, fs::path(repo_root), ec);
  if (ec) {
    return {};
  }
  const std::string result = rel.string();
  if (result.empty() || result == ".") {
    return ".";
  }
  if (result.rfind("..", 0) == 0) {
    return {};
  }
  return result;
}

std::string repo_to_workspace_rel(const std::string& workspace_root,
                                  const std::string& repo_root, const std::string& repo_rel) {
  if (workspace_root.empty() || repo_root.empty() || repo_rel.empty()) {
    return {};
  }
  std::error_code ec;
  const fs::path abs = fs::path(repo_root) / repo_rel;
  const fs::path rel = fs::relative(abs, fs::path(workspace_root), ec);
  if (ec) {
    return {};
  }
  const std::string result = rel.string();
  if (result.rfind("..", 0) == 0) {
    return {};
  }
  return result;
}

ResolvedGitPath resolve_git_path(const std::string& workspace_root,
                                 const std::string& main_repo_root, bool main_valid,
                                 const std::vector<GitSubrepoInfo>& subrepos,
                                 const std::string& path) {
  ResolvedGitPath resolved;
  if (workspace_root.empty() || path.empty()) {
    return resolved;
  }

  std::string workspace_rel = path;
  if (!path.empty() && path[0] == '/') {
    std::error_code ec;
    const fs::path rel = fs::relative(fs::path(path), fs::path(workspace_root), ec);
    if (ec) {
      return resolved;
    }
    workspace_rel = rel.string();
  }

  resolved.workspace_rel = workspace_rel;

  std::vector<const GitSubrepoInfo*> sorted;
  sorted.reserve(subrepos.size());
  for (const auto& sub : subrepos) {
    sorted.push_back(&sub);
  }
  std::sort(sorted.begin(), sorted.end(), [](const GitSubrepoInfo* a, const GitSubrepoInfo* b) {
    return a->workspace_path.size() > b->workspace_path.size();
  });

  for (const GitSubrepoInfo* sub : sorted) {
    if (!path_is_prefix(sub->workspace_path, workspace_rel)) {
      continue;
    }
    resolved.repo_root = sub->root;
    resolved.repo_rel = workspace_to_repo_rel(workspace_root, sub->root, workspace_rel);
    resolved.valid = !resolved.repo_rel.empty();
    return resolved;
  }

  if (main_valid && !main_repo_root.empty()) {
    resolved.repo_root = main_repo_root;
    resolved.repo_rel = workspace_to_repo_rel(workspace_root, main_repo_root, workspace_rel);
    resolved.valid = !resolved.repo_rel.empty();
  }
  return resolved;
}

}  // namespace tgdb
