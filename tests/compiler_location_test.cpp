#include "util/compiler_location.hpp"

#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void test_gcc_with_column() {
  const auto match = tuide::find_compiler_location(
      "src/app/application.cpp:42:10: error: 'foo' was not declared in this scope");
  expect(match.has_value(), "gcc error with column");
  expect(match->path == "src/app/application.cpp", "gcc path");
  expect(match->line == 42, "gcc line");
  expect(match->column == 10, "gcc column");
  expect(match->span_start == 0, "gcc span start");
  expect(match->span_end == 29, "gcc span end");
}

void test_gcc_without_column() {
  const auto match =
      tuide::find_compiler_location("main.cpp:7: warning: unused variable 'x'");
  expect(match.has_value(), "gcc warning without column");
  expect(match->path == "main.cpp", "gcc path short");
  expect(match->line == 7, "gcc line short");
  expect(match->column == 0, "gcc column absent");
}

void test_absolute_path() {
  const auto match = tuide::find_compiler_location(
      "/home/user/project/src/main.cpp:100:5: fatal error: no such file");
  expect(match.has_value(), "absolute path");
  expect(match->path == "/home/user/project/src/main.cpp", "absolute path value");
  expect(match->line == 100, "absolute line");
  expect(match->column == 5, "absolute column");
}

void test_msvc_style() {
  const auto match =
      tuide::find_compiler_location("C:\\project\\main.cpp(42,5): error C2065: undeclared");
  expect(match.has_value(), "msvc error");
  expect(match->path == "C:\\project\\main.cpp", "msvc path");
  expect(match->line == 42, "msvc line");
  expect(match->column == 5, "msvc column");
}

void test_included_from() {
  const auto match = tuide::find_compiler_location(
      "In file included from include/header.hpp:12:");
  expect(match.has_value(), "included from");
  expect(match->path == "include/header.hpp", "included path");
  expect(match->line == 12, "included line");
}

void test_from_continuation() {
  const auto match = tuide::find_compiler_location(
      "                 from /home/user/project/src/main.cpp:10,");
  expect(match.has_value(), "from continuation");
  expect(match->path == "/home/user/project/src/main.cpp", "from continuation path");
  expect(match->line == 10, "from continuation line");
}

void test_from_prefix_stripped() {
  const auto match = tuide::find_compiler_location("from src/foo.cpp:42:");
  expect(match.has_value(), "from prefix");
  expect(match->path == "src/foo.cpp", "from prefix path");
  expect(match->line == 42, "from prefix line");
}

void test_no_match() {
  expect(!tuide::find_compiler_location("make[2]: *** [target] Error 1").has_value(), "make error");
  expect(!tuide::find_compiler_location("").has_value(), "empty line");
}

}  // namespace

int main() {
  test_gcc_with_column();
  test_gcc_without_column();
  test_absolute_path();
  test_msvc_style();
  test_included_from();
  test_from_continuation();
  test_from_prefix_stripped();
  test_no_match();
  std::cout << "compiler_location_test: OK\n";
  return 0;
}
