#include <cassert>
#include <iostream>
#include <string>

#include "util/nm_reader.hpp"

using namespace tuide;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_translate_types() {
  expect(translate_nm_type('U') == "Sin definir (referencia externa)", "U translation");
  expect(translate_nm_type('T') == "Código (global, definido)", "T translation");
  expect(classify_nm_type('U') == NmSymbolType::kUndefined, "U category");
  expect(classify_nm_type('T') == NmSymbolType::kText, "T category");
}

void test_parse_defined_symbol() {
  const char* output =
      "000000000040112c 00000042 T main\n"
      "/home/user/main.cpp:42\n";
  const auto symbols = parse_nm_output(output);
  expect(symbols.size() == 1, "one symbol");
  if (!symbols.empty()) {
    expect(symbols[0].name == "main", "symbol name");
    expect(symbols[0].has_address, "has address");
    expect(symbols[0].address == 0x40112c, "address value");
    expect(symbols[0].has_size, "has size");
    expect(symbols[0].size == 0x42, "size value");
    expect(symbols[0].source_file == "/home/user/main.cpp", "source file");
    expect(symbols[0].source_line == 42, "source line");
  }
}

void test_parse_undefined_symbol() {
  const char* output = "                 U _ZN3Foo3barEv\n";
  const auto symbols = parse_nm_output(output);
  expect(symbols.size() == 1, "undefined symbol count");
  if (!symbols.empty()) {
    expect(symbols[0].category == NmSymbolType::kUndefined, "undefined category");
    expect(!symbols[0].has_address, "undefined has no address");
    expect(symbols[0].name == "_ZN3Foo3barEv", "undefined name");
  }
}

void test_filter() {
  NmSymbol symbol;
  symbol.name = "MyClass::missing()";
  symbol.category = NmSymbolType::kUndefined;
  expect(nm_symbol_matches_filter(symbol, "missing", NmBindingFilter::kUndefined), "filter match");
  expect(!nm_symbol_matches_filter(symbol, "other", NmBindingFilter::kUndefined), "filter miss");
  expect(nm_symbol_matches_filter(symbol, "", NmBindingFilter::kUndefined), "empty query");
  symbol.category = NmSymbolType::kText;
  expect(nm_symbol_matches_filter(symbol, "missing", NmBindingFilter::kDefined), "defined filter");
  expect(!nm_symbol_matches_filter(symbol, "missing", NmBindingFilter::kUndefined),
         "defined not undefined");
}

void test_parse_archive_symbols() {
  const char* output =
      "\n"
      "vterm.c.o:\n"
      "000000000000013f 0000000000000185 T vterm_build\t/home/src/vterm.c:51\n"
      "                 U exit\t/home/src/vterm.c:420\n";
  const auto symbols = parse_nm_output(output);
  expect(symbols.size() >= 2, "archive symbols");
  bool found_build = false;
  for (const auto& sym : symbols) {
    if (sym.name == "vterm_build") {
      found_build = true;
      expect(sym.source_line == 51, "inline source line");
    }
  }
  expect(found_build, "vterm_build found");
}

void test_is_nm_analyzable_path() {
  expect(is_nm_analyzable_path("/home/lorenzo/workspace/tgdb/build/hello"), "hello ELF");
  expect(is_nm_analyzable_path("/home/lorenzo/workspace/tgdb/build/liblibvterm.a"), "static lib");
}

void test_linker_parser() {
  const std::string line =
      "main.o: In function `main':\n"
      "main.cpp:(.text+0x12): undefined reference to `Foo::bar()'";
  const auto symbol = parse_linker_undefined_reference(line);
  expect(symbol.has_value(), "linker parse found");
  if (symbol.has_value()) {
    expect(*symbol == "Foo::bar()", "linker symbol name");
  }
}

}  // namespace

int main() {
  test_translate_types();
  test_parse_defined_symbol();
  test_parse_undefined_symbol();
  test_filter();
  test_parse_archive_symbols();
  test_is_nm_analyzable_path();
  test_linker_parser();
  if (failures > 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "All nm_reader tests passed\n";
  return 0;
}
