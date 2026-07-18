#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tuide {

struct EditorBuffer;

// Describes the single contiguous edit that transformed the previous raw
// joined source (lines joined by '\n', no trailing separator -- see
// join_editor_lines) into the current one, in the same terms tree-sitter's
// TSInputEdit uses (byte offsets + row/column, column counted in bytes).
// Populated opportunistically by the editor_buffer_note_* functions below,
// which already compute these exact offsets to maintain the incremental
// joined-source cache -- see the "Fase 3" migration plan. Consumers (e.g.
// tree_sitter_document.cpp) can use this to skip an O(document size)
// prefix/suffix diff against the previous source entirely, but only when
// editor_buffer_take_edit_hint() confirms it reflects exactly one edit.
struct EditorTextEditHint {
  std::size_t start_byte = 0;
  std::size_t old_end_byte = 0;
  std::size_t new_end_byte = 0;
  int start_row = 0;
  int start_col = 0;
  int old_end_row = 0;
  int old_end_col = 0;
  int new_end_row = 0;
  int new_end_col = 0;
  // Whether the raw joined source ended with '\n' before/after this edit
  // (i.e. the buffer's last line was empty). normalize_editor_source() only
  // ever strips a single trailing '\n', so consumers that compare against a
  // *normalized* source need this to know whether the very last byte was
  // dropped on either side before trusting byte offsets near the end of the
  // document.
  bool old_ends_with_newline = false;
  bool new_ends_with_newline = false;
};

struct EditorJoinedSourceCache {
  std::string text;
  std::vector<std::size_t> line_starts;
  bool valid = false;
  // Set by exactly one editor_buffer_note_* call; cleared (with
  // hint_poisoned set instead) if a second one arrives before it is consumed
  // via editor_buffer_take_edit_hint(), since a single TSInputEdit-shaped
  // hint cannot describe two disjoint edits (e.g. multi-cursor typing, or a
  // multi-line paste that is applied as several smaller mutations).
  std::optional<EditorTextEditHint> pending_edit_hint;
  bool hint_poisoned = false;
};

const std::string& editor_buffer_joined_source(const EditorBuffer& buffer);
void editor_buffer_rebuild_joined(EditorBuffer* buffer);
int editor_buffer_max_line_length(const EditorBuffer& buffer);
void editor_buffer_invalidate_joined(EditorBuffer* buffer);
void editor_buffer_note_char_inserted(EditorBuffer* buffer, int line, int col,
                                      std::string_view text);
void editor_buffer_note_char_removed(EditorBuffer* buffer, int line, int col, std::size_t count);
void editor_buffer_note_line_split(EditorBuffer* buffer, int line, int col);
void editor_buffer_note_line_joined(EditorBuffer* buffer, int line, int join_col);

// Returns the hint describing the single edit that produced the buffer's
// current raw joined source from its previous state, if (and only if)
// exactly one editor_buffer_note_* call happened since the last time this
// was called for this buffer. Always resets the tracking state (whether or
// not a hint is returned), so it is safe to call at most once per edit
// cycle -- see mark_editor_content_edited().
std::optional<EditorTextEditHint> editor_buffer_take_edit_hint(EditorBuffer* buffer);

}  // namespace tuide
