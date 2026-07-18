#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

#include "ui/glyphs.hpp"

namespace {

int failures = 0;

class EnvGuard {
 public:
  EnvGuard(const char* name, const char* value) : name_(name), had_(std::getenv(name) != nullptr) {
    if (had_) {
      saved_ = std::getenv(name_);
    }
    if (value == nullptr) {
      unsetenv(name_);
    } else {
      setenv(name_, value, 1);
    }
  }

  ~EnvGuard() {
    if (had_) {
      setenv(name_, saved_.c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }

 private:
  const char* name_;
  bool had_;
  std::string saved_;
};

void expect(bool condition, const char* message) {
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

}  // namespace

int main() {
  tuide::configure_glyphs(tuide::IconMode::Never);

  expect_eq(tuide::symbol_kind_glyph(tuide::SymbolKind::kMethod), "M", "method ascii");
  expect_eq(tuide::symbol_kind_glyph(tuide::SymbolKind::kFunction), "f", "function ascii");
  expect_eq(tuide::file_glyph("foo.cpp"), "++", "cpp ascii");
  expect_eq(tuide::file_glyph("CMakeLists.txt"), "cm", "cmake ascii");
  expect_eq(tuide::file_glyph("Makefile"), "mk", "makefile ascii");
  expect_eq(tuide::file_glyph_display("LICENSE"), "· ", "license ascii padded");
  expect_eq(tuide::file_glyph_display("README.md"), "md", "markdown ascii");
  expect_eq(tuide::folder_glyph(false), ">", "folder closed ascii");
  expect_eq(tuide::folder_glyph(true), "v", "folder open ascii");
  expect_eq(tuide::strip_symbol_kind_prefix("M foo::bar"), "foo::bar", "strip method prefix");
  expect_eq(tuide::strip_symbol_kind_prefix("plain"), "plain", "strip no prefix");
  expect(!tuide::glyphs_use_nerd(), "never mode disables nerd");

  tuide::configure_glyphs(tuide::IconMode::Always);
  expect(tuide::glyphs_use_nerd(), "always mode enables nerd");
  const std::string nerd_method = tuide::symbol_kind_glyph(tuide::SymbolKind::kMethod);
  expect(nerd_method != "M", "method nerd differs");
  expect(tuide::file_glyph("main.cpp") != "++", "cpp nerd differs");
  expect(!tuide::file_glyph("CMakeLists.txt").empty(), "cmake nerd non-empty");
  expect(tuide::file_glyph("CMakeLists.txt") != "cm", "cmake nerd not ascii");
  expect(tuide::symbol_kind_glyph(tuide::SymbolKind::kMethod) == nerd_method,
         "method nerd stable");

  {
    EnvGuard term("TERM", "xterm-256color");
    EnvGuard term_program("TERM_PROGRAM", "Konsole");
    EnvGuard nerd_hint("TUIDE_NERD_FONT", nullptr);
    tuide::configure_glyphs(tuide::IconMode::Auto);
    expect(!tuide::glyphs_use_nerd(), "auto konsole uses ascii");
    expect_eq(tuide::symbol_kind_glyph(tuide::SymbolKind::kMethod), "M", "auto konsole method ascii");
  }

  {
    EnvGuard term("TERM", "xterm-kitty");
    EnvGuard nerd_hint("TUIDE_NERD_FONT", nullptr);
    tuide::configure_glyphs(tuide::IconMode::Auto);
    expect(tuide::glyphs_use_nerd(), "auto kitty uses nerd");
  }

  {
    EnvGuard term("TERM", "xterm-256color");
    EnvGuard term_program("TERM_PROGRAM", "Konsole");
    EnvGuard nerd_hint("TUIDE_NERD_FONT", "1");
    tuide::configure_glyphs(tuide::IconMode::Auto);
    expect(tuide::glyphs_use_nerd(), "auto with nerd hint uses nerd");
  }

  if (failures == 0) {
    std::cout << "glyphs_test: OK\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
