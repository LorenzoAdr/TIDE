#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ai/l2_brain.hpp"
#include "ai/l2_grammar.hpp"
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

static fs::path repo_root_for_tests() {
  fs::path repo = fs::path(__FILE__).parent_path().parent_path();
  if (fs::exists(repo / "tools/l2_battery/grammars/l2_json.gbnf")) {
    return repo;
  }
  repo = fs::current_path();
  if (fs::exists(repo / "tools/l2_battery/grammars/l2_json.gbnf")) {
    return repo;
  }
  return fs::current_path().parent_path();
}

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
      // compile OK returns to edit; model must explicitly done
      R"({"action":"done","summary":"value=2 in src/foo.cpp"})",
  });

  Level2AutonomousLoopOpts opts;
  opts.workspace_root = root.string();
  opts.settings.max_steps = 8;
  opts.settings.max_tokens = 256;
  opts.budget = tuide::budget_from_n_ctx(8192, "local");
  expect(opts.budget.prompt_explore == 10000, "injected budget explore");

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
        R"({"action":"done","summary":"value=3"})",
    });
    Level2AutonomousLoopOpts opts2;
    opts2.workspace_root = root2.string();
    opts2.settings.max_steps = 10;
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

  // Anti-loop: repeated get_code_of after covering pack forces phase=edit.
  {
    const fs::path rootl = fs::temp_directory_path() / "tuide_l2_anti_loop_test";
    fs::remove_all(rootl, ec);
    fs::create_directories(rootl / ".tuide" / "ai", ec);
    fs::create_directories(rootl / "src", ec);
    fs::create_directories(rootl / "tools", ec);
    {
      std::ofstream map(rootl / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\nquery: bump\n";
    }
    {
      std::ofstream foo(rootl / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    {
      std::ofstream sh(rootl / "tools" / "compile.sh");
      sh << "#!/bin/sh\nexit 0\n";
    }
    fs::permissions(rootl / "tools" / "compile.sh", fs::perms::owner_all, ec);
    ToolRegistry toolsl;
    toolsl.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "int value = 1;\n — " + arg};
    });
    toolsl.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg};
    });
    Level2Session sessionl(Level2SessionDeps{&toolsl, {}, {}});
    Level2BootstrapOpts bl;
    bl.workspace_root = rootl.string();
    bl.query = "bump";
    bl.instruction = "edit value";
    expect(sessionl.bootstrap(bl, &err), "bootstrap anti-loop " + err);
    ScriptedBrain brainl({
        R"({"action":"plan","targets":["src/foo.cpp:value"],"summary":"go"})",
        R"({"action":"tool","name":"get_code_of","arg":"src/foo.cpp:value#tail"})",
        R"({"action":"tool","name":"get_code_of","arg":"src/foo.cpp:value#tail"})",
        R"({"action":"edit","hunks":[{"path":"src/foo.cpp","search":"int value = 1;","replace":"int value = 9;"}]})",
        R"({"action":"done","summary":"value=9"})",
    });
    Level2AutonomousLoopOpts optsl;
    optsl.workspace_root = rootl.string();
    optsl.settings.max_steps = 10;
    std::vector<std::string> logsl;
    const auto rl = run_level2_autonomous(
        sessionl, brainl, optsl, [&](const std::string& line) { logsl.push_back(line); }, nullptr);
    expect(rl.ok, "anti-loop ok");
    expect(rl.phase == "done", "anti-loop done got=" + rl.phase);
    bool saw_force = false;
    for (const auto& line : logsl) {
      if (line.find("force phase=edit") != std::string::npos ||
          line.find("tool loop tras pack") != std::string::npos) {
        saw_force = true;
        break;
      }
    }
    expect(saw_force, "saw force phase=edit after repeated tool");
    {
      std::ifstream in(rootl / "src" / "foo.cpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("int value = 9;") != std::string::npos, "anti-loop file edited");
    }
    fs::remove_all(rootl, ec);
  }

  // Repeated covering plan in explore forces phase=edit (modelo no emite done/edit).
  {
    const fs::path rootp = fs::temp_directory_path() / "tuide_l2_plan_loop_test";
    fs::remove_all(rootp, ec);
    fs::create_directories(rootp / ".tuide" / "ai", ec);
    fs::create_directories(rootp / "src", ec);
    fs::create_directories(rootp / "tools", ec);
    {
      std::ofstream map(rootp / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\nquery: bump\n";
    }
    {
      std::ofstream foo(rootp / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    {
      std::ofstream sh(rootp / "tools" / "compile.sh");
      sh << "#!/bin/sh\nexit 0\n";
    }
    fs::permissions(rootp / "tools" / "compile.sh", fs::perms::owner_all, ec);
    ToolRegistry toolsp;
    toolsp.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "int value = 1;\n — " + arg};
    });
    toolsp.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg};
    });
    Level2Session sessionp(Level2SessionDeps{&toolsp, {}, {}});
    Level2BootstrapOpts bp;
    bp.workspace_root = rootp.string();
    bp.query = "bump";
    bp.instruction = "edit value";
    expect(sessionp.bootstrap(bp, &err), "bootstrap plan-loop " + err);
    ScriptedBrain brainp({
        R"({"action":"plan","targets":["src/foo.cpp:value"],"summary":"go"})",
        R"({"action":"plan","targets":["src/foo.cpp:value"],"summary":"again"})",
        R"({"action":"tool","name":"sibling_of","arg":"src/foo.cpp"})",
        R"({"action":"edit","hunks":[{"path":"src/foo.cpp","search":"int value = 1;","replace":"int value = 7;"}]})",
        R"({"action":"done","summary":"value=7"})",
    });
    Level2AutonomousLoopOpts optsp;
    optsp.workspace_root = rootp.string();
    optsp.settings.max_steps = 10;
    std::vector<std::string> logsp;
    const auto rp = run_level2_autonomous(
        sessionp, brainp, optsp, [&](const std::string& line) { logsp.push_back(line); }, nullptr);
    expect(rp.ok, "plan-loop ok");
    expect(rp.phase == "done", "plan-loop done got=" + rp.phase);
    bool saw_plan_force = false;
    bool saw_edit_phase = false;
    bool saw_plan_ignored = false;
    for (const auto& line : logsp) {
      if (line.find("plan loop tras pack") != std::string::npos) {
        saw_plan_force = true;
      }
      if (line.find("fase=edit") != std::string::npos || line.find("phase=edit") != std::string::npos) {
        saw_edit_phase = true;
      }
      if (line.find("tool ignorado en phase=edit") != std::string::npos ||
          line.find("plan ignorado en phase=edit") != std::string::npos) {
        saw_plan_ignored = true;
      }
    }
    expect(saw_plan_force, "saw force phase=edit after repeated plan");
    expect(saw_edit_phase, "reached phase=edit after plan loop");
    expect(saw_plan_ignored, "ignored extra tool in phase=edit");
    {
      std::ifstream in(rootp / "src" / "foo.cpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("int value = 7;") != std::string::npos, "plan-loop file edited");
    }
    fs::remove_all(rootp, ec);
  }

  // Compile fail x3 returns to edit; loop must not spin in phase=compile.
  {
    const fs::path rootc = fs::temp_directory_path() / "tuide_l2_compile_spin_test";
    fs::remove_all(rootc, ec);
    fs::create_directories(rootc / ".tuide" / "ai", ec);
    fs::create_directories(rootc / "src", ec);
    fs::create_directories(rootc / "tools", ec);
    {
      std::ofstream map(rootc / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\nquery: bump\n";
    }
    {
      std::ofstream foo(rootc / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    {
      std::ofstream sh(rootc / "tools" / "compile.sh");
      sh << "#!/bin/sh\nexit 1\n";
    }
    fs::permissions(rootc / "tools" / "compile.sh", fs::perms::owner_all, ec);
    ToolRegistry toolsc;
    toolsc.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "int value = 1;\n — " + arg};
    });
    Level2Session sessionc(Level2SessionDeps{&toolsc, {}, {}});
    Level2BootstrapOpts bc;
    bc.workspace_root = rootc.string();
    bc.query = "bump";
    bc.instruction = "edit value";
    expect(sessionc.bootstrap(bc, &err), "bootstrap compile-spin " + err);
    class PhaseBrain : public L2Brain {
     public:
      std::vector<std::string> phases;
      std::string name() const override { return "phase"; }
      bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                        std::string*) override {
        return true;
      }
      L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>*) override {
        phases.push_back(req.phase);
        L2BrainResult out;
        out.ok = true;
        out.backend = "phase";
        if (phases.size() == 1) {
          out.text = R"({"action":"tool","name":"get_code_of","arg":"src/foo.cpp:value"})";
        } else if (phases.size() == 2) {
          out.text = R"({"action":"done","summary":"ready","next":"edit"})";
        } else {
          out.text =
              R"({"action":"edit","hunks":[{"path":"src/foo.cpp","search":"int value = 1;","replace":"int value = 8;"}]})";
        }
        return out;
      }
    };
    PhaseBrain brainc;
    Level2AutonomousLoopOpts optsc;
    optsc.workspace_root = rootc.string();
    optsc.settings.max_steps = 8;
    std::vector<std::string> logsc;
    const auto rc = run_level2_autonomous(
        sessionc, brainc, optsc, [&](const std::string& line) { logsc.push_back(line); }, nullptr);
    int n_compile_phase = 0;
    int n_edit_propose = 0;
    for (const auto& p : brainc.phases) {
      if (p == "compile") {
        ++n_compile_phase;
      }
      if (p == "edit") {
        ++n_edit_propose;
      }
    }
    expect(n_compile_phase == 0, "model never proposed in phase=compile");
    expect(n_edit_propose >= 3, "kept proposing edit after compile fails got=" +
                                    std::to_string(n_edit_propose));
    int n_loop_compile = 0;
    for (const auto& line : logsc) {
      if (line.find("fase=compile") != std::string::npos) {
        ++n_loop_compile;
      }
    }
    expect(n_loop_compile == 0, "loop did not spin fase=compile");
    (void)rc;
    fs::remove_all(rootc, ec);
  }

  // Lean observations: last compile_feedback whole turn, drop unrelated on-disk excerpts.
  {
    const std::string sess = R"(# L2 session
## Instruction
query: foo

## Observations

### turn 2 — tool `get_code_of`
int x;

### turn 4 — compile_feedback

path: `src/util/shell_utils.hpp`

```stderr (tail)
error: something
```

## on disk `src/ui/terminal_keyboard.cpp` (base del search en el hermano)
void enable_extended_key_reporting() { huge dump }

## on disk `src/util/shell_utils.hpp` (base del search)
bool command_exists();

### turn 5 — plan
ignore me
)";
    const std::string lean = Level2Session::last_edit_relevant_observation(sess, 1600);
    expect(lean.find("compile_feedback") != std::string::npos, "keeps compile_feedback");
    expect(lean.find("terminal_keyboard") == std::string::npos, "drops unrelated on disk");
    expect(lean.find("shell_utils.hpp") != std::string::npos, "keeps failed path on disk");
    expect(lean.find("get_code_of") == std::string::npos, "drops explore tool turns");
    expect(lean.find("…[cola]…") == std::string::npos, "no byte-tail marker");
    const std::string guide = Level2Session::tool_guide_edit_markdown();
    expect(guide.find("primera mirada") == std::string::npos &&
               guide.find("Ranked map") == std::string::npos,
           "edit guide has no explore table");
    expect(guide.find("<<<<<<< SEARCH") != std::string::npos, "edit guide has Aider SEARCH");
  }

  // JSON invalid streak: recover note is injected into the next user prompt.
  {
    const fs::path rootj = fs::temp_directory_path() / "tuide_l2_json_recover_test";
    fs::remove_all(rootj, ec);
    fs::create_directories(rootj / ".tuide" / "ai", ec);
    fs::create_directories(rootj / "src", ec);
    fs::create_directories(rootj / "tools", ec);
    {
      std::ofstream map(rootj / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\nquery: bump\n";
    }
    {
      std::ofstream foo(rootj / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    {
      std::ofstream sh(rootj / "tools" / "compile.sh");
      sh << "#!/bin/sh\nexit 0\n";
    }
    fs::permissions(rootj / "tools" / "compile.sh", fs::perms::owner_all, ec);
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);

    class CaptureBrain : public L2Brain {
     public:
      std::vector<std::string> users;
      std::vector<std::string> systems;
      std::string name() const override { return "capture"; }
      bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                        std::string*) override {
        return true;
      }
      L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>*) override {
        users.push_back(req.user_prompt);
        systems.push_back(req.system_prompt);
        L2BrainResult out;
        out.ok = true;
        out.backend = "capture";
        if (users.size() == 1) {
          out.text = R"({"action":"edit","hunks":[)";  // truncated JSON
        } else {
          out.text = R"({"action":"done","summary":"ok","next":"clarify"})";
        }
        return out;
      }
    };

    ToolRegistry toolsj;
    Level2Session sessionj(Level2SessionDeps{&toolsj, {}, {}});
    Level2BootstrapOpts bj;
    bj.workspace_root = rootj.string();
    bj.query = "bump";
    bj.instruction = "edit";
    expect(sessionj.bootstrap(bj, &err), "bootstrap json recover " + err);
    CaptureBrain brainj;
    Level2AutonomousLoopOpts optsj;
    optsj.workspace_root = rootj.string();
    optsj.settings.max_steps = 4;
    optsj.workflow = "agent";
    const auto rj = run_level2_autonomous(sessionj, brainj, optsj, nullptr, nullptr);
    expect(brainj.users.size() >= 2, "retried after invalid json");
    bool saw_recover = false;
    for (std::size_t i = 1; i < brainj.users.size(); ++i) {
      if (brainj.users[i].find("JSON inválido") != std::string::npos) {
        saw_recover = true;
        break;
      }
    }
    (void)rj;
    expect(saw_recover, "user prompt contains JSON recover note");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    fs::remove_all(rootj, ec);
  }

  {
    setenv("L2_FEAT_JSON_GRAMMAR", "1", 1);
    const fs::path repo = repo_root_for_tests();
    const fs::path rootg = fs::temp_directory_path() / "tuide_l2_grammar_test";
    std::error_code ec;
    fs::remove_all(rootg, ec);
    fs::create_directories(rootg / "tools/l2_battery/grammars", ec);
    fs::copy_file(repo / "tools/l2_battery/grammars/l2_json.gbnf",
                  rootg / "tools/l2_battery/grammars/l2_json.gbnf", ec);
    fs::copy_file(repo / "tools/l2_battery/grammars/l2_edit.gbnf",
                  rootg / "tools/l2_battery/grammars/l2_edit.gbnf", ec);
    const std::string explore =
        tuide::l2_grammar::resolve_for_phase(rootg.string(), "explore");
    const std::string edit =
        tuide::l2_grammar::resolve_for_phase(rootg.string(), "edit");
    expect(explore.empty(), "explore has no grammar (C only on edit/compile)");
    expect(!edit.empty() && edit.find("l2_edit.gbnf") != std::string::npos, "edit grammar path");
    setenv("L2_FEAT_JSON_GRAMMAR", "0", 1);
    expect(tuide::l2_grammar::resolve_for_phase(rootg.string(), "edit").empty(),
           "grammar off when flag 0");
    unsetenv("L2_FEAT_JSON_GRAMMAR");
    fs::remove_all(rootg, ec);
  }

  if (failures == 0) {
    std::cout << "level2_autonomous_loop_test OK\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
