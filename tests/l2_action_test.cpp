#include <cassert>
#include <iostream>
#include <string>

#include "ai/l2_action.hpp"

using tuide::L2ActionKind;
using tuide::kL2MaxToolBatch;
using tuide::parse_l2_action;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    const auto a = parse_l2_action(R"({"action":"tool","name":"get_code_of","arg":"a.cpp:Foo"})");
    expect(a.kind == L2ActionKind::Tool, "single tool");
    expect(a.calls.size() == 1 && a.calls[0].name == "get_code_of", "mirrored call");
  }
  {
    const auto a = parse_l2_action(
        R"({"action":"tools","calls":[{"name":"get_code_of","arg":"a.cpp:Foo"},{"name":"file_outline","arg":"b.cpp"},{"name":"search","arg":"wake"}]})");
    expect(a.kind == L2ActionKind::Tools, "tools kind");
    expect(a.calls.size() == 3, "3 calls");
    expect(a.calls[1].name == "file_outline" && a.calls[1].arg == "b.cpp", "call1");
  }
  {
    // Cap at kL2MaxToolBatch
    std::string json = R"({"action":"tools","calls":[)";
    for (int i = 0; i < kL2MaxToolBatch + 3; ++i) {
      if (i) {
        json += ",";
      }
      json += R"({"name":"search","arg":")" + std::to_string(i) + R"("})";
    }
    json += "]}";
    const auto a = parse_l2_action(json);
    expect(a.kind == L2ActionKind::Tools, "tools capped kind");
    expect(static_cast<int>(a.calls.size()) == kL2MaxToolBatch, "capped to max");
  }
  {
    const auto a = parse_l2_action(
        R"({"action":"tool","calls":[{"name":"get_code_of","arg":"x.cpp:Y"},{"name":"get_code_of","arg":"z.cpp:W"}]})");
    expect(a.kind == L2ActionKind::Tools, "tool+calls => tools");
    expect(a.calls.size() == 2, "2 from tool+calls");
  }
  {
    const auto a = parse_l2_action(R"({"action":"tools","calls":[]})");
    expect(a.kind == L2ActionKind::Error, "empty calls error");
  }
  {
    const auto a = parse_l2_action(
        R"({"action":"plan","targets":["src/a.cpp:Foo","src/b.cpp:10","src/c.hpp"],"summary":"hot"})");
    expect(a.kind == L2ActionKind::Plan, "plan kind");
    expect(a.targets.size() == 3, "3 targets");
    expect(a.targets[0] == "src/a.cpp:Foo", "target0");
    expect(a.summary == "hot", "plan summary");
  }
  {
    const auto a = parse_l2_action(
        R"({"action":"watchlist","targets":[{"path":"src/x.cpp","symbol":"Bar"}]})");
    expect(a.kind == L2ActionKind::Plan, "watchlist alias");
    expect(a.targets.size() == 1 && a.targets[0] == "src/x.cpp:Bar", "object target");
  }
  {
    // Cap plan targets
    std::string json = R"({"action":"plan","targets":[)";
    for (int i = 0; i < tuide::kL2MaxPlanTargets + 4; ++i) {
      if (i) {
        json += ",";
      }
      json += "\"src/f.cpp:S" + std::to_string(i) + "\"";
    }
    json += "]}";
    const auto a = parse_l2_action(json);
    expect(a.kind == L2ActionKind::Plan, "plan capped kind");
    expect(static_cast<int>(a.targets.size()) == tuide::kL2MaxPlanTargets, "plan capped");
  }
  {
    // Invalid JSON \s* inside strings → repaired then normalized to newlines.
    const auto a = parse_l2_action(
        "{\"action\":\"edit\",\"hunks\":[{\"path\":\"a.cpp\",\"search\":\"foo\\s*bar\","
        "\"replace\":\"foo\\nbar\"}]}");
    expect(a.kind == L2ActionKind::Edit, "edit with \\s* repair");
    expect(a.hunks.size() == 1, "1 hunk");
    expect(a.hunks[0].search == "foo\nbar", "search newlines from \\s*");
    expect(a.hunks[0].replace == "foo\nbar", "replace newline");
  }
  {
    // Valid JSON with doubled backslash \\s* → normalize after parse.
    const auto a = parse_l2_action(
        R"({"action":"edit","hunks":[{"path":"a.cpp","search":"foo\\s*bar","replace":"x"}]})");
    expect(a.kind == L2ActionKind::Edit, "edit with \\\\s*");
    expect(a.hunks.size() == 1 && a.hunks[0].search == "foo\nbar", "normalized \\\\s*");
  }
  {
    // Raw newline inside JSON string repaired.
    const auto a = parse_l2_action(
        "{\"action\":\"edit\",\"hunks\":[{\"path\":\"a.cpp\",\"search\":\"line1\nline2\","
        "\"replace\":\"ok\"}]}");
    expect(a.kind == L2ActionKind::Edit, "raw newline repaired");
    expect(a.hunks.size() == 1 && a.hunks[0].search == "line1\nline2", "kept as newline");
  }
  {
    const auto a = parse_l2_action(R"({"action":"edit","hunks":[)");
    expect(a.kind == L2ActionKind::Error, "truncated hunks still error");
    expect(a.error.find("hunks") != std::string::npos || a.error.find("JSON") != std::string::npos,
           "truncated error mentions hunks/json");
  }

  {
    const auto a = parse_l2_action(
        "src/foo.cpp\n<<<<<<< SEARCH\nint x = 1;\n=======\nint x = 2;\n>>>>>>> REPLACE\n");
    expect(a.kind == L2ActionKind::Edit, "aider edit kind");
    expect(a.hunks.size() == 1 && a.hunks[0].path == "src/foo.cpp", "aider path");
    expect(a.hunks[0].search == "int x = 1;\n" && a.hunks[0].replace == "int x = 2;\n",
           "aider bodies");
  }
  {
    const auto a = parse_l2_action(
        "PROHIBIDO JSON: ni plan, ni tool, ni sibling_of.\n"
        "Empieza con un path del pack y <<<<<<< SEARCH\n"
        "src/foo.cpp\n<<<<<<< SEARCH\nspan del pack\n=======\nnuevo\n>>>>>>> REPLACE\n"
        "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
        "bool command_exists(const std::string& command);\n"
        "=======\n"
        "bool command_exists(const std::string& command);\nbool always_true();\n"
        ">>>>>>> REPLACE\n");
    expect(a.kind == L2ActionKind::Edit, "skips echoed Aider example");
    expect(a.hunks.size() == 1 && a.hunks[0].path == "src/util/shell_utils.hpp",
           "keeps real path got=" + (a.hunks.empty() ? std::string("none") : a.hunks[0].path));
    expect(a.hunks[0].replace.find("always_true") != std::string::npos, "keeps always_true replace");
  }

  {
    const auto a = parse_l2_action(
        R"({"action":"a_judge","verdicts":[{"target":"src/a.cpp:Foo#tail","verdict":"useful",
        "anchor":"src/a.cpp:90","stem":"a","role":"primary","why":"gate"}],"done":false})");
    expect(a.kind == L2ActionKind::AJudge, "a_judge kind");
    expect(a.a_verdicts.size() == 1 && a.a_verdicts[0].stem == "a", "a_judge verdict");
    expect(!a.a_turn_done, "a_judge done false");
  }
  {
    const auto a = parse_l2_action(
        R"({"action":"a_done","loci":[{"stem":"wake","anchor":"src/ui/wake.cpp:tick","role":"primary",
        "why":"policy"}],"summary":"locked"})");
    expect(a.kind == L2ActionKind::ADone, "a_done kind");
    expect(a.a_loci.size() == 1 && a.a_loci[0].anchor == "src/ui/wake.cpp:tick", "a_done locus");
    expect(a.summary == "locked", "a_done summary");
  }
  {
    const auto a = parse_l2_action(R"({"action":"a_done","loci":[]})");
    expect(a.kind == L2ActionKind::Error, "a_done empty loci");
  }
  {
    // Coerce verdict-as-action (7B common in A1 dataflow).
    const auto a = parse_l2_action(
        R"({"action":"reject","target":"src/ui/busy_strip.cpp:spinner_busy_set","why":"no"})");
    expect(a.kind == L2ActionKind::AJudge, "reject→a_judge");
    expect(a.a_verdicts.size() == 1 && a.a_verdicts[0].verdict == tuide::AVerdictKind::Reject,
           "reject verdict");
  }
  {
    const auto a = parse_l2_action(
        R"({"action":"interesting","target":"S1","why":"caller sets busy"})");
    expect(a.kind == L2ActionKind::ATrailJudge, "interesting→a_trail_judge");
    expect(a.a_verdicts.size() == 1 &&
               a.a_verdicts[0].verdict == tuide::AVerdictKind::Interesting,
           "interesting verdict");
  }
  {
    const auto a = parse_l2_action(
        R"({"verdicts":[{"target":"src/a.cpp:Foo","verdict":"expand","expand_with":"trail"}]})");
    expect(a.kind == L2ActionKind::AJudge, "empty action+verdicts→a_judge");
  }

  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "OK l2_action_test\n";
  return 0;
}
