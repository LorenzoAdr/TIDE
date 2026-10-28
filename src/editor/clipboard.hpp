#pragma once

#include <string>
#include <vector>

#include "editor/editor_state.hpp"

namespace tuide {

std::string& editor_clipboard();
void publish_clipboard_text(const std::string& text);
std::string read_clipboard_for_paste();
std::string extract_selection_text(const EditorBuffer& buffer, const MultiCursor& cursor);
bool copy_selection(EditorBuffer* buffer);
bool cut_selection(EditorBuffer* buffer);
void paste_text(EditorBuffer* buffer, const std::string& text);

// Multi-cursor clipboard support
void copy_all_selections(EditorBuffer* buffer);
std::vector<std::string> read_all_clipboard_texts();

}  // namespace tuide