#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "lsp/semantic_tokens.hpp"
#include "parser/tree_sitter_document.hpp"

namespace tgdb {

struct SyntaxHighlightContext {
  std::string file_path;
  const std::vector<std::string>* lines = nullptr;
  uint64_t buffer_token = 0;
  mutable std::string joined_source;
  mutable uint64_t joined_token = 0;
  mutable uint64_t ts_revision = 0;
  mutable const std::vector<LineHighlights>* ts_line_highlights = nullptr;

  const std::string& joined() const;
  const std::vector<LineHighlights>* tree_sitter_highlights() const;
};

ftxui::Element HighlightCodeLine(const std::string& line, int line_index,
                                 const SemanticTokenDocument* semantic_tokens,
                                 int cursor_col = -1,
                                 ftxui::Decorator cursor_style = {},
                                 int col_offset = 0,
                                 const SyntaxHighlightContext* ctx = nullptr);

}  // namespace tgdb
