#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_locals.hpp"
#include "parser/tree_sitter_service.hpp"
#include "parser/tree_sitter_symbols.hpp"

#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "editor/bracket_match.hpp"
#include "editor/editor_buffer_source.hpp"
#include "editor/editor_folds.hpp"
#include "editor/editor_state.hpp"
#include "editor/text_ops.hpp"

namespace tuide {
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

void test_parse_python_symbols() {
  const std::string source = R"py(
class Foo:
    def bar(self):
        pass

def baz():
    def nested():
        pass
)py";
  const auto symbols = wait_symbols("test.py", source);
  assert(!symbols.empty());
  bool found_foo = false;
  bool found_bar = false;
  bool found_baz = false;
  bool found_nested = false;
  int bar_depth = -1;
  int nested_depth = -1;
  for (const SymbolInfo& sym : symbols) {
    if (sym.name.find("Foo") != std::string::npos) {
      found_foo = true;
      assert(sym.kind == SymbolKind::kClass);
    }
    if (sym.name.find("bar") != std::string::npos) {
      found_bar = true;
      bar_depth = sym.depth;
      assert(sym.kind == SymbolKind::kMethod);
    }
    if (sym.name.find("baz") != std::string::npos) {
      found_baz = true;
      assert(sym.kind == SymbolKind::kFunction);
    }
    if (sym.name.find("nested") != std::string::npos) {
      found_nested = true;
      nested_depth = sym.depth;
      assert(sym.kind == SymbolKind::kFunction);
    }
  }
  assert(found_foo);
  assert(found_bar);
  assert(found_baz);
  assert(found_nested);
  assert(bar_depth == 1);
  assert(nested_depth == 1);
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

void test_fold_scroll_stable_on_collapse() {
  EditorBuffer buffer = make_buffer({"head", "open {", "hidden a", "hidden b", "}", "tail"});
  buffer.fold_regions = {{1, 4}};
  buffer.scroll = 5;
  buffer.reset_to_single_cursor(0, 0);

  assert(toggle_fold_at(&buffer, 1, buffer.fold_regions));
  stabilize_scroll_after_fold_change(&buffer, buffer.fold_regions, 3);
  assert(buffer.scroll == 5);

  buffer.scroll = 2;
  assert(toggle_fold_at(&buffer, 1, buffer.fold_regions));
  stabilize_scroll_after_fold_change(&buffer, buffer.fold_regions, 3);
  assert(buffer.scroll == 1);
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

void test_symbols_refresh_after_sync_edit() {
  const std::string path = "sync_symbols.cpp";
  const std::string initial = "int foo() { return 0; }\n";
  wait_document_ready(path, initial);
  assert(tree_sitter_service().document_symbols_ready(path, initial));
  assert(!tree_sitter_service().symbols_for_file(path, initial).empty());

  EditorBuffer buffer;
  buffer.path = path;
  buffer.lines = {"int foo() { return 0; }"};
  editor_buffer_note_char_inserted(&buffer, 0, static_cast<int>(buffer.lines[0].size()) - 2, "1");
  const std::optional<EditorTextEditHint> hint = editor_buffer_take_edit_hint(&buffer);
  const std::string edited = editor_buffer_joined_source(buffer);
  tree_sitter_service().prepare_document(path, edited, hint);
  assert(tree_sitter_service().document_ready(path, edited));

  for (int attempt = 0; attempt < 500; ++attempt) {
    if (tree_sitter_service().document_symbols_ready(path, edited)) {
      const auto symbols = tree_sitter_service().symbols_for_file(path, edited);
      assert(!symbols.empty());
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(false && "symbols did not refresh after sync edit");
}

void test_sync_edit_keeps_ast_before_worker() {
  const std::string path = "sync_live.cpp";
  const std::string initial = "int foo() { return 0; }\n";
  const std::string edited = "int foo() { return 1; }\n";
  wait_document_ready(path, initial);
  assert(tree_sitter_service().document_ready(path, initial));

  tree_sitter_service().prepare_document(path, edited);
  assert(tree_sitter_service().document_ready(path, edited));

  EditorBuffer buffer;
  buffer.path = path;
  buffer.lines = {"int foo() { return 1; }"};
  const std::vector<ColoredBraceMarker> markers = find_colored_curly_braces(buffer);
  assert(!markers.empty());

  for (int attempt = 0; attempt < 500; ++attempt) {
    const std::vector<LineHighlights>* highlights =
        tree_sitter_service().highlights_for(path, edited);
    if (highlights != nullptr && highlights_ready_span_count(*highlights) > 0) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(false && "highlights did not refresh after sync edit");
}

void test_editing_line_highlights_sync() {
  const std::string path = "edit_line_sync.cpp";
  const std::string initial = "int foo() { return 0; }\nint bar() { return 0; }\n";
  wait_document_ready(path, initial);

  const std::string edited = "int foo() { return 0; }\nint bar() { return 1; }\n";
  tree_sitter_service().prepare_document(path, edited);
  assert(tree_sitter_service().document_ready(path, edited));

  const std::optional<LineHighlights> live =
      tree_sitter_service().highlights_for_editing_line(path, edited, 1);
  assert(live.has_value());
  assert(!live->spans.empty());

  const std::vector<LineHighlights>* stale =
      tree_sitter_service().stale_highlights_for(path, 2);
  assert(stale != nullptr);
  assert(stale->size() >= 2);
}

void test_stale_highlights_tolerate_trailing_newline_buffer_line_count() {
  // Buffer representation of a file that ends with '\n' includes a trailing
  // empty line, so lines.size() is one higher than the normalized tree-sitter
  // document. Incremental syntax highlighting must still reuse the baseline.
  const std::string path = "trailing_nl_stale.cpp";
  const std::string initial = "int foo() { return 0; }\nint bar() { return 0; }\n";
  wait_document_ready(path, initial);

  const std::vector<LineHighlights>* by_doc_lines =
      tree_sitter_service().stale_highlights_for(path, 2);
  assert(by_doc_lines != nullptr);
  assert(by_doc_lines->size() == 2);

  const std::vector<LineHighlights>* by_buffer_lines =
      tree_sitter_service().stale_highlights_for(path, 3);
  assert(by_buffer_lines != nullptr);
  assert(by_buffer_lines->size() == 2);
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

void test_duplicate_line_highlights_escape_string() {
  const std::string line = "\treturn event == Event::Special(\"\\x1B[1;3A\");";
  const std::string initial = "void f() {\n" + line + "\n}\n";
  const std::string path = "dup_line.cpp";
  wait_document_ready(path, initial);

  const std::string duplicated = "void f() {\n" + line + "\n" + line + "\n}\n";
  tree_sitter_service().prepare_document(path, duplicated);
  for (int attempt = 0; attempt < 500; ++attempt) {
    if (tree_sitter_service().document_ready(path, duplicated)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(tree_sitter_service().document_ready(path, duplicated));

  const std::vector<LineHighlights>* highlights = nullptr;
  for (int attempt = 0; attempt < 500; ++attempt) {
    highlights = tree_sitter_service().highlights_for(path, duplicated);
    if (highlights != nullptr && highlights->size() >= 3 &&
        highlights_ready_span_count(*highlights) > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(highlights != nullptr);
  assert(highlights->size() >= 3);

  auto covers_keyword_return = [&](const LineHighlights& line_hl) {
    for (const HighlightSpan& span : line_hl.spans) {
      if (span.capture != "keyword") {
        continue;
      }
      if (span.start_col <= 1 && span.end_col >= 7) {
        return true;
      }
    }
    return false;
  };
  auto covers_string_literal = [&](const LineHighlights& line_hl) {
    const int q = static_cast<int>(line.find('"'));
    assert(q > 0);
    for (const HighlightSpan& span : line_hl.spans) {
      if (span.capture != "string") {
        continue;
      }
      if (span.start_col <= q && span.end_col >= static_cast<int>(line.size()) - 1) {
        return true;
      }
    }
    return false;
  };

  for (int line_index : {1, 2}) {
    const LineHighlights& line_hl = (*highlights)[static_cast<std::size_t>(line_index)];
    assert(covers_keyword_return(line_hl));
    assert(covers_string_literal(line_hl));
    int col = 0;
    std::string rendered;
    for (const HighlightSpan& span : line_hl.spans) {
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
}

bool line_highlights_equal(const std::vector<LineHighlights>& a, const std::vector<LineHighlights>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].spans.size() != b[i].spans.size()) {
      return false;
    }
    for (std::size_t j = 0; j < a[i].spans.size(); ++j) {
      const HighlightSpan& sa = a[i].spans[j];
      const HighlightSpan& sb = b[i].spans[j];
      if (sa.start_col != sb.start_col || sa.end_col != sb.end_col || sa.capture != sb.capture) {
        return false;
      }
    }
  }
  return true;
}

std::vector<LineHighlights> wait_highlights(const std::string& path, const std::string& source) {
  for (int attempt = 0; attempt < 500; ++attempt) {
    const std::vector<LineHighlights>* highlights = tree_sitter_service().highlights_for(path, source);
    if (highlights != nullptr && tree_sitter_service().document_ready(path, source)) {
      return *highlights;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(false && "highlights never became ready");
  return {};
}

// Runs the same edit (via text_ops.cpp, so it goes through the real
// editor_buffer_note_* hint-tracking path) against two otherwise-identical
// documents -- one fed the resulting EditorTextEditHint, one not -- and
// checks that TSInputEdit-from-hint (an O(1) conversion) produces exactly
// the same parse/highlight result as the O(document size) prefix/suffix
// diff fallback. This is the correctness guarantee the "Fase 3" hint
// optimization depends on: a wrong hint would silently desync the tree from
// the source, and tree-sitter has no way to detect that on its own.
void run_hint_vs_diff_scenario(const std::string& scenario_name,
                               const std::vector<std::string>& initial_lines,
                               const std::function<void(EditorBuffer*)>& mutate) {
  const std::string hinted_path = "hint_" + scenario_name + ".cpp";
  const std::string baseline_path = "diff_" + scenario_name + ".cpp";

  EditorBuffer hinted;
  hinted.path = hinted_path;
  hinted.lines.assign(initial_lines);
  hinted.ensure_cursors();
  wait_document_ready(hinted_path, join_editor_lines(hinted.lines));

  wait_document_ready(baseline_path, join_editor_lines(initial_lines));

  mutate(&hinted);
  const std::optional<EditorTextEditHint> hint = editor_buffer_take_edit_hint(&hinted);
  assert(hint.has_value() && ("expected a usable hint for scenario: " + scenario_name).c_str());

  const std::string new_source = editor_buffer_joined_source(hinted);
  tree_sitter_service().prepare_document(hinted_path, new_source, hint);
  tree_sitter_service().prepare_document(baseline_path, new_source);  // no hint: forces the diff path

  const std::vector<LineHighlights> hinted_highlights = wait_highlights(hinted_path, new_source);
  const std::vector<LineHighlights> baseline_highlights = wait_highlights(baseline_path, new_source);
  assert(line_highlights_equal(hinted_highlights, baseline_highlights) &&
        ("hint-based and diff-based highlights diverged for scenario: " + scenario_name).c_str());
}

void test_edit_hint_matches_diff_on_char_insert() {
  run_hint_vs_diff_scenario("char_insert", {"int foo() {", "  int value = 1;", "  return value;", "}"},
                            [](EditorBuffer* buffer) {
                              buffer->set_primary(1, 14);  // right after "value = "
                              insert_char(buffer, '9');
                            });
}

void test_edit_hint_matches_diff_on_newline_insert() {
  run_hint_vs_diff_scenario("newline_insert",
                            {"int foo() {", "  int value = 1;", "  return value;", "}"},
                            [](EditorBuffer* buffer) {
                              buffer->set_primary(0, 0);  // beginning of file, per the earlier bug report
                              newline(buffer);
                            });
}

void test_edit_hint_matches_diff_on_backspace_join() {
  run_hint_vs_diff_scenario("backspace_join",
                            {"int foo() {", "  int value = 1;", "  return value;", "}"},
                            [](EditorBuffer* buffer) {
                              buffer->set_primary(2, 0);  // start of "  return value;"
                              backspace(buffer);          // joins line 1 and 2
                            });
}

void test_edit_hint_poisoned_by_multiple_edits_falls_back_correctly() {
  const std::string path = "hint_poisoned.cpp";
  const std::vector<std::string> initial_lines = {"int foo() {", "  int value = 1;", "  return value;",
                                                   "}"};
  EditorBuffer buffer;
  buffer.path = path;
  buffer.lines.assign(initial_lines);
  buffer.ensure_cursors();
  wait_document_ready(path, join_editor_lines(buffer.lines));

  // Two separate mutations without an intervening take_edit_hint() call --
  // this is what a multi-cursor edit looks like from editor_buffer_source's
  // point of view. The pending hint can only describe one contiguous edit,
  // so it must poison itself rather than silently returning a hint that
  // only covers the first mutation.
  buffer.set_primary(1, 14);
  insert_char(&buffer, '9');
  buffer.set_primary(2, 15);
  insert_char(&buffer, '9');

  const std::optional<EditorTextEditHint> hint = editor_buffer_take_edit_hint(&buffer);
  assert(!hint.has_value());

  const std::string new_source = editor_buffer_joined_source(buffer);
  tree_sitter_service().prepare_document(path, new_source, hint);
  const std::vector<LineHighlights> highlights = wait_highlights(path, new_source);
  assert(!highlights.empty());
}

void test_viewport_preview_before_full_parse() {
  const std::string path = "viewport_preview.cpp";
  const std::string source = "int foo() {\n  return 0;\n}\nint bar() { return 1; }\n";
  const std::string canonical = normalize_editor_source(source);
  tree_sitter_service().invalidate(path);
  assert(!tree_sitter_service().document_highlights_ready(path, canonical));

  tree_sitter_service().ensure_viewport_preview(path, canonical, {0, 1});
  const LineHighlights* line0 = tree_sitter_service().viewport_preview_line(path, canonical, 0);
  const LineHighlights* line1 = tree_sitter_service().viewport_preview_line(path, canonical, 1);
  assert(line0 != nullptr);
  assert(!line0->spans.empty());
  assert(line1 != nullptr);
  assert(!line1->spans.empty());
  assert(tree_sitter_service().viewport_preview_line(path, canonical, 3) == nullptr);

  wait_document_ready(path, canonical);
  assert(tree_sitter_service().document_highlights_ready(path, canonical));
  assert(tree_sitter_service().viewport_preview_line(path, canonical, 0) == nullptr);
}

void test_normalize_editor_source_trailing_newline() {
  const std::string from_buffer = join_editor_lines({"int main() {}", "return 0;"});
  const std::string from_file = "int main() {}\nreturn 0;\n";
  assert(normalize_editor_source(from_buffer) == normalize_editor_source(from_file));
}

}  // namespace
}  // namespace tuide

int main() {
  tuide::test_parse_symbols();
  tuide::test_parse_python_symbols();
  tuide::test_simple_pair();
  tuide::test_nested();
  tuide::test_ignores_string();
  tuide::test_empty_lines_no_hang();
  tuide::test_empty_string_literal_cursor();
  tuide::test_hello_cpp_anonymous_namespace();
  tuide::test_highlights_after_prepare();
  tuide::test_hello_cpp_highlights_from_disk();
  tuide::test_highlight_overlapping_spans_do_not_duplicate();
  tuide::test_highlight_col_offset_indent();
  tuide::test_incremental_parse_after_single_edit();
  tuide::test_cursor_in_code_uses_ast_when_ready();
  tuide::test_cursor_in_code_optimistic_when_parse_stale();
  tuide::test_locals_scope_chain();
  tuide::test_innermost_scope_range();
  tuide::test_innermost_scope_while_loop();
  tuide::test_innermost_scope_declaration_in_if();
  tuide::test_fold_regions_from_tree();
  tuide::test_editor_fold_visibility();
  tuide::test_fold_scroll_stable_on_collapse();
  tuide::test_colored_curly_braces_depths();
  tuide::test_local_completions_include_parameters();
  tuide::test_symbols_refresh_after_sync_edit();
  tuide::test_sync_edit_keeps_ast_before_worker();
  tuide::test_editing_line_highlights_sync();
  tuide::test_stale_highlights_tolerate_trailing_newline_buffer_line_count();
  tuide::test_parse_debounce_coalesces_edits();
  tuide::test_duplicate_line_highlights_escape_string();
  tuide::test_edit_hint_matches_diff_on_char_insert();
  tuide::test_edit_hint_matches_diff_on_newline_insert();
  tuide::test_edit_hint_matches_diff_on_backspace_join();
  tuide::test_edit_hint_poisoned_by_multiple_edits_falls_back_correctly();
  tuide::test_viewport_preview_before_full_parse();
  tuide::test_normalize_editor_source_trailing_newline();
  std::cout << "tree_sitter_test ok\n";
  return 0;
}
