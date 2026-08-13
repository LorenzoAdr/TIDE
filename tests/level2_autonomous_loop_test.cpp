#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ai/l2_brain.hpp"
#include "ai/level2_autonomous_loop.hpp"
#include "ai/level2_session.hpp"
#include "ai/tool_registry.hpp"

namespace fs = std::filesystem;
using tuide::AiToolResult;
using tuide::L2Brain;
using tuide::L2BrainRequest;
using tuide::L2BrainResult;
using tuide::Level2AutonomousLoopOpts;
using tuide::Level2BootstrapOpts;
using tuide::Level2Session;
using tuide::Level2SessionDeps;
using tuide::ToolRegistry;
using tuide::run_level2_autonomous;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

class ScriptedBrain : public L2Brain {
 public:
  explicit ScriptedBrain(std::vector<std::string> script) : script_(std::move(script)) {}
  std::string name() const override { return "scripted"; }
  bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                    std::string*) override {
    return true;
  }
  L2BrainResult propose(const L2BrainRequest&, std::atomic<bool>*) override {
    L2BrainResult out;
    out.backend = "scripted";
    if (i_ >= script_.size()) {
      out.error = "script exhausted";
      return out;
    }
    out.ok = true;
    out.text = script_[i_++];
    return out;
  }

 private:
  std::vector<std::string> script_;
  std::size_t i_ = 0;
};

int main() {
  const fs::path root = fs::temp_directory_path() / "tuide_l2_auto_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".tuide" / "ai", ec);
  fs::create_directories(root / "src", ec);
  fs::create_directories(root / "tools", ec);
  {
    std::ofstream map(root / ".tuide" / "ai" / "map_last.md");
    map << "# Ranked map\n\nquery: bump\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `value`\n";
  }
  {
    std::ofstream foo(root / "src" / "foo.cpp");
    foo << "int value = 1;\n";
  }
  {
    std::ofstream sh(root / "tools" / "compile.sh");
    sh << "#!/bin/sh\nexit 0\n";
  }
  fs::permissions(root / "tools" / "compile.sh", fs::perms::owner_all, ec);

  ToolRegistry tools;
  tools.register_tool("get_code_of", "stub", [](const std::string& arg) {
    return AiToolResult{true, "int value = 1;\n — " + arg};
  });

  Level2Session session(Level2SessionDeps{&tools, {}, {}});
  Level2BootstrapOpts bopts;
  bopts.workspace_root = root.string();
  bopts.query = "cambia value a 2";
  bopts.instruction = "edit value";
  std::string err;
  expect(session.bootstrap(bopts, &err), "bootstrap " + err);

  ScriptedBrain brain({
      R"({"action":"tool","name":"get_code_of","arg":"src/foo.cpp:value"})",
      R"({"action":"done","summary":"src/foo.cpp:1 value","next":"edit"})",
      R"({"action":"edit","hunks":[{"path":"src/foo.cpp","search":"int value = 1;","replace":"int value = 2;"}]})",
  });

  Level2AutonomousLoopOpts opts;
  opts.workspace_root = root.string();
  opts.settings.max_steps = 8;
  opts.settings.max_tokens = 256;

  std::vector<std::string> log_lines;
  const auto result =
      run_level2_autonomous(session, brain, opts,
                            [&](const std::string& line) { log_lines.push_back(line); }, nullptr);

  expect(result.ok, "loop ok");
  expect(result.phase == "done", "phase done got=" + result.phase);
  expect(result.steps >= 3, "steps>=3");
  {
    std::ifstream in(root / "src" / "foo.cpp");
    std::ostringstream ss;
    ss << in.rdbuf();
    expect(ss.str().find("int value = 2;") != std::string::npos, "file edited");
  }
  bool saw_phase = false;
  for (const auto& line : log_lines) {
    if (line.find("L2 ▸ fase=") != std::string::npos) {
      saw_phase = true;
      break;
    }
  }
  expect(saw_phase, "phase streaming lines");

  // Caso B: el modelo salta done next=edit y emite edit aún en explore.
  {
    const fs::path root2 = fs::temp_directory_path() / "tuide_l2_auto_promo_test";
    fs::remove_all(root2, ec);
    fs::create_directories(root2 / ".tuide" / "ai", ec);
    fs::create_directories(root2 / "src", ec);
    fs::create_directories(root2 / "tools", ec);
    {
      std::ofstream map(root2 / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\nquery: bump\n";
    }
    {
      std::ofstream foo(root2 / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    {
      std::ofstream sh(root2 / "tools" / "compile.sh");
      sh << "#!/bin/sh\nexit 0\n";
    }
    fs::permissions(root2 / "tools" / "compile.sh", fs::perms::owner_all, ec);

    ToolRegistry tools2;
    tools2.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "int value = 1;\n — " + arg};
    });
    Level2Session session2(Level2SessionDeps{&tools2, {}, {}});
    Level2BootstrapOpts b2;
    b2.workspace_root = root2.string();
    b2.query = "bump";
    b2.instruction = "edit";
    expect(session2.bootstrap(b2, &err), "bootstrap promo " + err);

    ScriptedBrain brain2({
        R"({"action":"tool","name":"get_code_of","arg":"src/foo.cpp:value"})",
        // Salta done next=edit → runtime debe auto-promover.
        R"({"action":"edit","hunks":[{"path":"src/foo.cpp","search":"int value = 1;","replace":"int value = 3;"}]})",
    });
    Level2AutonomousLoopOpts opts2;
    opts2.workspace_root = root2.string();
    opts2.settings.max_steps = 8;
    std::vector<std::string> logs2;
    const auto r2 = run_level2_autonomous(
        session2, brain2, opts2, [&](const std::string& line) { logs2.push_back(line); }, nullptr);
    expect(r2.ok, "promo loop ok");
    expect(r2.phase == "done", "promo phase done got=" + r2.phase);
    {
      std::ifstream in(root2 / "src" / "foo.cpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("int value = 3;") != std::string::npos, "promo file edited");
    }
    bool saw_auto = false;
    for (const auto& line : logs2) {
      if (line.find("auto phase=edit") != std::string::npos) {
        saw_auto = true;
        break;
      }
    }
    expect(saw_auto, "saw auto phase=edit log");
  }

  if (failures == 0) {
    std::cout << "level2_autonomous_loop_test OK\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
