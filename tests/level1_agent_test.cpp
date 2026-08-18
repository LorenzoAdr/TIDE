#include <cassert>
#include <iostream>
#include <string>

#include "ai/level1_action.hpp"

using tuide::Level1ActionKind;
using tuide::parse_level1_action;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    const auto a = parse_level1_action(R"({"action":"tool","name":"search","arg":"Foo"})");
    expect(a.kind == Level1ActionKind::Tool && a.tool_name == "search" && a.arg == "Foo",
           "tool");
  }
  {
    const auto a = parse_level1_action(
        "Sure.\n{\"action\":\"seeds\",\"seeds\":[\"PayloadBuilder\",\"XClient\"],\"note\":\"plan\"}\n");
    expect(a.kind == Level1ActionKind::Seeds && a.seeds.size() == 2 &&
               a.seeds[0] == "PayloadBuilder",
           "seeds with prose");
  }
  {
    const auto a =
        parse_level1_action(R"({"action":"needs_level2","instruction":"rewrite foo","seeds":["Foo"]})");
    expect(a.kind == Level1ActionKind::NeedsLevel2 && a.instruction == "rewrite foo",
           "needs_level2");
  }
  {
    const auto a = parse_level1_action(R"({"action":"final","text":"listo"})");
    expect(a.kind == Level1ActionKind::Final && a.text == "listo", "final");
  }
  {
    const auto a = parse_level1_action("no json here");
    expect(a.kind == Level1ActionKind::Error, "missing json");
  }
  {
    // Shorthand that 1.5B models often emit.
    const auto a =
        parse_level1_action(R"({"action":"git_status","arg":"workspace"})");
    expect(a.kind == Level1ActionKind::Tool && a.tool_name == "git_status",
           "shorthand action=tool_name");
  }
  {
    const auto a = parse_level1_action(R"({"name":"search","arg":"Foo"})");
    expect(a.kind == Level1ActionKind::Tool && a.tool_name == "search" && a.arg == "Foo",
           "name without action");
  }

  {
    const auto a = parse_level1_action(
        R"({"action":"tool","name":"search","needles":["panel_performance","performance_panel"]})");
    expect(a.kind == Level1ActionKind::Tool && a.tool_name == "search" &&
               a.arg.find("panel_performance") != std::string::npos &&
               a.arg.find("performance_panel") != std::string::npos,
           "needles array → arg joined");
  }
  {
    // Truncated mid-array (max_tokens) + prose prefix — must still parse.
    std::string raw =
        "donde esta el codigo\n{\"action\":\"tool\",\"name\":\"search\",\"needles\":[\"explorer\","
        "\"explorer\",\"file_tree\"";
    const auto a = parse_level1_action(raw);
    expect(a.kind == Level1ActionKind::Tool && a.tool_name == "search" &&
               a.arg.find("explorer") != std::string::npos,
           "truncated JSON repaired");
    expect(a.arg.find("explorer|explorer") == std::string::npos, "needles deduped");
  }
  {
    const auto a = parse_level1_action(
        R"({"action":"tool","name":"search","needles":[...]})");
    expect(a.kind == Level1ActionKind::Error, "needles placeholder → error");
  }
  {
    // Echoed prompt example + real JSON at the end → take the LAST action object.
    std::string raw =
        "Formato: {\"action\":\"tool\",\"name\":\"search\",\"needles\":[\"module_name\","
        "\"ModuleName\",\"request_handler\"]}\n"
        "{\"action\":\"tool\",\"name\":\"search\",\"needles\":[\"editor_tab\",\"TabBar\"]}";
    const auto a = parse_level1_action(raw);
    expect(a.kind == Level1ActionKind::Tool && a.tool_name == "search" &&
               a.arg.find("editor_tab") != std::string::npos,
           "prefer last JSON action");
  }
  {
    const auto a = parse_level1_action(
        "Error: request (2247 tokens) exceeds the available context size (2048 tokens)");
    expect(a.kind == Level1ActionKind::Error, "context overflow → error not final");
  }
  {
    const auto a = parse_level1_action(
        "El código del panel está en src/ui/file_tree_panel.cpp y se rellena con el indexer.");
    expect(a.kind == Level1ActionKind::Final && a.text.find("file_tree_panel") != std::string::npos,
           "prose-only → final");
  }

  {
    const auto a = parse_level1_action(R"({"action":"pick_stem","stem":"performance_panel"})");
    expect(a.kind == Level1ActionKind::PickStem && a.stem == "performance_panel", "pick_stem");
  }
  {
    const auto a = parse_level1_action(
        "ok\n{\"action\":\"choose_stem\",\"stem\":\"settings_modal\"}\n");
    expect(a.kind == Level1ActionKind::PickStem && a.stem == "settings_modal",
           "choose_stem alias");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level1_agent_test ok\n";
  return 0;
}
