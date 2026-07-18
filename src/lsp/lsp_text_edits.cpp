#include "lsp/lsp_text_edits.hpp"

#include <algorithm>
#include <optional>

#include "lsp/lsp_position.hpp"
#include "lsp/lsp_uri.hpp"
#include "util/path_normalize.hpp"

namespace tuide {

namespace {

std::size_t byte_offset_at(const std::vector<std::string>& lines, int line, int character) {
  if (line < 0 || character < 0) {
    return 0;
  }
  std::size_t offset = 0;
  const int line_count = static_cast<int>(lines.size());
  for (int i = 0; i < line && i < line_count; ++i) {
    offset += lines[static_cast<std::size_t>(i)].size() + 1;
  }
  if (line < line_count) {
    const std::string& row = lines[static_cast<std::size_t>(line)];
    const int col = std::min(character, static_cast<int>(row.size()));
    offset += static_cast<std::size_t>(col);
  }
  return offset;
}

bool parse_range(const nlohmann::json& range, LspTextEdit* edit) {
  if (edit == nullptr || !range.is_object() || !range.contains("start") || !range.contains("end")) {
    return false;
  }
  const auto& start = range["start"];
  const auto& end = range["end"];
  if (!start.is_object() || !end.is_object()) {
    return false;
  }
  edit->start_line = start["line"].get<int>();
  edit->start_character = start["character"].get<int>();
  edit->end_line = end["line"].get<int>();
  edit->end_character = end["character"].get<int>();
  return true;
}

}  // namespace

std::vector<LspTextEdit> parse_lsp_text_edits(const nlohmann::json& result) {
  std::vector<LspTextEdit> edits;
  if (!result.is_array()) {
    return edits;
  }
  for (const auto& item : result) {
    if (!item.is_object() || !item.contains("range")) {
      continue;
    }
    LspTextEdit edit;
    if (!parse_range(item["range"], &edit)) {
      continue;
    }
    if (item.contains("newText") && item["newText"].is_string()) {
      edit.new_text = item["newText"].get<std::string>();
    }
    edits.push_back(std::move(edit));
  }
  return edits;
}

std::vector<LspFileEdits> parse_workspace_edit(const nlohmann::json& result) {
  std::vector<LspFileEdits> files;
  if (!result.is_object()) {
    return files;
  }

  if (result.contains("changes") && result["changes"].is_object()) {
    for (const auto& [uri, edits_json] : result["changes"].items()) {
      if (!edits_json.is_array()) {
        continue;
      }
      const std::string path = normalize_lsp_path(uri_to_path(uri));
      if (path.empty()) {
        continue;
      }
      LspFileEdits file;
      file.path = path;
      file.edits = parse_lsp_text_edits(edits_json);
      if (!file.edits.empty()) {
        files.push_back(std::move(file));
      }
    }
    return files;
  }

  if (result.contains("documentChanges") && result["documentChanges"].is_array()) {
    for (const auto& change : result["documentChanges"]) {
      if (!change.is_object()) {
        continue;
      }
      std::string path;
      if (change.contains("textDocument") && change["textDocument"].is_object() &&
          change["textDocument"].contains("uri") &&
          change["textDocument"]["uri"].is_string()) {
        path = normalize_lsp_path(uri_to_path(change["textDocument"]["uri"].get<std::string>()));
      }
      if (path.empty()) {
        continue;
      }
      nlohmann::json edits_json = nlohmann::json::array();
      if (change.contains("edits") && change["edits"].is_array()) {
        edits_json = change["edits"];
      }
      LspFileEdits file;
      file.path = path;
      file.edits = parse_lsp_text_edits(edits_json);
      if (!file.edits.empty()) {
        files.push_back(std::move(file));
      }
    }
  }
  return files;
}

std::string apply_lsp_text_edits(const std::string& text, const std::vector<LspTextEdit>& edits) {
  if (edits.empty()) {
    return text;
  }

  const std::vector<std::string> original_lines = lines_from_document_text(text);
  std::vector<LspTextEdit> sorted = edits;
  std::sort(sorted.begin(), sorted.end(), [](const LspTextEdit& a, const LspTextEdit& b) {
    if (a.start_line != b.start_line) {
      return a.start_line > b.start_line;
    }
    return a.start_character > b.start_character;
  });

  std::string output = text;
  for (const LspTextEdit& edit : sorted) {
    const std::size_t start = byte_offset_at(original_lines, edit.start_line, edit.start_character);
    const std::size_t end = byte_offset_at(original_lines, edit.end_line, edit.end_character);
    if (start > output.size() || end > output.size() || start > end) {
      continue;
    }
    output.replace(start, end - start, edit.new_text);
  }
  return output;
}

std::vector<std::string> lines_from_document_text(const std::string& text) {
  std::vector<std::string> lines;
  if (text.empty()) {
    lines.push_back("");
    return lines;
  }
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  if (!lines.empty() && text.back() == '\n') {
    lines.push_back("");
  }
  return lines;
}

void byte_offset_to_line_byte_col(const std::string& text, std::size_t byte_offset, int* line,
                                  int* col) {
  if (line == nullptr || col == nullptr) {
    return;
  }
  *line = 0;
  *col = 0;
  std::size_t offset = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (offset >= byte_offset) {
      break;
    }
    if (text[i] == '\n') {
      ++*line;
      *col = 0;
    } else {
      ++*col;
    }
    ++offset;
  }
}

std::optional<LspTextEdit> single_lsp_edit_between(const std::string& old_source,
                                                   const std::string& new_source) {
  if (old_source == new_source) {
    return std::nullopt;
  }
  const std::size_t max_prefix = std::min(old_source.size(), new_source.size());
  std::size_t prefix = 0;
  while (prefix < max_prefix && old_source[prefix] == new_source[prefix]) {
    ++prefix;
  }
  if (prefix == old_source.size() && prefix == new_source.size()) {
    return std::nullopt;
  }

  std::size_t old_suffix = old_source.size();
  std::size_t new_suffix = new_source.size();
  while (old_suffix > prefix && new_suffix > prefix &&
         old_source[old_suffix - 1] == new_source[new_suffix - 1]) {
    --old_suffix;
    --new_suffix;
  }
  if (old_suffix < prefix || new_suffix < prefix) {
    return std::nullopt;
  }

  int start_line = 0;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
  byte_offset_to_line_byte_col(old_source, prefix, &start_line, &start_col);
  byte_offset_to_line_byte_col(old_source, old_suffix, &end_line, &end_col);

  LspTextEdit edit;
  edit.start_line = start_line;
  edit.start_character = lsp_utf16_column(line_text_at(old_source, start_line), start_col);
  edit.end_line = end_line;
  edit.end_character = lsp_utf16_column(line_text_at(old_source, end_line), end_col);
  edit.new_text = new_source.substr(prefix, new_suffix - prefix);
  return edit;
}

nlohmann::json lsp_content_change_json(const LspTextEdit& edit) {
  return {{"range",
           {{"start", {{"line", edit.start_line}, {"character", edit.start_character}}},
            {"end", {{"line", edit.end_line}, {"character", edit.end_character}}}}},
          {"text", edit.new_text}};
}

}  // namespace tuide
