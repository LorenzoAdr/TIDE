#include "editor/editor_find_state.hpp"

#include "editor/text_ops.hpp"

namespace tgdb {

namespace {

FindMatchKey find_match_key_from(const EditorBuffer& buffer, const std::string& query) {
  FindMatchKey key;
  key.query = query;
  key.path = buffer.path;
  key.view_token = buffer.view_token;
  return key;
}

}  // namespace

void EditorFindState::cancel_matches() {
  runner_.cancel();
  inflight_id_ = 0;
}

void EditorFindState::reset_search_state() {
  matches.clear();
  committed_key_ = {};
  cancel_matches();
}

void EditorFindState::request_matches(const EditorBuffer& buffer) {
  const FindMatchKey key = find_match_key_from(buffer, query);
  if (key == committed_key_) {
    return;
  }

  committed_key_ = key;
  matches.clear();
  cancel_matches();

  if (query.empty()) {
    return;
  }

  const uint64_t request_id = ++request_counter_;
  runner_.start(request_id, key, buffer.lines, query);
  inflight_id_ = request_id;
}

bool EditorFindState::tick_matches(const EditorBuffer& buffer) {
  if (!open) {
    return false;
  }

  bool updated = false;
  if (inflight_id_ != 0) {
    const FindMatchKey current = find_match_key_from(buffer, query);
    std::vector<TextMatch> ready;
    if (runner_.poll(inflight_id_, current, &ready)) {
      inflight_id_ = 0;
      matches = std::move(ready);
      updated = true;
    }
  }

  request_matches(buffer);
  return updated || inflight_id_ != 0;
}

bool EditorFindState::matches_inflight() const {
  return inflight_id_ != 0;
}

bool EditorFindState::jump_to_next_match(EditorBuffer* buffer, int visible_lines) {
  if (buffer == nullptr || query.empty()) {
    return false;
  }

  const FindMatchKey current = find_match_key_from(*buffer, query);
  if (inflight_id_ != 0) {
    std::vector<TextMatch> ready;
    if (runner_.poll(inflight_id_, current, &ready)) {
      inflight_id_ = 0;
      matches = std::move(ready);
    }
  }

  if (matches.empty() && current != committed_key_) {
    request_matches(*buffer);
  }

  if (matches.empty()) {
    matches = find_all_matches(*buffer, query);
    committed_key_ = current;
    cancel_matches();
  }

  if (matches.empty()) {
    return false;
  }

  CursorPos from = buffer->primary().head;
  if (buffer->primary().has_selection()) {
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    buffer->primary().normalized_range(&start_line, &start_col, &end_line, &end_col);
    from = {end_line, end_col};
  }

  for (const TextMatch& match : matches) {
    if (match.line > from.line || (match.line == from.line && match.col >= from.col)) {
      buffer->reset_to_single_cursor(match.line, match.col);
      buffer->primary().anchor = {match.line, match.col};
      buffer->primary().head = {match.line, match.col + match.length};
      ensure_scroll_visible(buffer, visible_lines);
      return true;
    }
  }

  const TextMatch& first = matches.front();
  buffer->reset_to_single_cursor(first.line, first.col);
  buffer->primary().anchor = {first.line, first.col};
  buffer->primary().head = {first.line, first.col + first.length};
  ensure_scroll_visible(buffer, visible_lines);
  return true;
}

void open_find_bar(EditorFindState* find, EditorBuffer* buffer) {
  if (find == nullptr) {
    return;
  }
  find->cursor_pos = 0;
  find->open = true;
  if (buffer != nullptr) {
    find->request_matches(*buffer);
  }
}

void close_find_bar(EditorFindState* find) {
  if (find == nullptr) {
    return;
  }
  find->open = false;
  find->query.clear();
  find->reset_search_state();
}

}  // namespace tgdb
