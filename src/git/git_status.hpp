#pragma once

#include <string>
#include <vector>

namespace tgdb {

enum class GitFileStatus {
  kUnmodified,
  kModified,
  kAdded,
  kDeleted,
  kRenamed,
  kCopied,
  kUntracked,
  kIgnored,
};

struct GitStatusEntry {
  std::string path;
  GitFileStatus staged = GitFileStatus::kUnmodified;
  GitFileStatus unstaged = GitFileStatus::kUnmodified;
};

struct GitStatusSnapshot {
  std::vector<GitStatusEntry> entries;
  int staged_count = 0;
  int unstaged_count = 0;
  int untracked_count = 0;
};

GitStatusSnapshot parse_git_status_porcelain(const std::string& output);

}  // namespace tgdb
