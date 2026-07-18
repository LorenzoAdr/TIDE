#include "editor/doc_comment.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void test_extract_cpp_params() {
  const auto names =
      tgdb::extract_param_names("int foo(const std::string& bar, int x = 0)", "cpp");
  expect(names.size() == 2, "two cpp params");
  expect(names[0] == "bar", "first param bar");
  expect(names[1] == "x", "second param x");
}

void test_extract_python_params() {
  const auto names =
      tgdb::extract_param_names("def foo(self, bar: int, x=1):", "python");
  expect(names.size() == 2, "two python params skipping self");
  expect(names[0] == "bar", "python bar");
  expect(names[1] == "x", "python x");
}

void test_cpp_doc_comment_snippet() {
  tgdb::DocCommentRequest req;
  req.path = "foo.cpp";
  req.kind = tgdb::SymbolKind::kFunction;
  req.symbol_name = "foo";
  req.declaration_line = "void foo(int a, double b)";
  req.indent_cols = 0;
  const std::string snippet = tgdb::build_doc_comment_snippet(req);
  expect(snippet.find("@brief") != std::string::npos, "has brief");
  expect(snippet.find("@param a") != std::string::npos, "has param a");
  expect(snippet.find("@param b") != std::string::npos, "has param b");
  expect(snippet.find("@units") != std::string::npos, "has units");
  expect(snippet.find("${1:") != std::string::npos, "has tab stop 1");
  expect(snippet.find("${2:") != std::string::npos, "has tab stop 2");
}

void test_python_doc_comment_snippet() {
  tgdb::DocCommentRequest req;
  req.path = "foo.py";
  req.kind = tgdb::SymbolKind::kFunction;
  req.symbol_name = "foo";
  req.declaration_line = "def foo(self, bar):";
  req.indent_cols = 0;
  const std::string snippet = tgdb::build_doc_comment_snippet(req);
  expect(snippet.find("\"\"\"") != std::string::npos, "has docstring quotes");
  expect(snippet.find(":param bar:") != std::string::npos, "has param bar");
  expect(snippet.find(":units bar:") != std::string::npos, "has units bar");

  const tgdb::DocCommentInsertPlan plan = tgdb::plan_doc_comment_insert(req, 10);
  expect(plan.insert_line == 11, "python inserts below def");
}

void test_separator_snippet() {
  const std::string snippet = tgdb::build_separator_snippet("foo.cpp", 2);
  expect(snippet.find("/*") != std::string::npos, "separator opens block");
  expect(snippet.find("${1:text}") != std::string::npos, "separator has text placeholder");
  expect(snippet.find("  /*") != std::string::npos, "respects indent");
}

void test_file_header_banner() {
  const std::string snippet = tgdb::build_file_header_snippet("src/widget.cpp");
  expect(snippet.find("widget.cpp") != std::string::npos, "includes filename");
  expect(snippet.find("Description:") != std::string::npos, "has Description");
  expect(snippet.find("Author:") != std::string::npos, "has Author");
  expect(snippet.find("License:") != std::string::npos, "has License");
  expect(snippet.find("Notes:") != std::string::npos, "has Notes");
  expect(snippet.find(std::string(40, '*')) != std::string::npos, "has star banner");
  expect(snippet.find("${1:") != std::string::npos, "has first placeholder");

  const std::string py = tgdb::build_file_header_snippet("mod.py");
  expect(py.find("# ") != std::string::npos, "python header uses hash");
  expect(py.find("mod.py") != std::string::npos, "python filename");
}

}  // namespace

int main() {
  test_extract_cpp_params();
  test_extract_python_params();
  test_cpp_doc_comment_snippet();
  test_python_doc_comment_snippet();
  test_separator_snippet();
  test_file_header_banner();
  std::cout << "doc_comment_test: ok\n";
  return 0;
}
