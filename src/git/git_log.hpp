#pragma once

#include <string>
#include <vector>

namespace tuide {

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

struct GitCommitFileEntry {
  std::string status;
  std::string path;
};

std::vector<GitCommitEntry> parse_git_log_oneline(const std::string& output);
std::vector<GitCommitEntry> parse_git_log_list(const std::string& output);
std::vector<GitCommitEntry> parse_git_log_timeline(const std::string& output);
std::vector<GitBranchEntry> parse_git_branches(const std::string& output);
std::vector<GitCommitFileEntry> parse_commit_name_status(const std::string& output);
std::vector<std::string> split_git_graph_lines(const std::string& output);

}  // namespace tuide
