#include "util/syntax_highlight.hpp"

#include <string_view>

#include "editor/editor_buffer_source.hpp"
#include "editor/indent_guides.hpp"
#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_service.hpp"
#include "ui/theme.hpp"
#include "util/clang_format_config.hpp"

namespace tgdb {

using namespace ftxui;

const std::string& SyntaxHighlightContext::joined() const {
  if (joined_override != nullptr) {
    return *joined_override;
  }
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
  if (prepare_token != buffer_token) {
    tree_sitter_service().prepare_document(file_path, source);
    prepare_token = buffer_token;
  }
  const uint64_t revision = tree_sitter_service().revision_for(file_path);
  if (ts_revision != revision || ts_line_highlights == nullptr) {
    ts_line_highlights = tree_sitter_service().highlights_for(file_path, source);
    ts_revision = revision;
  }
  return ts_line_highlights;
}

namespace {

constexpr uint64_t kSyntaxSpanHashOffset = 14695981039346656037ULL;
constexpr uint64_t kSyntaxSpanHashPrime = 1099511628211ULL;

uint64_t syntax_span_hash_u64(uint64_t h, uint64_t v) {
  return (h ^ v) * kSyntaxSpanHashPrime;
}

uint64_t syntax_span_hash_string(uint64_t h, std::string_view s) {
  for (unsigned char c : s) {
    h = syntax_span_hash_u64(h, c);
  }
  return h;
}

uint64_t syntax_line_span_cache_key(int line_index, const std::string& source_line, int col_offset,
                                    int display_len, uint64_t buffer_token, uint64_t ts_revision,
                                    uint64_t semantic_revision) {
  uint64_t h = kSyntaxSpanHashOffset;
  h = syntax_span_hash_u64(h, static_cast<uint64_t>(line_index));
  h = syntax_span_hash_string(h, source_line);
  h = syntax_span_hash_u64(h, static_cast<uint64_t>(col_offset));
  h = syntax_span_hash_u64(h, static_cast<uint64_t>(display_len));
  h = syntax_span_hash_u64(h, buffer_token);
  h = syntax_span_hash_u64(h, ts_revision);
  h = syntax_span_hash_u64(h, semantic_revision);
  return h;
}

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
    const int rel_end_clamped = std::max(0, rel_end);
    mapped.end_col = std::max(mapped.start_col, std::min(display_length, rel_end_clamped));
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

std::vector<SemanticTokenSpan> display_semantic_spans_for_line(
    const std::vector<SemanticTokenSpan>& source_spans, const std::string& source_line,
    int source_byte_offset, int display_length, int tab_size) {
  std::vector<SemanticTokenSpan> out;
  if (display_length <= 0 || source_spans.empty()) {
    return out;
  }
  const int base_vis =
      byte_index_to_visual_column(source_line, std::max(0, source_byte_offset), tab_size);
  for (const SemanticTokenSpan& span : source_spans) {
    const int span_start_vis = byte_index_to_visual_column(source_line, span.start_col, tab_size);
    const int span_end_vis =
        byte_index_to_visual_column(source_line, span.start_col + span.length, tab_size);
    const int rel_start = span_start_vis - base_vis;
    const int rel_end = span_end_vis - base_vis;
    if (rel_end <= 0 || rel_start >= display_length) {
      continue;
    }
    SemanticTokenSpan mapped = span;
    mapped.start_col = std::max(0, rel_start);
    const int rel_end_clamped = std::max(0, rel_end);
    mapped.length = std::max(0, std::min(display_length, rel_end_clamped) - mapped.start_col);
    if (mapped.length > 0) {
      out.push_back(mapped);
    }
  }
  return out;
}

const CachedSyntaxLineSpans* cached_syntax_spans_for_line(
    SyntaxHighlightContext* ctx, int line_index, const SemanticTokenDocument* semantic_tokens,
    int col_offset, int display_len) {
  if (ctx == nullptr || ctx->lines == nullptr || ctx->line_span_cache == nullptr ||
      line_index < 0 || line_index >= static_cast<int>(ctx->lines->size())) {
    return nullptr;
  }
  const std::string& source_line = (*ctx->lines)[static_cast<std::size_t>(line_index)];
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  ctx->tree_sitter_highlights();
  const uint64_t key = syntax_line_span_cache_key(line_index, source_line, col_offset, display_len,
                                                  ctx->buffer_token, ctx->ts_revision,
                                                  ctx->semantic_revision);
  auto& cache = *ctx->line_span_cache;
  const auto it = cache.find(key);
  if (it != cache.end()) {
    return &it->second;
  }

  CachedSyntaxLineSpans entry;
  if (const auto* all_highlights = ctx->ts_line_highlights) {
    if (line_index >= 0 && line_index < static_cast<int>(all_highlights->size())) {
      const LineHighlights& source_hl = (*all_highlights)[static_cast<std::size_t>(line_index)];
      entry.ts_display_spans =
          display_spans_for_line(source_hl, source_line, col_offset, display_len, tab_size);
    }
  }
  if (semantic_tokens != nullptr && semantic_tokens->ready &&
      line_index < static_cast<int>(semantic_tokens->lines.size())) {
    const auto& line_spans = semantic_tokens->lines[static_cast<std::size_t>(line_index)];
    entry.semantic_display_spans = display_semantic_spans_for_line(
        line_spans, source_line, col_offset, display_len, tab_size);
    entry.has_semantic = !entry.semantic_display_spans.empty();
  }
  cache[key] = std::move(entry);
  return &cache[key];
}

int fragment_display_to_source_byte(const std::string& source_line, int source_byte_offset,
                                    int fragment_display_col, int tab_size) {
  return source_byte_at_display_column(source_line, source_byte_offset, fragment_display_col,
                                       tab_size);
}

Element highlight_semantic_segment(const std::string& segment, const SemanticTokenSpan& span,
                                   const std::vector<std::string>& types, int line_cursor_col,
                                   Decorator cursor_style, int display_col_offset) {
  const Decorator style = style_for_token_type(types, span.type);
  if (line_cursor_col < 0 || !cursor_style) {
    return text(segment) | style;
  }

  const int span_view_start = span.start_col - display_col_offset;
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
                                  SyntaxHighlightContext* ctx, int cursor_col,
                                  Decorator cursor_style, int col_offset,
                                  const SemanticTokenDocument* semantic_tokens) {
  if (ctx != nullptr && ctx->line_span_cache != nullptr) {
    if (const CachedSyntaxLineSpans* cached = cached_syntax_spans_for_line(
            ctx, line_index, semantic_tokens, col_offset, static_cast<int>(line.size()))) {
      return HighlightTreeSitterLine(line, line_index, cached->ts_display_spans, cursor_col,
                                   cursor_style, 0);
    }
  }
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
                                const std::vector<SemanticTokenSpan>& display_spans,
                                const std::vector<std::string>& types, int cursor_col,
                                Decorator cursor_style, int source_byte_offset,
                                SyntaxHighlightContext* ctx,
                                const SemanticTokenDocument* semantic_tokens) {
  if (display_spans.empty()) {
    return highlight_tree_sitter_gap(line, line_index, ctx, cursor_col, cursor_style,
                                   source_byte_offset, semantic_tokens);
  }

  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const std::string* source_line = nullptr;
  if (ctx != nullptr && ctx->lines != nullptr &&
      line_index >= 0 && line_index < static_cast<int>(ctx->lines->size())) {
    source_line = &(*ctx->lines)[static_cast<std::size_t>(line_index)];
  }

  Elements parts;
  const int segment_end = static_cast<int>(line.size());
  int col = 0;
  for (const SemanticTokenSpan& span : display_spans) {
    const int span_end = span.start_col + span.length;
    if (span_end <= 0 || span.start_col >= segment_end) {
      continue;
    }

    if (span.start_col > col) {
      const int gap_start = col;
      const int gap_end = std::min(segment_end, span.start_col);
      if (gap_end > gap_start) {
        const std::string gap = line.substr(static_cast<std::size_t>(gap_start),
                                            static_cast<std::size_t>(gap_end - gap_start));
        const int gap_cursor =
            (cursor_col >= gap_start && cursor_col < gap_end) ? cursor_col : -1;
        const int gap_source_byte =
            source_line != nullptr
                ? fragment_display_to_source_byte(*source_line, source_byte_offset, gap_start,
                                                  tab_size)
                : source_byte_offset;
        parts.push_back(highlight_tree_sitter_gap(gap, line_index, ctx, gap_cursor, cursor_style,
                                                  gap_source_byte, semantic_tokens));
      }
      col = span.start_col;
    }

    const int slice_start = std::max(col, span.start_col);
    const int slice_end = std::min(segment_end, span_end);
    if (slice_end <= slice_start) {
      continue;
    }

    const std::string segment =
        line.substr(static_cast<std::size_t>(slice_start),
                    static_cast<std::size_t>(slice_end - slice_start));
    SemanticTokenSpan clipped = span;
    clipped.start_col = slice_start;
    clipped.length = slice_end - slice_start;
    parts.push_back(highlight_semantic_segment(segment, clipped, types, cursor_col, cursor_style,
                                               /*display_col_offset=*/0));
    col = slice_end;
  }

  if (col < segment_end) {
    const std::string tail = line.substr(static_cast<std::size_t>(col));
    const int tail_cursor = cursor_col >= col ? cursor_col : -1;
    const int tail_source_byte =
        source_line != nullptr
            ? fragment_display_to_source_byte(*source_line, source_byte_offset, col, tab_size)
            : source_byte_offset;
    parts.push_back(highlight_tree_sitter_gap(tail, line_index, ctx, tail_cursor, cursor_style,
                                              tail_source_byte, semantic_tokens));
  }

  if (parts.empty()) {
    return highlight_tree_sitter_gap(line, line_index, ctx, cursor_col, cursor_style,
                                     source_byte_offset, semantic_tokens);
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
  SyntaxHighlightContext* mutable_ctx = const_cast<SyntaxHighlightContext*>(ctx);
  if (mutable_ctx != nullptr && mutable_ctx->line_span_cache != nullptr) {
    if (const CachedSyntaxLineSpans* cached = cached_syntax_spans_for_line(
            mutable_ctx, line_index, semantic_tokens, col_offset, static_cast<int>(line.size()))) {
      if (cached->has_semantic && semantic_tokens != nullptr && semantic_tokens->ready) {
        return highlight_semantic_line(line, line_index, cached->semantic_display_spans,
                                       semantic_tokens->token_types, cursor_col, cursor_style,
                                       col_offset, mutable_ctx, semantic_tokens);
      }
      return HighlightTreeSitterLine(line, line_index, cached->ts_display_spans, cursor_col,
                                     cursor_style, 0);
    }
  }

  if (semantic_tokens == nullptr || !semantic_tokens->ready || line_index < 0 ||
      line_index >= static_cast<int>(semantic_tokens->lines.size())) {
    return highlight_tree_sitter_gap(line, line_index, mutable_ctx, cursor_col, cursor_style,
                                   col_offset, semantic_tokens);
  }

  if (ctx == nullptr || ctx->lines == nullptr ||
      line_index >= static_cast<int>(ctx->lines->size())) {
    return highlight_tree_sitter_gap(line, line_index, mutable_ctx, cursor_col, cursor_style,
                                     col_offset, semantic_tokens);
  }

  const std::string& source_line = (*ctx->lines)[static_cast<std::size_t>(line_index)];
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const auto& line_spans = semantic_tokens->lines[static_cast<std::size_t>(line_index)];
  const auto display_spans = display_semantic_spans_for_line(
      line_spans, source_line, col_offset, static_cast<int>(line.size()), tab_size);
  if (display_spans.empty()) {
    return highlight_tree_sitter_gap(line, line_index, mutable_ctx, cursor_col, cursor_style,
                                     col_offset, semantic_tokens);
  }

  return highlight_semantic_line(line, line_index, display_spans, semantic_tokens->token_types,
                                 cursor_col, cursor_style, col_offset, mutable_ctx,
                                 semantic_tokens);
}

}  // namespace tgdb
