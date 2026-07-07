#include "util/syntax_highlight.hpp"

#include "editor/indent_guides.hpp"
#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_service.hpp"
#include "ui/theme.hpp"
#include "util/clang_format_config.hpp"

namespace tgdb {

using namespace ftxui;

const std::string& SyntaxHighlightContext::joined() const {
  if (lines == nullptr) {
    return joined_source;
  }
  if (joined_token != buffer_token) {
    joined_source = join_editor_lines(*lines);
    joined_token = buffer_token;
  }
  return joined_source;
}

const std::vector<LineHighlights>* SyntaxHighlightContext::tree_sitter_highlights() const {
  if (lines == nullptr || lines->empty() || file_path.empty()) {
    return nullptr;
  }
  const std::string& source = joined();
  if (source.empty()) {
    return nullptr;
  }
  tree_sitter_service().prepare_document(file_path, source);
  const uint64_t revision = tree_sitter_service().revision_for(file_path);
  if (ts_revision != revision || ts_line_highlights == nullptr) {
    ts_line_highlights = tree_sitter_service().highlights_for(file_path, source);
    ts_revision = revision;
  }
  return ts_line_highlights;
}

namespace {

Decorator style_for_token_type(const std::vector<std::string>& types, int type_index) {
  std::string name;
  if (type_index >= 0 && type_index < static_cast<int>(types.size())) {
    name = types[static_cast<std::size_t>(type_index)];
  }

  if (name == "comment") {
    return color(theme::SyntaxComment()) | dim;
  }
  if (name == "string" || name == "regexp") {
    return color(theme::SyntaxString());
  }
  if (name == "number") {
    return color(theme::SyntaxNumber());
  }
  if (name == "keyword" || name == "modifier") {
    return color(theme::SyntaxKeyword()) | bold;
  }
  if (name == "macro" || name == "decorator") {
    return color(theme::SyntaxMacro());
  }
  if (name == "namespace") {
    return color(theme::SyntaxNamespace());
  }
  if (name == "type" || name == "class" || name == "struct" || name == "enum" ||
      name == "interface" || name == "typeParameter") {
    return color(theme::SyntaxType());
  }
  if (name == "function" || name == "method" || name == "event") {
    return color(theme::SyntaxFunction());
  }
  if (name == "parameter") {
    return color(theme::SyntaxParameter());
  }
  if (name == "property" || name == "enumMember") {
    return color(theme::SyntaxProperty());
  }
  if (name == "operator") {
    return color(theme::SyntaxOperator());
  }
  if (name == "variable") {
    return color(theme::SyntaxVariable());
  }
  return color(theme::SyntaxDefault());
}

LineHighlights display_spans_for_line(const LineHighlights& source_spans,
                                      const std::string& source_line, int col_offset,
                                      int display_length, int tab_size) {
  LineHighlights out;
  if (display_length <= 0) {
    return out;
  }
  const int base_vis = byte_index_to_visual_column(source_line, std::max(0, col_offset), tab_size);
  for (const HighlightSpan& span : source_spans.spans) {
    const int rel_start =
        byte_index_to_visual_column(source_line, span.start_col, tab_size) - base_vis;
    const int rel_end =
        byte_index_to_visual_column(source_line, span.end_col, tab_size) - base_vis;
    if (rel_end <= 0 || rel_start >= display_length) {
      continue;
    }
    HighlightSpan mapped = span;
    mapped.start_col = std::max(0, rel_start);
    mapped.end_col = std::min(display_length, rel_end);
    if (mapped.end_col > mapped.start_col) {
      out.spans.push_back(mapped);
    }
  }
  std::sort(out.spans.begin(), out.spans.end(),
            [](const HighlightSpan& a, const HighlightSpan& b) {
              if (a.start_col != b.start_col) {
                return a.start_col < b.start_col;
              }
              return a.end_col > b.end_col;
            });
  return out;
}

Element highlight_semantic_segment(const std::string& segment, const SemanticTokenSpan& span,
                                   const std::vector<std::string>& types, int line_cursor_col,
                                   Decorator cursor_style, int col_offset) {
  const Decorator style = style_for_token_type(types, span.type);
  if (line_cursor_col < 0 || !cursor_style) {
    return text(segment) | style;
  }

  const int span_view_start = span.start_col - col_offset;
  const int rel = line_cursor_col - span_view_start;
  if (rel < 0 || rel >= static_cast<int>(segment.size())) {
    return text(segment) | style;
  }

  std::string token_name;
  if (span.type >= 0 && span.type < static_cast<int>(types.size())) {
    token_name = types[static_cast<std::size_t>(span.type)];
  }
  const bool inverted_cursor = token_name == "keyword" || token_name == "modifier" ||
                               token_name == "type" || token_name == "class" ||
                               token_name == "struct" || token_name == "enum";

  Elements parts;
  if (rel > 0) {
    parts.push_back(text(segment.substr(0, static_cast<std::size_t>(rel))) | style);
  }
  Element cursor_cell = text(segment.substr(static_cast<std::size_t>(rel), 1));
  if (inverted_cursor) {
    cursor_cell = cursor_cell | inverted | bold;
  } else {
    cursor_cell = cursor_cell | cursor_style;
  }
  parts.push_back(std::move(cursor_cell));
  if (rel + 1 < static_cast<int>(segment.size())) {
    parts.push_back(text(segment.substr(static_cast<std::size_t>(rel + 1))) | style);
  }
  return hbox(std::move(parts));
}

Element highlight_tree_sitter_gap(const std::string& line, int line_index,
                                  const SyntaxHighlightContext* ctx, int cursor_col,
                                  Decorator cursor_style, int col_offset) {
  if (ctx == nullptr || ctx->lines == nullptr || ctx->lines->empty()) {
    return text(line);
  }
  const auto* all_highlights = ctx->tree_sitter_highlights();
  if (all_highlights == nullptr || line_index < 0 ||
      line_index >= static_cast<int>(all_highlights->size())) {
    return text(line);
  }
  const std::string& source_line = (*ctx->lines)[static_cast<std::size_t>(line_index)];
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const LineHighlights& source_hl = (*all_highlights)[static_cast<std::size_t>(line_index)];
  const LineHighlights display_hl =
      display_spans_for_line(source_hl, source_line, col_offset, static_cast<int>(line.size()),
                             tab_size);
  return HighlightTreeSitterLine(line, line_index, display_hl, cursor_col, cursor_style, 0);
}

Element highlight_semantic_line(const std::string& line, int line_index,
                                const std::vector<SemanticTokenSpan>& spans,
                                const std::vector<std::string>& types, int cursor_col,
                                Decorator cursor_style, int col_offset,
                                const SyntaxHighlightContext* ctx) {
  if (spans.empty()) {
    return highlight_tree_sitter_gap(line, line_index, ctx, cursor_col, cursor_style, col_offset);
  }

  Elements parts;
  const int segment_end = col_offset + static_cast<int>(line.size());
  int col = col_offset;
  for (const SemanticTokenSpan& span : spans) {
    const int span_end = span.start_col + span.length;
    if (span_end <= col_offset || span.start_col >= segment_end) {
      continue;
    }

    if (span.start_col > col) {
      const int gap_start = col - col_offset;
      const int gap_end = std::min(static_cast<int>(line.size()), span.start_col - col_offset);
      if (gap_end > gap_start) {
        const std::string gap = line.substr(static_cast<std::size_t>(gap_start),
                                            static_cast<std::size_t>(gap_end - gap_start));
        const int gap_view_start = col - col_offset;
        const int gap_view_end = span.start_col - col_offset;
        const int gap_cursor = (cursor_col >= gap_view_start && cursor_col < gap_view_end)
                                   ? col_offset + cursor_col
                                   : -1;
        parts.push_back(highlight_tree_sitter_gap(gap, line_index, ctx, gap_cursor, cursor_style,
                                                  col));
      }
      col = span.start_col;
    }

    const int slice_start = std::max(col, span.start_col);
    const int slice_end = std::min(segment_end, span_end);
    if (slice_end <= slice_start) {
      continue;
    }

    const std::string segment =
        line.substr(static_cast<std::size_t>(slice_start - col_offset),
                    static_cast<std::size_t>(slice_end - slice_start));
    SemanticTokenSpan clipped = span;
    clipped.start_col = slice_start;
    clipped.length = slice_end - slice_start;
    parts.push_back(
        highlight_semantic_segment(segment, clipped, types, cursor_col, cursor_style, col_offset));
    col = slice_end;
  }

  if (col < segment_end) {
    const std::string tail = line.substr(static_cast<std::size_t>(col - col_offset));
    const int tail_view_start = col - col_offset;
    const int tail_cursor = cursor_col >= tail_view_start ? col_offset + cursor_col : -1;
    parts.push_back(highlight_tree_sitter_gap(tail, line_index, ctx, tail_cursor, cursor_style,
                                              col));
  }

  if (parts.empty()) {
    const int abs_cursor = cursor_col >= 0 ? cursor_col + col_offset : -1;
    return highlight_tree_sitter_gap(line, line_index, ctx, abs_cursor, cursor_style, col_offset);
  }
  return hbox(std::move(parts));
}

std::vector<SemanticTokenSpan> spans_for_segment(const std::vector<SemanticTokenSpan>& line_spans,
                                                 int col_offset, int segment_length) {
  std::vector<SemanticTokenSpan> out;
  const int segment_end = col_offset + segment_length;
  for (const SemanticTokenSpan& span : line_spans) {
    const int span_end = span.start_col + span.length;
    if (span_end <= col_offset || span.start_col >= segment_end) {
      continue;
    }
    out.push_back(span);
  }
  return out;
}

}  // namespace

Element HighlightCodeLine(const std::string& line, int line_index,
                          const SemanticTokenDocument* semantic_tokens, int cursor_col,
                          Decorator cursor_style, int col_offset,
                          const SyntaxHighlightContext* ctx) {
  if (semantic_tokens == nullptr || !semantic_tokens->ready || line_index < 0 ||
      line_index >= static_cast<int>(semantic_tokens->lines.size())) {
    return highlight_tree_sitter_gap(line, line_index, ctx, cursor_col, cursor_style, col_offset);
  }

  const auto& line_spans = semantic_tokens->lines[static_cast<std::size_t>(line_index)];
  const auto spans = spans_for_segment(line_spans, col_offset, static_cast<int>(line.size()));
  if (spans.empty()) {
    const int abs_cursor = cursor_col >= 0 ? cursor_col + col_offset : -1;
    return highlight_tree_sitter_gap(line, line_index, ctx, abs_cursor, cursor_style, col_offset);
  }

  return highlight_semantic_line(line, line_index, spans, semantic_tokens->token_types, cursor_col,
                                 cursor_style, col_offset, ctx);
}

}  // namespace tgdb
