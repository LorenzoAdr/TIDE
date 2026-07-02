#include "editor/editor_state.hpp"

#include <algorithm>

#include "ui/cursor_blink.hpp"

namespace tgdb {

void MultiCursor::normalized_range(int* start_line, int* start_col, int* end_line,
                                   int* end_col) const {
  CursorPos start = anchor;
  CursorPos end = head;
  if (head < anchor) {
    start = head;
    end = anchor;
  }
  *start_line = start.line;
  *start_col = start.col;
  *end_line = end.line;
  *end_col = end.col;
}

int EditorBuffer::primary_line() const {
  return cursors.empty() ? 0 : cursors.front().head.line;
}

int EditorBuffer::primary_col() const {
  return cursors.empty() ? 0 : cursors.front().head.col;
}

void EditorBuffer::set_primary(int line, int col) {
  ensure_cursors();
  cursors.front().set_pos(line, col);
  cursor_blink::show();
}

void EditorBuffer::reset_to_single_cursor(int line, int col) {
  cursors.clear();
  cursors.push_back({});
  cursors.front().set_pos(line, col);
  cursor_blink::show();
}

void EditorBuffer::ensure_cursors() {
  if (cursors.empty()) {
    cursors.push_back({});
  }
}

bool EditorBuffer::multi_cursor_active() const { return cursors.size() > 1; }

MultiCursor& EditorBuffer::primary() {
  ensure_cursors();
  return cursors.front();
}

const MultiCursor& EditorBuffer::primary() const {
  static const MultiCursor kEmpty{};
  return cursors.empty() ? kEmpty : cursors.front();
}

void clamp_cursor(MultiCursor* cursor, const EditorBuffer& buffer) {
  if (buffer.lines.empty()) {
    return;
  }
  const int max_line = static_cast<int>(buffer.lines.size()) - 1;
  cursor->head.line = std::max(0, std::min(cursor->head.line, max_line));
  cursor->anchor.line = std::max(0, std::min(cursor->anchor.line, max_line));

  const int head_len =
      static_cast<int>(buffer.lines[static_cast<std::size_t>(cursor->head.line)].size());
  cursor->head.col = std::max(0, std::min(cursor->head.col, head_len));

  const int anchor_len =
      static_cast<int>(buffer.lines[static_cast<std::size_t>(cursor->anchor.line)].size());
  cursor->anchor.col = std::max(0, std::min(cursor->anchor.col, anchor_len));
}

void clamp_all_cursors(EditorBuffer* buffer) {
  if (buffer->lines.empty()) {
    buffer->lines.push_back("");
  }
  buffer->ensure_cursors();
  for (auto& cursor : buffer->cursors) {
    clamp_cursor(&cursor, *buffer);
  }
}

void merge_overlapping_cursors(EditorBuffer* buffer) {
  if (buffer->cursors.size() < 2) {
    return;
  }
  clamp_all_cursors(buffer);
  std::vector<MultiCursor> merged;
  merged.push_back(buffer->cursors.front());
  for (std::size_t i = 1; i < buffer->cursors.size(); ++i) {
    const auto& cur = buffer->cursors[i];
    auto& last = merged.back();
    if (last.head.line == cur.head.line && last.head.col == cur.head.col &&
        last.anchor.line == cur.anchor.line && last.anchor.col == cur.anchor.col) {
      continue;
    }
    merged.push_back(cur);
  }
  buffer->cursors = std::move(merged);
  buffer->ensure_cursors();
}

void sort_cursors_for_edit(std::vector<MultiCursor>* cursors) {
  std::sort(cursors->begin(), cursors->end(), [](const MultiCursor& a, const MultiCursor& b) {
    if (a.head.line != b.head.line) {
      return a.head.line > b.head.line;
    }
    return a.head.col > b.head.col;
  });
}

void exit_multi_cursor_mode(EditorBuffer* buffer) {
  buffer->ensure_cursors();
  const MultiCursor primary = buffer->cursors.front();
  MultiCursor collapsed = primary;
  collapsed.collapse_to_head();
  buffer->cursors = {collapsed};
}

}  // namespace tgdb
