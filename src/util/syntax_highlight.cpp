#include "util/syntax_highlight.hpp"

#include "ui/theme.hpp"
#include "util/cpp_highlight.hpp"

namespace tgdb {

using namespace ftxui;

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

Element highlight_semantic_segment(const std::string& segment, const SemanticTokenSpan& span,
                                   const std::vector<std::string>& types, int line_cursor_col,
                                   Decorator cursor_style) {
  const Decorator style = style_for_token_type(types, span.type);
  if (line_cursor_col < 0 || !cursor_style) {
    return text(segment) | style;
  }

  const int rel = line_cursor_col - span.start_col;
  if (rel < 0 || rel >= span.length) {
    return text(segment) | style;
  }

  Elements parts;
  if (rel > 0) {
    parts.push_back(text(segment.substr(0, static_cast<std::size_t>(rel))) | style);
  }
  parts.push_back(text(segment.substr(static_cast<std::size_t>(rel), 1)) | cursor_style);
  if (rel + 1 < static_cast<int>(segment.size())) {
    parts.push_back(text(segment.substr(static_cast<std::size_t>(rel + 1))) | style);
  }
  return hbox(std::move(parts));
}

Element highlight_semantic_line(const std::string& line,
                                const std::vector<SemanticTokenSpan>& spans,
                                const std::vector<std::string>& types, int cursor_col,
                                Decorator cursor_style, int col_offset,
                                CppHighlightContext* ctx) {
  if (spans.empty()) {
    return HighlightCppLine(line, cursor_col, cursor_style, col_offset, ctx);
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
        const int gap_cursor =
            (cursor_col >= col && cursor_col < span.start_col) ? cursor_col - col : -1;
        parts.push_back(HighlightCppLine(gap, gap_cursor, cursor_style, col, ctx));
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
    parts.push_back(highlight_semantic_segment(segment, clipped, types, cursor_col, cursor_style));
    col = slice_end;
  }

  if (col < segment_end) {
    const std::string tail = line.substr(static_cast<std::size_t>(col - col_offset));
    const int tail_cursor = cursor_col >= col ? cursor_col - col : -1;
    parts.push_back(HighlightCppLine(tail, tail_cursor, cursor_style, col, ctx));
  }

  if (parts.empty()) {
    return HighlightCppLine(line, cursor_col, cursor_style, col_offset, ctx);
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
                          Decorator cursor_style, int col_offset, CppHighlightContext* ctx) {
  if (ctx != nullptr && ctx->in_block_comment) {
    return HighlightCppLine(line, cursor_col, cursor_style, col_offset, ctx);
  }

  if (semantic_tokens == nullptr || !semantic_tokens->ready ||
      line_index < 0 ||
      line_index >= static_cast<int>(semantic_tokens->lines.size())) {
    return HighlightCppLine(line, cursor_col, cursor_style, col_offset, ctx);
  }

  const auto& line_spans = semantic_tokens->lines[static_cast<std::size_t>(line_index)];
  const auto spans = spans_for_segment(line_spans, col_offset, static_cast<int>(line.size()));
  if (spans.empty()) {
    return HighlightCppLine(line, cursor_col, cursor_style, col_offset, ctx);
  }

  return highlight_semantic_line(line, spans, semantic_tokens->token_types, cursor_col, cursor_style,
                                 col_offset, ctx);
}

}  // namespace tgdb
