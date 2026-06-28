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
SnippetResult finalize_function_call_insert(const std::string& insert_text, const std::string& detail,
                                            bool is_callable);

}  // namespace tgdb
