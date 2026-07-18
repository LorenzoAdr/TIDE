#include "app/workspace_detect.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect_true(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_eq(const std::string& actual, const std::string& expected, const char* message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " (got '" << actual << "', expected '" << expected
              << "')\n";
    ++failures;
  }
}

std::string write_file(const fs::path& path, const std::string& contents) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream output(path);
  output << contents;
  return fs::absolute(path, ec).string();
}

}  // namespace

int main() {
  const fs::path base = fs::temp_directory_path() / "tuide_workspace_detect_test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);

  const std::string project = write_file(base / "proj" / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  const std::string src_dir = (base / "proj" / "src").string();
  fs::create_directories(src_dir, ec);
  const std::string nested = write_file(base / "proj" / "src" / "main.cpp", "int main() {}\n");

  {
    const tuide::WorkspaceDetectResult result =
        tuide::detect_workspace_root(base / "proj" / "src");
    expect_true(result.marker_found, "finds CMakeLists.txt in parent");
    expect_eq(result.marker, "CMakeLists.txt", "cmake marker name");
    expect_eq(result.workspace_root, fs::path(project).parent_path().string(),
              "workspace root is project dir");
  }

  const fs::path make_base = base / "make_proj";
  fs::create_directories(make_base / "lib", ec);
  write_file(make_base / "Makefile", "all:\n");
  {
    const tuide::WorkspaceDetectResult result =
        tuide::detect_workspace_root(make_base / "lib");
    expect_true(result.marker_found, "finds Makefile in parent");
    expect_eq(result.marker, "Makefile", "make marker name");
    expect_eq(result.workspace_root, fs::absolute(make_base, ec).string(),
              "workspace root is makefile dir");
  }

  const fs::path plain = base / "plain" / "docs";
  fs::create_directories(plain, ec);
  {
    const tuide::WorkspaceDetectResult result = tuide::detect_workspace_root(plain);
    expect_true(!result.marker_found, "no marker falls back to anchor");
    expect_eq(result.workspace_root, fs::absolute(plain, ec).string(), "fallback anchor path");
  }

  const fs::path deep_base = base / "deep";
  fs::create_directories(deep_base, ec);
  write_file(deep_base / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  fs::path deep_anchor = deep_base;
  for (int i = 0; i < 25; ++i) {
    deep_anchor /= "level";
    fs::create_directories(deep_anchor, ec);
  }
  {
    const tuide::WorkspaceDetectResult result =
        tuide::detect_workspace_root(deep_anchor, 5);
    expect_true(!result.marker_found, "max depth stops before project root");
    expect_eq(result.workspace_root, fs::absolute(deep_anchor, ec).string(),
              "max depth fallback to anchor");
  }

  (void)nested;
  fs::remove_all(base, ec);
  return failures == 0 ? 0 : 1;
}
