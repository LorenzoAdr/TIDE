#pragma once

#include <string>
#include <vector>

#include "editor/editor_state.hpp"

namespace tgdb {

struct TextMatch {
  int line = 0;
  int col = 0;
  int length = 0;
};

bool is_ident_char(char c);
bool is_ident_start(char c);

std::string word_at_cursor(const EditorBuffer& buffer, const MultiCursor& cursor);
bool ident_range_at_cursor(const EditorBuffer& buffer, const MultiCursor& cursor,
                             int* start_col, int* end_col);
std::string selection_text(const EditorBuffer& buffer, const MultiCursor& cursor);

std::string search_needle(const EditorBuffer& buffer);

bool find_next_match(const EditorBuffer& buffer, const std::string& needle,
                     const CursorPos& from, TextMatch* out,
                     const std::vector<TextMatch>* skip = nullptr);

std::vector<TextMatch> find_all_matches(const EditorBuffer& buffer, const std::string& needle);

std::vector<TextMatch> find_selection_occurrences(const EditorBuffer& buffer);

bool match_occupied(const TextMatch& match, const EditorBuffer& buffer);

void add_next_selection_match(EditorBuffer* buffer, int visible_lines);
void select_all_matches(EditorBuffer* buffer);

}  // namespace tgdb
