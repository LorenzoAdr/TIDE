#pragma once

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

#include "parser/tree_sitter_document.hpp"

namespace tgdb {

LineHighlights highlights_for_line(TSNode root, const std::string& source, int line_0);

std::vector<LineHighlights> highlights_for_document(TSNode root, const std::string& source);

ftxui::Element HighlightTreeSitterLine(const std::string& line, int line_index,
                                       const LineHighlights& highlights, int cursor_col = -1,
                                       ftxui::Decorator cursor_style = {}, int col_offset = 0);

ftxui::Element HighlightTreeSitterLine(const std::string& line, int line_index,
                                       const std::string& source, TSNode root, int cursor_col = -1,
                                       ftxui::Decorator cursor_style = {}, int col_offset = 0);

}  // namespace tgdb
