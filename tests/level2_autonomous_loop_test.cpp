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
    const std::string sess_cov = sess +
                                 "\n### turn 6 — post_edit_coverage\n\n"
                                 "### fresh `src/util/shell_utils.cpp`\n";
    const std::string lean_cov = Level2Session::last_edit_relevant_observation(sess_cov, 1600);
    expect(lean_cov.find("post_edit_coverage") != std::string::npos, "prefers latest coverage obs");
    expect(lean_cov.find("compile_feedback") == std::string::npos, "coverage hides older compile");
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

  // After partial sibling edit, next prompt is Aider coverage — not ranked-map JSON.
  {
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    const fs::path rootc = fs::temp_directory_path() / "tuide_l2_coverage_recover_test";
    fs::remove_all(rootc, ec);
    fs::create_directories(rootc / ".tuide" / "ai", ec);
    fs::create_directories(rootc / "src" / "util", ec);
    {
      std::ofstream map(rootc / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootc / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootc / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n"
             "bool command_exists(const std::string& c) { return !c.empty(); }\n}\n";
    }
    class CoverageBrain : public L2Brain {
     public:
      std::vector<std::string> users;
      std::string name() const override { return "coverage"; }
      bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                        std::string*) override {
        return true;
      }
      L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>*) override {
        users.push_back(req.user_prompt);
        L2BrainResult out;
        out.ok = true;
        out.backend = "coverage";
        if (users.size() == 1) {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              ">>>>>>> REPLACE\n";
        } else {
          out.text = R"({"action":"done","summary":"ok","next":"clarify"})";
        }
        return out;
      }
    };
    ToolRegistry toolsc;
    Level2SessionDeps depsc{&toolsc, {}, [](std::string* o) {
      if (o) {
        *o = "ok\n";
      }
      return 0;
    }};
    depsc.pack_incomplete_pushback_max = 0;
    Level2Session sessionc(depsc);
    Level2BootstrapOpts bc;
    bc.workspace_root = rootc.string();
    bc.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y en src/util/shell_utils.cpp";
    bc.instruction = "edit both";
    expect(sessionc.bootstrap(bc, &err), "bootstrap coverage recover " + err);
    expect(sessionc.mark_done(rootc.string(), "ready", "edit").ok, "to edit for coverage recover");
    CoverageBrain brainc;
    Level2AutonomousLoopOpts optsc;
    optsc.workspace_root = rootc.string();
    optsc.settings.max_steps = 4;
    optsc.workflow = "agent";
    const auto rcov = run_level2_autonomous(sessionc, brainc, optsc, nullptr, nullptr);
    (void)rcov;
    expect(brainc.users.size() >= 2, "second prompt after partial coverage edit");
    bool saw_cov_note = false;
    bool saw_map_json = false;
    for (std::size_t i = 1; i < brainc.users.size(); ++i) {
      if (brainc.users[i].find("Coverage incompleta") != std::string::npos) {
        saw_cov_note = true;
      }
      if (brainc.users[i].find("mapa inicial completo") != std::string::npos) {
        saw_map_json = true;
      }
    }
    expect(saw_cov_note, "user prompt has Aider coverage recover note");
    expect(!saw_map_json, "coverage follow-up is not ranked-map JSON");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    fs::remove_all(rootc, ec);
  }

  // After Instruction is covered, invalid JSON auto-dones (no ranked map, no clarify loop).
  {
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    const fs::path rootd = fs::temp_directory_path() / "tuide_l2_closeout_auto_done";
    fs::remove_all(rootd, ec);
    fs::create_directories(rootd / ".tuide" / "ai", ec);
    fs::create_directories(rootd / "src" / "util", ec);
    {
      std::ofstream map(rootd / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootd / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootd / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n"
             "bool command_exists(const std::string& c) { return !c.empty(); }\n}\n";
    }
    class CloseoutBrain : public L2Brain {
     public:
      std::vector<std::string> users;
      std::string name() const override { return "closeout"; }
      bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                        std::string*) override {
        return true;
      }
      L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>*) override {
        users.push_back(req.user_prompt);
        L2BrainResult out;
        out.ok = true;
        out.backend = "closeout";
        if (users.size() == 1) {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              ">>>>>>> REPLACE\n";
        } else if (users.size() == 2) {
          out.text =
              "src/util/shell_utils.cpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c) { return !c.empty(); }\n"
              "=======\n"
              "bool command_exists(const std::string& c) { return !c.empty(); }\n"
              "bool always_true() { return true; }\n"
              ">>>>>>> REPLACE\n";
        } else {
          out.text = "listo, no hay JSON aqui";
        }
        return out;
      }
    };
    ToolRegistry toolsd;
    Level2SessionDeps depsd{&toolsd, {}, [](std::string* o) {
      if (o) {
        *o = "ok\n";
      }
      return 0;
    }};
    depsd.pack_incomplete_pushback_max = 0;
    Level2Session sessiond(depsd);
    Level2BootstrapOpts bd;
    bd.workspace_root = rootd.string();
    bd.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y en src/util/shell_utils.cpp";
    bd.instruction = "edit both";
    expect(sessiond.bootstrap(bd, &err), "bootstrap closeout " + err);
    expect(sessiond.mark_done(rootd.string(), "ready", "edit").ok, "to edit for closeout");
    CloseoutBrain braind;
    Level2AutonomousLoopOpts optsd;
    optsd.workspace_root = rootd.string();
    optsd.settings.max_steps = 6;
    optsd.workflow = "agent";
    std::vector<std::string> logd;
    const auto rd = run_level2_autonomous(
        sessiond, braind, optsd, [&](const std::string& line) { logd.push_back(line); }, nullptr);
    expect(rd.ok, "closeout run ok");
    expect(rd.phase == "done", "invalid JSON after coverage → done, got=" + rd.phase);
    bool saw_closeout_prompt = false;
    bool saw_map_on_closeout = false;
    for (std::size_t i = 0; i < braind.users.size(); ++i) {
      if (braind.users[i].find("Instruction cubierta") != std::string::npos) {
        saw_closeout_prompt = true;
      }
      if (i + 1 == braind.users.size() &&
          braind.users[i].find("mapa inicial completo") != std::string::npos) {
        saw_map_on_closeout = true;
      }
    }
    expect(saw_closeout_prompt, "closeout prompt prefers done");
    expect(!saw_map_on_closeout, "closeout prompt has no ranked map");
    bool saw_auto = false;
    for (const auto& line : logd) {
      if (line.find("closeout auto-done") != std::string::npos) {
        saw_auto = true;
        break;
      }
    }
    expect(saw_auto, "log mentions closeout auto-done");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    fs::remove_all(rootd, ec);
  }

  // Extra edit after Instruction is covered → auto-done, tree kept (no duplicate decls).
  {
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    const fs::path roote = fs::temp_directory_path() / "tuide_l2_closeout_extra_edit";
    fs::remove_all(roote, ec);
    fs::create_directories(roote / ".tuide" / "ai", ec);
    fs::create_directories(roote / "src" / "util", ec);
    {
      std::ofstream map(roote / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(roote / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(roote / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n"
             "bool command_exists(const std::string& c) { return !c.empty(); }\n}\n";
    }
    class ExtraEditBrain : public L2Brain {
     public:
      int n = 0;
      std::string name() const override { return "extra-edit"; }
      bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                        std::string*) override {
        return true;
      }
      L2BrainResult propose(const L2BrainRequest&, std::atomic<bool>*) override {
        ++n;
        L2BrainResult out;
        out.ok = true;
        out.backend = "extra-edit";
        if (n == 1) {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              ">>>>>>> REPLACE\n";
        } else if (n == 2) {
          out.text =
              "src/util/shell_utils.cpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c) { return !c.empty(); }\n"
              "=======\n"
              "bool command_exists(const std::string& c) { return !c.empty(); }\n"
              "bool always_true() { return true; }\n"
              ">>>>>>> REPLACE\n";
        } else {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              "bool always_true();\n"
              ">>>>>>> REPLACE\n";
        }
        return out;
      }
    };
    ToolRegistry toolse;
    Level2SessionDeps depse{&toolse, {}, [](std::string* o) {
      if (o) {
        *o = "ok\n";
      }
      return 0;
    }};
    depse.pack_incomplete_pushback_max = 0;
    Level2Session sessione(depse);
    Level2BootstrapOpts be;
    be.workspace_root = roote.string();
    be.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y en src/util/shell_utils.cpp";
    be.instruction = "edit both";
    expect(sessione.bootstrap(be, &err), "bootstrap extra-edit " + err);
    expect(sessione.mark_done(roote.string(), "ready", "edit").ok, "to edit extra-edit");
    ExtraEditBrain braine;
    Level2AutonomousLoopOpts optse;
    optse.workspace_root = roote.string();
    optse.settings.max_steps = 8;
    optse.workflow = "agent";
    std::vector<std::string> loge;
    const auto re = run_level2_autonomous(
        sessione, braine, optse, [&](const std::string& line) { loge.push_back(line); }, nullptr);
    expect(re.ok, "extra-edit closeout ok");
    expect(re.phase == "done", "extra edit after coverage → done, got=" + re.phase);
    bool saw_extra = false;
    for (const auto& line : loge) {
      if (line.find("edit extra tras Instruction cubierta") != std::string::npos) {
        saw_extra = true;
        break;
      }
    }
    expect(saw_extra, "log mentions extra-edit auto-done");
    {
      std::ifstream in(roote / "src" / "util" / "shell_utils.hpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      const std::string hpp = ss.str();
      int n = 0;
      for (std::size_t p = 0; (p = hpp.find("always_true", p)) != std::string::npos; ++n) {
        p += 11;
      }
      expect(n == 1, "hpp kept a single always_true, got " + std::to_string(n) + ": " + hpp);
    }
    {
      std::ifstream in(roote / "src" / "util" / "shell_utils.cpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("return true") != std::string::npos, "cpp body kept");
    }
    expect(braine.n == 3, "third propose was the extra edit");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    fs::remove_all(roote, ec);
  }

  // Re-edit of a covered path is rejected while Instruction still has gaps.
  {
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    const fs::path rootg = fs::temp_directory_path() / "tuide_l2_covered_path_gap";
    fs::remove_all(rootg, ec);
    fs::create_directories(rootg / ".tuide" / "ai", ec);
    fs::create_directories(rootg / "src" / "util", ec);
    {
      std::ofstream map(rootg / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootg / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootg / "src" / "util" / "shell_utils.cpp");
      cpp << "bool command_exists(const std::string& c) { return !c.empty(); }\n";
    }
    class GapBrain : public L2Brain {
     public:
      std::vector<std::string> users;
      std::string name() const override { return "gap"; }
      bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                        std::string*) override {
        return true;
      }
      L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>*) override {
        users.push_back(req.user_prompt);
        L2BrainResult out;
        out.ok = true;
        out.backend = "gap";
        if (users.size() == 1) {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              ">>>>>>> REPLACE\n";
        } else if (users.size() == 2) {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              ">>>>>>> REPLACE\n";
        } else if (users.size() == 3) {
          out.text =
              "src/util/shell_utils.cpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c) { return !c.empty(); }\n"
              "=======\n"
              "bool command_exists(const std::string& c) { return !c.empty(); }\n"
              "bool always_true() { return true; }\n"
              ">>>>>>> REPLACE\n";
        } else {
          out.text = R"({"action":"done","summary":"always_true en hpp y cpp"})";
        }
        return out;
      }
    };
    ToolRegistry toolsg;
    Level2SessionDeps depsg{&toolsg, {}, [](std::string* o) {
      if (o) {
        *o = "ok\n";
      }
      return 0;
    }};
    depsg.pack_incomplete_pushback_max = 0;
    Level2Session sessiong(depsg);
    Level2BootstrapOpts bg;
    bg.workspace_root = rootg.string();
    bg.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y en src/util/shell_utils.cpp";
    bg.instruction = "edit both";
    expect(sessiong.bootstrap(bg, &err), "bootstrap covered-gap " + err);
    expect(sessiong.mark_done(rootg.string(), "ready", "edit").ok, "to edit covered-gap");
    GapBrain braing;
    Level2AutonomousLoopOpts optsg;
    optsg.workspace_root = rootg.string();
    optsg.settings.max_steps = 6;
    optsg.workflow = "agent";
    std::vector<std::string> logg;
    const auto rg = run_level2_autonomous(
        sessiong, braing, optsg, [&](const std::string& line) { logg.push_back(line); }, nullptr);
    (void)rg;
    bool saw_covered = false;
    for (const auto& line : logg) {
      if (line.find("edit_covered_path") != std::string::npos) {
        saw_covered = true;
        break;
      }
    }
    expect(saw_covered, "loop logs edit_covered_path");
    {
      std::ifstream in(rootg / "src" / "util" / "shell_utils.cpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("always_true") != std::string::npos,
             "cpp edited after covered-path nudge");
    }
    expect(sessiong.status_text(rootg.string()).find("phase: clarify") == std::string::npos,
           "did not clarify from hpp loop");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    fs::remove_all(rootg, ec);
  }

  // Covered-path loop hits the reject cap and stops without the cpp.
  {
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    const fs::path rootc = fs::temp_directory_path() / "tuide_l2_covered_path_cap";
    fs::remove_all(rootc, ec);
    fs::create_directories(rootc / ".tuide" / "ai", ec);
    fs::create_directories(rootc / "src" / "util", ec);
    {
      std::ofstream map(rootc / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootc / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootc / "src" / "util" / "shell_utils.cpp");
      cpp << "bool command_exists(const std::string& c) { return !c.empty(); }\n";
    }
    class StubbornHppBrain : public L2Brain {
     public:
      int n = 0;
      std::vector<std::string> users;
      std::string name() const override { return "stubborn"; }
      bool ensure_ready(const tuide::AiSettings&, const std::function<void(const std::string&)>&,
                        std::string*) override {
        return true;
      }
      L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>*) override {
        ++n;
        users.push_back(req.user_prompt);
        L2BrainResult out;
        out.ok = true;
        out.backend = "stubborn";
        if (n == 1) {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              ">>>>>>> REPLACE\n";
        } else {
          out.text =
              "src/util/shell_utils.hpp\n<<<<<<< SEARCH\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              "=======\n"
              "bool command_exists(const std::string& c);\nbool always_true();\n"
              ">>>>>>> REPLACE\n";
        }
        return out;
      }
    };
    ToolRegistry toolsc;
    Level2SessionDeps depsc{&toolsc, {}, [](std::string* o) {
      if (o) {
        *o = "ok\n";
      }
      return 0;
    }};
    depsc.pack_incomplete_pushback_max = 0;
    Level2Session sessionc(depsc);
    Level2BootstrapOpts bc;
    bc.workspace_root = rootc.string();
    bc.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y en src/util/shell_utils.cpp";
    bc.instruction = "edit both";
    expect(sessionc.bootstrap(bc, &err), "bootstrap covered-cap " + err);
    expect(sessionc.mark_done(rootc.string(), "ready", "edit").ok, "to edit covered-cap");
    StubbornHppBrain brainc;
    Level2AutonomousLoopOpts optsc;
    optsc.workspace_root = rootc.string();
    optsc.settings.max_steps = 12;
    optsc.workflow = "agent";
    std::vector<std::string> logc;
    const auto rc = run_level2_autonomous(
        sessionc, brainc, optsc, [&](const std::string& line) { logc.push_back(line); }, nullptr);
    expect(rc.phase == "done", "cap ends in done not timeout");
    expect(rc.summary.find("covered_path_limit") != std::string::npos,
           "loop summary is covered_path_limit: " + rc.summary);
    {
      std::ifstream in(rootc / "src" / "util" / "shell_utils.cpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("always_true") == std::string::npos, "cpp not edited after cap");
    }
    {
      std::ifstream in(rootc / "src" / "util" / "shell_utils.hpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("always_true") != std::string::npos, "hpp kept after cap");
    }
    expect(brainc.users.size() >= 2, "coverage turn was proposed");
    if (brainc.users.size() >= 2) {
      const std::string& u = brainc.users[1];
      expect(u.find("pack original omitido") != std::string::npos, "omits frozen pack on coverage");
      const auto fresh = u.find("### fresh `src/util/shell_utils.cpp`");
      const auto firma = u.find("firma (referencia, NO edites) `src/util/shell_utils.hpp`");
      expect(fresh != std::string::npos, "prompt has cpp fresh");
      expect(firma != std::string::npos, "prompt has hpp firma");
      expect(fresh < firma, "cpp before hpp firma in prompt");
    }
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    fs::remove_all(rootc, ec);
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

  {
    // Phase A locate: seed → a_judge → a_done without writing pack.md
    setenv("L2_FEAT_L2_EXPLORE_PHASE_A", "1", 1);
    const fs::path rootA = fs::temp_directory_path() / "tuide_l2_phase_a_test";
    std::error_code ec;
    fs::remove_all(rootA, ec);
    fs::create_directories(rootA / ".tuide" / "ai", ec);
    fs::create_directories(rootA / "src", ec);
    {
      std::ofstream map(rootA / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\n\nquery: spinner\n\n## Ranked entries\n\n"
             "1. src/busy.cpp:1 — `set_busy`\n";
    }
    {
      std::ofstream f(rootA / "src" / "busy.cpp");
      f << "void set_busy() {}\nint other() { return 0; }\n";
    }
    ToolRegistry toolsA;
    toolsA.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "void set_busy() {}\n — " + arg};
    });
    Level2Session sessionA(Level2SessionDeps{&toolsA, {}, {}});
    Level2BootstrapOpts bA;
    bA.workspace_root = rootA.string();
    bA.query = "spinner stuck";
    bA.instruction = "fix spinner";
    std::string errA;
    expect(sessionA.bootstrap(bA, &errA), "phaseA bootstrap " + errA);
    expect(sessionA.status_text(rootA.string()).find("phase: explore_a") != std::string::npos,
           "bootstrap phase explore_a");

    std::vector<tuide::AQueueBuildInput> ranked;
    {
      tuide::AQueueBuildInput in;
      in.file = "src/busy.cpp";
      in.name = "set_busy";
      in.line = 1;
      in.score = 100;
      in.functionish = true;
      in.body_lines = 5;
      ranked.push_back(in);
    }
    {
      tuide::AQueueBuildInput in;
      in.file = "src/busy.cpp";
      in.name = "other";
      in.line = 2;
      in.score = 50;
      in.functionish = true;
      in.body_lines = 3;
      ranked.push_back(in);
    }
    expect(sessionA.seed_a_queue(rootA.string(), ranked).ok, "seed_a_queue");
    expect(!sessionA.build_a_peek_tranche_markdown(rootA.string()).empty(), "peek tranche");

    std::vector<tuide::AVerdict> vs;
    {
      tuide::AVerdict v;
      v.target = "src/busy.cpp:set_busy";
      v.verdict = tuide::AVerdictKind::Useful;
      v.anchor = "src/busy.cpp:set_busy";
      v.stem = "busy";
      v.role = tuide::ALocusRole::Primary;
      v.why = "sets busy flag";
      vs.push_back(v);
    }
    {
      tuide::AVerdict v;
      v.target = "src/busy.cpp:other";
      v.verdict = tuide::AVerdictKind::Reject;
      v.why = "unrelated";
      vs.push_back(v);
    }
    const auto jdg = sessionA.apply_a_judge(rootA.string(), vs, false);
    expect(jdg.ok, "a_judge " + jdg.error);

    // plan must be rejected during explore_a
    const auto bad_plan =
        sessionA.apply_plan(rootA.string(), {"src/busy.cpp:set_busy"}, "too soon");
    expect(!bad_plan.ok, "plan blocked in explore_a");

    std::vector<tuide::ALocus> loci;
    {
      tuide::ALocus loc;
      loc.stem = "busy";
      loc.anchor = "src/busy.cpp:set_busy";
      loc.role = tuide::ALocusRole::Primary;
      loc.why = "busy control";
      loci.push_back(loc);
    }
    const auto ad = sessionA.apply_a_done(rootA.string(), loci, "locked busy");
    expect(ad.ok && ad.phase == "explore_b", "a_done → explore_b");

    // P4: plan outside loci rejected
    const auto bad_b =
        sessionA.apply_plan(rootA.string(), {"src/unrelated/foo.cpp:bar"}, "noise");
    expect(!bad_b.ok || bad_b.error == "plan_outside_loci" ||
               bad_b.summary.find("plan_outside_loci") != std::string::npos,
           "plan outside loci blocked");

    // Auto-plan from watchlist (empty targets) builds pack
    const auto good_b = sessionA.apply_plan(rootA.string(), {}, "from loci");
    expect(good_b.ok, "plan from watchlist " + good_b.error);
    expect(fs::exists(rootA / ".tuide" / "ai" / "l2" / "pack.md", ec), "pack after B plan");

    const auto ast = Level2Session::load_a_state(rootA.string());
    expect(ast.done && ast.loci_draft.size() == 1, "a_state loci");

    // P5: LSP tools denied in explore_b; local get_code_of allowed via apply_tool path
    // (explore_a already blocks tools). Re-enter explore_b after plan — still no LSP.
    expect(!tuide::Level2Session::tool_allowed_in_phase("hover", "explore_b"),
           "hover denied in explore_b");
    expect(!tuide::Level2Session::tool_allowed_in_phase("workspace_symbols", "explore_a"),
           "workspace_symbols denied in explore_a");
    expect(tuide::Level2Session::tool_allowed_in_phase("get_code_of", "explore_b"),
           "get_code_of allowed local");
    expect(tuide::Level2Session::tool_allowed_in_phase("hover", "edit"),
           "hover still allowed in edit");

    unsetenv("L2_FEAT_L2_EXPLORE_PHASE_A");
    fs::remove_all(rootA, ec);
  }

  {
    // P5: A→B with registry that has NO LSP tools (clangd absent).
    setenv("L2_FEAT_L2_EXPLORE_PHASE_A", "1", 1);
    const fs::path rootN = fs::temp_directory_path() / "tuide_l2_no_lsp_test";
    std::error_code ec;
    fs::remove_all(rootN, ec);
    fs::create_directories(rootN / ".tuide" / "ai", ec);
    fs::create_directories(rootN / "src", ec);
    {
      std::ofstream map(rootN / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\n\nquery: wake\n\n1. src/wake.cpp:1 — `should_wake`\n";
    }
    {
      std::ofstream f(rootN / "src" / "wake.cpp");
      f << "bool should_wake() { return true; }\n";
    }
    ToolRegistry toolsN;
    // Intentionally no hover / workspace_symbols / definition.
    toolsN.register_tool("get_code_of", "local", [](const std::string& arg) {
      return AiToolResult{true, "bool should_wake() { return true; }\n — " + arg};
    });
    toolsN.register_tool("file_outline", "local", [](const std::string& arg) {
      return AiToolResult{true, "should_wake @1\n — " + arg};
    });
    toolsN.register_tool("search", "local",
                         [](const std::string&) { return AiToolResult{true, "src/wake.cpp:1\n"}; });
    Level2Session sessionN(Level2SessionDeps{&toolsN, {}, {}});
    Level2BootstrapOpts bN;
    bN.workspace_root = rootN.string();
    bN.query = "wake policy";
    bN.instruction = "fix should_wake";
    std::string errN;
    expect(sessionN.bootstrap(bN, &errN), "no-lsp bootstrap " + errN);

    std::vector<tuide::AQueueBuildInput> rankedN;
    {
      tuide::AQueueBuildInput in;
      in.file = "src/wake.cpp";
      in.name = "should_wake";
      in.line = 1;
      in.score = 100;
      in.body_lines = 3;
      rankedN.push_back(in);
    }
    expect(sessionN.seed_a_queue(rootN.string(), rankedN).ok, "no-lsp seed");
    expect(!sessionN.build_a_peek_tranche_markdown(rootN.string()).empty(), "no-lsp peek");

    std::vector<tuide::AVerdict> vsN;
    {
      tuide::AVerdict v;
      v.target = "src/wake.cpp:should_wake";
      v.verdict = tuide::AVerdictKind::Useful;
      v.anchor = "src/wake.cpp:should_wake";
      v.stem = "wake";
      v.role = tuide::ALocusRole::Primary;
      v.why = "policy";
      vsN.push_back(v);
    }
    expect(sessionN.apply_a_judge(rootN.string(), vsN, false).ok, "no-lsp judge");

    std::vector<tuide::ALocus> lociN;
    {
      tuide::ALocus loc;
      loc.stem = "wake";
      loc.anchor = "src/wake.cpp:should_wake";
      loc.role = tuide::ALocusRole::Primary;
      lociN.push_back(loc);
    }
    expect(sessionN.apply_a_done(rootN.string(), lociN, "locked").ok, "no-lsp a_done");
    const auto planN = sessionN.apply_plan(rootN.string(), {}, "from loci");
    expect(planN.ok, "no-lsp plan " + planN.error);
    expect(fs::exists(rootN / ".tuide" / "ai" / "l2" / "pack.md", ec), "no-lsp pack");

    // LSP tool must be rejected even if somehow registered later
    toolsN.register_tool("hover", "lsp",
                         [](const std::string&) { return AiToolResult{true, "should not run"}; });
    const auto hover = sessionN.apply_tool(rootN.string(), "hover", "src/wake.cpp:1:0");
    expect(!hover.ok || hover.error.find("LSP") != std::string::npos ||
               hover.error.find("no permitido") != std::string::npos,
           "no-lsp hover blocked");

    unsetenv("L2_FEAT_L2_EXPLORE_PHASE_A");
    fs::remove_all(rootN, ec);
  }

  if (failures == 0) {
    std::cout << "level2_autonomous_loop_test OK\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
