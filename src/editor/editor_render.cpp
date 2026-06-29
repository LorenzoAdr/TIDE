#include "editor/editor_render.hpp"

#include <algorithm>
#include <set>

#include "ftxui/dom/elements.hpp"
#include "indexer/index_rules.hpp"
#include "ui/cursor_blink.hpp"
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

bool line_has_diagnostics(const std::vector<Diagnostic>* line_diagnostics) {
  return line_diagnostics != nullptr && !line_diagnostics->empty();
}

bool line_needs_rich_decorations(int line_index, const EditorBuffer& buffer,
                                 const std::vector<TextMatch>* find_matches,
                                 const BracketPairHighlight* bracket,
                                 const std::vector<Diagnostic>* line_diagnostics,
                                 const EditorSymbolPress* symbol_press) {
  if (symbol_press != nullptr && symbol_press->active) {
    return true;
  }
  if (line_has_diagnostics(line_diagnostics)) {
    return true;
  }
  if (bracket != nullptr && bracket->valid) {
    if (line_index == bracket->line_a || line_index == bracket->line_b) {
      return true;
    }
  }
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
      case EditorDecoration::Kind::DiagnosticWarning:
        priority = 2;
        break;
      case EditorDecoration::Kind::DiagnosticError:
        priority = 3;
        break;
      case EditorDecoration::Kind::MatchingBracket:
        priority = 4;
        break;
      case EditorDecoration::Kind::Selection:
        priority = 5;
        break;
      case EditorDecoration::Kind::SecondaryCaret:
        priority = 6;
        break;
      case EditorDecoration::Kind::PressFlash:
        priority = 8;
        break;
      case EditorDecoration::Kind::PrimaryCaret:
        priority = 7;
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
    case EditorDecoration::Kind::DiagnosticWarning:
      return element | color(theme::Warning()) | underlined;
    case EditorDecoration::Kind::DiagnosticError:
      return element | color(theme::Error()) | underlined | bold;
    case EditorDecoration::Kind::MatchingBracket:
      return element | bgcolor(theme::BracketMatchBg()) | bold;
    case EditorDecoration::Kind::Selection:
      return element | bgcolor(theme::SelectionBg());
    case EditorDecoration::Kind::PrimaryCaret:
      if (!cursor_blink::visible()) {
        return element;
      }
      return element | cursor_blink::cell_decorator();
    case EditorDecoration::Kind::SecondaryCaret:
      if (!cursor_blink::visible()) {
        return element;
      }
      return element | inverted | bold;
    case EditorDecoration::Kind::PressFlash:
      return element | bold | inverted | bgcolor(theme::TabPressed());
  }
  return element;
}

ftxui::Color suffix_color_for(const std::vector<Diagnostic>& diagnostics) {
  for (const auto& item : diagnostics) {
    if (item.severity == DiagnosticSeverity::kError) {
      return theme::Error();
    }
  }
  for (const auto& item : diagnostics) {
    if (item.severity == DiagnosticSeverity::kWarning) {
      return theme::Warning();
    }
  }
  return theme::Muted();
}

Element wrap_with_suffix(Element line_content, const Decorator& line_bg,
                         const std::string* diagnostic_suffix,
                         const std::vector<Diagnostic>* line_diagnostics) {
  if (diagnostic_suffix == nullptr || diagnostic_suffix->empty()) {
    return line_content | line_bg;
  }
  // hbox sin filler: el suffix va pegado al final de la línea. filler()+reflect anidados
  // en el vbox del panel cuelgan FTXUI al destruir el DOM.
  const Color suffix_color =
      line_diagnostics != nullptr ? suffix_color_for(*line_diagnostics) : theme::Muted();
  return hbox({std::move(line_content),
               text(*diagnostic_suffix) | color(suffix_color) | dim}) |
         line_bg;
}

Element render_line_content(const std::string& line, int line_index,
                            const SemanticTokenDocument* semantic_tokens, bool syntax_highlight,
                            int cursor_col = -1, Decorator cursor_style = {},
                            int col_offset = 0) {
  if (!syntax_highlight) {
    if (cursor_col < 0 || !cursor_style || !cursor_blink::visible()) {
      return line.empty() ? text(" ") : text(line);
    }
    const int clamped = std::max(0, std::min(cursor_col, static_cast<int>(line.size())));
    Elements parts;
    if (clamped > 0) {
      parts.push_back(text(line.substr(0, static_cast<std::size_t>(clamped))));
    }
    const std::string cursor_char =
        clamped < static_cast<int>(line.size())
            ? line.substr(static_cast<std::size_t>(clamped), 1)
            : " ";
    parts.push_back(text(cursor_char) | cursor_style);
    if (clamped + 1 < static_cast<int>(line.size())) {
      parts.push_back(text(line.substr(static_cast<std::size_t>(clamped + 1))));
    }
    return hbox(std::move(parts));
  }
  return HighlightCodeLine(line, line_index, semantic_tokens, cursor_col, cursor_style, col_offset);
}

Element render_simple_line(const std::string& line, int line_index, const EditorBuffer& buffer,
                           bool editor_focused, const SemanticTokenDocument* semantic_tokens,
                           bool syntax_highlight, const Decorator& line_bg, bool show_caret) {
  (void)line_bg;
  if (!editor_focused || line_index != buffer.primary_line() || !show_caret ||
      buffer.primary().has_selection()) {
    return render_line_content(line, line_index, semantic_tokens, syntax_highlight);
  }
  const int col = buffer.primary_col();
  const Decorator cursor_cell = cursor_blink::cell_decorator();
  const int clamped = std::max(0, std::min(col, static_cast<int>(line.size())));
  const int draw_col = cursor_blink::effective_col(clamped);
  if (line.empty() || clamped >= static_cast<int>(line.size())) {
    Elements parts;
    if (!line.empty()) {
      parts.push_back(render_line_content(line, line_index, semantic_tokens, syntax_highlight));
    }
    if (cursor_blink::visible()) {
      parts.push_back(text(" ") | cursor_cell);
    } else {
      parts.push_back(text(" "));
    }
    return hbox(std::move(parts));
  }
  return render_line_content(line, line_index, semantic_tokens, syntax_highlight, draw_col,
                             cursor_cell);
}

Element render_rich_line(const std::string& line, int line_index, const EditorBuffer& buffer,
                         bool editor_focused, const std::vector<TextMatch>* find_matches,
                         const SemanticTokenDocument* semantic_tokens, bool syntax_highlight,
                         const BracketPairHighlight* bracket,
                         const std::vector<Diagnostic>* line_diagnostics,
                         const EditorSymbolPress* symbol_press, bool show_caret) {
  std::vector<EditorDecoration> decorations;
  if (symbol_press != nullptr && symbol_press->active) {
    collect_press_decorations(line_index, *symbol_press, &decorations);
  }
  if (line_diagnostics != nullptr) {
    collect_diagnostic_decorations(line_index, *line_diagnostics, &decorations);
  }
  if (find_matches != nullptr) {
    collect_find_decorations(line_index, *find_matches, &decorations);
  }
  if (bracket != nullptr) {
    collect_bracket_decorations(line_index, *bracket, &decorations);
  }
  collect_line_decorations(line_index, buffer, editor_focused, show_caret, &decorations);

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
    const bool caret_on_line =
        show_caret && editor_focused && line_index == buffer.primary_line() &&
        !buffer.primary().has_selection();
    int segment_cursor = -1;
    if (caret_on_line && buffer.primary_col() >= prev && buffer.primary_col() < bp) {
      if (syntax_highlight) {
        segment_cursor = cursor_blink::effective_col(buffer.primary_col());
      } else {
        segment_cursor = cursor_blink::effective_col(buffer.primary_col() - prev);
      }
    }
    const int col_offset = syntax_highlight ? prev : 0;
    const Decorator cursor_cell = cursor_blink::cell_decorator();
    parts.push_back(apply_decoration(
        segment.empty() ? text(" ")
                        : render_line_content(segment, line_index, semantic_tokens, syntax_highlight,
                                              segment_cursor, cursor_cell, col_offset),
        chosen));
    prev = bp;
  }

  if (parts.empty()) {
    parts.push_back(text(" "));
  }
  return hbox(std::move(parts));
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
                              bool show_caret, std::vector<EditorDecoration>* out) {
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
    if (show_caret && !cursor.has_selection() && cursor.head.line == line_index) {
      const auto kind = (i == 0) ? EditorDecoration::Kind::PrimaryCaret
                                 : EditorDecoration::Kind::SecondaryCaret;
      const int col = cursor.head.col;
      out->push_back({col, col + 1, kind});
    }
  }
}

void collect_bracket_decorations(int line_index, const BracketPairHighlight& bracket,
                                 std::vector<EditorDecoration>* out) {
  if (out == nullptr || !bracket.valid) {
    return;
  }
  if (line_index == bracket.line_a) {
    out->push_back({bracket.col_a, bracket.col_a + 1, EditorDecoration::Kind::MatchingBracket});
  }
  if (line_index == bracket.line_b) {
    out->push_back({bracket.col_b, bracket.col_b + 1, EditorDecoration::Kind::MatchingBracket});
  }
}

void collect_diagnostic_decorations(int line_index, const std::vector<Diagnostic>& diagnostics,
                                  std::vector<EditorDecoration>* out) {
  if (out == nullptr) {
    return;
  }
  for (const auto& item : diagnostics) {
    if (item.line != line_index) {
      continue;
    }
    const int start = std::max(0, item.start_col);
    const int end = std::max(start + 1, item.end_col);
    const auto kind = item.severity == DiagnosticSeverity::kError
                          ? EditorDecoration::Kind::DiagnosticError
                          : EditorDecoration::Kind::DiagnosticWarning;
    out->push_back({start, end, kind});
  }
}

void collect_press_decorations(int line_index, const EditorSymbolPress& press,
                               std::vector<EditorDecoration>* out) {
  if (out == nullptr || !press.active) {
    return;
  }
  out->push_back({press.start_col, press.end_col, EditorDecoration::Kind::PressFlash});
}

Element RenderEditorLine(const std::string& line, int line_index, const EditorBuffer& buffer,
                         bool editor_focused, const std::vector<TextMatch>* find_matches,
                         const SemanticTokenDocument* semantic_tokens,
                         const BracketPairHighlight* bracket,
                         const std::vector<Diagnostic>* line_diagnostics,
                         const std::string* diagnostic_suffix,
                         const EditorSymbolPress* symbol_press, bool show_caret) {
  const Decorator line_bg =
      line_index == buffer.primary_line() ? bgcolor(theme::EditorLineHi())
                                          : bgcolor(theme::CodeBg());

  const bool syntax_highlight =
      !buffer.path.empty() && is_indexed_source_path(buffer.path);

  const bool rich = line_needs_rich_decorations(line_index, buffer, find_matches, bracket,
                                                line_diagnostics, symbol_press);

  Element content =
      rich ? render_rich_line(line, line_index, buffer, editor_focused, find_matches,
                              semantic_tokens, syntax_highlight, bracket, line_diagnostics,
                              symbol_press, show_caret)
           : render_simple_line(line, line_index, buffer, editor_focused, semantic_tokens,
                                syntax_highlight, line_bg, show_caret);

  return wrap_with_suffix(std::move(content), line_bg, diagnostic_suffix, line_diagnostics);
}

}  // namespace tgdb
