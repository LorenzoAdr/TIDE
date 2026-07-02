#pragma once

#include <string>

namespace tgdb {

struct SnippetResult {
  std::string text;
  int caret_line_offset = 0;
  int caret_col = 0;
  int sel_start_col = -1;
  int sel_end_col = -1;
};

SnippetResult expand_snippet(const std::string& snippet);
SnippetResult adjust_snippet_for_existing_open_paren(const std::string& raw_snippet);
SnippetResult finalize_function_call_insert(const std::string& insert_text, const std::string& detail,
                                            bool is_callable, bool paren_already_there = false);
bool has_char_at(const std::string& line, int col, char expected);
bool completion_insert_is_empty_call(const std::string& insert_text);

}  // namespace tgdb
