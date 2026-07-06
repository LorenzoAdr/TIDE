#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace tgdb {

// Byte offset within a single line -> LSP UTF-16 code unit offset.
int lsp_utf16_column(const std::string& line, int byte_col);

std::string line_text_at(const std::string& text, int line);

nlohmann::json make_lsp_position(const std::string& text, int line, int byte_col);

}  // namespace tgdb
