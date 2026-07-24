#include "editor/editor_render.hpp"

#include <algorithm>
#include <set>

#include "editor/indent_guides.hpp"
#include "ftxui/dom/elements.hpp"
#include "indexer/index_rules.hpp"
#include "lsp/language_server_spec.hpp"
#include "lsp/lsp_uri.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/theme.hpp"
#include "util/syntax_highlight.hpp"
#include "util/build_file_highlight.hpp"
#include "util/clang_format_config.hpp"

namespace tuide {

using namespace ftxui;

namespace {

std::string slice_line_for_view(const std::string& line, int scroll_col, int view_width) {
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const int scroll_vis = byte_index_to_visual_column(line, scroll_col, tab_size);
  const std::string expanded = expand_tabs_for_display(line, tab_size);
  if (scroll_vis >= static_cast<int>(expanded.size())) {
    return {};
  }
  std::string slice = expanded.substr(static_cast<std::size_t>(scroll_vis));
  if (view_width > 0 && static_cast<int>(slice.size()) > view_width) {
    slice.resize(static_cast<std::size_t>(view_width));
  }
  return slice;
}

int body_start_visual_column(const std::string& source_line, int scroll_col,
                             int guide_prefix_visual, int tab_size) {
  return byte_index_to_visual_column(source_line, scroll_col, tab_size) + guide_prefix_visual;
}

int caret_column_in_body(const std::string& source_line, int primary_byte_col, int scroll_col,
                         int guide_prefix_visual, int tab_size) {
  const int body_start = body_start_visual_column(source_line, scroll_col, guide_prefix_visual,
                                                  tab_size);
  return byte_index_to_visual_column(source_line, primary_byte_col, tab_size) - body_start;
}

int body_source_byte_column(const std::string& source_line, int scroll_col,
                            int guide_prefix_visual, int tab_size) {
  const int body_start = body_start_visual_column(source_line, scroll_col, guide_prefix_visual,
                                                  tab_size);
  return visual_column_to_byte_index(source_line, body_start, tab_size);
}

int caret_visual_in_view(const std::string& line, int primary_col, int scroll_col, int tab_size) {
  const int primary_vis = byte_index_to_visual_column(line, primary_col, tab_size);
  const int scroll_vis = byte_index_to_visual_column(line, scroll_col, tab_size);
  return std::max(0, primary_vis - scroll_vis);
}

// guide_text cells are ASCII space (1 byte) or UTF-8 "│" (3 bytes).
std::size_t advance_guide_cell_bytes(const std::string& guide_text, std::size_t i) {
  if (i >= guide_text.size()) {
    return i;
  }
  const unsigned char c = static_cast<unsigned char>(guide_text[i]);
  if (c < 0x80) {
    return i + 1;
  }
  if ((c & 0xF0) == 0xE0 && i + 2 < guide_text.size()) {
    return i + 3;
  }
  if ((c & 0xE0) == 0xC0 && i + 1 < guide_text.size()) {
    return i + 2;
  }
  if ((c & 0xF8) == 0xF0 && i + 3 < guide_text.size()) {
    return i + 4;
  }
  return i + 1;
}

std::size_t guide_byte_offset_at_visual(const std::string& guide_text, int visual_col) {
  if (visual_col <= 0) {
    return 0;
  }
  int col = 0;
  std::size_t i = 0;
  while (i < guide_text.size() && col < visual_col) {
    i = advance_guide_cell_bytes(guide_text, i);
    ++col;
  }
  return i;
}

int guide_visual_width(const std::string& guide_text) {
  int width = 0;
  for (std::size_t i = 0; i < guide_text.size(); i = advance_guide_cell_bytes(guide_text, i)) {
    ++width;
  }
  return width;
}

Element render_guide_with_caret(const std::string& guide_text, int caret_vis_in_guide,
                                const Decorator& primary_cursor) {
  const int vis_width = guide_visual_width(guide_text);
  if (caret_vis_in_guide < 0 || caret_vis_in_guide >= vis_width) {
    return text(guide_text) | color(theme::AccentDim());
  }
  Elements parts;
  const std::size_t caret_byte = guide_byte_offset_at_visual(guide_text, caret_vis_in_guide);
  if (caret_vis_in_guide > 0) {
    parts.push_back(text(guide_text.substr(0, caret_byte)) | color(theme::AccentDim()));
  }
  if (cursor_blink::visible()) {
    parts.push_back(text(" ") | primary_cursor);
  } else {
    parts.push_back(text(" "));
  }
  if (caret_vis_in_guide + 1 < vis_width) {
    const std::size_t after_byte = guide_byte_offset_at_visual(guide_text, caret_vis_in_guide + 1);
    parts.push_back(text(guide_text.substr(after_byte)) | color(theme::AccentDim()));
  }
  return hbox(std::move(parts));
}

Element render_primary_caret_tail(const EditorBuffer& buffer, int line_index, bool editor_focused,
                                  bool show_caret, const std::string& line, int scroll_col,
                                  int guide_visual_cols, int tab_size,
                                  const Decorator& primary_cursor) {
  if (!editor_focused || line_index != buffer.primary_line() || !show_caret) {
    return text(" ");
  }
  const int caret_vis_in_view =
      caret_visual_in_view(line, buffer.primary_col(), scroll_col, tab_size);
  const int tail_vis = std::max(0, caret_vis_in_view - guide_visual_cols);
  std::string pad(static_cast<std::size_t>(tail_vis), ' ');
  Elements parts;
  if (!pad.empty()) {
    parts.push_back(text(pad));
  }
  if (cursor_blink::visible()) {
    parts.push_back(text(" ") | primary_cursor);
  } else {
    parts.push_back(text(" "));
  }
  return parts.empty() ? text(" ") : hbox(std::move(parts));
}

void shift_decorations_to_body(std::vector<EditorDecoration>* decorations,
                               const std::string& source_line, int scroll_col,
                               int guide_prefix_visual, int view_width, int tab_size) {
  if (decorations == nullptr) {
    return;
  }
  const int body_start =
      body_start_visual_column(source_line, scroll_col, guide_prefix_visual, tab_size);
  std::vector<EditorDecoration> shifted;
  shifted.reserve(decorations->size());
  for (const auto& deco : *decorations) {
    const int start_vis =
        byte_index_to_visual_column(source_line, deco.start_col, tab_size) - body_start;
    const int end_vis =
        byte_index_to_visual_column(source_line, deco.end_col, tab_size) - body_start;
    if (view_width > 0 && start_vis >= view_width && end_vis <= 0) {
      continue;
    }
    if (end_vis <= 0 && start_vis < 0) {
      continue;
    }
    EditorDecoration copy = deco;
    copy.start_col = std::max(0, start_vis);
    copy.end_col = std::max(copy.start_col + 1, end_vis);
    if (view_width > 0) {
      if (copy.start_col >= view_width) {
        continue;
      }
      copy.end_col = std::min(copy.end_col, view_width);
    }
    shifted.push_back(copy);
  }
  *decorations = std::move(shifted);
}

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

bool line_has_selection_occurrences(int line_index,
                                    const std::vector<TextMatch>* selection_occurrences) {
  if (selection_occurrences == nullptr) {
    return false;
  }
  for (const auto& match : *selection_occurrences) {
    if (match.line == line_index) {
      return true;
    }
  }
  return false;
}

bool line_has_diagnostics(const std::vector<Diagnostic>* line_diagnostics) {
  return line_diagnostics != nullptr && !line_diagnostics->empty();
}

bool line_has_colored_braces(int line_index, const std::vector<ColoredBraceMarker>* braces) {
  if (braces == nullptr) {
    return false;
  }
  for (const ColoredBraceMarker& marker : *braces) {
    if (marker.line == line_index) {
      return true;
    }
  }
  return false;
}

bool line_needs_rich_decorations(int line_index, const EditorBuffer& buffer,
                                 const std::vector<TextMatch>* find_matches,
                                 const std::vector<TextMatch>* selection_occurrences,
                                 const BracketPairHighlight* bracket,
                                 const BracketPairHighlight* scope_bracket,
                                 const std::vector<ColoredBraceMarker>* colored_braces,
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
  if (scope_bracket != nullptr && scope_bracket->valid) {
    if (line_index == scope_bracket->line_a || line_index == scope_bracket->line_b) {
      return true;
    }
  }
  if (line_has_colored_braces(line_index, colored_braces)) {
    return true;
  }
  if (line_has_find_matches(line_index, find_matches)) {
    return true;
  }
  if (line_has_selection_occurrences(line_index, selection_occurrences)) {
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
      case EditorDecoration::Kind::SelectionOccurrence:
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
      case EditorDecoration::Kind::ScopeBrace:
        priority = 5;
        break;
      case EditorDecoration::Kind::ColoredBrace:
        priority = 3;
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

Element apply_decoration(Element element, const EditorDecoration* deco,
                         const Decorator& primary_cursor) {
  if (deco == nullptr) {
    return element;
  }
  switch (deco->kind) {
    case EditorDecoration::Kind::FindMatch:
      return element | bgcolor(theme::FindMatchBg());
    case EditorDecoration::Kind::SelectionOccurrence:
      return element | bgcolor(theme::SelectionOccurrenceBg()) | bold;
    case EditorDecoration::Kind::DiagnosticWarning:
      return element | color(theme::Warning()) | underlined;
    case EditorDecoration::Kind::DiagnosticError:
      // Avoid bold: some terminals draw bold glyphs wider and overwrite the previous
      // cell (e.g. the '+' before an undeclared identifier), which looks like a missing
      // character. Color + underline is enough to mark the error span.
      return element | color(theme::Error()) | underlined;
    case EditorDecoration::Kind::MatchingBracket:
      return element | bgcolor(theme::BracketMatchBg()) | bold;
    case EditorDecoration::Kind::ScopeBrace:
      return element | bgcolor(theme::ScopeBraceBg(deco->brace_depth)) | bold;
    case EditorDecoration::Kind::ColoredBrace:
      return element | color(theme::BracePairColor(deco->brace_depth)) | bold;
    case EditorDecoration::Kind::Selection:
      return element | bgcolor(theme::SelectionBg());
    case EditorDecoration::Kind::PrimaryCaret:
      if (!cursor_blink::visible()) {
        return element;
      }
      return element | primary_cursor;
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
  //
  // Critical: when code + suffix exceeds the raster width, FTXUI's hbox shrinks
  // non-flex children proportionally. Rich diagnostic underlines split the line into
  // several text nodes, so that shrink eats the last character of each segment
  // (e.g. the '+' in "(prueba1+prueba2)"). Keep the code notflex and only allow the
  // suffix to shrink/clip.
  const Color suffix_color =
      line_diagnostics != nullptr ? suffix_color_for(*line_diagnostics) : theme::Muted();
  return hbox({std::move(line_content) | notflex,
               text(*diagnostic_suffix) | color(suffix_color) | dim | xflex_shrink}) |
         line_bg;
}

Element render_line_content(const std::string& line, int line_index,
                            const SemanticTokenDocument* semantic_tokens, bool syntax_highlight,
                            int cursor_col = -1, Decorator cursor_style = {},
                            int col_offset = 0, SyntaxHighlightContext* highlight_ctx = nullptr,
                            BuildFileKind build_file_kind = BuildFileKind::kNone) {
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const std::string display_line = expand_tabs_for_display(line, tab_size);
  const bool expanded_line = line.find('\t') == std::string::npos;
  int draw_col = cursor_col;
  if (draw_col >= 0) {
    if (expanded_line) {
      // Body fragments are already tab-expanded; cursor_col is visual within the fragment.
    } else {
      const int visual_col_offset = byte_index_to_visual_column(line, col_offset, tab_size);
      draw_col =
          byte_index_to_visual_column(line, draw_col + col_offset, tab_size) - visual_col_offset;
    }
  }
  if (build_file_kind != BuildFileKind::kNone) {
    return HighlightBuildFileLine(build_file_kind, display_line, draw_col, cursor_style);
  }
  if (!syntax_highlight) {
    if (draw_col < 0 || !cursor_style || !cursor_blink::visible()) {
      return display_line.empty() ? text(" ") : text(display_line);
    }
    const int clamped = std::max(0, std::min(draw_col, static_cast<int>(display_line.size())));
    Elements parts;
    if (clamped > 0) {
      parts.push_back(text(display_line.substr(0, static_cast<std::size_t>(clamped))));
    }
    const std::string cursor_char =
        clamped < static_cast<int>(display_line.size())
            ? display_line.substr(static_cast<std::size_t>(clamped), 1)
            : " ";
    parts.push_back(text(cursor_char) | cursor_style);
    if (clamped + 1 < static_cast<int>(display_line.size())) {
      parts.push_back(text(display_line.substr(static_cast<std::size_t>(clamped + 1))));
    }
    return hbox(std::move(parts));
  }
  return HighlightCodeLine(display_line, line_index, semantic_tokens, draw_col, cursor_style,
                         col_offset, highlight_ctx);
}

Element render_simple_line(const std::string& line, int line_index, const EditorBuffer& buffer,
                           bool editor_focused, const SemanticTokenDocument* semantic_tokens,
                           bool syntax_highlight, const Decorator& line_bg, bool show_caret,
                           int scroll_col, SyntaxHighlightContext* highlight_ctx,
                           BuildFileKind build_file_kind, int guide_prefix_visual,
                           const Decorator& primary_cursor) {
  (void)line_bg;
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const std::string& source_line = buffer.lines[static_cast<std::size_t>(line_index)];
  const int source_col_offset =
      body_source_byte_column(source_line, scroll_col, guide_prefix_visual, tab_size);
  if (!editor_focused || line_index != buffer.primary_line() || !show_caret) {
    return render_line_content(line, line_index, semantic_tokens, syntax_highlight, -1, {},
                               source_col_offset, highlight_ctx, build_file_kind);
  }
  const int caret_in_body =
      caret_column_in_body(source_line, buffer.primary_col(), scroll_col, guide_prefix_visual,
                           tab_size);
  const Decorator cursor_cell = primary_cursor;
  const int clamped = std::max(0, std::min(caret_in_body, static_cast<int>(line.size())));
  const int draw_col = cursor_blink::effective_col(clamped);
  if (line.empty() || caret_in_body >= static_cast<int>(line.size())) {
    Elements parts;
    if (!line.empty()) {
      parts.push_back(render_line_content(line, line_index, semantic_tokens, syntax_highlight, -1,
                                          {}, source_col_offset, highlight_ctx, build_file_kind));
    }
    if (cursor_blink::visible()) {
      parts.push_back(text(" ") | cursor_cell);
    } else {
      parts.push_back(text(" "));
    }
    return hbox(std::move(parts));
  }
  return render_line_content(line, line_index, semantic_tokens, syntax_highlight, draw_col,
                             cursor_cell, source_col_offset, highlight_ctx, build_file_kind);
}

Element render_rich_line(const std::string& line, int line_index, const EditorBuffer& buffer,
                         bool editor_focused, const std::vector<TextMatch>* find_matches,
                         const std::vector<TextMatch>* selection_occurrences,
                         const SemanticTokenDocument* semantic_tokens, bool syntax_highlight,
                         const BracketPairHighlight* bracket,
                         const BracketPairHighlight* scope_bracket, int scope_highlight_strength,
                         const std::vector<ColoredBraceMarker>* colored_braces,
                         const std::vector<Diagnostic>* line_diagnostics,
                         const EditorSymbolPress* symbol_press, bool show_caret, int scroll_col,
                         int view_width, SyntaxHighlightContext* highlight_ctx,
                         BuildFileKind build_file_kind, int guide_prefix_visual,
                         const Decorator& primary_cursor) {
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const std::string& source_line = buffer.lines[static_cast<std::size_t>(line_index)];
  const int source_col_offset =
      body_source_byte_column(source_line, scroll_col, guide_prefix_visual, tab_size);
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
  if (selection_occurrences != nullptr) {
    collect_selection_occurrence_decorations(line_index, *selection_occurrences, &decorations);
  }
  if (bracket != nullptr) {
    collect_bracket_decorations(line_index, *bracket, &decorations);
  }
  if (scope_bracket != nullptr) {
    collect_scope_bracket_decorations(line_index, *scope_bracket, scope_highlight_strength,
                                      &decorations);
  }
  if (colored_braces != nullptr) {
    collect_colored_brace_decorations(line_index, *colored_braces, &decorations);
  }
  collect_line_decorations(line_index, buffer, editor_focused, show_caret, &decorations);
  shift_decorations_to_body(&decorations, source_line, scroll_col, guide_prefix_visual, view_width,
                            tab_size);

  std::set<int> breakpoints;
  breakpoints.insert(static_cast<int>(line.size()));
  for (const auto& deco : decorations) {
    breakpoints.insert(std::max(0, deco.start_col));
    if (deco.kind == EditorDecoration::Kind::PrimaryCaret ||
        deco.kind == EditorDecoration::Kind::SecondaryCaret) {
      breakpoints.insert(std::max(0, deco.end_col));
    } else {
      breakpoints.insert(std::max(0, std::min(deco.end_col, static_cast<int>(line.size()))));
    }
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
    const bool use_build_highlight = build_file_kind != BuildFileKind::kNone;
    const int col_offset =
        syntax_highlight
            ? source_byte_at_display_column(source_line, source_col_offset, prev, tab_size)
            : 0;
    const bool highlight_segment = syntax_highlight && !use_build_highlight;
    parts.push_back(apply_decoration(
        segment.empty() ? text(" ")
                        : render_line_content(segment, line_index, semantic_tokens,
                                              highlight_segment, -1, {}, col_offset,
                                              highlight_ctx, build_file_kind),
        chosen, primary_cursor));
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
      out->push_back({match.col, match.col + match.length, 0, EditorDecoration::Kind::FindMatch});
    }
  }
}

void collect_selection_occurrence_decorations(int line_index, const std::vector<TextMatch>& matches,
                                              std::vector<EditorDecoration>* out) {
  for (const auto& match : matches) {
    if (match.line == line_index && match.length > 0) {
      out->push_back(
          {match.col, match.col + match.length, 0, EditorDecoration::Kind::SelectionOccurrence});
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
          out->push_back({sel_start, sel_end, 0, EditorDecoration::Kind::Selection});
        }
      }
    }
    if (show_caret && cursor.head.line == line_index) {
      const auto kind = (i == 0) ? EditorDecoration::Kind::PrimaryCaret
                                 : EditorDecoration::Kind::SecondaryCaret;
      const int col = cursor.head.col;
      out->push_back({col, col + 1, 0, kind});
    }
  }
}

void collect_bracket_decorations(int line_index, const BracketPairHighlight& bracket,
                                 std::vector<EditorDecoration>* out) {
  if (out == nullptr || !bracket.valid) {
    return;
  }
  if (line_index == bracket.line_a) {
    out->push_back({bracket.col_a, bracket.col_a + 1, 0, EditorDecoration::Kind::MatchingBracket});
  }
  if (line_index == bracket.line_b) {
    out->push_back({bracket.col_b, bracket.col_b + 1, 0, EditorDecoration::Kind::MatchingBracket});
  }
}

void collect_scope_bracket_decorations(int line_index, const BracketPairHighlight& bracket,
                                       int scope_highlight_strength,
                                       std::vector<EditorDecoration>* out) {
  if (out == nullptr || !bracket.valid) {
    return;
  }
  const int strength = std::max(10, std::min(85, scope_highlight_strength));
  if (line_index == bracket.line_a) {
    out->push_back({bracket.col_a, bracket.col_a + 1, strength,
                    EditorDecoration::Kind::ScopeBrace});
  }
  if (line_index == bracket.line_b) {
    out->push_back({bracket.col_b, bracket.col_b + 1, strength,
                    EditorDecoration::Kind::ScopeBrace});
  }
}

void collect_colored_brace_decorations(int line_index,
                                       const std::vector<ColoredBraceMarker>& braces,
                                       std::vector<EditorDecoration>* out) {
  if (out == nullptr) {
    return;
  }
  for (const ColoredBraceMarker& marker : braces) {
    if (marker.line != line_index) {
      continue;
    }
    out->push_back({marker.col, marker.col + 1, marker.depth, EditorDecoration::Kind::ColoredBrace});
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
    out->push_back({start, end, 0, kind});
  }
}

void collect_press_decorations(int line_index, const EditorSymbolPress& press,
                               std::vector<EditorDecoration>* out) {
  if (out == nullptr || !press.active) {
    return;
  }
  out->push_back({press.start_col, press.end_col, 0, EditorDecoration::Kind::PressFlash});
}

Element RenderEditorLine(const std::string& line, int line_index, const EditorBuffer& buffer,
                         bool editor_focused, const std::vector<TextMatch>* find_matches,
                         const std::vector<TextMatch>* selection_occurrences,
                         const SemanticTokenDocument* semantic_tokens,
                         const BracketPairHighlight* bracket,
                         const BracketPairHighlight* scope_bracket, int scope_highlight_strength,
                         const std::vector<Diagnostic>* line_diagnostics,
                         const std::string* diagnostic_suffix,
                         const std::vector<Diagnostic>* suffix_diagnostics,
                         const EditorSymbolPress* symbol_press, bool show_caret, int scroll_col,
                         int view_width, SyntaxHighlightContext* highlight_ctx,
                         bool sticky_scroll_line, bool indent_guides_enabled,
                         int indent_guide_depth, bool defer_rich_decorations,
                         ftxui::Color cursor_cell_bg,
                         const std::vector<ColoredBraceMarker>* colored_braces) {
  const Decorator primary_cursor = cursor_blink::cell_decorator(cursor_cell_bg);
  const std::string view_line = slice_line_for_view(line, scroll_col, view_width);
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  IndentGuideSplit guide_split;
  if (indent_guides_enabled && indent_guide_depth > 0) {
    guide_split = split_indent_guide_prefix(view_line, tab_size, indent_guide_depth);
  }

  const std::string& body_line =
      guide_split.prefix_byte_length > 0 ? guide_split.suffix : view_line;
  const int guide_prefix_visual = guide_split.prefix_visual_width;
  const BuildFileKind detected_build_kind = detect_build_file_kind(buffer.path);
  const std::string lang_id = language_id_for_path(buffer.path);
  // CMake/Makefile use Tree-sitter highlighting (not the legacy build-file highlighter
  // and not LSP semantic tokens).
  const bool tree_sitter_build_lang =
      language_id_is_cmake(lang_id) || language_id_is_make(lang_id) || language_id_is_yaml(lang_id);
  const BuildFileKind build_file_kind =
      tree_sitter_build_lang ? BuildFileKind::kNone : detected_build_kind;
  const bool is_build_file = build_file_kind != BuildFileKind::kNone;
  const bool use_warm_line_bg = detected_build_kind == BuildFileKind::kMakefile;
  const Decorator line_bg =
      sticky_scroll_line
          ? bgcolor(theme::TabIdle())
          : (line_index == buffer.primary_line()
                 ? bgcolor(theme::EditorLineHi())
                 : (use_warm_line_bg ? bgcolor(theme::BuildFileLineBg())
                                     : bgcolor(theme::CodeBg())));

  const bool syntax_highlight =
      !buffer.path.empty() && is_indexed_source_path(buffer.path) && !is_build_file;

  const bool press_active = symbol_press != nullptr && symbol_press->active;
  bool rich =
      press_active ||
      (!defer_rich_decorations &&
       line_needs_rich_decorations(line_index, buffer, find_matches, selection_occurrences, bracket,
                                   scope_bracket, colored_braces, line_diagnostics, symbol_press));
  const bool caret_on_primary =
      show_caret && editor_focused && line_index == buffer.primary_line();
  const int caret_vis_in_view =
      caret_on_primary ? caret_visual_in_view(line, buffer.primary_col(), scroll_col, tab_size)
                       : -1;
  const bool caret_in_guide_prefix =
      guide_prefix_visual > 0 && caret_vis_in_view >= 0 &&
      caret_vis_in_view < guide_prefix_visual;
  const bool body_show_caret = show_caret && !caret_in_guide_prefix;

  Element content =
      rich ? render_rich_line(body_line, line_index, buffer, editor_focused, find_matches,
                              selection_occurrences, semantic_tokens, syntax_highlight, bracket,
                              scope_bracket, scope_highlight_strength, colored_braces,
                              line_diagnostics, symbol_press, body_show_caret, scroll_col,
                              view_width, highlight_ctx, build_file_kind, guide_prefix_visual,
                              primary_cursor)
           : render_simple_line(body_line, line_index, buffer, editor_focused, semantic_tokens,
                                syntax_highlight, line_bg, body_show_caret, scroll_col, highlight_ctx,
                                build_file_kind, guide_prefix_visual, primary_cursor);

  if (guide_prefix_visual > 0) {
    Element guide =
        caret_in_guide_prefix
            ? render_guide_with_caret(guide_split.guide_text, caret_vis_in_view, primary_cursor)
            : text(guide_split.guide_text) | color(theme::AccentDim());
    if (body_line.empty()) {
      if (caret_vis_in_view >= guide_split.prefix_visual_width) {
        content = hbox({std::move(guide),
                        render_primary_caret_tail(buffer, line_index, editor_focused, show_caret,
                                                  line, scroll_col, guide_split.prefix_visual_width,
                                                  tab_size, primary_cursor)});
      } else {
        content = std::move(guide);
      }
    } else {
      content = hbox({std::move(guide), std::move(content)});
    }
  } else if (indent_guides_enabled && indent_guide_depth > 0 && view_line.empty()) {
    const std::string blank_guides =
        build_blank_line_guides(tab_size, indent_guide_depth, view_width);
    if (!blank_guides.empty()) {
      const int guide_width = indent_guide_depth * tab_size;
      Element guide =
          caret_on_primary && caret_vis_in_view >= 0 && caret_vis_in_view < guide_width
              ? render_guide_with_caret(blank_guides, caret_vis_in_view, primary_cursor)
              : text(blank_guides) | color(theme::AccentDim());
      if (caret_on_primary && caret_vis_in_view >= guide_width) {
        content = hbox({std::move(guide),
                        render_primary_caret_tail(buffer, line_index, editor_focused, show_caret,
                                                  line, scroll_col, guide_width, tab_size,
                                                  primary_cursor)});
      } else {
        content = std::move(guide);
      }
    }
  }

  const std::vector<Diagnostic>* suffix_color =
      suffix_diagnostics != nullptr ? suffix_diagnostics : line_diagnostics;
  return wrap_with_suffix(std::move(content), line_bg, diagnostic_suffix, suffix_color);
}

}  // namespace tuide
