#pragma once

#include "editor/editor_state.hpp"

namespace tuide {

void commit_undo_group(EditorBuffer* buffer);
void push_undo(EditorBuffer* buffer);
bool undo(EditorBuffer* buffer);
bool redo(EditorBuffer* buffer);
void clear_undo(EditorBuffer* buffer);

}  // namespace tuide
