#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"

namespace tgdb {

struct CppHighlightContext {
  bool in_block_comment = false;
};

void advance_cpp_highlight_context(const std::string& line, CppHighlightContext* ctx);

bool block_comment_state_after_line(const std::string& line, bool in_block_comment);

ftxui::Element HighlightCppLine(const std::string& line, int cursor_col = -1,
                                ftxui::Decorator cursor_style = {}, int col_offset = 0,
                                CppHighlightContext* ctx = nullptr);

}  // namespace tgdb
