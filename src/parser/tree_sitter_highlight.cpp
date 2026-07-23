#include "parser/tree_sitter_highlight.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>

#include "ftxui/dom/elements.hpp"
#include "tree_sitter_grammar_queries.gen.hpp"
#include "parser/tree_sitter_language.hpp"
#include "util/syntax_highlight.hpp"
#include "util/syntax_scope.hpp"

namespace tuide {

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

const char* kEmbeddedPythonHighlightsQuery = R"scm(
; Tree-sitter highlight query for Python (tree-sitter-python).
; No #match? predicates — the C client does not evaluate them.

(identifier) @variable

(decorator) @function
(decorator
  (identifier) @function)

(call
  function: (attribute attribute: (identifier) @function.method))
(call
  function: (identifier) @function)

(function_definition
  name: (identifier) @function)

(class_definition
  name: (identifier) @type)

(attribute attribute: (identifier) @property)
(type (identifier) @type)

[
  (none)
  (true)
  (false)
] @constant.builtin

[
  (integer)
  (float)
] @number

(comment) @comment
(string) @string
(escape_sequence) @escape

[
  "as"
  "assert"
  "async"
  "await"
  "break"
  "class"
  "continue"
  "def"
  "del"
  "elif"
  "else"
  "except"
  "finally"
  "for"
  "from"
  "global"
  "if"
  "import"
  "lambda"
  "nonlocal"
  "pass"
  "raise"
  "return"
  "try"
  "while"
  "with"
  "yield"
  "match"
  "case"
  "and"
  "in"
  "is"
  "not"
  "or"
] @keyword

[
  "-"
  "-="
  "!="
  "*"
  "**"
  "**="
  "*="
  "/"
  "//"
  "//="
  "/="
  "&"
  "&="
  "%"
  "%="
  "^"
  "^="
  "+"
  "->"
  "+="
  "<"
  "<<"
  "<<="
  "<="
  "<>"
  "="
  ":="
  "=="
  ">"
  ">="
  ">>"
  ">>="
  "|"
  "|="
  "~"
  "@="
] @operator
)scm";

const char* kEmbeddedBashHighlightsQuery = R"scm(
; Tree-sitter highlight query for Bash (no #match? / #eq? predicates).

[
  (string)
  (raw_string)
  (ansi_c_string)
  (heredoc_body)
] @string

[
  (heredoc_start)
  (heredoc_end)
] @label

(comment) @comment
(number) @number
(test_operator) @operator

(command_name) @function
(function_definition name: (word) @function)

(simple_expansion) @variable
(expansion) @variable
(special_variable_name) @constant

[
  "if"
  "then"
  "else"
  "elif"
  "fi"
  "case"
  "in"
  "esac"
  "for"
  "do"
  "done"
  "select"
  "until"
  "while"
  "declare"
  "typeset"
  "readonly"
  "local"
  "unset"
  "unsetenv"
  "export"
  "function"
  "time"
  "coproc"
] @keyword

[
  ">"
  ">>"
  "<"
  "<<"
  "&&"
  "|"
  "|&"
  "||"
  "="
  "+="
  "=~"
  "=="
  "!="
  "!"
] @operator
)scm";

const char* kEmbeddedLatexHighlightsQuery = R"scm(
; Tree-sitter highlight query for LaTeX (no #match? / #eq? predicates).

[
  (comment)
  (line_comment)
  (block_comment)
] @comment

(command_name) @function

(begin
  command: _ @function.builtin
  name: (curly_group_text (text) @function.macro))

(end
  command: _ @function.builtin
  name: (curly_group_text (text) @function.macro))

(section
  command: _ @function.macro
  text: (_) @type)

(subsection
  command: _ @function.macro
  text: (_) @type)

(subsubsection
  command: _ @function.macro
  text: (_) @type)

(chapter
  command: _ @function.macro
  text: (_) @type)

(part
  command: _ @function.macro
  text: (_) @type)

(label_definition
  command: _ @function.macro
  name: (curly_group_text (_) @label))

(label_reference
  command: _ @function.macro
  names: (curly_group_text_list (_) @label))

(math_environment) @string
(inline_formula) @string
(displayed_equation) @string

[(operator) "="] @operator
["[" "]" "{" "}"] @punctuation.bracket
)scm";

TSQuery* highlight_query_for_lang(TreeSitterLangKind lang) {
  auto cached_query = [](TSQuery** slot, const TSLanguage* language, const char* source) -> TSQuery* {
    if (*slot == nullptr && source != nullptr && source[0] != '\0') {
      uint32_t error_offset = 0;
      TSQueryError error_type = TSQueryErrorNone;
      *slot = ts_query_new(language, source, static_cast<uint32_t>(std::strlen(source)), &error_offset,
                           &error_type);
    }
    return *slot;
  };

  if (lang == TreeSitterLangKind::kPython) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_python_language(), kEmbeddedPythonHighlightsQuery);
  }
  if (lang == TreeSitterLangKind::kBash) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_bash_language(), kEmbeddedBashHighlightsQuery);
  }
  if (lang == TreeSitterLangKind::kLatex) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_latex_language(), kEmbeddedLatexHighlightsQuery);
  }
  if (lang == TreeSitterLangKind::kRust) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_rust_language(), tree_sitter_queries::rust());
  }
  if (lang == TreeSitterLangKind::kGo) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_go_language(), tree_sitter_queries::go());
  }
  if (lang == TreeSitterLangKind::kZig) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_zig_language(), tree_sitter_queries::zig());
  }
  if (lang == TreeSitterLangKind::kFortran) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_fortran_language(), tree_sitter_queries::fortran());
  }
  if (lang == TreeSitterLangKind::kLua) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_lua_language(), tree_sitter_queries::lua());
  }
  if (lang == TreeSitterLangKind::kJavaScript) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_javascript_language(), tree_sitter_queries::javascript());
  }
  if (lang == TreeSitterLangKind::kTypeScript) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_typescript_language(), tree_sitter_queries::typescript());
  }
  if (lang == TreeSitterLangKind::kCmake) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_cmake_language(), tree_sitter_queries::cmake());
  }
  if (lang == TreeSitterLangKind::kMake) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_make_language(), tree_sitter_queries::make());
  }
  if (lang == TreeSitterLangKind::kYaml) {
    static TSQuery* query = nullptr;
    return cached_query(&query, tree_sitter_yaml_language(), tree_sitter_queries::yaml());
  }
  if (lang == TreeSitterLangKind::kNone) {
    return nullptr;
  }
  static TSQuery* query = nullptr;
  return cached_query(&query, tree_sitter_cpp_language(), kEmbeddedHighlightsQuery);
}

TSQuery* highlight_query() {
  return highlight_query_for_lang(TreeSitterLangKind::kCpp);
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
  if (rel < 0 || rel >= len) {
    Element element = text(segment);
    if (style) {
      element = element | style;
    }
    out->push_back(std::move(element));
    return;
  }
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

std::vector<LineHighlights> highlights_after_incremental_parse(
    TSTree* old_tree, TSTree* new_tree, TSNode new_root, const std::string& source,
    const std::vector<LineHighlights>& previous, int layout_shift_from_row,
    TreeSitterLangKind lang) {
  if (old_tree == nullptr || new_tree == nullptr || ts_node_is_null(new_root) || previous.empty()) {
    return highlights_for_document(new_root, source, lang);
  }

  const int line_count = count_source_lines(source);
  std::vector<LineHighlights> out(static_cast<std::size_t>(line_count));

  uint32_t range_count = 0;
  TSRange* ranges = ts_tree_get_changed_ranges(old_tree, new_tree, &range_count);

  std::set<int> affected;
  for (uint32_t i = 0; i < range_count; ++i) {
    const uint32_t first = ranges[i].start_point.row;
    const uint32_t last = ranges[i].end_point.row;
    for (uint32_t row = first; row <= last; ++row) {
      affected.insert(static_cast<int>(row));
    }
  }
  if (ranges != nullptr) {
    free(ranges);
  }

  const bool layout_shifted = layout_shift_from_row >= 0;
  std::vector<LineHighlights> fresh;
  if (layout_shifted) {
    // One query pass for the tail; per-line queries are O(lines * tree).
    fresh = highlights_for_document(new_root, source, lang);
  }

  for (int line = 0; line < line_count; ++line) {
    const bool needs_layout_refresh = layout_shifted && line >= layout_shift_from_row;
    if (!needs_layout_refresh && line < static_cast<int>(previous.size()) &&
        affected.count(line) == 0) {
      out[static_cast<std::size_t>(line)] = previous[static_cast<std::size_t>(line)];
    } else if (needs_layout_refresh) {
      out[static_cast<std::size_t>(line)] = fresh[static_cast<std::size_t>(line)];
    } else {
      out[static_cast<std::size_t>(line)] = highlights_for_line(new_root, source, line, lang);
    }
  }
  return out;
}

std::vector<LineHighlights> highlights_for_document(TSNode root, const std::string& source,
                                                    TreeSitterLangKind lang) {
  const int line_count = count_source_lines(source);
  std::vector<LineHighlights> per_line(static_cast<std::size_t>(line_count));

  TSQuery* query = highlight_query_for_lang(lang);
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

LineHighlights highlights_for_line(TSNode root, const std::string& source, int line_0,
                                   TreeSitterLangKind lang) {
  LineHighlights result;
  TSQuery* query = highlight_query_for_lang(lang);
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
    return HighlightCodeLineLite(line, cursor_col, cursor_style);
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
      const SyntaxScope scope = SyntaxScopeForTreeSitterCapture(span.capture);
      const bool inverted_cursor = SyntaxScopeUsesInvertedCursor(scope);
      emit_segment(&parts,
                   line.substr(static_cast<std::size_t>(span.start_col),
                               static_cast<std::size_t>(clamped_end - span.start_col)),
                   DecoratorForSyntaxScope(scope), span.start_col, cursor_col, cursor_style,
                   inverted_cursor);
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
                                int col_offset, TreeSitterLangKind lang) {
  (void)source;
  return HighlightTreeSitterLine(line, line_index,
                                 highlights_for_line(root, source, line_index, lang), cursor_col,
                                 cursor_style, col_offset);
}

}  // namespace tuide
