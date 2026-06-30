#pragma once

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace tgdb {

enum class GitLineChange {
  kAdded,
  kModified,
  kDeleted,
};

struct GitFileDiff {
  std::string path;
  std::map<int, GitLineChange> line_changes;
  std::map<int, std::string> previous_content_by_line;
  std::vector<std::string> head_lines;
  bool loaded = false;
  bool untracked = false;
};

GitFileDiff parse_unified_diff(const std::string& path, const std::string& diff_output);

std::vector<std::string> parse_file_lines(const std::string& content);

struct LineDiffResult {
  std::unordered_set<int> changed_new_lines;
  std::map<int, std::string> previous_content_by_new_line;
};

LineDiffResult compute_line_diff(const std::vector<std::string>& head,
                                 const std::vector<std::string>& current);

std::unordered_set<int> compute_changed_lines(const std::vector<std::string>& current,
                                              const std::vector<std::string>& head);

}  // namespace tgdb
