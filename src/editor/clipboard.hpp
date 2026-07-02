#pragma once

#include <string>

#include "editor/editor_state.hpp"

namespace tgdb {

std::string& editor_clipboard();
std::string read_clipboard_for_paste();
std::string extract_selection_text(const EditorBuffer& buffer, const MultiCursor& cursor);
bool copy_selection(EditorBuffer* buffer);
bool cut_selection(EditorBuffer* buffer);
void paste_text(EditorBuffer* buffer, const std::string& text);

}  // namespace tgdb
