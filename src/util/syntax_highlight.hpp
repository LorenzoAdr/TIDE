#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"
#include "lsp/semantic_tokens.hpp"
#include "util/cpp_highlight.hpp"

namespace tgdb {

ftxui::Element HighlightCodeLine(const std::string& line, int line_index,
                                 const SemanticTokenDocument* semantic_tokens,
                                 int cursor_col = -1,
                                 ftxui::Decorator cursor_style = {},
                                 int col_offset = 0,
                                 CppHighlightContext* ctx = nullptr);

}  // namespace tgdb
