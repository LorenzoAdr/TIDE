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
    const std::string aider = "src/foo.cpp\n<<<<<<< SEARCH\nold\n=======\nnew\n>>>>>>> REPLACE\n";
    std::string err;
    const auto hunks = parse_search_replace_aider(aider, &err);
    expect(hunks.size() == 1 && hunks[0].path == "src/foo.cpp", "aider path");
    expect(hunks[0].search == "old\n" && hunks[0].replace == "new\n", "aider body");
  }
  if (failures) {
    std::cerr << failures << " failures\n";
    return 1;
  }
  std::cout << "search_replace_test OK\n";
  return 0;
}
