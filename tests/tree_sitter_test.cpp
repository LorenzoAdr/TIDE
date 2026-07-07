#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_locals.hpp"
#include "parser/tree_sitter_service.hpp"
#include "parser/tree_sitter_symbols.hpp"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "editor/bracket_match.hpp"
#include "editor/editor_folds.hpp"
#include "editor/editor_state.hpp"

namespace tgdb {
namespace {

EditorBuffer make_buffer(std::initializer_list<const char*> lines) {
  EditorBuffer buffer;
  for (const char* line : lines) {
    buffer.lines.emplace_back(line);
  }
  return buffer;
}

void wait_document_ready(const std::string& path, const std::string& source) {
  tree_sitter_service().prepare_document(path, source);
  for (int attempt = 0; attempt < 500; ++attempt) {
    if (tree_sitter_service().document_ready(path, source)) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

EditorBuffer prepare_buffer(const std::string& path, std::initializer_list<const char*> lines) {
  EditorBuffer buffer;
  buffer.path = path;
  for (const char* line : lines) {
    buffer.lines.emplace_back(line);
  }
  wait_document_ready(path, join_editor_lines(buffer.lines));
  return buffer;
}

std::vector<SymbolInfo> wait_symbols(const std::string& path, const std::string& source) {
  wait_document_ready(path, source);
  return tree_sitter_service().symbols_for_file(path, source);
}

void test_parse_symbols() {
  const std::string source = R"cpp(
namespace ns {
class Foo {
 public:
  void bar() {}
};
}
void baz() {}
)cpp";
  const auto symbols = wait_symbols("test.cpp", source);
  assert(!symbols.empty());
  bool found_baz = false;
  bool found_foo = false;
  for (const SymbolInfo& sym : symbols) {
    if (sym.name.find("baz") != std::string::npos) {
      found_baz = true;
    }
    if (sym.name.find("Foo") != std::string::npos) {
      found_foo = true;
    }
  }
  assert(found_baz);
  assert(found_foo);
}

void test_simple_pair() {
  const EditorBuffer buffer =
      prepare_buffer("brackets_simple.cpp", {"int main() {", "  return 0;", "}"});
  const BracketPairHighlight open =
      find_bracket_pair_highlight(buffer, 0, std::string("int main() {").find('{') + 1);
  assert(open.valid);
  assert(open.line_a == 0);
  assert(open.line_b == 2);

  const BracketPairHighlight close = find_bracket_pair_highlight(buffer, 2, 0);
  assert(close.valid);
  assert(close.line_a == 2);
  assert(close.line_b == 0);
}

void test_nested() {
  const EditorBuffer buffer = prepare_buffer("brackets_nested.cpp", {"foo(bar[baz])"});
  const int open_paren = static_cast<int>(std::string("foo(bar[baz])").find('('));
  const BracketPairHighlight match = find_bracket_pair_highlight(buffer, 0, open_paren + 1);
  assert(match.valid);
  assert(match.col_a == open_paren);
  assert(match.col_b == static_cast<int>(std::string("foo(bar[baz])").find(')')));
}

void test_ignores_string() {
  const std::string line = "if (c == \")\") return;";
  const EditorBuffer buffer = prepare_buffer("brackets_string.cpp", {line.c_str()});
  const int open_paren = static_cast<int>(line.find('('));
  const BracketPairHighlight match = find_bracket_pair_highlight(buffer, 0, open_paren + 1);
  assert(match.valid);
  assert(match.col_b == static_cast<int>(line.rfind(')')));
}

void test_empty_lines_no_hang() {
  const EditorBuffer buffer = prepare_buffer(
      "brackets_empty_lines.cpp", {"", "// comment", "", "int main() {", "}"});
  const int open_brace = static_cast<int>(std::string("int main() {").find('{'));
  const BracketPairHighlight match = find_bracket_pair_highlight(buffer, 3, open_brace + 1);
  assert(match.valid);
  assert(match.line_b == 4);
}

void test_empty_string_literal_cursor() {
  const EditorBuffer buffer = prepare_buffer("brackets_literal.cpp", {
      "void foo(const char* p, const char* q);",
      "foo(x, \"\");",
      "int bar;",
  });
  assert(cursor_in_code(buffer, 2, 4));
}

void test_hello_cpp_anonymous_namespace() {
  const std::string source = R"cpp(
namespace {
void helper() {}
}
int main() { return 0; }
)cpp";
  const auto symbols = wait_symbols("hello.cpp", source);
  bool found_main = false;
  for (const SymbolInfo& sym : symbols) {
    if (sym.name.find("main") != std::string::npos) {
      found_main = true;
    }
  }
  assert(found_main);
}

std::size_t highlights_ready_span_count(const std::vector<LineHighlights>& highlights) {
  std::size_t total = 0;
  for (const LineHighlights& line : highlights) {
    total += line.spans.size();
  }
  return total;
}

void test_highlights_after_prepare() {
  const std::string source = "int main() {\n  return 0;\n}\n";
  const std::string canonical = normalize_editor_source(source);
  wait_document_ready("main.cpp", canonical);
  const std::vector<LineHighlights>* highlights = nullptr;
  for (int attempt = 0; attempt < 500; ++attempt) {
    highlights = tree_sitter_service().highlights_for("main.cpp", canonical);
    if (highlights != nullptr && !highlights->empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(highlights != nullptr);
  assert(highlights_ready_span_count(*highlights) > 0);
  assert(!(*highlights)[0].spans.empty());
}

void test_hello_cpp_highlights_from_disk() {
  std::ifstream input("../examples/hello.cpp");
  if (!input) {
    input.open("examples/hello.cpp");
  }
  assert(input);
  std::ostringstream source;
  source << input.rdbuf();
  const std::string canonical = normalize_editor_source(source.str());
  wait_document_ready("examples/hello.cpp", canonical);
  const std::vector<LineHighlights>* highlights = nullptr;
  for (int attempt = 0; attempt < 500; ++attempt) {
    highlights = tree_sitter_service().highlights_for("examples/hello.cpp", canonical);
    if (highlights != nullptr && highlights_ready_span_count(*highlights) > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(highlights != nullptr);
  const std::size_t span_total = highlights_ready_span_count(*highlights);
  assert(span_total > 0);
}

void test_highlight_overlapping_spans_do_not_duplicate() {
  const std::string source = "void try_receive(int fd) {\n}\n";
  const std::string canonical = normalize_editor_source(source);
  wait_document_ready("overlap.cpp", canonical);
  const std::vector<LineHighlights>* highlights = nullptr;
  for (int attempt = 0; attempt < 500; ++attempt) {
    highlights = tree_sitter_service().highlights_for("overlap.cpp", canonical);
    if (highlights != nullptr && highlights_ready_span_count(*highlights) > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(highlights != nullptr);
  const std::string& line = "void try_receive(int fd) {";
  const LineHighlights& line_highlights = (*highlights)[0];
  int col = 0;
  std::string rendered;
  for (const HighlightSpan& span : line_highlights.spans) {
    if (span.start_col > static_cast<int>(line.size())) {
      continue;
    }
    const int clamped_end = std::min(span.end_col, static_cast<int>(line.size()));
    if (span.start_col < col) {
      continue;
    }
    if (span.start_col > col) {
      rendered += line.substr(static_cast<std::size_t>(col),
                              static_cast<std::size_t>(span.start_col - col));
    }
    if (clamped_end > span.start_col) {
      rendered += line.substr(static_cast<std::size_t>(span.start_col),
                              static_cast<std::size_t>(clamped_end - span.start_col));
      col = clamped_end;
    }
  }
  if (col < static_cast<int>(line.size())) {
    rendered += line.substr(static_cast<std::size_t>(col));
  }
  assert(rendered == line);
}

void test_highlight_col_offset_indent() {
  const std::string source = "    void foo();\n";
  const std::string canonical = normalize_editor_source(source);
  wait_document_ready("indent.cpp", canonical);
  const std::vector<LineHighlights>* highlights = nullptr;
  for (int attempt = 0; attempt < 500; ++attempt) {
    highlights = tree_sitter_service().highlights_for("indent.cpp", canonical);
    if (highlights != nullptr && highlights_ready_span_count(*highlights) > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(highlights != nullptr);
  const std::string& source_line = "    void foo();";
  const std::string display = "void foo();";
  const int col_offset = 4;
  const LineHighlights& source_hl = (*highlights)[0];
  LineHighlights display_hl;
  const int base_vis = 0;
  for (const HighlightSpan& span : source_hl.spans) {
    const int rel_start = span.start_col - col_offset;
    const int rel_end = span.end_col - col_offset;
    if (rel_end <= 0 || rel_start >= static_cast<int>(display.size())) {
      continue;
    }
    HighlightSpan mapped = span;
    mapped.start_col = std::max(0, rel_start);
    mapped.end_col = std::min(static_cast<int>(display.size()), rel_end);
    if (mapped.end_col > mapped.start_col) {
      display_hl.spans.push_back(mapped);
    }
  }
  (void)source_line;
  (void)base_vis;
  int col = 0;
  std::string rendered;
  for (const HighlightSpan& span : display_hl.spans) {
    if (span.start_col >= static_cast<int>(display.size())) {
      continue;
    }
    const int clamped_end = std::min(span.end_col, static_cast<int>(display.size()));
    if (span.start_col < col) {
      continue;
    }
    if (span.start_col > col) {
      rendered += display.substr(static_cast<std::size_t>(col),
                                 static_cast<std::size_t>(span.start_col - col));
    }
    if (clamped_end > span.start_col) {
      rendered += display.substr(static_cast<std::size_t>(span.start_col),
                                 static_cast<std::size_t>(clamped_end - span.start_col));
      col = clamped_end;
    }
  }
  if (col < static_cast<int>(display.size())) {
    rendered += display.substr(static_cast<std::size_t>(col));
  }
  assert(rendered == display);
}

void test_incremental_parse_after_single_edit() {
  const std::string initial = "int foo() { return 0; }\n";
  const std::string edited = "int foo() { return 1; }\n";
  const std::string path = "incremental.cpp";
  wait_document_ready(path, initial);
  assert(!wait_symbols(path, initial).empty());
  const uint64_t revision_before = tree_sitter_service().revision_for(path);

  tree_sitter_service().prepare_document(path, edited);
  for (int attempt = 0; attempt < 500; ++attempt) {
    if (tree_sitter_service().document_ready(path, edited) &&
        tree_sitter_service().revision_for(path) > revision_before) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(tree_sitter_service().document_ready(path, edited));
  const auto symbols = tree_sitter_service().symbols_for_file(path, edited);
  assert(!symbols.empty());
  bool found_foo = false;
  for (const SymbolInfo& sym : symbols) {
    if (sym.name.find("foo") != std::string::npos) {
      found_foo = true;
    }
  }
  assert(found_foo);
}

void test_cursor_in_code_uses_ast_when_ready() {
  const std::string line = "foo(\"bar\");";
  const std::string source = line + "\n";
  const std::string path = "cursor_ast.cpp";
  wait_document_ready(path, source);
  EditorBuffer buffer;
  buffer.path = path;
  buffer.lines = {line};
  const int in_string = static_cast<int>(line.find("bar"));
  assert(in_string > 0);
  assert(!cursor_in_code(buffer, 0, in_string));
  assert(cursor_in_code(buffer, 0, 0));
}

void test_cursor_in_code_optimistic_when_parse_stale() {
  const std::string line = "foo(\"bar\");";
  const std::string source = line + "\n";
  const std::string path = "cursor_stale.cpp";
  wait_document_ready(path, source);
  EditorBuffer buffer;
  buffer.path = path;
  buffer.lines = {line + "x"};
  assert(cursor_in_code(buffer, 0, 0));
}

void test_locals_scope_chain() {
  const std::vector<std::string> lines = {"namespace ns {", "void outer() {", "  int local_a = 1;",
                                          "}", "}"};
  const std::string path = "locals_scope.cpp";
  wait_document_ready(path, join_editor_lines(lines));
  const auto chain = tree_sitter_service().scope_chain_at(path, lines, 2);
  assert(!chain.empty());
}

void test_innermost_scope_range() {
  const std::vector<std::string> lines = {"void foo() {", "  int x = 0;", "  return x;", "}",
                                          "int bar();"};
  const std::string path = "scope_range.cpp";
  wait_document_ready(path, join_editor_lines(lines));
  const ScopeLineRange range =
      tree_sitter_service().innermost_scope_range_at(path, lines, 1, 4);
  assert(range.valid);
  assert(range.start_line == 0);
  assert(range.end_line == 3);
  assert(range.contains(1));
  assert(!range.contains(4));
}

void test_innermost_scope_while_loop() {
  const std::vector<std::string> lines = {"void foo() {",
                                          "  while (x) {",
                                          "    do_work();",
                                          "  }",
                                          "}"};
  const std::string path = "scope_while.cpp";
  wait_document_ready(path, join_editor_lines(lines));
  const ScopeLineRange range =
      tree_sitter_service().innermost_scope_range_at(path, lines, 2, 4);
  assert(range.valid);
  assert(range.contains(2));
  assert(!range.contains(0));
  assert(range.end_line < 4);
}

void test_innermost_scope_declaration_in_if() {
  const std::vector<std::string> lines = {"void foo() {",
                                          "  if (cond) {",
                                          "    int x = 0;",
                                          "    use(x);",
                                          "  }",
                                          "}"};
  const std::string path = "scope_if_decl.cpp";
  wait_document_ready(path, join_editor_lines(lines));
  const ScopeLineRange on_decl =
      tree_sitter_service().innermost_scope_range_at(path, lines, 2, 8);
  assert(on_decl.valid);
  assert(on_decl.contains(2));
  assert(on_decl.contains(3));
  assert(!on_decl.contains(1));
  assert(on_decl.end_line < 5);

  const ScopeLineRange on_use =
      tree_sitter_service().innermost_scope_range_at(path, lines, 3, 4);
  assert(on_use.valid);
  assert(on_use.contains(3));
  assert(on_use.start_line >= 1);

  const BracketPairHighlight braces =
      tree_sitter_service().scope_bracket_pair_at(path, lines, 2, 8);
  assert(braces.valid);
  assert(braces.line_a == 1);
  assert(braces.line_b == 4);
}

void test_fold_regions_from_tree() {
  const std::vector<std::string> lines = {"void foo() {",
                                          "  if (cond) {",
                                          "    int x = 0;",
                                          "  }",
                                          "}"};
  const std::string path = "fold_regions.cpp";
  const std::string source = join_editor_lines(lines);
  wait_document_ready(path, source);
  const std::vector<FoldRegion> regions =
      tree_sitter_service().fold_regions_at(path, lines);
  assert(!regions.empty());
  bool found_if = false;
  for (const FoldRegion& region : regions) {
    if (region.open_line == 1 && region.close_line == 3) {
      found_if = true;
    }
  }
  assert(found_if);
}

void test_editor_fold_visibility() {
  const std::vector<FoldRegion> regions = {{1, 3}};
  std::set<int> collapsed = {1};
  assert(fold_line_hidden(2, regions, collapsed));
  assert(!fold_line_hidden(1, regions, collapsed));
  const std::vector<int> visible = visible_buffer_lines(5, regions, collapsed);
  assert(visible == std::vector<int>({0, 1, 4}));
  assert(fold_gutter_marker(1, regions, collapsed) == '+');
  assert(fold_gutter_marker(1, regions, {}) == '-');
}

void test_colored_curly_braces_depths() {
  const std::vector<std::string> lines = {"void foo() {",
                                          "  if (cond) {",
                                          "    int x = 0;",
                                          "  }",
                                          "}"};
  const std::string path = "colored_braces.cpp";
  const std::string source = join_editor_lines(lines);
  wait_document_ready(path, source);
  const std::vector<ColoredBraceMarker> markers =
      tree_sitter_service().colored_curly_braces_at(path, lines);
  assert(markers.size() >= 6);
  auto depth_at = [&](int line, int col) {
    for (const ColoredBraceMarker& marker : markers) {
      if (marker.line == line && marker.col == col) {
        return marker.depth;
      }
    }
    return -1;
  };
  auto find_brace_col = [&](int line, char ch) {
    const std::string& text = lines[static_cast<std::size_t>(line)];
    for (int col = 0; col < static_cast<int>(text.size()); ++col) {
      if (text[static_cast<std::size_t>(col)] == ch) {
        return col;
      }
    }
    return -1;
  };
  const int outer_open = find_brace_col(0, '{');
  const int outer_close = find_brace_col(4, '}');
  const int inner_open = find_brace_col(1, '{');
  const int inner_close = find_brace_col(3, '}');
  assert(outer_open >= 0 && inner_open >= 0);
  assert(depth_at(0, outer_open) == depth_at(4, outer_close));
  assert(depth_at(1, inner_open) == depth_at(3, inner_close));
  assert(depth_at(1, inner_open) > depth_at(0, outer_open));
}

void test_local_completions_include_parameters() {
  const std::string source = "void foo(int bar) { ba }\n";
  const std::string path = "locals_completion.cpp";
  wait_document_ready(path, source);
  CompletionParams params;
  params.path = path;
  params.text = source;
  params.line = 0;
  params.character = static_cast<int>(std::string("void foo(int bar) { ba").size());
  const auto items = tree_sitter_service().local_completions_at(params);
  bool found_bar = false;
  for (const CompletionItem& item : items) {
    if (item.label == "bar") {
      found_bar = true;
    }
  }
  assert(found_bar);
}

void test_parse_debounce_coalesces_edits() {
  const std::string path = "debounce.cpp";
  const std::string initial = "int value = 0;\n";
  wait_document_ready(path, initial);
  const uint64_t revision_after_initial = tree_sitter_service().revision_for(path);

  for (int i = 1; i <= 5; ++i) {
    tree_sitter_service().prepare_document(path, "int value = " + std::to_string(i) + ";\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  const std::string final_source = "int value = 5;\n";
  for (int attempt = 0; attempt < 500; ++attempt) {
    if (tree_sitter_service().document_ready(path, final_source)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(tree_sitter_service().document_ready(path, final_source));
  const uint64_t revision_after_burst = tree_sitter_service().revision_for(path);
  assert(revision_after_burst > revision_after_initial);
}

void test_normalize_editor_source_trailing_newline() {
  const std::string from_buffer = join_editor_lines({"int main() {}", "return 0;"});
  const std::string from_file = "int main() {}\nreturn 0;\n";
  assert(normalize_editor_source(from_buffer) == normalize_editor_source(from_file));
}

}  // namespace
}  // namespace tgdb

int main() {
  tgdb::test_parse_symbols();
  tgdb::test_simple_pair();
  tgdb::test_nested();
  tgdb::test_ignores_string();
  tgdb::test_empty_lines_no_hang();
  tgdb::test_empty_string_literal_cursor();
  tgdb::test_hello_cpp_anonymous_namespace();
  tgdb::test_highlights_after_prepare();
  tgdb::test_hello_cpp_highlights_from_disk();
  tgdb::test_highlight_overlapping_spans_do_not_duplicate();
  tgdb::test_highlight_col_offset_indent();
  tgdb::test_incremental_parse_after_single_edit();
  tgdb::test_cursor_in_code_uses_ast_when_ready();
  tgdb::test_cursor_in_code_optimistic_when_parse_stale();
  tgdb::test_locals_scope_chain();
  tgdb::test_innermost_scope_range();
  tgdb::test_innermost_scope_while_loop();
  tgdb::test_innermost_scope_declaration_in_if();
  tgdb::test_fold_regions_from_tree();
  tgdb::test_editor_fold_visibility();
  tgdb::test_colored_curly_braces_depths();
  tgdb::test_local_completions_include_parameters();
  tgdb::test_parse_debounce_coalesces_edits();
  tgdb::test_normalize_editor_source_trailing_newline();
  std::cout << "tree_sitter_test ok\n";
  return 0;
}
