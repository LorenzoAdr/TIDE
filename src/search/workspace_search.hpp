#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct WorkspaceSearchResult {
  std::string file;
  int line = 1;
  int col = 1;
  std::string preview;
};

struct WorkspaceSearchOptions {
  std::string workspace_root;
  std::vector<std::string> files;
  std::string needle;
  std::string path_filter;
  std::string include_pattern;
  std::string exclude_pattern;
};

struct WorkspaceReplaceResult {
  int files_modified = 0;
  int replacements = 0;
};

bool glob_match(const std::string& pattern, const std::string& text);

bool file_excluded(const std::string& relative_path, const std::string& exclude_pattern);

bool file_included(const std::string& relative_path, const std::string& include_pattern);

bool file_in_search_path(const std::string& relative_path, const std::string& path_filter);

std::vector<WorkspaceSearchResult> search_workspace(const WorkspaceSearchOptions& opts);

WorkspaceReplaceResult replace_in_workspace(const WorkspaceSearchOptions& opts,
                                            const std::string& replacement);

}  // namespace tgdb
