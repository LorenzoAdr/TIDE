#include "git/git_diff.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace tgdb {

namespace {

int parse_line_number(const std::string& hunk, char marker) {
  const std::string token = std::string(1, marker);
  const auto pos = hunk.find(token, 2);
  if (pos == std::string::npos) {
    return -1;
  }
  std::size_t start = pos + 1;
  const std::size_t comma = hunk.find(',', start);
  const std::size_t space = hunk.find(' ', start);
  const std::size_t end = comma != std::string::npos && (space == std::string::npos || comma < space)
                              ? comma
                              : space;
  const std::string num =
      end == std::string::npos ? hunk.substr(start) : hunk.substr(start, end - start);
  return std::atoi(num.c_str()) - 1;
}

}  // namespace

std::vector<std::string> parse_file_lines(const std::string& content) {
  std::vector<std::string> lines;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

GitFileDiff parse_unified_diff(const std::string& path, const std::string& diff_output) {
  GitFileDiff diff;
  diff.path = path;
  diff.loaded = true;
  if (diff_output.empty()) {
    return diff;
  }

  int old_line = 0;
  int new_line = 0;
  bool in_hunk = false;
  std::string pending_removed;

  std::istringstream stream(diff_output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("@@", 0) == 0) {
      old_line = parse_line_number(line, '-');
      new_line = parse_line_number(line, '+');
      in_hunk = old_line >= 0 && new_line >= 0;
      pending_removed.clear();
      continue;
    }
    if (!in_hunk || line.empty()) {
      continue;
    }
    if (line.rfind("\\ No newline at end of file", 0) == 0) {
      continue;
    }
    const char prefix = line[0];
    if (prefix != ' ' && prefix != '-' && prefix != '+') {
      continue;
    }
    const std::string content = line.size() > 1 ? line.substr(1) : std::string{};
    if (prefix == ' ') {
      pending_removed.clear();
      ++old_line;
      ++new_line;
    } else if (prefix == '-') {
      pending_removed = content;
      ++old_line;
    } else if (prefix == '+') {
      diff.line_changes[new_line] =
          pending_removed.empty() ? GitLineChange::kAdded : GitLineChange::kModified;
      if (!pending_removed.empty()) {
        diff.previous_content_by_line[new_line] = pending_removed;
        pending_removed.clear();
      }
      ++new_line;
    }
  }
  return diff;
}

LineDiffResult compute_line_diff(const std::vector<std::string>& head,
                                 const std::vector<std::string>& current) {
  LineDiffResult result;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < current.size() && j < head.size()) {
    if (current[i] == head[j]) {
      ++i;
      ++j;
      continue;
    }
    if (j + 1 < head.size() && current[i] == head[j + 1]) {
      ++j;
      continue;
    }
    if (i + 1 < current.size() && current[i + 1] == head[j]) {
      const int line = static_cast<int>(i);
      result.changed_new_lines.insert(line);
      ++i;
      continue;
    }
    const int line = static_cast<int>(i);
    result.changed_new_lines.insert(line);
    result.previous_content_by_new_line[line] = head[j];
    ++i;
    ++j;
  }
  while (i < current.size()) {
    result.changed_new_lines.insert(static_cast<int>(i));
    ++i;
  }
  return result;
}

std::unordered_set<int> compute_changed_lines(const std::vector<std::string>& current,
                                              const std::vector<std::string>& head) {
  return compute_line_diff(head, current).changed_new_lines;
}

GitLineMap build_git_line_map(const std::vector<std::string>& head,
                              const std::vector<std::string>& working) {
  GitLineMap map;
  map.working_to_head.assign(working.size(), -1);
  map.head_to_working.assign(head.size(), -1);

  std::size_t i = 0;
  std::size_t j = 0;
  while (i < working.size() && j < head.size()) {
    if (working[i] == head[j]) {
      map.working_to_head[i] = static_cast<int>(j);
      map.head_to_working[j] = static_cast<int>(i);
      ++i;
      ++j;
      continue;
    }
    if (j + 1 < head.size() && working[i] == head[j + 1]) {
      ++j;
      continue;
    }
    if (i + 1 < working.size() && working[i + 1] == head[j]) {
      ++i;
      continue;
    }
    map.working_to_head[i] = static_cast<int>(j);
    map.head_to_working[j] = static_cast<int>(i);
    ++i;
    ++j;
  }
  return map;
}

int map_git_scroll_line(const std::vector<int>& line_map, int line) {
  if (line_map.empty()) {
    return std::max(0, line);
  }
  if (line >= 0 && line < static_cast<int>(line_map.size()) && line_map[static_cast<std::size_t>(line)] >= 0) {
    return line_map[static_cast<std::size_t>(line)];
  }
  const int start = std::min(line, static_cast<int>(line_map.size()) - 1);
  for (int i = start; i >= 0; --i) {
    if (line_map[static_cast<std::size_t>(i)] >= 0) {
      return line_map[static_cast<std::size_t>(i)];
    }
  }
  for (int i = std::max(0, line); i < static_cast<int>(line_map.size()); ++i) {
    if (line_map[static_cast<std::size_t>(i)] >= 0) {
      return line_map[static_cast<std::size_t>(i)];
    }
  }
  return 0;
}

}  // namespace tgdb
