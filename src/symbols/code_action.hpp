#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/diagnostics.hpp"
#include "lsp/lsp_text_edits.hpp"

namespace tgdb {

struct CodeActionParams {
  std::string path;
  std::string text;
  int line = 0;
  int start_col = 0;
  int end_col = 0;
  Diagnostic diagnostic;
};

struct CodeActionItem {
  std::string title;
  std::string kind;
  std::vector<LspFileEdits> file_edits;
  nlohmann::json lsp_payload = nlohmann::json::object();
};

}  // namespace tgdb
