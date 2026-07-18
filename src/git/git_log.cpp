#include "git/git_log.hpp"

#include <sstream>

namespace tuide {

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

std::vector<GitCommitEntry> parse_git_log_list(const std::string& output) {
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
    const auto p1 = line.find('|');
    if (p1 == std::string::npos) {
      continue;
    }
    const auto p2 = line.find('|', p1 + 1);
    if (p2 == std::string::npos) {
      continue;
    }
    entry.hash = line.substr(0, p1);
    entry.short_hash = line.substr(p1 + 1, p2 - p1 - 1);
    entry.message = line.substr(p2 + 1);
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<GitCommitEntry> parse_git_log_timeline(const std::string& output) {
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
    const auto p1 = line.find('|');
    if (p1 == std::string::npos) {
      continue;
    }
    const auto p2 = line.find('|', p1 + 1);
    if (p2 == std::string::npos) {
      continue;
    }
    const auto p3 = line.find('|', p2 + 1);
    if (p3 == std::string::npos) {
      continue;
    }
    const auto p4 = line.find('|', p3 + 1);
    if (p4 == std::string::npos) {
      continue;
    }
    entry.hash = line.substr(0, p1);
    entry.short_hash = line.substr(p1 + 1, p2 - p1 - 1);
    entry.message = line.substr(p2 + 1, p3 - p2 - 1);
    entry.author = line.substr(p3 + 1, p4 - p3 - 1);
    entry.date = line.substr(p4 + 1);
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<GitCommitFileEntry> parse_commit_name_status(const std::string& output) {
  std::vector<GitCommitFileEntry> entries;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const auto tab = line.find('\t');
    if (tab == std::string::npos) {
      continue;
    }
    GitCommitFileEntry entry;
    entry.status = line.substr(0, tab);
    if (entry.status.size() > 1) {
      entry.status = entry.status.substr(0, 1);
    }
    const std::string rest = line.substr(tab + 1);
    const auto tab2 = rest.find('\t');
    entry.path = tab2 == std::string::npos ? rest : rest.substr(tab2 + 1);
    if (!entry.path.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

std::vector<std::string> split_git_graph_lines(const std::string& output) {
  std::vector<std::string> lines;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
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

}  // namespace tuide
