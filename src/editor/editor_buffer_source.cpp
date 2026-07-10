#include "editor/editor_buffer_source.hpp"

#include "editor/editor_state.hpp"
#include "parser/tree_sitter_document.hpp"

namespace tgdb {

namespace {

void rebuild_line_starts(const EditorBuffer& buffer, EditorJoinedSourceCache* cache) {
  cache->line_starts.resize(buffer.lines.size());
  std::size_t offset = 0;
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    cache->line_starts[i] = offset;
    offset += buffer.lines[i].size();
    if (i + 1 < buffer.lines.size()) {
      ++offset;
    }
  }
}

void rebuild_joined(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  cache.text = join_editor_lines(buffer->lines);
  rebuild_line_starts(*buffer, &cache);
  cache.valid = true;
}

std::size_t byte_offset_for(const EditorBuffer& buffer, const EditorJoinedSourceCache& cache,
                            int line, int col) {
  if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    return 0;
  }
  if (line >= static_cast<int>(cache.line_starts.size())) {
    return 0;
  }
  return cache.line_starts[static_cast<std::size_t>(line)] + static_cast<std::size_t>(col);
}

void recompute_line_starts_from(EditorBuffer* buffer, int from_line) {
  if (buffer == nullptr) {
    return;
  }
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  if (from_line <= 0) {
    rebuild_line_starts(*buffer, &cache);
    return;
  }
  if (static_cast<std::size_t>(from_line) >= buffer->lines.size()) {
    cache.line_starts.resize(buffer->lines.size());
    return;
  }
  cache.line_starts.resize(buffer->lines.size());
  std::size_t offset = cache.line_starts[static_cast<std::size_t>(from_line - 1)] +
                       buffer->lines[static_cast<std::size_t>(from_line - 1)].size() + 1;
  for (int i = from_line; i < static_cast<int>(buffer->lines.size()); ++i) {
    cache.line_starts[static_cast<std::size_t>(i)] = offset;
    offset += buffer->lines[static_cast<std::size_t>(i)].size();
    if (i + 1 < static_cast<int>(buffer->lines.size())) {
      ++offset;
    }
  }
}

}  // namespace

void editor_buffer_rebuild_joined(EditorBuffer* buffer) {
  rebuild_joined(buffer);
}

const std::string& editor_buffer_joined_source(const EditorBuffer& buffer) {
  EditorBuffer& mutable_buffer = const_cast<EditorBuffer&>(buffer);
  if (!mutable_buffer.joined_source_cache.valid) {
    rebuild_joined(&mutable_buffer);
  }
  return mutable_buffer.joined_source_cache.text;
}

void editor_buffer_invalidate_joined(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  buffer->joined_source_cache.valid = false;
  buffer->joined_source_cache.text.clear();
  buffer->joined_source_cache.line_starts.clear();
  buffer->cached_max_line_len = -1;
}

int editor_buffer_max_line_length(const EditorBuffer& buffer) {
  EditorBuffer& mutable_buffer = const_cast<EditorBuffer&>(buffer);
  if (mutable_buffer.cached_max_line_len < 0) {
    int max_len = 0;
    for (const auto& line : buffer.lines) {
      max_len = std::max(max_len, static_cast<int>(line.size()));
    }
    mutable_buffer.cached_max_line_len = max_len;
  }
  return mutable_buffer.cached_max_line_len;
}

void editor_buffer_note_char_inserted(EditorBuffer* buffer, int line, int col,
                                      std::string_view text) {
  if (buffer == nullptr || text.empty()) {
    return;
  }
  if (buffer->cached_max_line_len >= 0 && line >= 0 &&
      line < static_cast<int>(buffer->lines.size())) {
    buffer->cached_max_line_len =
        std::max(buffer->cached_max_line_len,
                 static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size()));
  }
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  if (!cache.valid) {
    return;
  }
  const std::size_t offset = byte_offset_for(*buffer, cache, line, col);
  cache.text.insert(offset, text);
  recompute_line_starts_from(buffer, line + 1);
}

void editor_buffer_note_char_removed(EditorBuffer* buffer, int line, int col, std::size_t count) {
  if (buffer == nullptr || count == 0) {
    return;
  }
  if (buffer->cached_max_line_len >= 0 && line >= 0 &&
      line < static_cast<int>(buffer->lines.size())) {
    const int line_len = static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
    if (line_len + static_cast<int>(count) >= buffer->cached_max_line_len) {
      buffer->cached_max_line_len = -1;
    }
  }
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  if (!cache.valid) {
    return;
  }
  const std::size_t offset = byte_offset_for(*buffer, cache, line, col);
  if (offset + count > cache.text.size()) {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  cache.text.erase(offset, count);
  recompute_line_starts_from(buffer, line + 1);
}

void editor_buffer_note_line_split(EditorBuffer* buffer, int line, int col) {
  if (buffer == nullptr) {
    return;
  }
  buffer->cached_max_line_len = -1;
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  if (!cache.valid) {
    return;
  }
  const std::size_t offset = byte_offset_for(*buffer, cache, line, col);
  if (offset > cache.text.size()) {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  cache.text.insert(offset, 1, '\n');
  recompute_line_starts_from(buffer, line + 1);
}

void editor_buffer_note_line_joined(EditorBuffer* buffer, int line) {
  if (buffer == nullptr || line <= 0) {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  buffer->cached_max_line_len = -1;
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  if (!cache.valid) {
    return;
  }
  const std::size_t join_at =
      cache.line_starts[static_cast<std::size_t>(line - 1)] +
      buffer->lines[static_cast<std::size_t>(line - 1)].size();
  if (join_at >= cache.text.size() || cache.text[join_at] != '\n') {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  cache.text.erase(join_at, 1);
  recompute_line_starts_from(buffer, line);
}

}  // namespace tgdb
