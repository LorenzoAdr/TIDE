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

  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "OK l2_action_test\n";
  return 0;
}
