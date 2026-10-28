#include "editor/clipboard.hpp"

#include "editor/text_ops.hpp"
#include "editor/undo_stack.hpp"
#include "util/system_clipboard.hpp"

namespace tuide {

// Internal storage for multi-text clipboard
static std::vector<std::string> s_clipboard_texts;

std::string& editor_clipboard() {
  static std::string clipboard;
  return clipboard;
}

void publish_clipboard_text(const std::string& text) {
  editor_clipboard() = text;
  s_clipboard_texts.clear();
  s_clipboard_texts.push_back(text);
  set_system_clipboard_async(text);
}

std::string read_clipboard_for_paste() {
  // Keep the cache warm for the next paste without blocking this keystroke.
  refresh_system_clipboard_cache_async();
  if (system_clipboard_cache_ready()) {
    const std::string cached = peek_system_clipboard_cache();
    if (!cached.empty()) {
      editor_clipboard() = cached;
      s_clipboard_texts.clear();
      s_clipboard_texts.push_back(cached);
      return cached;
    }
    return editor_clipboard();
  }
  // Cold start only: one sync read so the first paste after launch still works.
  const std::string system = get_system_clipboard();
  if (!system.empty()) {
    editor_clipboard() = system;
    s_clipboard_texts.clear();
    s_clipboard_texts.push_back(system);
    return system;
  }
  return editor_clipboard();
}

std::vector<std::string> read_all_clipboard_texts() {
  // If we have multiple texts stored, return them
  if (!s_clipboard_texts.empty()) {
    return s_clipboard_texts;
  }
  // Fallback to single text mode
  return {editor_clipboard()};
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
  // In multi-cursor mode, copy all selections
  if (buffer->multi_cursor_active()) {
    copy_all_selections(buffer);
    return true;
  }
  
  const std::string text = extract_selection_text(*buffer, buffer->primary());
  if (text.empty()) {
    return false;
  }
  publish_clipboard_text(text);
  return true;
}

void copy_all_selections(EditorBuffer* buffer) {
  std::vector<std::string> texts;
  texts.reserve(buffer->cursors.size());
  
  // Extract text from each cursor's selection
  for (const auto& cursor : buffer->cursors) {
    std::string text = extract_selection_text(*buffer, cursor);
    // If cursor has no selection, use empty string to preserve position mapping
    texts.push_back(text);
  }
  
  // Store all texts in the multi-text clipboard
  s_clipboard_texts = std::move(texts);
  
  // For system clipboard, join with newlines (for external paste compatibility)
  std::string system_text;
  for (size_t i = 0; i < s_clipboard_texts.size(); ++i) {
    if (!s_clipboard_texts[i].empty()) {
      if (!system_text.empty()) {
        system_text += "\n";
      }
      system_text += s_clipboard_texts[i];
    }
  }
  if (!system_text.empty()) {
    set_system_clipboard_async(system_text);
  }
}

bool cut_selection(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return false;
  }
  
  // In multi-cursor mode, cut all selections
  if (buffer->multi_cursor_active()) {
    // Extract all texts first
    std::vector<std::string> texts;
    texts.reserve(buffer->cursors.size());
    bool any_selection = false;
    
    for (const auto& cursor : buffer->cursors) {
      std::string text = extract_selection_text(*buffer, cursor);
      if (!text.empty()) {
        any_selection = true;
      }
      texts.push_back(text);
    }
    
    if (!any_selection) {
      return false;
    }
    
    // Store all texts in the multi-text clipboard
    s_clipboard_texts = std::move(texts);
    
    // For system clipboard, join with newlines
    std::string system_text;
    for (size_t i = 0; i < s_clipboard_texts.size(); ++i) {
      if (!s_clipboard_texts[i].empty()) {
        if (!system_text.empty()) {
          system_text += "\n";
        }
        system_text += s_clipboard_texts[i];
      }
    }
    if (!system_text.empty()) {
      set_system_clipboard_async(system_text);
    }
    
    push_undo(buffer);
    delete_all_selections(buffer);
    buffer->dirty = true;
    buffer->view_token++;
    return true;
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
