#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ai/search_replace.hpp"

using tuide::SearchReplaceHunk;
using tuide::SearchReplaceSpan;
using tuide::apply_hunk_to_text;
using tuide::find_unique_span;
using tuide::parse_search_replace_aider;
using tuide::parse_search_replace_json;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    SearchReplaceSpan sp;
    std::string err;
    expect(find_unique_span("aa X bb X cc", "X", &sp, &err) == false, "ambiguous");
    expect(find_unique_span("aa UNIQUE bb", "UNIQUE", &sp, &err), "unique");
    expect(sp.start_line == 1, "line1");
  }
  {
    SearchReplaceHunk h;
    h.path = "t.cpp";
    h.search = "int a = 1;\n";
    h.replace = "int a = 2;\n";
    const auto r = apply_hunk_to_text("void f() {\n  int a = 1;\n}\n", h);
    expect(r.ok, "apply ok");
    expect(r.after.find("int a = 2;") != std::string::npos, "replaced");
  }
  {
    nlohmann::json j = {{"hunks", {{{"path", "a.cpp"}, {"search", "x"}, {"replace", "y"}}}}};
    std::string err;
    const auto hunks = parse_search_replace_json(j, &err);
    expect(hunks.size() == 1 && hunks[0].path == "a.cpp", "json hunks");
  }
  {
    using tuide::normalize_hunk_escape_noise;
    expect(normalize_hunk_escape_noise("a\\s*b\\sc") == "a\nb\nc", "normalize \\s* / \\s");
    expect(normalize_hunk_escape_noise("a\\nb\\t") == "a\nb\t", "normalize \\n \\t");
    SearchReplaceHunk h;
    h.search = "x\\s*y";
    h.replace = "x\\ny";
    normalize_hunk_escape_noise(&h);
    expect(h.search == "x\ny" && h.replace == "x\ny", "normalize hunk");
  }
  {
    // Flex match: model search collapses blank lines vs disk.
    const std::string disk =
        "#pragma once\n\n#include <string>\n\nnamespace tuide {\n\nbool command_exists();\n\n}\n";
    const std::string collapsed =
        "#pragma once\n#include <string>\nnamespace tuide {\nbool command_exists();\n}\n";
    SearchReplaceSpan sp;
    std::string err;
    expect(!find_unique_span(disk, collapsed, &sp, &err), "exact misses blanks");
    expect(tuide::find_unique_span_flex(disk, collapsed, &sp, &err), "flex hits blanks");
    expect(sp.byte_begin == 0, "flex begin");
    SearchReplaceHunk h;
    h.path = "shell_utils.hpp";
    h.search = collapsed;
    h.replace = "#pragma once\n\n#include <string>\n\nnamespace tuide {\n\n"
                "// L2_PS_A\nbool command_exists();\n\n}\n";
    const auto r = apply_hunk_to_text(disk, h);
    expect(r.ok, "flex apply ok");
    expect(r.after.find("// L2_PS_A") != std::string::npos, "flex applied marker");
    expect(r.old_text.find("\n\n") != std::string::npos, "old_text is disk span");
  }
  {
    // Trailing whitespace tolerance.
    SearchReplaceSpan sp;
    std::string err;
    expect(tuide::find_unique_span_flex("foo  \nbar\n", "foo\nbar\n", &sp, &err), "rstrip flex");
  }
  {
    const std::string disk = "a\n\nb\n\nc\n";
    const std::string excerpt = tuide::disk_excerpt_near_search(disk, "b\n", 2, 10);
    expect(excerpt.find("a\n") != std::string::npos && excerpt.find("b\n") != std::string::npos,
           "disk excerpt");
  }
  {
    SearchReplaceSpan sp;
    std::string err;
    const std::string hay =
        "bool command_exists() {\n  return false;\n}\nbool other() { return 1; }\n";
    expect(find_unique_span(hay, "bool command_exists() {", &sp, &err), "opener unique");
    expect(tuide::extend_span_to_matching_brace(hay, &sp, &err), "extend brace");
    const std::string full = hay.substr(sp.byte_begin, sp.byte_end - sp.byte_begin);
    expect(full.find("return false;") != std::string::npos, "includes body");
    expect(full.find("bool other") == std::string::npos, "stops before next fn");
  }
  {
    // parse_search_replace_json also normalizes.
    nlohmann::json j = {
        {"hunks", {{{"path", "a.cpp"}, {"search", "foo\\s*bar"}, {"replace", "z"}}}}};
    std::string err;
    const auto hunks = parse_search_replace_json(j, &err);
    expect(hunks.size() == 1 && hunks[0].search == "foo\nbar", "json normalize \\s*");
  }
  {
    const std::string aider = "src/foo.cpp\n<<<<<<< SEARCH\nold\n=======\nnew\n>>>>>>> REPLACE\n";
    std::string err;
    const auto hunks = parse_search_replace_aider(aider, &err);
    expect(hunks.size() == 1 && hunks[0].path == "src/foo.cpp", "aider path");
    expect(hunks[0].search == "old\n" && hunks[0].replace == "new\n", "aider body");
  }
  {
    const std::string mixed =
        "PROHIBIDO JSON: ni plan.\n<<<<<<< SEARCH\n"
        "src/foo.cpp\n<<<<<<< SEARCH\nspan del pack\n=======\nx\n>>>>>>> REPLACE\n"
        "src/util/shell_utils.hpp\n<<<<<<< SEARCH\nold\n=======\nnew\n>>>>>>> REPLACE\n";
    std::string err;
    const auto hunks = parse_search_replace_aider(mixed, &err);
    expect(hunks.size() == 1 && hunks[0].path == "src/util/shell_utils.hpp",
           "skip echo hunks got n=" + std::to_string(hunks.size()));
  }
  if (failures) {
    std::cerr << failures << " failures\n";
    return 1;
  }
  std::cout << "search_replace_test OK\n";
  return 0;
}
