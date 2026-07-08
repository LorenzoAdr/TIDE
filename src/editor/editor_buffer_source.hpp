#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace tgdb {

struct EditorBuffer;

struct EditorJoinedSourceCache {
  std::string text;
  std::vector<std::size_t> line_starts;
  bool valid = false;
};

const std::string& editor_buffer_joined_source(const EditorBuffer& buffer);
void editor_buffer_invalidate_joined(EditorBuffer* buffer);
void editor_buffer_note_char_inserted(EditorBuffer* buffer, int line, int col,
                                      std::string_view text);
void editor_buffer_note_char_removed(EditorBuffer* buffer, int line, int col, std::size_t count);
void editor_buffer_note_line_split(EditorBuffer* buffer, int line, int col);
void editor_buffer_note_line_joined(EditorBuffer* buffer, int line);

}  // namespace tgdb
