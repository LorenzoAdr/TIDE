#include "util/syntax_highlight.hpp"

#include <cctype>
#include <string_view>

#include "editor/editor_buffer_source.hpp"
#include "editor/indent_guides.hpp"
#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_service.hpp"
#include "ui/theme.hpp"
#include "util/clang_format_config.hpp"
#include "util/syntax_scope.hpp"

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
  if (lines == nullptr || lines->size() == 0 || file_path.empty()) {
    return nullptr;
  }
  if (syntax_incremental) {
    const uint64_t revision = tree_sitter_service().revision_for(file_path);
    if (ts_line_highlights == nullptr || ts_revision != revision) {
      ts_line_highlights = tree_sitter_service().stale_highlights_for(
          file_path, static_cast<int>(lines->size()));
      ts_revision = revision;
    }
    return ts_line_highlights;
  }
  const std::string& source = joined();
  if (source.empty()) {
    return nullptr;
  }
  const uint64_t revision = tree_sitter_service().revision_for(file_path);
  if (prepare_token != revision) {
    tree_sitter_service().prepare_document(file_path, source);
    prepare_token = revision;
  }
  if (ts_revision != revision || ts_line_highlights == nullptr) {
    ts_line_highlights = tree_sitter_service().highlights_for(file_path, source);
    ts_revision = revision;
  }
  return ts_line_highlights;
}

namespace {

const LineHighlights* live_line_highlights(SyntaxHighlightContext* ctx, int line_index) {
  if (ctx == nullptr || !ctx->syntax_incremental || line_index < 0 || ctx->file_path.empty()) {
    return nullptr;
  }
  if (ctx->editing_live_token != ctx->buffer_token) {
    ctx->editing_live_by_line.clear();
    ctx->editing_live_token = ctx->buffer_token;
  }
  const auto cached = ctx->editing_live_by_line.find(line_index);
  if (cached != ctx->editing_live_by_line.end()) {
    return &cached->second;
  }
  const std::string& source = ctx->joined();
  if (source.empty()) {
    ctx->editing_live_by_line.emplace(line_index, LineHighlights{});
    return &ctx->editing_live_by_line.at(line_index);
  }
  std::optional<LineHighlights> live =
      tree_sitter_service().highlights_for_editing_line(ctx->file_path, source, line_index);
  ctx->editing_live_by_line.emplace(line_index,
                                    live.has_value() ? std::move(*live) : LineHighlights{});
  return &ctx->editing_live_by_line.at(line_index);
}

bool line_uses_live_ts_overlay(const SyntaxHighlightContext* ctx, int line_index) {
  if (ctx == nullptr || !ctx->syntax_incremental || line_index < 0) {
    return false;
  }
  if (line_index == ctx->editing_line) {
    return true;
  }
  return ctx->dirty_highlight_lines != nullptr &&
         ctx->dirty_highlight_lines->count(line_index) > 0;
}

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
                                    int display_len, uint64_t ts_revision,
                                    uint64_t semantic_line_hash, uint64_t buffer_token) {
  uint64_t h = kSyntaxSpanHashOffset;
  h = syntax_span_hash_u64(h, static_cast<uint64_t>(line_index));
  h = syntax_span_hash_string(h, source_line);
  h = syntax_span_hash_u64(h, static_cast<uint64_t>(col_offset));
  h = syntax_span_hash_u64(h, static_cast<uint64_t>(display_len));
  h = syntax_span_hash_u64(h, buffer_token);
  h = syntax_span_hash_u64(h, ts_revision);
  // Content hash of this specific line's semantic spans (not the document-wide
  // revision counter, and not gated on the typing-burst flag): a clangd re-fetch that
  // leaves this line's tokens unchanged must not evict/recompute it (see
  // hash_semantic_token_line()), and neither must starting/stopping a typing burst on
  // some *other* line -- both used to force every visible line to switch cache keys
  // and re-render at once.
  h = syntax_span_hash_u64(h, semantic_line_hash);
  return h;
}

// Resolved through the same canonical scope translator tree-sitter uses (see
// util/syntax_scope.hpp), so a token type like "class" always renders with
// the exact color tree-sitter's "type" capture would use. That shared
// mapping is what prevents a visible color flip when semantic tokens arrive
// and take over from tree-sitter for a given line.
SyntaxScope scope_for_token_type(const std::vector<std::string>& types, int type_index) {
  std::string name;
  if (type_index >= 0 && type_index < static_cast<int>(types.size())) {
    name = types[static_cast<std::size_t>(type_index)];
  }
  return SyntaxScopeForLspTokenType(name);
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

bool semantic_spans_plausible_for_line(const std::vector<SemanticTokenSpan>& spans, int line_len) {
  if (line_len <= 0) {
    return spans.empty();
  }
  for (const SemanticTokenSpan& span : spans) {
    if (span.length <= 0 || span.start_col < 0 || span.start_col >= line_len) {
      return false;
    }
    if (span.start_col + span.length > line_len) {
      return false;
    }
  }
  return true;
}

const CachedSyntaxLineSpans* cached_syntax_spans_for_line(
    SyntaxHighlightContext* ctx, int line_index, const SemanticTokenDocument* semantic_tokens,
    int col_offset, int display_len) {
  if (ctx == nullptr || ctx->lines == nullptr || ctx->line_span_cache == nullptr ||
      line_index < 0 || line_index >= static_cast<int>(ctx->lines->size())) {
    return nullptr;
  }
  const std::string& source_line = ctx->lines->at(line_index);
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  ctx->tree_sitter_highlights();
  const std::vector<SemanticTokenSpan>* semantic_line_spans =
      (semantic_tokens != nullptr && line_index < static_cast<int>(semantic_tokens->lines.size()))
          ? &semantic_tokens->lines[static_cast<std::size_t>(line_index)]
          : nullptr;
  const uint64_t semantic_line_hash = hash_semantic_token_line(semantic_line_spans);
  const uint64_t key =
      syntax_line_span_cache_key(line_index, source_line, col_offset, display_len, ctx->ts_revision,
                                 semantic_line_hash, ctx->buffer_token);
  auto& cache = *ctx->line_span_cache;
  const auto it = cache.find(key);
  if (it != cache.end()) {
    return &it->second;
  }

  CachedSyntaxLineSpans entry;
  const LineHighlights* ts_source_hl = nullptr;
  if (line_uses_live_ts_overlay(ctx, line_index)) {
    ts_source_hl = live_line_highlights(ctx, line_index);
  }
  // Whether to even try semantic spans for this line is decided by the caller (it
  // passes semantic_tokens == nullptr for the one line whose text is actively changing
  // mid-typing-burst -- see the line_semantic ternary in editor_panel.cpp). It must NOT
  // also be gated on ctx->syntax_incremental here, or every *other* visible line would
  // lose its real semantic colors too just because typing started/stopped somewhere.
  if (semantic_tokens != nullptr && semantic_tokens->ready &&
      line_index < static_cast<int>(semantic_tokens->lines.size())) {
    const auto& line_spans = semantic_tokens->lines[static_cast<std::size_t>(line_index)];
    std::vector<SemanticTokenSpan> display = display_semantic_spans_for_line(
        line_spans, source_line, col_offset, display_len, tab_size);
    // The cached SemanticTokenDocument can be one edit behind the live buffer (the
    // editor keeps showing it across edits instead of blanking every line back to
    // tree-sitter the instant the doc's generation stops matching -- see the
    // semantic_tokens selection comment in editor_panel.cpp). Only trust its spans for
    // this line if they still plausibly fit the *current* text; otherwise fall back to
    // tree-sitter for just this one line until the next fetch lands.
    if (semantic_spans_plausible_for_line(display, display_len)) {
      entry.semantic_display_spans = std::move(display);
      entry.has_semantic = !entry.semantic_display_spans.empty();
    }
  }
  if (ts_source_hl == nullptr) {
    if (const auto* all_highlights = ctx->ts_line_highlights) {
      if (line_index >= 0 && line_index < static_cast<int>(all_highlights->size())) {
        ts_source_hl = &(*all_highlights)[static_cast<std::size_t>(line_index)];
      }
    }
  }
  if (ts_source_hl != nullptr) {
    entry.ts_display_spans =
        display_spans_for_line(*ts_source_hl, source_line, col_offset, display_len, tab_size);
  }
  const bool has_ts_spans = !entry.ts_display_spans.spans.empty();
  const bool stale_baseline_unmapped =
      ts_source_hl != nullptr && !ts_source_hl->spans.empty() && !has_ts_spans;
  const bool defer_empty_ts_cache =
      ctx->syntax_incremental && !line_uses_live_ts_overlay(ctx, line_index) && !has_ts_spans &&
      !source_line.empty() && ctx->ts_line_highlights != nullptr;
  if (defer_empty_ts_cache || stale_baseline_unmapped) {
    return nullptr;
  }
  if (line_uses_live_ts_overlay(ctx, line_index) && !has_ts_spans && !source_line.empty()) {
    return nullptr;
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
  const SyntaxScope scope = scope_for_token_type(types, span.type);
  const Decorator style = DecoratorForSyntaxScope(scope);
  if (line_cursor_col < 0 || !cursor_style) {
    return text(segment) | style;
  }

  const int span_view_start = span.start_col - display_col_offset;
  const int rel = line_cursor_col - span_view_start;
  if (rel < 0 || rel >= static_cast<int>(segment.size())) {
    return text(segment) | style;
  }

  const bool inverted_cursor = SyntaxScopeUsesInvertedCursor(scope);

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
  if (ctx == nullptr || ctx->lines == nullptr || ctx->lines->size() == 0) {
    return text(line);
  }
  if (line_uses_live_ts_overlay(ctx, line_index)) {
    if (const LineHighlights* live = live_line_highlights(ctx, line_index)) {
      const int tab_size = std::max(1, editor_indent::tab_display_width());
      const LineHighlights display_hl =
          display_spans_for_line(*live, ctx->lines->at(line_index), col_offset,
                                 static_cast<int>(line.size()), tab_size);
      if (!display_hl.spans.empty()) {
        return HighlightTreeSitterLine(line, line_index, display_hl, cursor_col, cursor_style, 0);
      }
    }
    return HighlightCodeLineLite(line, cursor_col, cursor_style);
  }
  const auto* all_highlights = ctx->tree_sitter_highlights();
  if (all_highlights == nullptr || line_index < 0 ||
      line_index >= static_cast<int>(all_highlights->size())) {
    return text(line);
  }
  const std::string& source_line = ctx->lines->at(line_index);
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const LineHighlights& source_hl = (*all_highlights)[static_cast<std::size_t>(line_index)];
  const LineHighlights display_hl =
      display_spans_for_line(source_hl, source_line, col_offset, static_cast<int>(line.size()),
                             tab_size);
  if (display_hl.spans.empty() && !line.empty()) {
    return HighlightCodeLineLite(line, cursor_col, cursor_style);
  }
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
    source_line = &ctx->lines->at(line_index);
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

bool keyword_lite(std::string_view word) {
  static constexpr const char* kWords[] = {
      "alignas",    "alignof",   "asm",         "auto",       "bool",       "break",
      "case",       "catch",     "char",        "class",      "const",      "constexpr",
      "continue",   "default",   "delete",      "do",         "double",     "else",
      "enum",       "explicit",  "extern",      "false",      "float",      "for",
      "friend",     "goto",      "if",          "inline",     "int",        "long",
      "mutable",    "namespace", "new",         "noexcept",   "nullptr",    "operator",
      "private",    "protected", "public",      "register",   "return",     "short",
      "signed",     "sizeof",    "static",      "struct",     "switch",     "template",
      "this",       "throw",     "true",        "try",        "typedef",    "typename",
      "union",      "unsigned",  "using",       "virtual",    "void",       "volatile",
      "while",      "concept",   "requires",    "co_await",   "co_return",  "co_yield",
      "import",     "module",    "consteval",   "constinit",  "char8_t",    "char16_t",
      "char32_t",   "wchar_t",   "static_cast", "dynamic_cast", "reinterpret_cast"};
  for (const char* kw : kWords) {
    if (word == kw) {
      return true;
    }
  }
  return false;
}

Element HighlightCodeLineLite(const std::string& line, int cursor_col, Decorator cursor_style) {
  if (line.empty()) {
    if (cursor_col == 0 && cursor_style) {
      return text(" ") | cursor_style;
    }
    return text(" ");
  }

  Elements parts;
  enum class Lex { kNormal, kLineComment, kBlockComment, kString, kChar };
  Lex lex = Lex::kNormal;
  std::size_t i = 0;
  while (i < line.size()) {
    const char c = line[i];
    if (lex == Lex::kLineComment) {
      const std::size_t start = i;
      i = line.size();
      parts.push_back(text(line.substr(start)) | color(theme::SyntaxComment()) | dim);
      break;
    }
    if (lex == Lex::kBlockComment) {
      const std::size_t start = i;
      while (i + 1 < line.size() && !(line[i] == '*' && line[i + 1] == '/')) {
        ++i;
      }
      if (i + 1 < line.size()) {
        i += 2;
      } else {
        i = line.size();
      }
      parts.push_back(text(line.substr(start, i - start)) | color(theme::SyntaxComment()) | dim);
      lex = Lex::kNormal;
      continue;
    }
    if (lex == Lex::kString || lex == Lex::kChar) {
      const char quote = (lex == Lex::kString) ? '"' : '\'';
      const std::size_t start = i;
      ++i;
      while (i < line.size()) {
        if (line[i] == '\\' && i + 1 < line.size()) {
          i += 2;
          continue;
        }
        if (line[i] == quote) {
          ++i;
          break;
        }
        ++i;
      }
      parts.push_back(text(line.substr(start, i - start)) | color(theme::SyntaxString()));
      lex = Lex::kNormal;
      continue;
    }

    if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
      lex = Lex::kLineComment;
      continue;
    }
    if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
      parts.push_back(text("/*") | color(theme::SyntaxComment()) | dim);
      i += 2;
      lex = Lex::kBlockComment;
      continue;
    }
    if (c == '"') {
      lex = Lex::kString;
      continue;
    }
    if (c == '\'') {
      lex = Lex::kChar;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      const std::size_t start = i;
      ++i;
      while (i < line.size() &&
             (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '.' || line[i] == 'x' ||
              line[i] == 'X' || line[i] == 'u' || line[i] == 'U' || line[i] == 'l' ||
              line[i] == 'L')) {
        ++i;
      }
      parts.push_back(text(line.substr(start, i - start)) | color(theme::SyntaxNumber()));
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      const std::size_t start = i;
      ++i;
      while (i < line.size() &&
             (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
        ++i;
      }
      const std::string_view word(line.data() + start, i - start);
      if (keyword_lite(word)) {
        parts.push_back(text(std::string(word)) | color(theme::SyntaxKeyword()) | bold);
      } else {
        parts.push_back(text(std::string(word)));
      }
      continue;
    }

    const int visual_col = static_cast<int>(i);
    if (cursor_col == visual_col && cursor_style) {
      parts.push_back(text(std::string(1, c)) | cursor_style);
    } else {
      parts.push_back(text(std::string(1, c)));
    }
    ++i;
  }

  if (parts.empty()) {
    return text(line);
  }
  return hbox(std::move(parts));
}

Element HighlightCodeLine(const std::string& line, int line_index,
                          const SemanticTokenDocument* semantic_tokens, int cursor_col,
                          Decorator cursor_style, int col_offset,
                          const SyntaxHighlightContext* ctx) {
  SyntaxHighlightContext* mutable_ctx = const_cast<SyntaxHighlightContext*>(ctx);
  if (mutable_ctx != nullptr && mutable_ctx->line_span_cache != nullptr) {
    if (const CachedSyntaxLineSpans* cached = cached_syntax_spans_for_line(
            mutable_ctx, line_index, semantic_tokens, col_offset, static_cast<int>(line.size()))) {
      const bool use_semantic =
          cached->has_semantic && semantic_tokens != nullptr && semantic_tokens->ready;
      if (use_semantic) {
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

  const std::string& source_line = ctx->lines->at(line_index);
  const int tab_size = std::max(1, editor_indent::tab_display_width());
  const auto& line_spans = semantic_tokens->lines[static_cast<std::size_t>(line_index)];
  const auto display_spans = display_semantic_spans_for_line(
      line_spans, source_line, col_offset, static_cast<int>(line.size()), tab_size);
  if (display_spans.empty() ||
      !semantic_spans_plausible_for_line(display_spans, static_cast<int>(line.size()))) {
    return highlight_tree_sitter_gap(line, line_index, mutable_ctx, cursor_col, cursor_style,
                                     col_offset, semantic_tokens);
  }

  return highlight_semantic_line(line, line_index, display_spans, semantic_tokens->token_types,
                                 cursor_col, cursor_style, col_offset, mutable_ctx,
                                 semantic_tokens);
}

}  // namespace tgdb
