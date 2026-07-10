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

// Re-highlight only lines touched by an incremental parse; falls back to full scan.
// When layout_shift_from_row >= 0, lines at/after that row are always re-highlighted
// (needed when line count changes and indices shift).
std::vector<LineHighlights> highlights_after_incremental_parse(
    TSTree* old_tree, TSTree* new_tree, TSNode new_root, const std::string& source,
    const std::vector<LineHighlights>& previous, int layout_shift_from_row = -1);

ftxui::Element HighlightTreeSitterLine(const std::string& line, int line_index,
                                       const LineHighlights& highlights, int cursor_col = -1,
                                       ftxui::Decorator cursor_style = {}, int col_offset = 0);

ftxui::Element HighlightTreeSitterLine(const std::string& line, int line_index,
                                       const std::string& source, TSNode root, int cursor_col = -1,
                                       ftxui::Decorator cursor_style = {}, int col_offset = 0);

}  // namespace tgdb
