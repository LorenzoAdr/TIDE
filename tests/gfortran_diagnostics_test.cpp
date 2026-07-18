#include "lsp/gfortran_diagnostics.hpp"

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

void test_inline_error() {
  const std::string stderr_text =
      "/tmp/tuide-gf-abc123:4:7: Error: Cannot convert CHARACTER(1) to INTEGER(4) at (1)\n";
  const auto items = tuide::parse_gfortran_stderr(stderr_text, "/tmp/tuide-gf-abc123");
  expect(items.size() == 1, "one inline error");
  expect(items[0].line == 3, "0-based line");
  expect(items[0].start_col == 6, "0-based column");
  expect(items[0].source == "gfortran", "source tag");
  expect(items[0].severity == tuide::DiagnosticSeverity::kError, "error severity");
  expect(items[0].message.find("Cannot convert") != std::string::npos, "message");
}

void test_split_warning() {
  const std::string stderr_text =
      "bad.f90:12:1:\n"
      "\n"
      "   12 |   call foo()\n"
      "      |  1\n"
      "Warning: 'foo' is used but not defined\n";
  const auto items = tuide::parse_gfortran_stderr(stderr_text, "bad.f90");
  expect(items.size() == 1, "one split warning");
  expect(items[0].line == 11, "split line");
  expect(items[0].severity == tuide::DiagnosticSeverity::kWarning, "warning severity");
  expect(items[0].message.find("not defined") != std::string::npos, "warning message");
}

void test_run_requires_fortran_suffix() {
  if (std::system("command -v gfortran >/dev/null 2>&1") != 0) {
    return;
  }
  const auto doc = tuide::run_gfortran_diagnostics(
      "/tmp/tuide-gf-unit.f90",
      "program bad\n  implicit none\n  integer :: x\n  x = 'oops'\nend program bad\n",
      "gfortran");
  expect(doc.has_value(), "gfortran run returns a document");
  expect(!doc->items.empty(), "gfortran reports at least one error on typed mismatch");
}

}  // namespace

int main() {
  test_inline_error();
  test_split_warning();
  test_run_requires_fortran_suffix();
  std::cout << "gfortran_diagnostics_test: ok\n";
  return 0;
}
