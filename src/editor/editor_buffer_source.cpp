#include "editor/editor_buffer_source.hpp"

#include "editor/editor_state.hpp"
#include "parser/tree_sitter_document.hpp"

namespace tgdb {

namespace {

// Scans every line exactly once via EditorText's sequential iterator
// (O(n) total, both backends) rather than indexing buffer.lines[i] in a
// loop -- for the rope backend, operator[] is O(log n) *per call* (see
// TextRope::line_at), so an index-based full scan would silently become
// O(n log n). This runs on every editor_buffer_rebuild_joined() call (e.g.
// after any multi-line edit), so it is squarely on the hot path this
// migration's Fase 3/4 target.
void rebuild_line_starts(const EditorBuffer& buffer, EditorJoinedSourceCache* cache) {
  const std::size_t n = buffer.lines.size();
  cache->line_starts.resize(n);
  std::size_t offset = 0;
  std::size_t i = 0;
  for (auto it = buffer.lines.begin(); it != buffer.lines.end(); ++it, ++i) {
    cache->line_starts[i] = offset;
    offset += (*it).size();
    if (i + 1 < n) {
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
  // A full rebuild means whatever incremental tracking preceded it (if any)
  // no longer describes a single well-defined edit against this new text --
  // e.g. it can be triggered by undo/redo swapping the whole line list, or by
  // a mutation path that doesn't call editor_buffer_note_*. Drop any pending
  // hint so a later editor_buffer_take_edit_hint() can't return one that
  // describes a transition that never actually produced this cache.text.
  cache.pending_edit_hint.reset();
  cache.hint_poisoned = false;
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

// Records `hint` as the buffer's pending single-edit hint, unless a previous
// edit this cycle is still unconsumed -- see EditorJoinedSourceCache's
// pending_edit_hint doc comment.
void note_edit_hint(EditorBuffer* buffer, const EditorTextEditHint& hint) {
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  if (cache.pending_edit_hint.has_value() || cache.hint_poisoned) {
    cache.pending_edit_hint.reset();
    cache.hint_poisoned = true;
    return;
  }
  cache.pending_edit_hint = hint;
}

bool buffer_last_line_empty(const EditorBuffer& buffer) {
  return !buffer.lines.empty() && buffer.lines.back().empty();
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
  const int n = static_cast<int>(buffer->lines.size());
  cache.line_starts.resize(buffer->lines.size());
  std::size_t offset = cache.line_starts[static_cast<std::size_t>(from_line - 1)] +
                       buffer->lines[static_cast<std::size_t>(from_line - 1)].size() + 1;
  // seek(from_line) + sequential ++ keeps this O(remaining lines) instead of
  // O(remaining lines * log n) -- see rebuild_line_starts's comment above;
  // the same trap applies here since this only rescans the suffix from
  // from_line onward.
  int i = from_line;
  for (auto it = buffer->lines.seek(from_line); it != buffer->lines.end(); ++it, ++i) {
    cache.line_starts[static_cast<std::size_t>(i)] = offset;
    offset += (*it).size();
    if (i + 1 < n) {
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
  buffer->joined_source_cache.pending_edit_hint.reset();
  buffer->joined_source_cache.hint_poisoned = false;
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
  const bool old_ends_with_newline = !cache.text.empty() && cache.text.back() == '\n';
  const std::size_t offset = byte_offset_for(*buffer, cache, line, col);
  cache.text.insert(offset, text);
  recompute_line_starts_from(buffer, line + 1);

  EditorTextEditHint hint;
  hint.start_byte = offset;
  hint.old_end_byte = offset;
  hint.new_end_byte = offset + text.size();
  hint.start_row = line;
  hint.start_col = col;
  hint.old_end_row = line;
  hint.old_end_col = col;
  hint.new_end_row = line;
  hint.new_end_col = col + static_cast<int>(text.size());
  hint.old_ends_with_newline = old_ends_with_newline;
  hint.new_ends_with_newline = buffer_last_line_empty(*buffer);
  note_edit_hint(buffer, hint);
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
  const bool old_ends_with_newline = !cache.text.empty() && cache.text.back() == '\n';
  const std::size_t offset = byte_offset_for(*buffer, cache, line, col);
  if (offset + count > cache.text.size()) {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  cache.text.erase(offset, count);
  recompute_line_starts_from(buffer, line + 1);

  EditorTextEditHint hint;
  hint.start_byte = offset;
  hint.old_end_byte = offset + count;
  hint.new_end_byte = offset;
  hint.start_row = line;
  hint.start_col = col;
  hint.old_end_row = line;
  hint.old_end_col = col + static_cast<int>(count);
  hint.new_end_row = line;
  hint.new_end_col = col;
  hint.old_ends_with_newline = old_ends_with_newline;
  hint.new_ends_with_newline = buffer_last_line_empty(*buffer);
  note_edit_hint(buffer, hint);
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
  const bool old_ends_with_newline = !cache.text.empty() && cache.text.back() == '\n';
  const std::size_t offset = byte_offset_for(*buffer, cache, line, col);
  if (offset > cache.text.size()) {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  cache.text.insert(offset, 1, '\n');
  recompute_line_starts_from(buffer, line + 1);

  EditorTextEditHint hint;
  hint.start_byte = offset;
  hint.old_end_byte = offset;
  hint.new_end_byte = offset + 1;
  hint.start_row = line;
  hint.start_col = col;
  hint.old_end_row = line;
  hint.old_end_col = col;
  hint.new_end_row = line + 1;
  hint.new_end_col = 0;
  hint.old_ends_with_newline = old_ends_with_newline;
  hint.new_ends_with_newline = buffer_last_line_empty(*buffer);
  note_edit_hint(buffer, hint);
}

void editor_buffer_note_line_joined(EditorBuffer* buffer, int line, int join_col) {
  if (buffer == nullptr || line <= 0) {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  buffer->cached_max_line_len = -1;
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  if (!cache.valid) {
    return;
  }
  const bool old_ends_with_newline = !cache.text.empty() && cache.text.back() == '\n';
  // join_col is the length of the previous line *before* the caller appended
  // the joined-away line's text to it -- NOT buffer->lines[line-1].size(),
  // which by the time this runs already reflects the merged line and would
  // overshoot by the tail's length whenever it's non-empty (silently
  // pointing past the '\n' this join is supposed to remove, and typically
  // failing the sanity check below, forcing a full cache rebuild on every
  // single backspace-join of two non-empty lines).
  const std::size_t join_at =
      cache.line_starts[static_cast<std::size_t>(line - 1)] + static_cast<std::size_t>(join_col);
  if (join_at >= cache.text.size() || cache.text[join_at] != '\n') {
    editor_buffer_invalidate_joined(buffer);
    return;
  }
  cache.text.erase(join_at, 1);
  recompute_line_starts_from(buffer, line);

  EditorTextEditHint hint;
  hint.start_byte = join_at;
  hint.old_end_byte = join_at + 1;
  hint.new_end_byte = join_at;
  hint.start_row = line - 1;
  hint.start_col = join_col;
  hint.old_end_row = line;
  hint.old_end_col = 0;
  hint.new_end_row = line - 1;
  hint.new_end_col = join_col;
  hint.old_ends_with_newline = old_ends_with_newline;
  hint.new_ends_with_newline = buffer_last_line_empty(*buffer);
  note_edit_hint(buffer, hint);
}

std::optional<EditorTextEditHint> editor_buffer_take_edit_hint(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return std::nullopt;
  }
  EditorJoinedSourceCache& cache = buffer->joined_source_cache;
  cache.hint_poisoned = false;
  std::optional<EditorTextEditHint> hint = cache.pending_edit_hint;
  cache.pending_edit_hint.reset();
  return hint;
}

}  // namespace tgdb
