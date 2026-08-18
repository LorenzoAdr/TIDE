#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "search/workspace_search.hpp"

namespace tuide {

enum class SearchRowKind { File, Match };

struct SearchDisplayRow {
  SearchRowKind kind = SearchRowKind::Match;
  std::string file;
  int result_index = -1;
  int match_count = 0;
  int depth = 0;
};

inline std::vector<SearchDisplayRow> flatten_search_results(
    const std::vector<WorkspaceSearchResult>& results,
    const std::unordered_set<std::string>& collapsed_files) {
  std::vector<std::string> file_order;
  std::unordered_map<std::string, std::vector<int>> by_file;
  file_order.reserve(64);
  by_file.reserve(64);
  for (int i = 0; i < static_cast<int>(results.size()); ++i) {
    const std::string& file = results[static_cast<std::size_t>(i)].file;
    auto it = by_file.find(file);
    if (it == by_file.end()) {
      file_order.push_back(file);
      by_file.emplace(file, std::vector<int>{i});
    } else {
      it->second.push_back(i);
    }
  }

  std::vector<SearchDisplayRow> rows;
  rows.reserve(results.size() + file_order.size());
  for (const auto& file : file_order) {
    const auto& indices = by_file[file];
    SearchDisplayRow header;
    header.kind = SearchRowKind::File;
    header.file = file;
    header.result_index = indices.empty() ? -1 : indices.front();
    header.match_count = static_cast<int>(indices.size());
    header.depth = 0;
    rows.push_back(std::move(header));
    if (collapsed_files.count(file) > 0) {
      continue;
    }
    for (int idx : indices) {
      SearchDisplayRow match;
      match.kind = SearchRowKind::Match;
      match.file = file;
      match.result_index = idx;
      match.depth = 1;
      rows.push_back(std::move(match));
    }
  }
  return rows;
}

inline bool search_all_files_collapsed(const std::vector<WorkspaceSearchResult>& results,
                                       const std::unordered_set<std::string>& collapsed_files) {
  if (results.empty() || collapsed_files.empty()) {
    return false;
  }
  for (const auto& hit : results) {
    if (collapsed_files.count(hit.file) == 0) {
      return false;
    }
  }
  return true;
}

inline int search_file_row_index(const std::vector<SearchDisplayRow>& rows, const std::string& file) {
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const auto& row = rows[static_cast<std::size_t>(i)];
    if (row.kind == SearchRowKind::File && row.file == file) {
      return i;
    }
  }
  return -1;
}

}  // namespace tuide
