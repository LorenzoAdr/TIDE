#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tgdb {

struct LspTextEdit {
  int start_line = 0;
  int start_character = 0;
  int end_line = 0;
  int end_character = 0;
  std::string new_text;
};

struct LspFileEdits {
  std::string path;
  std::vector<LspTextEdit> edits;
};

std::vector<LspTextEdit> parse_lsp_text_edits(const nlohmann::json& result);
std::vector<LspFileEdits> parse_workspace_edit(const nlohmann::json& result);
std::string apply_lsp_text_edits(const std::string& text, const std::vector<LspTextEdit>& edits);
std::vector<std::string> lines_from_document_text(const std::string& text);

}  // namespace tgdb
