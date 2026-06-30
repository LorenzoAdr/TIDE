#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct GitCommitEntry {
  std::string hash;
  std::string short_hash;
  std::string message;
  std::string author;
  std::string date;
};

struct GitBranchEntry {
  std::string name;
  bool current = false;
  bool remote = false;
};

std::vector<GitCommitEntry> parse_git_log_oneline(const std::string& output);
std::vector<GitBranchEntry> parse_git_branches(const std::string& output);

}  // namespace tgdb
