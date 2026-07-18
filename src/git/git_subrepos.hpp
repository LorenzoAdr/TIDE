#pragma once

#include <string>
#include <vector>

namespace tuide {

struct GitSubrepoInfo {
  std::string root;
  std::string workspace_path;
  bool is_submodule = false;
};

struct ResolvedGitPath {
  bool valid = false;
  std::string repo_root;
  std::string repo_rel;
  std::string workspace_rel;
};

bool is_git_work_tree(const std::string& path);
std::string git_toplevel(const std::string& path);

std::vector<GitSubrepoInfo> discover_submodules(const std::string& main_repo_root);
std::vector<GitSubrepoInfo> discover_nested_repos(const std::string& workspace_root,
                                                  const std::string& main_repo_root,
                                                  const std::vector<GitSubrepoInfo>& known);

std::string join_workspace_path(const std::string& prefix, const std::string& path);
std::string workspace_to_repo_rel(const std::string& workspace_root, const std::string& repo_root,
                                  const std::string& workspace_rel);
std::string repo_to_workspace_rel(const std::string& workspace_root,
                                  const std::string& repo_root, const std::string& repo_rel);

ResolvedGitPath resolve_git_path(const std::string& workspace_root,
                                 const std::string& main_repo_root, bool main_valid,
                                 const std::vector<GitSubrepoInfo>& subrepos,
                                 const std::string& path);

}  // namespace tuide
