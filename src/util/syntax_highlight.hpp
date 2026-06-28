#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"
#include "lsp/semantic_tokens.hpp"

namespace tgdb {

ftxui::Element HighlightCodeLine(const std::string& line, int line_index,
                                 const SemanticTokenDocument* semantic_tokens,
                                 int cursor_col = -1,
                                 ftxui::Decorator cursor_style = {},
                                 int col_offset = 0);

}  // namespace tgdb
