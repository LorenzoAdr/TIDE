#include "editor/editor_render.hpp"

#include <algorithm>
#include <set>

#include "ftxui/dom/elements.hpp"
#include "ui/theme.hpp"
#include "util/cpp_highlight.hpp"
#include "util/syntax_highlight.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

bool line_has_find_matches(int line_index, const std::vector<TextMatch>* find_matches) {
  if (find_matches == nullptr) {
    return false;
  }
  for (const auto& match : *find_matches) {
    if (match.line == line_index) {
      return true;
    }
  }
  return false;
}

bool line_needs_rich_decorations(int line_index, const EditorBuffer& buffer,
                                 const std::vector<TextMatch>* find_matches) {
  if (line_has_find_matches(line_index, find_matches)) {
    return true;
  }
  if (buffer.cursors.size() <= 1) {
    return buffer.primary().has_selection();
  }
  for (const auto& cursor : buffer.cursors) {
    if (cursor.head.line == line_index || cursor.anchor.line == line_index) {
      return true;
    }
  }
  return false;
}

const EditorDecoration* decoration_at(const std::vector<EditorDecoration>& decorations, int col) {
  const EditorDecoration* best = nullptr;
  int best_priority = -1;
  for (const auto& deco : decorations) {
    if (col < deco.start_col || col >= deco.end_col) {
      continue;
    }
    int priority = 0;
    switch (deco.kind) {
      case EditorDecoration::Kind::FindMatch:
        priority = 1;
        break;
      case EditorDecoration::Kind::Selection:
        priority = 2;
        break;
      case EditorDecoration::Kind::PrimaryCaret:
        priority = 4;
        break;
      case EditorDecoration::Kind::SecondaryCaret:
        priority = 3;
        break;
    }
    if (priority > best_priority) {
      best = &deco;
      best_priority = priority;
    }
  }
  return best;
}

Element apply_decoration(Element element, const EditorDecoration* deco) {
  if (deco == nullptr) {
    return element;
  }
  switch (deco->kind) {
    case EditorDecoration::Kind::FindMatch:
      return element | bgcolor(theme::FindMatchBg());
    case EditorDecoration::Kind::Selection:
      return element | bgcolor(theme::SelectionBg());
    case EditorDecoration::Kind::PrimaryCaret:
      return element | bgcolor(theme::CursorCell()) | color(Color::Black) | bold;
    case EditorDecoration::Kind::SecondaryCaret:
      return element | inverted | bold;
  }
  return element;
}

}  // namespace

void collect_find_decorations(int line_index, const std::vector<TextMatch>& matches,
                              std::vector<EditorDecoration>* out) {
  for (const auto& match : matches) {
    if (match.line == line_index && match.length > 0) {
      out->push_back({match.col, match.col + match.length, EditorDecoration::Kind::FindMatch});
    }
  }
}

void collect_line_decorations(int line_index, const EditorBuffer& buffer, bool editor_focused,
                              std::vector<EditorDecoration>* out) {
  if (!editor_focused) {
    return;
  }
  for (std::size_t i = 0; i < buffer.cursors.size(); ++i) {
    const auto& cursor = buffer.cursors[i];
    if (cursor.has_selection()) {
      int start_line = 0;
      int start_col = 0;
      int end_line = 0;
      int end_col = 0;
      cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
      if (line_index >= start_line && line_index <= end_line) {
        const int line_len =
            static_cast<int>(buffer.lines[static_cast<std::size_t>(line_index)].size());
        const int sel_start = (line_index == start_line) ? start_col : 0;
        const int sel_end = (line_index == end_line) ? end_col : line_len;
        if (sel_end > sel_start) {
          out->push_back({sel_start, sel_end, EditorDecoration::Kind::Selection});
        }
      }
    }
    if (cursor.head.line == line_index) {
      const auto kind = (i == 0) ? EditorDecoration::Kind::PrimaryCaret
                                 : EditorDecoration::Kind::SecondaryCaret;
      const int col = cursor.head.col;
      out->push_back({col, col + 1, kind});
    }
  }
}

Element RenderEditorLine(const std::string& line, int line_index, const EditorBuffer& buffer,
                         bool editor_focused, const std::vector<TextMatch>* find_matches,
                         const SemanticTokenDocument* semantic_tokens) {
  const bool is_primary_line = line_index == buffer.primary_line();
  const Decorator line_bg =
      is_primary_line ? bgcolor(theme::EditorLineHi()) : bgcolor(theme::CodeBg());

  if (!line_needs_rich_decorations(line_index, buffer, find_matches) || !editor_focused) {
    if (!editor_focused || line_index != buffer.primary_line()) {
      Element content =
          line.empty() ? text(" ") : HighlightCodeLine(line, line_index, semantic_tokens);
      return content | line_bg;
    }
    const int col = buffer.primary_col();
    const Decorator cursor_cell =
        bgcolor(theme::CursorCell()) | color(Color::Black) | bold;
    const int clamped = std::max(0, std::min(col, static_cast<int>(line.size())));
    if (line.empty() || clamped >= static_cast<int>(line.size())) {
      Elements parts;
      if (!line.empty()) {
        parts.push_back(HighlightCodeLine(line, line_index, semantic_tokens));
      }
      parts.push_back(text(" ") | cursor_cell);
      return hbox(std::move(parts)) | line_bg;
    }
    return HighlightCodeLine(line, line_index, semantic_tokens, clamped, cursor_cell) | line_bg;
  }

  std::vector<EditorDecoration> decorations;
  if (find_matches != nullptr) {
    collect_find_decorations(line_index, *find_matches, &decorations);
  }
  collect_line_decorations(line_index, buffer, editor_focused, &decorations);

  std::set<int> breakpoints;
  breakpoints.insert(0);
  breakpoints.insert(static_cast<int>(line.size()));
  for (const auto& deco : decorations) {
    breakpoints.insert(std::max(0, deco.start_col));
    breakpoints.insert(std::max(0, std::min(deco.end_col, static_cast<int>(line.size()))));
  }

  Elements parts;
  int prev = 0;
  for (const int bp : breakpoints) {
    if (bp <= prev) {
      continue;
    }
    const std::string segment = line.substr(static_cast<std::size_t>(prev),
                                            static_cast<std::size_t>(bp - prev));
    const EditorDecoration* chosen = decoration_at(decorations, prev);
    const int segment_cursor =
        (line_index == buffer.primary_line() && buffer.primary_col() >= prev &&
         buffer.primary_col() < bp)
            ? buffer.primary_col()
            : -1;
    const Decorator cursor_cell =
        bgcolor(theme::CursorCell()) | color(Color::Black) | bold;
    parts.push_back(apply_decoration(
        segment.empty() ? text(" ")
                        : HighlightCodeLine(segment, line_index, semantic_tokens, segment_cursor,
                                            cursor_cell, prev),
        chosen));
    prev = bp;
  }

  if (parts.empty()) {
    parts.push_back(text(" "));
  }
  return hbox(std::move(parts)) | line_bg;
}

}  // namespace tgdb
