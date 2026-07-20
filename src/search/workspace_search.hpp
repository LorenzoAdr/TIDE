#pragma once

#include <memory>
#include <string>
#include <vector>

namespace tuide {

struct WorkspaceSearchResult {
  std::string file;
  int line = 1;
  int col = 1;
  std::string preview;
};

struct WorkspaceSearchOptions {
  std::string workspace_root;
  // Owned file list (e.g. after an explicit scan). Prefer files_ref when sharing an index.
  std::vector<std::string> files;
  // Shared index file list — avoids copying tens of thousands of paths on the UI thread.
  std::shared_ptr<const std::vector<std::string>> files_ref;
  std::string needle;
  std::string path_filter;
  std::string include_pattern;
  std::string exclude_pattern;
};

// Returns files_ref if set, otherwise files. Does not scan the workspace.
const std::vector<std::string>& workspace_search_files(const WorkspaceSearchOptions& opts);

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

}  // namespace tuide
