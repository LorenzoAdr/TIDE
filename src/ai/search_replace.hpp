#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tuide {

struct SearchReplaceHunk {
  std::string path;
  std::string search;
  std::string replace;
};

struct SearchReplaceSpan {
  int start_line = 1;  // 1-based
  int start_col = 1;   // 1-based
  int end_line = 1;
  int end_col = 1;  // 1-based, exclusive (char after last of search)
  std::size_t byte_begin = 0;
  std::size_t byte_end = 0;
};

struct ApplyHunkResult {
  bool ok = false;
  std::string error;
  std::string abs_path;
  std::string old_text;  // == search (matched)
  std::string new_text;  // == replace
  std::string before;    // full file before
  std::string after;     // full file after
  SearchReplaceSpan span;
};

// Find needle exactly once in haystack. Returns false if 0 or ≥2 matches.
bool find_unique_span(const std::string& haystack, const std::string& needle, SearchReplaceSpan* out,
                      std::string* err);

std::vector<SearchReplaceHunk> parse_search_replace_json(const nlohmann::json& j, std::string* err);

// Aider-style blocks: <<<<<<< SEARCH / ======= / >>>>>>> REPLACE (optional path: header).
std::vector<SearchReplaceHunk> parse_search_replace_aider(const std::string& text, std::string* err);

ApplyHunkResult apply_hunk_to_text(const std::string& text, const SearchReplaceHunk& hunk);

// Reads file under workspace_root, applies unique SEARCH→REPLACE, optionally writes.
ApplyHunkResult apply_hunk_to_workspace_file(const std::string& workspace_root,
                                             const SearchReplaceHunk& hunk, bool write);

bool write_text_file(const std::string& abs_path, const std::string& body, std::string* err);

}  // namespace tuide
