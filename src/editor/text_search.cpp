#include "editor/text_search.hpp"

#include <algorithm>
#include <cctype>

#include "editor/clipboard.hpp"
#include "editor/text_ops.hpp"

namespace tgdb {

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool is_ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

std::string word_at_cursor(const EditorBuffer& buffer, const MultiCursor& cursor) {
  if (buffer.lines.empty()) {
    return {};
  }
  const int line_idx = std::max(0, std::min(cursor.head.line, static_cast<int>(buffer.lines.size()) - 1));
  const std::string& line = buffer.lines[static_cast<std::size_t>(line_idx)];
  if (line.empty()) {
    return {};
  }

  int col = std::max(0, std::min(cursor.head.col, static_cast<int>(line.size()) - 1));
  if (!is_ident_char(line[static_cast<std::size_t>(col)])) {
    if (col > 0 && is_ident_char(line[static_cast<std::size_t>(col - 1)])) {
      --col;
    } else {
      return {};
    }
  }

  int start = col;
  while (start > 0 && is_ident_char(line[static_cast<std::size_t>(start - 1)])) {
    --start;
  }
  int end = col + 1;
  while (end < static_cast<int>(line.size()) &&
         is_ident_char(line[static_cast<std::size_t>(end)])) {
    ++end;
  }
  return line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

bool ident_range_at_cursor(const EditorBuffer& buffer, const MultiCursor& cursor,
                             int* start_col, int* end_col) {
  if (start_col == nullptr || end_col == nullptr || buffer.lines.empty()) {
    return false;
  }
  const int line_idx =
      std::max(0, std::min(cursor.head.line, static_cast<int>(buffer.lines.size()) - 1));
  const std::string& line = buffer.lines[static_cast<std::size_t>(line_idx)];
  if (line.empty()) {
    *start_col = 0;
    *end_col = 0;
    return true;
  }

  int col = std::max(0, std::min(cursor.head.col, static_cast<int>(line.size()) - 1));
  if (!is_ident_char(line[static_cast<std::size_t>(col)])) {
    if (col > 0 && is_ident_char(line[static_cast<std::size_t>(col - 1)])) {
      --col;
    } else {
      *start_col = cursor.head.col;
      *end_col = cursor.head.col;
      return true;
    }
  }

  int start = col;
  while (start > 0 && is_ident_char(line[static_cast<std::size_t>(start - 1)])) {
    --start;
  }
  int end = col + 1;
  while (end < static_cast<int>(line.size()) &&
         is_ident_char(line[static_cast<std::size_t>(end)])) {
    ++end;
  }
  *start_col = start;
  *end_col = end;
  return true;
}

std::string selection_text(const EditorBuffer& buffer, const MultiCursor& cursor) {
  if (!cursor.has_selection()) {
    return {};
  }
  int start_line = 0;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
  cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
  if (start_line != end_line) {
    return {};
  }
  if (start_line < 0 || start_line >= static_cast<int>(buffer.lines.size())) {
    return {};
  }
  const std::string& line = buffer.lines[static_cast<std::size_t>(start_line)];
  start_col = std::max(0, std::min(start_col, static_cast<int>(line.size())));
  end_col = std::max(start_col, std::min(end_col, static_cast<int>(line.size())));
  return line.substr(static_cast<std::size_t>(start_col),
                     static_cast<std::size_t>(end_col - start_col));
}

std::string search_needle(const EditorBuffer& buffer) {
  return extract_selection_text(buffer, buffer.primary());
}

bool find_next_match(const EditorBuffer& buffer, const std::string& needle,
                     const CursorPos& from, TextMatch* out,
                     const std::vector<TextMatch>* skip) {
  if (out == nullptr || needle.empty() || buffer.lines.empty()) {
    return false;
  }

  auto is_skipped = [&](const TextMatch& candidate) {
    if (skip) {
      for (const auto& existing : *skip) {
        if (existing.line == candidate.line && existing.col == candidate.col &&
            existing.length == candidate.length) {
          return true;
        }
      }
    }
    return match_occupied(candidate, buffer);
  };

  for (int line = from.line; line < static_cast<int>(buffer.lines.size()); ++line) {
    const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
    std::size_t start = 0;
    if (line == from.line) {
      start = static_cast<std::size_t>(std::max(0, from.col));
    }
    while (start <= text.size()) {
      const auto pos = text.find(needle, start);
      if (pos == std::string::npos) {
        break;
      }
      TextMatch match{line, static_cast<int>(pos), static_cast<int>(needle.size())};
      if (!is_skipped(match)) {
        *out = match;
        return true;
      }
      start = pos + 1;
    }
  }
  return false;
}

std::vector<TextMatch> find_all_matches_in_lines(const std::vector<std::string>& lines,
                                                 const std::string& needle,
                                                 const std::atomic<uint64_t>* active_request_id,
                                                 uint64_t request_id) {
  std::vector<TextMatch> out;
  if (needle.empty()) {
    return out;
  }
  for (int line = 0; line < static_cast<int>(lines.size()); ++line) {
    if (active_request_id != nullptr && active_request_id->load() != request_id) {
      return {};
    }
    const std::string& text = lines[static_cast<std::size_t>(line)];
    std::size_t start = 0;
    while (start <= text.size()) {
      const auto pos = text.find(needle, start);
      if (pos == std::string::npos) {
        break;
      }
      out.push_back({line, static_cast<int>(pos), static_cast<int>(needle.size())});
      start = pos + needle.size();
    }
  }
  return out;
}

std::vector<TextMatch> find_all_matches(const EditorBuffer& buffer, const std::string& needle) {
  return find_all_matches_in_lines(buffer.lines, needle, nullptr, 0);
}

namespace {

constexpr std::size_t kMaxOccurrenceMatches = 512;

bool needle_is_identifier(const std::string& needle) {
  return !needle.empty() && is_ident_start(needle[0]) &&
         std::all_of(needle.begin(), needle.end(), [](unsigned char c) {
           return is_ident_char(static_cast<char>(c));
         });
}

std::vector<TextMatch> find_bounded_matches_in_lines(const std::vector<std::string>& lines,
                                                     const std::string& needle, bool whole_word,
                                                     const std::atomic<uint64_t>* active_request_id,
                                                     uint64_t request_id) {
  std::vector<TextMatch> out;
  out.reserve(32);
  for (int line = 0; line < static_cast<int>(lines.size()); ++line) {
    if (active_request_id != nullptr && active_request_id->load() != request_id) {
      return {};
    }
    const std::string& text = lines[static_cast<std::size_t>(line)];
    std::size_t start = 0;
    while (start <= text.size()) {
      const auto pos = text.find(needle, start);
      if (pos == std::string::npos) {
        break;
      }
      if (whole_word) {
        const bool before_ok = pos == 0 || !is_ident_char(text[pos - 1]);
        const std::size_t after_pos = pos + needle.size();
        const bool after_ok =
            after_pos >= text.size() || !is_ident_char(text[static_cast<std::size_t>(after_pos)]);
        if (!before_ok || !after_ok) {
          start = pos + 1;
          continue;
        }
      }
      out.push_back({line, static_cast<int>(pos), static_cast<int>(needle.size())});
      if (out.size() >= kMaxOccurrenceMatches) {
        return {};
      }
      start = pos + needle.size();
    }
  }
  return out;
}

std::vector<TextMatch> find_bounded_matches(const EditorBuffer& buffer, const std::string& needle,
                                            bool whole_word) {
  return find_bounded_matches_in_lines(buffer.lines, needle, whole_word, nullptr, 0);
}

}  // namespace

std::vector<TextMatch> find_occurrences_in_lines(const std::vector<std::string>& lines,
                                                 const std::string& needle, bool whole_word,
                                                 const std::atomic<uint64_t>* active_request_id,
                                                 uint64_t request_id) {
  constexpr std::size_t kMaxNeedle = 256;
  if (needle.size() < 2 || needle.size() > kMaxNeedle) {
    return {};
  }
  if (std::all_of(needle.begin(), needle.end(),
                  [](unsigned char c) { return std::isspace(static_cast<unsigned char>(c)); })) {
    return {};
  }

  const std::vector<TextMatch> matches =
      find_bounded_matches_in_lines(lines, needle, whole_word, active_request_id, request_id);
  if (matches.size() < 2) {
    return {};
  }
  return matches;
}

std::vector<TextMatch> find_selection_occurrences(const EditorBuffer& buffer) {
  if (buffer.cursors.size() != 1) {
    return {};
  }
  const MultiCursor& cursor = buffer.primary();
  if (!cursor.has_selection()) {
    return {};
  }

  int start_line = 0;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
  cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
  if (start_line != end_line) {
    return {};
  }

  const std::string needle = selection_text(buffer, cursor);
  constexpr std::size_t kMaxNeedle = 256;
  if (needle.size() < 2 || needle.size() > kMaxNeedle) {
    return {};
  }
  if (std::all_of(needle.begin(), needle.end(),
                  [](unsigned char c) { return std::isspace(static_cast<unsigned char>(c)); })) {
    return {};
  }

  const std::vector<TextMatch> matches =
      find_bounded_matches(buffer, needle, needle_is_identifier(needle));
  if (matches.size() < 2) {
    return {};
  }
  return matches;
}

bool match_occupied(const TextMatch& match, const EditorBuffer& buffer) {
  for (const auto& cursor : buffer.cursors) {
    if (!cursor.has_selection()) {
      if (cursor.head.line == match.line && cursor.head.col == match.col) {
        return true;
      }
      continue;
    }
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
    if (start_line == end_line && start_line == match.line && start_col == match.col &&
        end_col - start_col == match.length) {
      return true;
    }
  }
  return false;
}

void add_next_selection_match(EditorBuffer* buffer, int visible_lines) {
  buffer->ensure_cursors();
  clamp_all_cursors(buffer);

  const std::string needle = search_needle(*buffer);
  if (needle.empty()) {
    return;
  }

  if (!buffer->primary().has_selection()) {
    return;
  }

  std::vector<TextMatch> existing;
  for (const auto& cursor : buffer->cursors) {
    if (!cursor.has_selection()) {
      continue;
    }
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
    if (start_line == end_line) {
      existing.push_back({start_line, start_col, end_col - start_col});
    }
  }

  CursorPos from = buffer->primary().head;
  if (from.col < static_cast<int>(buffer->lines[static_cast<std::size_t>(from.line)].size())) {
    from.col += static_cast<int>(needle.size());
  } else {
    ++from.line;
    from.col = 0;
  }

  TextMatch next{};
  if (!find_next_match(*buffer, needle, from, &next, &existing)) {
    return;
  }

  MultiCursor added;
  added.anchor = {next.line, next.col};
  added.head = {next.line, next.col + next.length};
  buffer->cursors.push_back(added);
  merge_overlapping_cursors(buffer);

  const int target_line = next.line;
  if (visible_lines > 0) {
    buffer->scroll = std::max(0, target_line - visible_lines / 2);
  }
  ensure_scroll_visible(buffer, visible_lines);
}

void select_all_matches(EditorBuffer* buffer) {
  buffer->ensure_cursors();
  const std::string needle = search_needle(*buffer);
  if (needle.empty() || needle.find('\n') != std::string::npos) {
    return;
  }

  std::vector<TextMatch> matches =
      find_bounded_matches(*buffer, needle, needle_is_identifier(needle));
  if (matches.empty()) {
    matches = find_all_matches(*buffer, needle);
  }
  if (matches.empty()) {
    return;
  }

  buffer->cursors.clear();
  for (const auto& match : matches) {
    MultiCursor cursor;
    cursor.anchor = {match.line, match.col};
    cursor.head = {match.line, match.col + match.length};
    buffer->cursors.push_back(cursor);
  }
  merge_overlapping_cursors(buffer);
}

}  // namespace tgdb
