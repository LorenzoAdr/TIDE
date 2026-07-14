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

struct GitLineMap {
  std::vector<int> working_to_head;
  std::vector<int> head_to_working;
};

GitLineMap build_git_line_map(const std::vector<std::string>& head,
                              const std::vector<std::string>& working);

int map_git_scroll_line(const std::vector<int>& line_map, int line);

enum class SideBySideRowKind {
  kContext,
  kDeletion,
  kAddition,
  kModification,
};

struct SideBySideDiffRow {
  SideBySideRowKind kind = SideBySideRowKind::kContext;
  std::string left;
  std::string right;
  int left_line = 0;
  int right_line = 0;
};

std::vector<SideBySideDiffRow> build_side_by_side_rows(
    const std::string& unified_diff, const std::vector<std::string>& head_lines,
    const std::vector<std::string>& working_lines);

struct DiffOverviewLines {
  std::unordered_set<int> add_lines;
  std::unordered_set<int> change_lines;
};

DiffOverviewLines build_diff_overview_lines(const std::vector<SideBySideDiffRow>& rows);

}  // namespace tgdb
