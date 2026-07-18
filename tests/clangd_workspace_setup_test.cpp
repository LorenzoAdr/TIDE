#include "util/clangd_workspace_setup.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect_true(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_eq(std::size_t actual, std::size_t expected, const char* message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " (got " << actual << ", expected " << expected << ")\n";
    ++failures;
  }
}

}  // namespace

int main() {
  const fs::path temp_root = fs::temp_directory_path() / "tuide-clangd-setup-test";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root / "include" / "nested", ec);
  fs::create_directories(temp_root / "other", ec);

  const std::vector<std::string> roots = {(temp_root / "include").string()};
  const std::vector<std::string> flags = tuide::expand_recursive_include_flags(roots);

  // Root + immediate child "nested", not deeper levels or sibling "other".
  expect_eq(flags.size(), 2, "include flags bounded to root and direct children");
  bool has_root = false;
  bool has_nested = false;
  for (const std::string& flag : flags) {
    if (flag.find("/include") != std::string::npos && flag.find("/nested") == std::string::npos) {
      has_root = true;
    }
    if (flag.find("/nested") != std::string::npos) {
      has_nested = true;
    }
    expect_true(flag.rfind("-I", 0) == 0, "flag uses -I prefix");
  }
  expect_true(has_root, "root include directory present");
  expect_true(has_nested, "immediate child include directory present");

  fs::remove_all(temp_root, ec);
  return failures == 0 ? 0 : 1;
}
