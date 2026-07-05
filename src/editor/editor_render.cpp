#include "editor/editor_render.hpp"

#include <algorithm>
#include <set>

#include "editor/indent_guides.hpp"
#include "ftxui/dom/elements.hpp"
#include "indexer/index_rules.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/theme.hpp"
#include "util/cpp_highlight.hpp"
#include "util/build_file_highlight.hpp"
#include "util/clang_format_config.hpp"
#include "util/syntax_highlight.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string slice_line_for_view(const std::string& line, int scroll_col, int view_width) {
  if (scroll_col >= static_cast<int>(line.size())) {
    return {};
  }
  std::string slice = line.substr(static_cast<std::size_t>(scroll_col));
  if (view_width > 0 && static_cast<int>(slice.size()) > view_width) {
    slice.resize(static_cast<std::size_t>(view_width));
  }
  return slice;
}

int shift_col(int col, int scroll_col) { return col - scroll_col; }

int caret_visual_in_view(const std::string& line, int primary_col, int scroll_col, int tab_size) {
  const int primary_vis = byte_index_to_visual_column(line, primary_col, tab_size);
  const int scroll_vis = byte_index_to_visual_column(line, scroll_col, tab_size);
  return std::max(0, primary_vis - scroll_vis);
}

Element render_guide_with_caret(const std::string& guide_text, int caret_vis_in_guide) {
  if (caret_vis_in_guide < 0 || caret_vis_in_guide >= static_cast<int>(guide_text.size())) {
    return text(guide_text) | color(theme::AccentDim());
  }
  Elements parts;
  if (caret_vis_in_guide > 0) {
    parts.push_back(text(guide_text.substr(0, static_cast<std::size_t>(caret_vis_in_guide))) |
                    color(theme::AccentDim()));
  }
  if (cursor_blink::visible()) {
    parts.push_back(text(" ") | cursor_blink::cell_decorator());
  } else {
    parts.push_back(text(" "));
  }
  if (caret_vis_in_guide + 1 < static_cast<int>(guide_text.size())) {
    parts.push_back(
        text(guide_text.substr(static_cast<std::size_t>(caret_vis_in_guide + 1))) |
        color(theme::AccentDim()));
  }
  return hbox(std::move(parts));
}

Element render_primary_caret_tail(const EditorBuffer& buffer, int line_index, bool editor_focused,
                                  bool show_caret, const std::string& line, int scroll_col,
                                  int guide_visual_cols, int tab_size) {
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
    parts.push_back(text(" ") | cursor_blink::cell_decorator());
  } else {
    parts.push_back(text(" "));
  }
  return parts.empty() ? text(" ") : hbox(std::move(parts));
}

bool col_in_view(int col, int scroll_col, int view_width) {
  if (view_width <= 0) {
    return col >= scroll_col;
  }
  return col >= scroll_col && col < scroll_col + view_width;
}

void shift_decorations(std::vector<EditorDecoration>* decorations, int scroll_col, int view_width) {
  if (decorations == nullptr) {
    return;
  }
  std::vector<EditorDecoration> shifted;
  shifted.reserve(decorations->size());
  for (const auto& deco : *decorations) {
    if (!col_in_view(deco.end_col - 1, scroll_col, view_width) &&
        !col_in_view(deco.start_col, scroll_col, view_width)) {
      continue;
    }
    EditorDecoration copy = deco;
    copy.start_col = std::max(0, shift_col(copy.start_col, scroll_col));
    copy.end_col = std::max(copy.start_col + 1, shift_col(copy.end_col, scroll_col));
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

bool line_needs_rich_decorations(int line_index, const EditorBuffer& buffer,
                                 const std::vector<TextMatch>* find_matches,
                                 const std::vector<TextMatch>* selection_occurrences,
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
    case EditorDecoration::Kind::SelectionOccurrence:
      return element | bgcolor(theme::SelectionOccurrenceBg());
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
                            int col_offset = 0, CppHighlightContext* highlight_ctx = nullptr,
                            BuildFileKind build_file_kind = BuildFileKind::kNone) {
  if (build_file_kind != BuildFileKind::kNone) {
    const int draw_col = cursor_col >= 0 ? cursor_col - col_offset : -1;
    return HighlightBuildFileLine(build_file_kind, line, draw_col, cursor_style);
  }
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
  return HighlightCodeLine(line, line_index, semantic_tokens, cursor_col, cursor_style, col_offset,
                           highlight_ctx);
}

Element render_simple_line(const std::string& line, int line_index, const EditorBuffer& buffer,
                           bool editor_focused, const SemanticTokenDocument* semantic_tokens,
                           bool syntax_highlight, const Decorator& line_bg, bool show_caret,
                           int scroll_col, CppHighlightContext* highlight_ctx,
                           BuildFileKind build_file_kind, int guide_prefix_bytes = 0) {
  (void)line_bg;
  const int effective_scroll_col = scroll_col + guide_prefix_bytes;
  if (!editor_focused || line_index != buffer.primary_line() || !show_caret) {
    return render_line_content(line, line_index, semantic_tokens, syntax_highlight, -1, {},
                               effective_scroll_col, highlight_ctx, build_file_kind);
  }
  const int col = shift_col(buffer.primary_col(), effective_scroll_col);
  const Decorator cursor_cell = cursor_blink::cell_decorator();
  const int clamped = std::max(0, std::min(col, static_cast<int>(line.size())));
  const int draw_col = cursor_blink::effective_col(clamped);
  if (line.empty() || clamped >= static_cast<int>(line.size())) {
    Elements parts;
    if (!line.empty()) {
      parts.push_back(render_line_content(line, line_index, semantic_tokens, syntax_highlight, -1,
                                          {}, effective_scroll_col, highlight_ctx,
                                          build_file_kind));
    }
    if (cursor_blink::visible()) {
      parts.push_back(text(" ") | cursor_cell);
    } else {
      parts.push_back(text(" "));
    }
    return hbox(std::move(parts));
  }
  return render_line_content(line, line_index, semantic_tokens, syntax_highlight, draw_col,
                             cursor_cell, effective_scroll_col, highlight_ctx, build_file_kind);
}

Element render_rich_line(const std::string& line, int line_index, const EditorBuffer& buffer,
                         bool editor_focused, const std::vector<TextMatch>* find_matches,
                         const std::vector<TextMatch>* selection_occurrences,
                         const SemanticTokenDocument* semantic_tokens, bool syntax_highlight,
                         const BracketPairHighlight* bracket,
                         const std::vector<Diagnostic>* line_diagnostics,
                         const EditorSymbolPress* symbol_press, bool show_caret, int scroll_col,
                         int view_width, CppHighlightContext* highlight_ctx,
                         BuildFileKind build_file_kind, int guide_prefix_bytes = 0) {
  const int effective_scroll_col = scroll_col + guide_prefix_bytes;
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
  collect_line_decorations(line_index, buffer, editor_focused, show_caret, &decorations);
  shift_decorations(&decorations, effective_scroll_col, view_width);

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
    const int col_offset = syntax_highlight ? prev + effective_scroll_col : 0;
    const bool highlight_segment = syntax_highlight && !use_build_highlight;
    parts.push_back(apply_decoration(
        segment.empty() ? text(" ")
                        : render_line_content(segment, line_index, semantic_tokens,
                                              highlight_segment, -1, {}, col_offset,
                                              highlight_ctx, build_file_kind),
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

void collect_selection_occurrence_decorations(int line_index, const std::vector<TextMatch>& matches,
                                              std::vector<EditorDecoration>* out) {
  for (const auto& match : matches) {
    if (match.line == line_index && match.length > 0) {
      out->push_back(
          {match.col, match.col + match.length, EditorDecoration::Kind::SelectionOccurrence});
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
    if (show_caret && cursor.head.line == line_index) {
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
                         const std::vector<TextMatch>* selection_occurrences,
                         const SemanticTokenDocument* semantic_tokens,
                         const BracketPairHighlight* bracket,
                         const std::vector<Diagnostic>* line_diagnostics,
                         const std::string* diagnostic_suffix,
                         const std::vector<Diagnostic>* suffix_diagnostics,
                         const EditorSymbolPress* symbol_press, bool show_caret, int scroll_col,
                         int view_width, CppHighlightContext* highlight_ctx,
                         bool sticky_scroll_line, bool indent_guides_enabled,
                         int indent_guide_depth, bool defer_rich_decorations) {
  const std::string view_line = slice_line_for_view(line, scroll_col, view_width);
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  IndentGuideSplit guide_split;
  if (indent_guides_enabled && indent_guide_depth > 0) {
    guide_split = split_indent_guide_prefix(view_line, tab_size, indent_guide_depth);
  }

  const std::string& body_line =
      guide_split.prefix_byte_length > 0 ? guide_split.suffix : view_line;
  const int guide_prefix_bytes = guide_split.prefix_byte_length;
  const BuildFileKind build_file_kind = detect_build_file_kind(buffer.path);
  const bool is_build_file = build_file_kind != BuildFileKind::kNone;
  const bool use_warm_line_bg = build_file_kind == BuildFileKind::kMakefile;
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
                                   line_diagnostics, symbol_press));
  if (highlight_ctx != nullptr && highlight_ctx->in_block_comment) {
    rich = false;
  }

  const bool caret_on_primary =
      show_caret && editor_focused && line_index == buffer.primary_line();
  const int caret_vis_in_view =
      caret_on_primary ? caret_visual_in_view(line, buffer.primary_col(), scroll_col, tab_size)
                       : -1;
  const bool caret_in_guide_prefix =
      guide_prefix_bytes > 0 && caret_vis_in_view >= 0 &&
      caret_vis_in_view < guide_split.prefix_visual_width;
  const bool body_show_caret = show_caret && !caret_in_guide_prefix;

  Element content =
      rich ? render_rich_line(body_line, line_index, buffer, editor_focused, find_matches,
                              selection_occurrences, semantic_tokens, syntax_highlight, bracket,
                              line_diagnostics, symbol_press, body_show_caret, scroll_col, view_width,
                              highlight_ctx, build_file_kind, guide_prefix_bytes)
           : render_simple_line(body_line, line_index, buffer, editor_focused, semantic_tokens,
                                syntax_highlight, line_bg, body_show_caret, scroll_col, highlight_ctx,
                                build_file_kind, guide_prefix_bytes);

  if (guide_prefix_bytes > 0) {
    Element guide =
        caret_in_guide_prefix
            ? render_guide_with_caret(guide_split.guide_text, caret_vis_in_view)
            : text(guide_split.guide_text) | color(theme::AccentDim());
    if (body_line.empty()) {
      if (caret_vis_in_view >= guide_split.prefix_visual_width) {
        content = hbox({std::move(guide),
                        render_primary_caret_tail(buffer, line_index, editor_focused, show_caret,
                                                  line, scroll_col, guide_split.prefix_visual_width,
                                                  tab_size)});
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
              ? render_guide_with_caret(blank_guides, caret_vis_in_view)
              : text(blank_guides) | color(theme::AccentDim());
      if (caret_on_primary && caret_vis_in_view >= guide_width) {
        content = hbox({std::move(guide),
                        render_primary_caret_tail(buffer, line_index, editor_focused, show_caret,
                                                  line, scroll_col, guide_width, tab_size)});
      } else {
        content = std::move(guide);
      }
    }
  }

  const std::vector<Diagnostic>* suffix_color =
      suffix_diagnostics != nullptr ? suffix_diagnostics : line_diagnostics;
  return wrap_with_suffix(std::move(content), line_bg, diagnostic_suffix, suffix_color);
}

}  // namespace tgdb
