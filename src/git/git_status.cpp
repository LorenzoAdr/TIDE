#include "git/git_status.hpp"

namespace tuide {

namespace {

GitFileStatus char_to_status(char c) {
  switch (c) {
    case 'M':
      return GitFileStatus::kModified;
    case 'A':
      return GitFileStatus::kAdded;
    case 'D':
      return GitFileStatus::kDeleted;
    case 'R':
      return GitFileStatus::kRenamed;
    case 'C':
      return GitFileStatus::kCopied;
    case '?':
      return GitFileStatus::kUntracked;
    case '!':
      return GitFileStatus::kIgnored;
    default:
      return GitFileStatus::kUnmodified;
  }
}

}  // namespace

GitStatusSnapshot parse_git_status_porcelain(const std::string& output) {
  GitStatusSnapshot snapshot;
  std::size_t pos = 0;
  while (pos < output.size()) {
    const std::size_t end = output.find('\n', pos);
    const std::string line =
        end == std::string::npos ? output.substr(pos) : output.substr(pos, end - pos);
    pos = end == std::string::npos ? output.size() : end + 1;

    if (line.size() < 4 || line[0] == '#') {
      continue;
    }

    GitStatusEntry entry;
    entry.staged = char_to_status(line[0]);
    entry.unstaged = char_to_status(line[1]);
    entry.path = line.substr(3);

    if (entry.staged == GitFileStatus::kRenamed && entry.path.find(" -> ") != std::string::npos) {
      const auto arrow = entry.path.find(" -> ");
      entry.path = entry.path.substr(arrow + 4);
    }

    if (entry.staged != GitFileStatus::kUnmodified) {
      ++snapshot.staged_count;
    }
    if (entry.unstaged == GitFileStatus::kUntracked) {
      ++snapshot.untracked_count;
    } else if (entry.unstaged != GitFileStatus::kUnmodified) {
      ++snapshot.unstaged_count;
    }
    snapshot.entries.push_back(std::move(entry));
  }
  return snapshot;
}

}  // namespace tuide
