#include "search/workspace_search.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "indexer/index_rules.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr int kMaxResults = 2000;

std::string trim_preview(const std::string& line) {
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = line.find_last_not_of(" \t");
  return line.substr(start, end - start + 1);
}

std::string normalize_filter(std::string value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.erase(value.begin());
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  while (value.size() >= 2 && value[0] == '.' && value[1] == '/') {
    value = value.substr(2);
  }
  return value;
}

std::vector<std::string> split_patterns(const std::string& patterns) {
  std::vector<std::string> out;
  std::string current;
  for (char c : patterns) {
    if (c == ',' || c == ';') {
      if (!current.empty()) {
        out.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    out.push_back(current);
  }
  return out;
}

std::string replace_all(std::string text, const std::string& needle,
                        const std::string& replacement) {
  if (needle.empty()) {
    return text;
  }
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
  return text;
}

}  // namespace

bool glob_match(const std::string& pattern, const std::string& text) {
  std::size_t pi = 0;
  std::size_t ti = 0;
  std::size_t star_pi = std::string::npos;
  std::size_t star_ti = 0;

  while (ti <= text.size()) {
    if (pi < pattern.size() &&
        (pattern[pi] == '?' || pattern[pi] == text[ti])) {
      ++pi;
      ++ti;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      star_pi = pi++;
      star_ti = ti;
    } else if (star_pi != std::string::npos) {
      pi = star_pi + 1;
      ti = ++star_ti;
    } else {
      return false;
    }
  }

  while (pi < pattern.size() && pattern[pi] == '*') {
    ++pi;
  }
  return pi == pattern.size();
}

bool file_included(const std::string& relative_path, const std::string& include_pattern) {
  const std::string trimmed = normalize_filter(include_pattern);
  if (trimmed.empty()) {
    return true;
  }

  const auto basename = fs::path(relative_path).filename().string();
  for (const auto& pattern : split_patterns(trimmed)) {
    const std::string p = normalize_filter(pattern);
    if (p.empty()) {
      continue;
    }
    if (glob_match(p, relative_path) || glob_match(p, basename)) {
      return true;
    }
  }
  return false;
}

bool path_under_always_skipped_dir(const std::string& relative_path) {
  for (const auto& part : fs::path(relative_path)) {
    if (is_lazy_stub_dir_name(part.string())) {
      return true;
    }
  }
  return false;
}

bool file_excluded(const std::string& relative_path, const std::string& exclude_pattern) {
  if (path_under_always_skipped_dir(relative_path)) {
    return true;
  }

  const std::string trimmed = normalize_filter(exclude_pattern);
  if (trimmed.empty()) {
    return false;
  }

  const auto basename = fs::path(relative_path).filename().string();
  for (const auto& pattern : split_patterns(trimmed)) {
    const std::string p = normalize_filter(pattern);
    if (p.empty()) {
      continue;
    }
    if (glob_match(p, relative_path) || glob_match(p, basename)) {
      return true;
    }
  }
  return false;
}

bool file_in_search_path(const std::string& relative_path, const std::string& path_filter) {
  const std::string filter = normalize_filter(path_filter);
  if (filter.empty()) {
    return true;
  }
  if (relative_path == filter) {
    return true;
  }
  if (relative_path.size() > filter.size() &&
      relative_path.compare(0, filter.size(), filter) == 0 &&
      relative_path[filter.size()] == '/') {
    return true;
  }
  return false;
}

std::vector<WorkspaceSearchResult> search_workspace(const WorkspaceSearchOptions& opts) {
  std::vector<WorkspaceSearchResult> results;
  if (opts.needle.empty() || opts.workspace_root.empty()) {
    return results;
  }

  for (const auto& rel : opts.files) {
    if (!file_in_search_path(rel, opts.path_filter)) {
      continue;
    }
    if (!file_included(rel, opts.include_pattern)) {
      continue;
    }
    if (file_excluded(rel, opts.exclude_pattern)) {
      continue;
    }
    if (is_probably_binary_path(rel)) {
      continue;
    }

    std::error_code ec;
    const auto absolute = fs::path(opts.workspace_root) / rel;
    if (!fs::is_regular_file(absolute, ec)) {
      continue;
    }

    std::ifstream input(absolute, std::ios::binary);
    if (!input) {
      continue;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(input, line)) {
      if (line_num == 0 && text_looks_binary(line)) {
        break;
      }
      ++line_num;
      std::size_t pos = 0;
      while ((pos = line.find(opts.needle, pos)) != std::string::npos) {
        results.push_back(
            {rel, line_num, static_cast<int>(pos) + 1, trim_preview(line)});
        if (results.size() >= static_cast<std::size_t>(kMaxResults)) {
          return results;
        }
        pos += opts.needle.size();
      }
    }
  }
  return results;
}

WorkspaceReplaceResult replace_in_workspace(const WorkspaceSearchOptions& opts,
                                            const std::string& replacement) {
  WorkspaceReplaceResult result;
  if (opts.needle.empty() || opts.workspace_root.empty()) {
    return result;
  }

  for (const auto& rel : opts.files) {
    if (!file_in_search_path(rel, opts.path_filter)) {
      continue;
    }
    if (!file_included(rel, opts.include_pattern)) {
      continue;
    }
    if (file_excluded(rel, opts.exclude_pattern)) {
      continue;
    }

    std::error_code ec;
    const auto absolute = fs::path(opts.workspace_root) / rel;
    if (!fs::is_regular_file(absolute, ec)) {
      continue;
    }

    std::ifstream input(absolute);
    if (!input) {
      continue;
    }

    std::ostringstream buffer;
    std::string line;
    int file_replacements = 0;
    bool first = true;
    while (std::getline(input, line)) {
      if (!first) {
        buffer << '\n';
      }
      first = false;
      const std::string original = line;
      line = replace_all(line, opts.needle, replacement);
      if (line != original) {
        std::size_t pos = 0;
        while ((pos = original.find(opts.needle, pos)) != std::string::npos) {
          ++file_replacements;
          pos += opts.needle.size();
        }
      }
      buffer << line;
    }

    if (file_replacements == 0) {
      continue;
    }

    std::ofstream output(absolute, std::ios::trunc);
    if (!output) {
      continue;
    }
    output << buffer.str();
    ++result.files_modified;
    result.replacements += file_replacements;
  }
  return result;
}

}  // namespace tgdb
