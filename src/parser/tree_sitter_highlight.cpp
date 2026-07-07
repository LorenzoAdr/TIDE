#include "parser/tree_sitter_highlight.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

#include "ftxui/dom/elements.hpp"
#include "parser/tree_sitter_language.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

const char* kEmbeddedHighlightsQuery = R"scm(
[
  "alignas"
  "alignof"
  "asm"
  "break"
  "case"
  "catch"
  "class"
  "co_await"
  "co_return"
  "co_yield"
  "const"
  "constexpr"
  "consteval"
  "constinit"
  "continue"
  "decltype"
  "default"
  "delete"
  "do"
  "else"
  "enum"
  "explicit"
  "extern"
  "final"
  "for"
  "friend"
  "goto"
  "if"
  "inline"
  "mutable"
  "namespace"
  "new"
  "noexcept"
  "operator"
  "override"
  "private"
  "protected"
  "public"
  "register"
  "return"
  "static"
  "static_assert"
  "struct"
  "switch"
  "template"
  "thread_local"
  "throw"
  "try"
  "typedef"
  "typename"
  "union"
  "using"
  "virtual"
  "volatile"
  "while"
  "concept"
  "requires"
] @keyword
"nullptr" @constant
(null) @constant
(this) @variable.builtin
(comment) @comment
(raw_string_literal) @string
(string_literal) @string
(char_literal) @string
(number_literal) @number
(preproc_def) @macro
(preproc_function_def) @macro
(preproc_include) @macro
(preproc_call) @macro
(preproc_arg) @macro
(preproc_directive) @macro
(preproc_if) @macro
(preproc_elif) @macro
(preproc_else) @macro
(auto) @type
(primitive_type) @type
(type_identifier) @type
(namespace_identifier) @namespace
(class_specifier name: (_) @type)
(struct_specifier name: (_) @type)
(enum_specifier name: (_) @type)
(function_declarator declarator: (identifier) @function)
(function_declarator declarator: (field_identifier) @function)
(function_declarator declarator: (qualified_identifier name: (_) @function))
(call_expression function: (identifier) @function)
(call_expression function: (qualified_identifier name: (_) @function))
(field_identifier) @property
(identifier) @variable
(parameter_declaration declarator: (_) @parameter)
)scm";

TSQuery* highlight_query() {
  static TSQuery* query = nullptr;
  static uint32_t error_offset = 0;
  static TSQueryError error_type = TSQueryErrorNone;
  if (query == nullptr) {
    query = ts_query_new(tree_sitter_cpp_language(), kEmbeddedHighlightsQuery,
                         static_cast<uint32_t>(std::strlen(kEmbeddedHighlightsQuery)), &error_offset,
                         &error_type);
  }
  return query;
}

Decorator style_for_capture(const std::string& capture) {
  if (capture == "comment") {
    return color(theme::SyntaxComment()) | dim;
  }
  if (capture == "string") {
    return color(theme::SyntaxString());
  }
  if (capture == "number") {
    return color(theme::SyntaxNumber());
  }
  if (capture == "keyword") {
    return color(theme::SyntaxKeyword()) | bold;
  }
  if (capture == "macro") {
    return color(theme::SyntaxMacro());
  }
  if (capture == "namespace") {
    return color(theme::SyntaxNamespace());
  }
  if (capture == "type") {
    return color(theme::SyntaxType());
  }
  if (capture == "function") {
    return color(theme::SyntaxFunction());
  }
  if (capture == "parameter") {
    return color(theme::SyntaxParameter());
  }
  if (capture == "property") {
    return color(theme::SyntaxProperty());
  }
  if (capture == "constant" || capture == "variable.builtin") {
    return color(theme::SyntaxKeyword()) | bold;
  }
  if (capture == "variable") {
    return color(theme::SyntaxVariable());
  }
  return color(theme::SyntaxDefault());
}

std::size_t line_begin_offset(const std::string& source, int line_0) {
  std::size_t pos = 0;
  for (int row = 0; row < line_0; ++row) {
    const std::size_t next = source.find('\n', pos);
    if (next == std::string::npos) {
      return source.size();
    }
    pos = next + 1;
  }
  return pos;
}

std::size_t line_end_offset(const std::string& source, int line_0) {
  const std::size_t begin = line_begin_offset(source, line_0);
  const std::size_t next = source.find('\n', begin);
  return next == std::string::npos ? source.size() : next;
}

void emit_segment(Elements* out, const std::string& segment, Decorator style, int global_offset,
                  int cursor_col, Decorator cursor_style, bool keyword_token = false) {
  if (segment.empty()) {
    return;
  }
  const int len = static_cast<int>(segment.size());
  if (cursor_col < 0 || !cursor_style || cursor_col < global_offset ||
      cursor_col >= global_offset + len) {
    Element element = text(segment);
    if (style) {
      element = element | style;
    }
    out->push_back(std::move(element));
    return;
  }
  const int rel = cursor_col - global_offset;
  auto emit_part = [&](const std::string& part) {
    if (part.empty()) {
      return;
    }
    Element element = text(part);
    if (style) {
      element = element | style;
    }
    out->push_back(std::move(element));
  };
  emit_part(segment.substr(0, static_cast<std::size_t>(rel)));
  Element cursor_cell = text(segment.substr(static_cast<std::size_t>(rel), 1));
  if (keyword_token) {
    out->push_back(cursor_cell | inverted | bold);
  } else {
    out->push_back(cursor_cell | cursor_style);
  }
  emit_part(segment.substr(static_cast<std::size_t>(rel + 1)));
}

}  // namespace

int count_source_lines(const std::string& source) {
  if (source.empty()) {
    return 1;
  }
  int lines = 1;
  for (char ch : source) {
    if (ch == '\n') {
      ++lines;
    }
  }
  return lines;
}

std::vector<LineHighlights> highlights_for_document(TSNode root, const std::string& source) {
  const int line_count = count_source_lines(source);
  std::vector<LineHighlights> per_line(static_cast<std::size_t>(line_count));

  TSQuery* query = highlight_query();
  if (query == nullptr || ts_node_is_null(root)) {
    return per_line;
  }

  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, root);

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSNode capture_node = match.captures[i].node;
      const TSPoint start = ts_node_start_point(capture_node);
      const TSPoint end = ts_node_end_point(capture_node);
      const char* capture_name = nullptr;
      uint32_t capture_name_len = 0;
      capture_name =
          ts_query_capture_name_for_id(query, match.captures[i].index, &capture_name_len);

      const uint32_t first_row = start.row;
      const uint32_t last_row = end.row;
      for (uint32_t row = first_row; row <= last_row && row < static_cast<uint32_t>(line_count);
           ++row) {
        const std::size_t line_len =
            static_cast<std::size_t>(line_end_offset(source, static_cast<int>(row)) -
                                     line_begin_offset(source, static_cast<int>(row)));
        HighlightSpan span;
        span.start_col = row == first_row ? static_cast<int>(start.column) : 0;
        span.end_col = row == last_row ? static_cast<int>(end.column)
                                       : static_cast<int>(line_len);
        span.capture = capture_name != nullptr ? capture_name : "default";
        if (span.end_col > span.start_col) {
          per_line[static_cast<std::size_t>(row)].spans.push_back(span);
        }
      }
    }
  }
  ts_query_cursor_delete(cursor);

  for (LineHighlights& line : per_line) {
    std::sort(line.spans.begin(), line.spans.end(),
              [](const HighlightSpan& a, const HighlightSpan& b) {
                if (a.start_col != b.start_col) {
                  return a.start_col < b.start_col;
                }
                return a.end_col > b.end_col;
              });
  }
  return per_line;
}

LineHighlights highlights_for_line(TSNode root, const std::string& source, int line_0) {
  LineHighlights result;
  TSQuery* query = highlight_query();
  if (query == nullptr || ts_node_is_null(root)) {
    return result;
  }

  const std::size_t line_begin = line_begin_offset(source, line_0);
  const std::size_t line_end = line_end_offset(source, line_0);
  const int line_len = static_cast<int>(line_end - line_begin);

  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, root);

  TSQueryMatch match;
  const uint32_t target_row = static_cast<uint32_t>(line_0);
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSNode capture_node = match.captures[i].node;
      const TSPoint start = ts_node_start_point(capture_node);
      const TSPoint end = ts_node_end_point(capture_node);
      if (start.row > target_row || end.row < target_row) {
        continue;
      }
      const char* capture_name = nullptr;
      uint32_t capture_name_len = 0;
      capture_name =
          ts_query_capture_name_for_id(query, match.captures[i].index, &capture_name_len);
      HighlightSpan span;
      span.start_col = start.row == target_row ? static_cast<int>(start.column) : 0;
      span.end_col = end.row == target_row ? static_cast<int>(end.column) : line_len;
      span.capture = capture_name != nullptr ? capture_name : "default";
      if (span.end_col > span.start_col) {
        result.spans.push_back(span);
      }
    }
  }
  ts_query_cursor_delete(cursor);

  std::sort(result.spans.begin(), result.spans.end(),
            [](const HighlightSpan& a, const HighlightSpan& b) {
              if (a.start_col != b.start_col) {
                return a.start_col < b.start_col;
              }
              return a.end_col > b.end_col;
            });
  return result;
}

Element HighlightTreeSitterLine(const std::string& line, int line_index,
                                const LineHighlights& highlights, int cursor_col,
                                Decorator cursor_style, int col_offset) {
  if (highlights.spans.empty()) {
    return text(line);
  }

  Elements parts;
  int col = 0;
  for (const HighlightSpan& span : highlights.spans) {
    if (span.start_col > static_cast<int>(line.size())) {
      continue;
    }
    const int clamped_end = std::min(span.end_col, static_cast<int>(line.size()));
    if (span.start_col < col) {
      continue;
    }
    if (span.start_col > col) {
      emit_segment(&parts, line.substr(static_cast<std::size_t>(col),
                                       static_cast<std::size_t>(span.start_col - col)),
                   Decorator{}, col, cursor_col, cursor_style);
    }
    if (clamped_end > span.start_col) {
      const bool keyword = span.capture == "keyword";
      emit_segment(&parts,
                   line.substr(static_cast<std::size_t>(span.start_col),
                               static_cast<std::size_t>(clamped_end - span.start_col)),
                   style_for_capture(span.capture), span.start_col, cursor_col, cursor_style,
                   keyword);
      col = clamped_end;
    }
  }
  if (col < static_cast<int>(line.size())) {
    emit_segment(&parts, line.substr(static_cast<std::size_t>(col)), Decorator{}, col, cursor_col,
                 cursor_style);
  }
  return parts.empty() ? text(line) : hbox(std::move(parts));
}

Element HighlightTreeSitterLine(const std::string& line, int line_index, const std::string& source,
                                TSNode root, int cursor_col, Decorator cursor_style,
                                int col_offset) {
  (void)source;
  return HighlightTreeSitterLine(line, line_index, highlights_for_line(root, source, line_index),
                                 cursor_col, cursor_style, col_offset);
}

}  // namespace tgdb
