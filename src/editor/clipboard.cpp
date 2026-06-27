#include "editor/clipboard.hpp"

#include "editor/text_ops.hpp"

namespace tgdb {

std::string& editor_clipboard() {
  static std::string clipboard;
  return clipboard;
}

std::string extract_selection_text(const EditorBuffer& buffer, const MultiCursor& cursor) {
  if (!cursor.has_selection()) {
    return {};
  }

  int start_line = 0;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
  cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);

  if (start_line == end_line) {
    if (start_line < 0 || start_line >= static_cast<int>(buffer.lines.size())) {
      return {};
    }
    const std::string& line = buffer.lines[static_cast<std::size_t>(start_line)];
    start_col = std::max(0, std::min(start_col, static_cast<int>(line.size())));
    end_col = std::max(start_col, std::min(end_col, static_cast<int>(line.size())));
    return line.substr(static_cast<std::size_t>(start_col),
                       static_cast<std::size_t>(end_col - start_col));
  }

  std::string out;
  for (int line = start_line; line <= end_line; ++line) {
    if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
      continue;
    }
    const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
    const int from = (line == start_line) ? start_col : 0;
    const int to = (line == end_line) ? std::min(end_col, static_cast<int>(text.size()))
                                      : static_cast<int>(text.size());
    if (to > from) {
      out += text.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
    }
    if (line < end_line) {
      out += '\n';
    }
  }
  return out;
}

bool copy_selection(EditorBuffer* buffer) {
  const std::string text = extract_selection_text(*buffer, buffer->primary());
  if (text.empty()) {
    return false;
  }
  editor_clipboard() = text;
  return true;
}

void paste_text(EditorBuffer* buffer, const std::string& text) {
  paste_at_primary(buffer, text);
}

}  // namespace tgdb
