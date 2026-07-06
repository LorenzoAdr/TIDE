#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/editor_state.hpp"

namespace tgdb {

struct TextMatch {
  int line = 0;
  int col = 0;
  int length = 0;
};

struct TextRange {
  int start_line = 0;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
};

bool is_ident_char(char c);
bool is_ident_start(char c);

std::string word_at_cursor(const EditorBuffer& buffer, const MultiCursor& cursor);
std::string word_at_line_col(const std::string& line, int col);
bool ident_range_at_cursor(const EditorBuffer& buffer, const MultiCursor& cursor,
                             int* start_col, int* end_col);
std::string selection_text(const EditorBuffer& buffer, const MultiCursor& cursor);

std::string search_needle(const EditorBuffer& buffer);

bool find_next_match(const EditorBuffer& buffer, const std::string& needle,
                     const CursorPos& from, TextMatch* out,
                     const std::vector<TextMatch>* skip = nullptr);

std::vector<TextMatch> find_all_matches(const EditorBuffer& buffer, const std::string& needle);

std::vector<TextMatch> find_all_matches_in_lines(const std::vector<std::string>& lines,
                                                 const std::string& needle,
                                                 const std::atomic<uint64_t>* active_request_id,
                                                 uint64_t request_id);

std::vector<TextMatch> find_selection_occurrences(const EditorBuffer& buffer);

std::vector<TextMatch> find_occurrences_in_lines(const std::vector<std::string>& lines,
                                                 const std::string& needle, bool whole_word,
                                                 const std::atomic<uint64_t>* active_request_id,
                                                 uint64_t request_id);

bool match_occupied(const TextMatch& match, const EditorBuffer& buffer);

void add_next_selection_match(EditorBuffer* buffer, int visible_lines);
void select_all_matches(EditorBuffer* buffer, const TextRange* scope = nullptr);
bool apply_regex_match_cursors(EditorBuffer* buffer, const std::string& pattern,
                               const TextRange* scope);

}  // namespace tgdb
