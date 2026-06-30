#include "git/git_log.hpp"

#include <sstream>

namespace tgdb {

std::vector<GitCommitEntry> parse_git_log_oneline(const std::string& output) {
  std::vector<GitCommitEntry> entries;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    GitCommitEntry entry;
    const auto space = line.find(' ');
    if (space == std::string::npos) {
      continue;
    }
    entry.short_hash = line.substr(0, space);
    entry.hash = entry.short_hash;
    entry.message = line.substr(space + 1);
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<GitBranchEntry> parse_git_branches(const std::string& output) {
  std::vector<GitBranchEntry> entries;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    GitBranchEntry entry;
    std::size_t pos = 0;
    if (line.rfind("remotes/", 0) == 0) {
      entry.remote = true;
    }
    if (line[pos] == '*') {
      entry.current = true;
      ++pos;
    } else if (line[pos] == ' ') {
      ++pos;
    }
    entry.name = line.substr(pos);
    entries.push_back(std::move(entry));
  }
  return entries;
}

}  // namespace tgdb
