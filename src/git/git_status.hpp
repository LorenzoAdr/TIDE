#pragma once

#include <string>
#include <vector>

namespace tuide {

enum class GitFileStatus {
  kUnmodified,
  kModified,
  kAdded,
  kDeleted,
  kRenamed,
  kCopied,
  kUntracked,
  kIgnored,
  // Sin fusionar (merge/rebase/cherry-pick en curso): XY porcelain es uno de
  // DD, AU, UD, UA, DU, AA, UU. No es un estado de staged/unstaged normal.
  kConflicted,
};

struct GitStatusEntry {
  std::string path;
  std::string repo_prefix;
  GitFileStatus staged = GitFileStatus::kUnmodified;
  GitFileStatus unstaged = GitFileStatus::kUnmodified;

  bool is_conflicted() const {
    return staged == GitFileStatus::kConflicted || unstaged == GitFileStatus::kConflicted;
  }
};

struct GitStatusSnapshot {
  std::vector<GitStatusEntry> entries;
  int staged_count = 0;
  int unstaged_count = 0;
  int untracked_count = 0;
  int conflicted_count = 0;
};

GitStatusSnapshot parse_git_status_porcelain(const std::string& output);

}  // namespace tuide
