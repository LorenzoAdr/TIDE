#include "editor/clipboard.hpp"

#include "editor/text_ops.hpp"
#include "editor/undo_stack.hpp"
#include "util/system_clipboard.hpp"

namespace tuide {

std::string& editor_clipboard() {
  static std::string clipboard;
  return clipboard;
}

void publish_clipboard_text(const std::string& text) {
  editor_clipboard() = text;
  set_system_clipboard(text);
}

std::string read_clipboard_for_paste() {
  const std::string system = get_system_clipboard();
  if (!system.empty()) {
    editor_clipboard() = system;
    return system;
  }
  return editor_clipboard();
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
    std::string out = line.substr(static_cast<std::size_t>(start_col),
                                  static_cast<std::size_t>(end_col - start_col));
    // Last line of the file has no following line to anchor an exclusive '\n'
    // end on; still emit a trailing newline for a full-line (linewise) select.
    if (start_col == 0 && end_col == static_cast<int>(line.size()) &&
        start_line == static_cast<int>(buffer.lines.size()) - 1) {
      out.push_back('\n');
    }
    return out;
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
  // Multi-line selection ending at EOL of the last buffer line (no exclusive
  // next-line end available): append the trailing newline for linewise paste.
  if (start_col == 0 && end_line == static_cast<int>(buffer.lines.size()) - 1 &&
      end_col == static_cast<int>(buffer.lines[static_cast<std::size_t>(end_line)].size())) {
    out.push_back('\n');
  }
  return out;
}

bool copy_selection(EditorBuffer* buffer) {
  const std::string text = extract_selection_text(*buffer, buffer->primary());
  if (text.empty()) {
    return false;
  }
  publish_clipboard_text(text);
  return true;
}

bool cut_selection(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return false;
  }
  const std::string text = extract_selection_text(*buffer, buffer->primary());
  if (text.empty()) {
    return false;
  }
  publish_clipboard_text(text);
  push_undo(buffer);
  delete_all_selections(buffer);
  buffer->dirty = true;
  buffer->view_token++;
  return true;
}

void paste_text(EditorBuffer* buffer, const std::string& text) {
  paste_at_primary(buffer, text);
}

}  // namespace tuide
