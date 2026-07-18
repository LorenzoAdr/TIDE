#pragma once

#include <string>
#include <vector>

namespace tuide {

struct EditorBuffer;

struct SnippetPlaceholder {
  int index = 0;
  int line_offset = 0;
  int col = 0;
  int length = 0;
};

struct SnippetResult {
  std::string text;
  int caret_line_offset = 0;
  int caret_col = 0;
  int sel_start_col = -1;
  int sel_end_col = -1;
  std::vector<SnippetPlaceholder> placeholders;
  int active_placeholder_index = 0;
};

struct SnippetSessionStop {
  int index = 0;
  int line = 0;
  int col = 0;
  int length = 0;
};

struct SnippetSession {
  bool active = false;
  std::vector<SnippetSessionStop> placeholders;
  int current_index = 0;
};

SnippetResult expand_snippet(const std::string& snippet);
SnippetResult adjust_snippet_for_existing_open_paren(const std::string& raw_snippet);
SnippetResult finalize_function_call_insert(const std::string& insert_text, const std::string& detail,
                                            bool is_callable, bool paren_already_there = false);
bool has_char_at(const std::string& line, int col, char expected);
bool completion_insert_is_empty_call(const std::string& insert_text);

void begin_snippet_session(SnippetSession* session, int insert_line, int insert_start_col,
                           const SnippetResult& snippet);
void clear_snippet_session(SnippetSession* session);
bool snippet_session_active(const SnippetSession& session);
bool advance_snippet_session(EditorBuffer* buffer, SnippetSession* session);

}  // namespace tuide
