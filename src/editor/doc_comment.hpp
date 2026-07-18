#pragma once

#include <string>
#include <vector>

#include "symbols/symbol_kind.hpp"

namespace tgdb {

struct DocCommentRequest {
  std::string path;
  SymbolKind kind = SymbolKind::kVariable;
  std::string symbol_name;
  std::string declaration_line;
  int indent_cols = 0;
};

// Line (0-based) where the snippet should be inserted, and whether it goes above
// the symbol declaration (C-family) or on the body line below (Python).
struct DocCommentInsertPlan {
  int insert_line = 0;
  int insert_col = 0;
  std::string snippet;
};

std::vector<std::string> extract_param_names(const std::string& declaration_line,
                                             const std::string& language_id);

std::string build_doc_comment_snippet(const DocCommentRequest& request);
std::string build_separator_snippet(const std::string& path, int indent_cols = 0);
std::string build_file_header_snippet(const std::string& path);

// Convenience: pick insert line/col for a doc comment given the symbol line (0-based).
DocCommentInsertPlan plan_doc_comment_insert(const DocCommentRequest& request, int symbol_line);

}  // namespace tgdb
