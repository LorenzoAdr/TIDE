#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ai/level2_session.hpp"
#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"

namespace fs = std::filesystem;
using tuide::AiToolResult;
using tuide::Level2BootstrapOpts;
using tuide::Level2Session;
using tuide::Level2SessionDeps;
using tuide::SearchReplaceHunk;
using tuide::ToolRegistry;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

std::string read_all(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

int main() {
  const fs::path root = fs::temp_directory_path() / "tuide_l2_phase_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".tuide" / "ai", ec);
  fs::create_directories(root / "src", ec);

  {
    std::ofstream map(root / ".tuide" / "ai" / "map_last.md");
    map << "# Ranked map\n\nquery: test\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `foo`\n";
  }
  {
    std::ofstream foo(root / "src" / "foo.cpp");
    foo << "int value = 1;\n";
  }
  // Fake compile.sh that succeeds.
  fs::create_directories(root / "tools", ec);
  {
    std::ofstream sh(root / "tools" / "compile.sh");
    sh << "#!/bin/sh\nexit 0\n";
  }
  fs::permissions(root / "tools" / "compile.sh", fs::perms::owner_all, ec);

  ToolRegistry tools;
    tools.register_tool("get_code_of", "stub", [](const std::string& arg) {
      // Simulate a truncated long body with structured metadata.
      if (arg.find("huge") != std::string::npos) {
        return AiToolResult{
            true,
            std::string("src/foo.cpp:1-200 (huge) [TRUNCATED]\n") +
                "symbol_span: 1-200\n"
                "sent: 1-66 + 155-200\n"
                "missing_lines: 67-154\n"
                "refetch: get_code_of `src/foo.cpp:67-120` (o `#head`/`#mid`/`#tail` / `path:A-B`)\n"
                "note: cuerpo incompleto\n"
                "int huge() {\n  // head\n… [omitted lines 67-154] …\n  return 0;\n}\n"};
      }
      return AiToolResult{true, "body " + arg};
    });

  Level2Session session(Level2SessionDeps{&tools, {}, {}});
  Level2BootstrapOpts opts;
  opts.workspace_root = root.string();
  opts.query = "test fix loop";
  opts.instruction = "edit value";
  std::string err;
  expect(session.bootstrap(opts, &err), "bootstrap " + err);
  {
    const std::string sess0 = read_all(Level2Session::session_path(root.string()));
    expect(sess0.find("## Tool guide") == std::string::npos,
           "bootstrap session must not duplicate tool guide (lives in system prompt)");
    expect(sess0.find("## Instruction") != std::string::npos, "has Instruction");
    expect(sess0.find("## Ranked map") != std::string::npos, "has Ranked map");
  }

  {
    const auto tr = session.apply_tool(root.string(), "get_code_of", "src/foo.cpp:value");
    expect(tr.ok && tr.phase == "explore", "explore tool");
  }
  {
    const auto tr = session.apply_plan(root.string(), {"src/foo.cpp:value"}, "ready");
    expect(tr.ok, "plan before edit: " + tr.error);
  }
  {
    const auto tr = session.mark_done(root.string(), "ready", "edit");
    expect(tr.ok && tr.phase == "edit", "phase edit");
  }
  {
    SearchReplaceHunk h;
    h.path = "src/foo.cpp";
    h.search = "int value = 1;\n";
    h.replace = "int value = 2;\n";
    const auto tr = session.apply_edit(root.string(), {h});
    expect(tr.ok, "edit+compile ok: " + tr.error + " / " + tr.summary);
    expect(tr.phase == "edit", "compile ok resumes edit, got=" + tr.phase);
    expect(read_all(root / "src" / "foo.cpp").find("value = 2") != std::string::npos, "file edited");
    const std::string sess = read_all(Level2Session::session_path(root.string()));
    expect(sess.find("compile_ok") != std::string::npos, "compile_ok observation");
    expect(sess.find("cierra la tarea") != std::string::npos,
           "hint that compile is not task end");
    const auto fin = session.mark_done(root.string(), "value bumped in foo.cpp", "");
    expect(fin.ok && fin.phase == "done", "explicit done ends session");
  }

  // Ambiguous / unique parser
  {
    tuide::SearchReplaceSpan sp;
    std::string e;
    expect(!tuide::find_unique_span("a a", "a", &sp, &e), "ambiguous reject");
  }

  // Tail truncation keeps last lines
  {
    std::ostringstream long_log;
    for (int i = 1; i <= 100; ++i) {
      long_log << "line " << i << '\n';
    }
    const std::string trunc = Level2Session::truncate_observation_tail(long_log.str(), 40);
    expect(trunc.find("showing last 40 of 100") != std::string::npos, "tail marker");
    expect(trunc.find("line 61\n") != std::string::npos, "includes line 61");
    expect(trunc.find("line 100\n") != std::string::npos, "includes line 100");
    expect(trunc.find("line 60\n") == std::string::npos, "drops early lines");
  }

  // Compile fail stores stderr tail only (no full 200-line dump)
  {
    const fs::path root3 = fs::temp_directory_path() / "tuide_l2_compile_tail_test";
    fs::remove_all(root3, ec);
    fs::create_directories(root3 / ".tuide" / "ai", ec);
    fs::create_directories(root3 / "src", ec);
    {
      std::ofstream map(root3 / ".tuide" / "ai" / "map_last.md");
      map << "query: compile fail\n";
    }
    {
      std::ofstream foo(root3 / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    Level2SessionDeps deps{&tools, {}, [](std::string* out) {
      std::ostringstream log;
      for (int i = 1; i <= 80; ++i) {
        log << "noise " << i << '\n';
      }
      log << "error: boom at end\n";
      if (out) {
        *out = log.str();
      }
      return 1;
    }};
    deps.pack_incomplete_pushback_max = 0;
    Level2Session sess3(deps);
    Level2BootstrapOpts opts3;
    opts3.workspace_root = root3.string();
    opts3.query = "fix compile";
    expect(sess3.bootstrap(opts3, &err), "bootstrap3 " + err);
    expect(sess3.mark_done(root3.string(), "ready", "edit").ok, "to edit");
    SearchReplaceHunk h;
    h.path = "src/foo.cpp";
    h.search = "int value = 1;\n";
    h.replace = "int value = 3;\n";
    const auto tr = sess3.apply_edit(root3.string(), {h});
    expect(!tr.ok || tr.phase == "edit", "stays edit after fail");
    const std::string session = read_all(Level2Session::session_path(root3.string()));
    expect(session.find("compile_feedback") != std::string::npos, "compile_feedback block");
    expect(session.find("stderr (tail)") != std::string::npos, "stderr tail label");
    expect(session.find("error: boom at end") != std::string::npos, "keeps error tail");
    expect(session.find("noise 1\n") == std::string::npos, "drops early noise");
    expect(session.find("showing last") != std::string::npos, "shows last N marker");
    fs::remove_all(root3, ec);
  }

  // Edit apply fail → edit_feedback observation (breaks same-hunk resonance)
  {
    const fs::path root4 = fs::temp_directory_path() / "tuide_l2_edit_fail_obs_test";
    fs::remove_all(root4, ec);
    fs::create_directories(root4 / ".tuide" / "ai", ec);
    fs::create_directories(root4 / "src", ec);
    {
      std::ofstream map(root4 / ".tuide" / "ai" / "map_last.md");
      map << "query: edit fail\n";
    }
    {
      std::ofstream foo(root4 / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    Level2SessionDeps deps4{&tools, {}, [](std::string*) { return 0; }};
    deps4.pack_incomplete_pushback_max = 0;
    Level2Session sess4(deps4);
    Level2BootstrapOpts opts4;
    opts4.workspace_root = root4.string();
    opts4.query = "bad hunk";
    expect(sess4.bootstrap(opts4, &err), "bootstrap4 " + err);
    expect(sess4.mark_done(root4.string(), "ready", "edit").ok, "to edit4");
    SearchReplaceHunk bad;
    bad.path = "src/foo.cpp";
    bad.search = "this_string_does_not_exist_xyz\n";
    bad.replace = "int value = 9;\n";
    const auto tr = sess4.apply_edit(root4.string(), {bad});
    expect(!tr.ok && tr.phase == "edit", "edit fail stays edit");
    expect(tr.error.find("no encontrado") != std::string::npos ||
               tr.error.find("hunk falló") != std::string::npos,
           "error mentions fail: " + tr.error);
    const std::string session = read_all(Level2Session::session_path(root4.string()));
    expect(session.find("edit_feedback") != std::string::npos, "edit_feedback block");
    expect(session.find("this_string_does_not_exist_xyz") != std::string::npos,
           "keeps failed search in obs");
    expect(session.find("No repitas el mismo hunk") != std::string::npos, "retry hint");
    expect(read_all(root4 / "src" / "foo.cpp").find("value = 1") != std::string::npos,
           "file untouched");
    fs::remove_all(root4, ec);
  }

  // Map compaction: after tool, cold entries → name-only; hot stem keeps detail.
  {
    const fs::path rootc = fs::temp_directory_path() / "tuide_l2_compact_test";
    fs::remove_all(rootc, ec);
    fs::create_directories(rootc / ".tuide" / "ai", ec);
    fs::create_directories(rootc / "src", ec);
    {
      std::ofstream map(rootc / ".tuide" / "ai" / "map_last.md");
      map << "query: compact\n\n## Ranked entries\n\n";
      map << "1. src/hot.cpp:10  [score=99] — `hot_fn`\n";
      map << "    `void hot_fn() {`\n";
      map << "    why: stem=hot · role=core\n";
      map << "    ```\n    void hot_fn() { return; }\n    ```\n";
      map << "2. src/cold.cpp:1  [score=1] — `cold_fn`\n";
      map << "    `void cold_fn() {`\n";
      map << "    why: stem=cold\n";
      map << "    ```\n    void cold_fn() { return; }\n    ```\n";
    }
    {
      std::ofstream f(rootc / "src" / "hot.cpp");
      f << "void hot_fn() {}\n";
    }
    Level2Session sessc(Level2SessionDeps{&tools, {}, {}});
    Level2BootstrapOpts optsc;
    optsc.workspace_root = rootc.string();
    optsc.query = "compact map";
    expect(sessc.bootstrap(optsc, &err), "bootstrap compact " + err);
    const auto before = read_all(Level2Session::session_path(rootc.string()));
    expect(before.find("void cold_fn() { return; }") != std::string::npos, "cold body pre");
    expect(sessc.apply_tool(rootc.string(), "get_code_of", "src/hot.cpp:hot_fn").ok,
           "tool hot");
    const auto after = read_all(Level2Session::session_path(rootc.string()));
    expect(after.find("map compacted") != std::string::npos, "compact marker");
    expect(after.find("void hot_fn() { return; }") != std::string::npos, "hot detail kept");
    expect(after.find("void cold_fn() { return; }") == std::string::npos,
           "cold body stripped");
    expect(after.find("2. src/cold.cpp:1  [score=1] — `cold_fn`") != std::string::npos,
           "cold name line kept");
    // Pure helper
    const auto hot_keys = Level2Session::hot_keys_from_observations(
        "### turn 1 — `get_code_of` `src/hot.cpp:hot_fn`\n");
    expect(!hot_keys.empty(), "hot keys non-empty");
    bool has_hot = false;
    for (const auto& k : hot_keys) {
      if (k.find("hot") != std::string::npos) {
        has_hot = true;
      }
    }
    expect(has_hot, "hot key mentions hot");
    fs::remove_all(rootc, ec);
  }

  // Batch tools in one propose
  {
    const fs::path rootb = fs::temp_directory_path() / "tuide_l2_tools_batch_test";
    fs::remove_all(rootb, ec);
    fs::create_directories(rootb / ".tuide" / "ai", ec);
    fs::create_directories(rootb / "src", ec);
    {
      std::ofstream map(rootb / ".tuide" / "ai" / "map_last.md");
      map << "query: batch\n\n## Ranked entries\n\n1. src/a.cpp:1 — `a`\n";
    }
    tools.register_tool("file_outline", "stub", [](const std::string& arg) {
      return tuide::AiToolResult{true, "outline: " + arg + "  symbols=1\n"};
    });
    Level2Session sessb(Level2SessionDeps{&tools, {}, {}});
    Level2BootstrapOpts optsb;
    optsb.workspace_root = rootb.string();
    optsb.query = "batch tools";
    expect(sessb.bootstrap(optsb, &err), "bootstrap batch " + err);
    std::vector<tuide::L2ToolCall> calls = {{"get_code_of", "src/foo.cpp:value"},
                                            {"file_outline", "src/foo.cpp"}};
    // Ensure foo exists for get_code_of stub (registry stub returns body)
    {
      std::ofstream f(rootb / "src" / "foo.cpp");
      f << "int value = 1;\n";
    }
    const auto tr = sessb.apply_tools(rootb.string(), calls);
    expect(tr.ok, "apply_tools ok");
    expect(tr.summary.find("n=2") != std::string::npos, "summary n=2: " + tr.summary);
    const std::string sess = read_all(Level2Session::session_path(rootb.string()));
    expect(sess.find("`get_code_of`") != std::string::npos, "obs get_code_of");
    expect(sess.find("`file_outline`") != std::string::npos, "obs file_outline");
    fs::remove_all(rootb, ec);
  }

  // Explore fail → clarify pushback then accept
  {
    const fs::path root2 = fs::temp_directory_path() / "tuide_l2_clarify_test";
    fs::remove_all(root2, ec);
    fs::create_directories(root2 / ".tuide" / "ai", ec);
    {
      std::ofstream map(root2 / ".tuide" / "ai" / "map_last.md");
      map << "query: vague\n";
    }
    Level2SessionDeps deps{&tools, {}, {}, /*clarify_pushback_max=*/2};
    Level2Session sess2(deps);
    Level2BootstrapOpts opts2;
    opts2.workspace_root = root2.string();
    opts2.query = "algo vago";
    expect(sess2.bootstrap(opts2, &err), "bootstrap2");
    {
      const auto tr = sess2.mark_done(root2.string(), "no encontré; ¿módulo?", "clarify");
      expect(tr.ok && tr.phase == "explore", "first clarify → pushback stay explore");
      expect(tr.summary.find("clarify_pushback") != std::string::npos, "pushback summary");
      expect(read_all(Level2Session::session_path(root2.string())).find("clarify_pushback") !=
                 std::string::npos,
             "pushback in session");
    }
    {
      const auto tr = sess2.mark_done(root2.string(), "aún no", "clarify");
      expect(tr.ok && tr.phase == "explore", "second clarify → pushback 2/2");
    }
    {
      const auto tr = sess2.mark_done(root2.string(), "definitivo ¿módulo?", "clarify");
      expect(tr.ok && tr.phase == "clarify", "third clarify accepted");
      expect(read_all(Level2Session::session_path(root2.string())).find("arreglo cancelado") !=
                 std::string::npos,
             "final clarify in session");
    }
    fs::remove_all(root2, ec);
  }

  // clarify_pushback_max=0 accepts immediately
  {
    const fs::path rootz = fs::temp_directory_path() / "tuide_l2_clarify_zero";
    fs::remove_all(rootz, ec);
    fs::create_directories(rootz / ".tuide" / "ai", ec);
    {
      std::ofstream map(rootz / ".tuide" / "ai" / "map_last.md");
      map << "query: z\n";
    }
    Level2Session sessz(Level2SessionDeps{&tools, {}, {}, 0});
    Level2BootstrapOpts optsz;
    optsz.workspace_root = rootz.string();
    optsz.query = "z";
    expect(sessz.bootstrap(optsz, &err), "bootstrapz");
    const auto tr = sessz.mark_done(rootz.string(), "need info", "clarify");
    expect(tr.ok && tr.phase == "clarify", "max=0 accepts clarify");
    fs::remove_all(rootz, ec);
  }

  // plan → pack + map_initial; compile_ok → map_review
  {
    const fs::path rootp = fs::temp_directory_path() / "tuide_l2_plan_pack_test";
    fs::remove_all(rootp, ec);
    fs::create_directories(rootp / ".tuide" / "ai", ec);
    fs::create_directories(rootp / "src", ec);
    {
      std::ofstream map(rootp / ".tuide" / "ai" / "map_last.md");
      map << "query: plan\n\n## Ranked entries\n\n"
             "1. src/foo.cpp:1 — `value` score=9\n"
             "   snippet: int value\n\n"
             "2. src/bar.cpp:1 — `bar` score=3\n";
    }
    {
      std::ofstream foo(rootp / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    if (!tools.has("file_outline")) {
      tools.register_tool("file_outline", "stub", [](const std::string& arg) {
        return AiToolResult{true, "outline " + arg + " symbols=1\n"};
      });
    }
    Level2SessionDeps deps{&tools, {}, [](std::string*) { return 0; }};
    Level2Session sessp(deps);
    Level2BootstrapOpts optsp;
    optsp.workspace_root = rootp.string();
    optsp.query = "plan pack";
    optsp.instruction = "bump value";
    expect(sessp.bootstrap(optsp, &err), "bootstrap plan " + err);
    expect(!read_all(Level2Session::map_initial_path(rootp.string())).empty(), "map_initial saved");
    {
      const auto tr =
          sessp.apply_plan(rootp.string(), {"src/foo.cpp:value", "src/foo.cpp:huge"}, "watch");
      expect(tr.ok, "apply_plan ok: " + tr.error);
      expect(tr.summary.find("pack=") != std::string::npos, "pack summary");
      const std::string pack = read_all(Level2Session::pack_path(rootp.string()));
      expect(pack.find("## Fragments") != std::string::npos, "pack fragments");
      expect(pack.find("## Outlines") != std::string::npos, "pack outlines");
      expect(pack.find("get_code_of") != std::string::npos, "pack get_code_of");
      expect(pack.find("## Truncated") != std::string::npos, "pack Truncated index");
      expect(pack.find("src/foo.cpp:67-120") != std::string::npos ||
                 pack.find("#mid") != std::string::npos,
             "pack refetch hint");
      expect(sessp.status_text(rootp.string()).find("has_pack: yes") != std::string::npos,
             "has_pack yes");
      // Truncation alone does not mark incomplete when Instruction facets are covered.
      expect(sessp.status_text(rootp.string()).find("pack_incomplete: no") != std::string::npos,
             "pack_incomplete no despite truncated");
      expect(tr.summary.find("incomplete=1") == std::string::npos, "summary not incomplete");
    }
    {
      const auto tr = sessp.mark_done(rootp.string(), "ready", "edit");
      expect(tr.ok && tr.phase == "edit", "truncated-only pack → edit without pushback");
    }
    {
      SearchReplaceHunk h;
      h.path = "src/foo.cpp";
      h.search = "int value = 1;\n";
      h.replace = "int value = 7;\n";
      const auto tr = sessp.apply_edit(rootp.string(), {h});
      expect(tr.ok && tr.phase == "edit", "edit+compile after plan");
      expect(tr.summary.find("map_review") != std::string::npos ||
                 sessp.status_text(rootp.string()).find("map_review: yes") != std::string::npos,
             "map_review after compile_ok");
      const std::string sess = read_all(Level2Session::session_path(rootp.string()));
      expect(sess.find("¿Algo más?") != std::string::npos ||
                 sess.find("Algo más") != std::string::npos,
             "algo más hint in session");
      // Restored map should bring back the initial ranked entry detail.
      expect(sess.find("score=9") != std::string::npos ||
                 sess.find("`value`") != std::string::npos,
             "initial map restored into session");
    }
    fs::remove_all(rootp, ec);
  }

  // pack_incomplete from Instruction gaps (not truncation) → pushback on done --edit.
  {
    const fs::path rootg = root.parent_path() / "tuide_l2_pack_gaps";
    fs::remove_all(rootg, ec);
    fs::create_directories(rootg / ".tuide" / "ai", ec);
    fs::create_directories(rootg / "src", ec);
    {
      std::ofstream map(rootg / ".tuide" / "ai" / "map_last.md");
      map << "query: shortcut\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `value`\n";
    }
    {
      std::ofstream foo(rootg / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    ToolRegistry toolsg;
    toolsg.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "src/foo.cpp:1-1 (value)\nbody " + arg};
    });
    toolsg.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + " symbols=1\n"};
    });
    Level2Session sessg(Level2SessionDeps{&toolsg, {}, [](std::string*) { return 0; }});
    Level2BootstrapOpts optsg;
    optsg.workspace_root = rootg.string();
    optsg.query = "Ctrl+Shift+M ToggleLineMark shortcut";
    optsg.instruction = "add keybind shortcut ToggleLineMark";
    expect(sessg.bootstrap(optsg, &err), "bootstrap gaps " + err);
    {
      const auto tr = sessg.apply_plan(rootg.string(), {"src/foo.cpp:value"}, "thin");
      expect(tr.ok, "plan gaps ok: " + tr.error);
      expect(sessg.status_text(rootg.string()).find("pack_incomplete: yes") != std::string::npos,
             "pack_incomplete yes from missing Instruction idents");
      expect(tr.summary.find("incomplete=1") != std::string::npos, "summary incomplete");
    }
    {
      const auto push = sessg.mark_done(rootg.string(), "ready", "edit");
      expect(push.ok, "pushback returns ok");
      expect(push.summary.find("pack_incomplete_pushback") != std::string::npos,
             "pack_incomplete pushback on next=edit");
      expect(push.phase == "explore", "still explore after pushback");
      const std::string sess = read_all(Level2Session::session_path(rootg.string()));
      expect(sess.find("togglelinemark") != std::string::npos ||
                 sess.find("ToggleLineMark") != std::string::npos ||
                 sess.find("re-plan anclado") != std::string::npos,
             "pushback lists missing Instruction idents");
    }
    expect(sessg.mark_done(rootg.string(), "retry", "edit").ok, "second pushback");
    {
      const auto tr = sessg.mark_done(rootg.string(), "force edit", "edit");
      expect(tr.ok && tr.phase == "edit", "to edit after pushback max");
    }
    fs::remove_all(rootg, ec);
  }

  // Soft plan nudge after 8 explore tools without plan.
  {
    const fs::path rootn = root.parent_path() / "tuide_l2_plan_nudge";
    fs::remove_all(rootn, ec);
    fs::create_directories(rootn / ".tuide" / "ai", ec);
    fs::create_directories(rootn / "src", ec);
    {
      std::ofstream map(rootn / ".tuide" / "ai" / "map_last.md");
      map << "query: nudge\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `value`\n";
    }
    {
      std::ofstream foo(rootn / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    ToolRegistry toolsn;
    toolsn.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "body " + arg};
    });
    Level2Session sessn(Level2SessionDeps{&toolsn, {}, {}});
    Level2BootstrapOpts optsn;
    optsn.workspace_root = rootn.string();
    optsn.query = "nudge plan";
    optsn.instruction = "find code";
    expect(sessn.bootstrap(optsn, &err), "bootstrap nudge " + err);
    for (int i = 0; i < Level2Session::kExplorePlanNudgeAfter; ++i) {
      expect(sessn.apply_tool(rootn.string(), "get_code_of", "src/foo.cpp:value").ok,
             "nudge tool " + std::to_string(i));
    }
    const std::string sess = read_all(Level2Session::session_path(rootn.string()));
    expect(sess.find("_nudge:_") != std::string::npos, "session contains plan nudge");
    expect(sess.find("action=plan") != std::string::npos, "nudge asks for plan");
    fs::remove_all(rootn, ec);
  }

  // Soft edit nudge after 8 extras once pack covers Instruction.
  {
    const fs::path roote = root.parent_path() / "tuide_l2_edit_nudge";
    fs::remove_all(roote, ec);
    fs::create_directories(roote / ".tuide" / "ai", ec);
    fs::create_directories(roote / "src", ec);
    {
      std::ofstream map(roote / ".tuide" / "ai" / "map_last.md");
      map << "query: bump helper_value\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `helper_value`\n";
    }
    {
      std::ofstream foo(roote / "src" / "foo.cpp");
      foo << "int helper_value = 1;\n";
    }
    ToolRegistry toolse;
    toolse.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "src/foo.cpp:1-1 (helper_value)\nint helper_value = 1;\n"};
    });
    toolse.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + " symbols=1\n  helper_value\n"};
    });
    Level2Session sesse(Level2SessionDeps{&toolse, {}, {}});
    Level2BootstrapOpts optse;
    optse.workspace_root = roote.string();
    optse.query = "bump helper_value";
    optse.instruction = "change helper_value";
    expect(sesse.bootstrap(optse, &err), "bootstrap edit nudge " + err);
    expect(sesse.apply_plan(roote.string(), {"src/foo.cpp:helper_value"}, "ready").ok,
           "plan for edit nudge");
    expect(sesse.status_text(roote.string()).find("pack_incomplete: no") != std::string::npos,
           "covering pack");
    expect(read_all(Level2Session::session_path(roote.string())).find("_nudge:_") == std::string::npos,
           "no nudge yet after covering plan");
    for (int i = 0; i < Level2Session::kPostPackEditNudgeAfter; ++i) {
      expect(sesse.apply_tool(roote.string(), "get_code_of", "src/foo.cpp:helper_value").ok,
             "post-pack tool " + std::to_string(i));
    }
    const std::string sess = read_all(Level2Session::session_path(roote.string()));
    expect(sess.find("_nudge:_") != std::string::npos, "session contains edit nudge");
    expect(sess.find("done next=edit") != std::string::npos, "nudge asks for edit");
    fs::remove_all(roote, ec);
  }

  // map_stale when map_last query poorly overlaps Instruction.
  {
    const fs::path roots = root.parent_path() / "tuide_l2_map_stale";
    fs::remove_all(roots, ec);
    fs::create_directories(roots / ".tuide" / "ai", ec);
    {
      std::ofstream map(roots / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\n\nquery: pty wakes terminal scrollback\n\n"
             "## Ranked entries\n\n1. src/pty.cpp:1 — `on_pty_output`\n";
    }
    ToolRegistry tools_s;
    Level2Session sesss(Level2SessionDeps{&tools_s, {}, {}});
    Level2BootstrapOpts optss;
    optss.workspace_root = roots.string();
    optss.query = "añade pestaña temporal";
    optss.instruction = "add temporary tab with fixed text";
    expect(sesss.bootstrap(optss, &err), "bootstrap map_stale " + err);
    const std::string sess = read_all(Level2Session::session_path(roots.string()));
    expect(sess.find("map_stale=1") != std::string::npos, "session warns map_stale");
    expect(sess.find("omitido") != std::string::npos, "stale map not injected into session");
    expect(sess.find("on_pty_output") == std::string::npos, "stale ranked entries omitted");
    expect(read_all(Level2Session::map_initial_path(roots.string())).find("on_pty_output") !=
               std::string::npos,
           "map_initial still has full stale map");
    expect(sesss.status_text(roots.string()).find("map_stale: yes") != std::string::npos,
           "status map_stale yes");
    fs::remove_all(roots, ec);
  }

  // Merge packs + bare-path normalize via outline needles.
  {
    const fs::path rootm = root.parent_path() / "tuide_l2_plan_merge";
    fs::remove_all(rootm, ec);
    fs::create_directories(rootm / ".tuide" / "ai", ec);
    fs::create_directories(rootm / "src", ec);
    {
      std::ofstream map(rootm / ".tuide" / "ai" / "map_last.md");
      map << "# Ranked map\n\nquery: bump helper value\n\n## Ranked entries\n\n"
             "1. src/foo.cpp:1 — `helper_value`\n";
    }
    {
      std::ofstream foo(rootm / "src" / "foo.cpp");
      foo << "int helper_value = 1;\nint other_thing = 2;\n";
    }
    ToolRegistry toolsm;
    toolsm.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "src/foo.cpp:1-1 (helper_value)\nbody " + arg};
    });
    toolsm.register_tool("file_outline", "stub", [](const std::string&) {
      return AiToolResult{true, "symbols=2\n  helper_value\n  other_thing\n"};
    });
    Level2Session sessm(Level2SessionDeps{&toolsm, {}, {}});
    Level2BootstrapOpts optsm;
    optsm.workspace_root = rootm.string();
    optsm.query = "bump helper value";
    optsm.instruction = "change helper_value constant";
    expect(sessm.bootstrap(optsm, &err), "bootstrap merge " + err);
    expect(sessm.status_text(rootm.string()).find("map_stale: no") != std::string::npos,
           "aligned map not stale");
    {
      // Stale pack.md from a prior session must not leak into plan1 after bootstrap.
      {
        std::ofstream stale(Level2Session::pack_path(rootm.string()));
        stale << "# L2 code pack\n\ntargets (2): `src/ui/console_panel.cpp:make_tab_button` "
                 "`src/ui/press_ids.hpp:kConsoleTabAi`\n";
      }
      expect(sessm.bootstrap(optsm, &err), "bootstrap clears stale pack");
      const auto tr0 = sessm.apply_plan(rootm.string(), {"src/foo.cpp:helper_value"}, "fresh");
      expect(tr0.ok, "plan after stale pack clear: " + tr0.error);
      const std::string pack0 = read_all(Level2Session::pack_path(rootm.string()));
      expect(pack0.find("make_tab_button") == std::string::npos,
             "stale pack targets not merged after bootstrap");
      expect(pack0.find("helper_value") != std::string::npos, "fresh plan target present");
    }
    {
      const auto tr = sessm.apply_plan(rootm.string(), {"src/foo.cpp"}, "bare");
      expect(tr.ok, "plan bare ok: " + tr.error);
      const std::string pack = read_all(Level2Session::pack_path(rootm.string()));
      expect(pack.find("## Target normalize") != std::string::npos, "normalize section");
      expect(pack.find("helper_value") != std::string::npos, "bare → helper_value");
      expect(sessm.status_text(rootm.string()).find("pack_incomplete: no") != std::string::npos,
             "no truncated → pack complete");
    }
    {
      // Bare path that cannot resolve should be omitted (not junk AST symbol).
      ToolRegistry toolsj;
      toolsj.register_tool("get_code_of", "stub", [](const std::string& arg) {
        if (arg.find("welcome") != std::string::npos) {
          return AiToolResult{true, "src/ui/press_ids.hpp:1-1 (welcome_recent)\nbody"};
        }
        return AiToolResult{true, "src/foo.cpp:1-1 (helper_value)\nbody " + arg};
      });
      toolsj.register_tool("file_outline", "stub", [](const std::string&) {
        return AiToolResult{true, "outline: x symbols=1\nvar welcome_recent :68\n"};
      });
      const fs::path rootj = root.parent_path() / "tuide_l2_bare_omit";
      fs::remove_all(rootj, ec);
      fs::create_directories(rootj / ".tuide" / "ai", ec);
      fs::create_directories(rootj / "src" / "ui", ec);
      {
        std::ofstream map(rootj / ".tuide" / "ai" / "map_last.md");
        map << "# Ranked map\n\nquery: add temporal tab\n\n## Ranked entries\n\n1. x\n";
      }
      {
        std::ofstream f(rootj / "src" / "ui" / "press_ids.hpp");
        f << "constexpr int kConsoleTabAi = 1;\n";
      }
      Level2Session sessj(Level2SessionDeps{&toolsj, {}, {}});
      Level2BootstrapOpts optsj;
      optsj.workspace_root = rootj.string();
      optsj.query = "add temporal tab";
      optsj.instruction = "add ConsolePanelTabs temporal make_tab_button";
      optsj.seeds = {"ConsolePanelTabs", "make_tab_button", "kConsoleTabAi"};
      expect(sessj.bootstrap(optsj, &err), "bootstrap bare omit");
      // Pre-seed observations with explore hits so needles include code idents.
      {
        std::string sess = read_all(Level2Session::session_path(rootj.string()));
        const auto obs = sess.find("## Observations");
        if (obs != std::string::npos) {
          sess.insert(obs + 16,
                      "\n### turn 0 — tool search\n\nsearch ConsolePanelTabs|make_tab_button\n\n");
          std::ofstream out(Level2Session::session_path(rootj.string()), std::ios::trunc);
          out << sess;
        }
      }
      const auto tr = sessj.apply_plan(rootj.string(), {"src/ui/press_ids.hpp"}, "bare");
      expect(tr.ok, "plan bare omit ok: " + tr.error);
      const std::string pack = read_all(Level2Session::pack_path(rootj.string()));
      expect(pack.find("welcome_recent") == std::string::npos ||
                 pack.find("omitido") != std::string::npos ||
                 pack.find("search-in-file") != std::string::npos ||
                 pack.find("sin símbolo claro") != std::string::npos,
             "no junk welcome_recent body (omit/normalize)");
      fs::remove_all(rootj, ec);
    }
    {
      const auto tr = sessm.apply_plan(rootm.string(), {"src/foo.cpp:other_thing"}, "merge");
      expect(tr.ok, "plan2 merge ok");
      const std::string pack = read_all(Level2Session::pack_path(rootm.string()));
      expect(pack.find("helper_value") != std::string::npos, "keeps plan1 target");
      expect(pack.find("other_thing") != std::string::npos, "adds plan2 target");
    }
    expect(sessm.mark_done(rootm.string(), "ready", "edit").ok &&
               sessm.status_text(rootm.string()).find("phase: edit") != std::string::npos,
           "complete pack → edit without pushback");
    fs::remove_all(rootm, ec);
  }

  // Sibling .hpp is packed with the .cpp (decl / #include surface).
  {
    const fs::path roots = fs::temp_directory_path() / "tuide_l2_sibling_hdr";
    fs::remove_all(roots, ec);
    fs::create_directories(roots / ".tuide" / "ai", ec);
    fs::create_directories(roots / "src", ec);
    {
      std::ofstream map(roots / ".tuide" / "ai" / "map_last.md");
      map << "query: helper_value\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `helper_value`\n";
    }
    {
      std::ofstream foo(roots / "src" / "foo.cpp");
      foo << "int helper_value() { return 1; }\n";
    }
    {
      std::ofstream hdr(roots / "src" / "foo.hpp");
      hdr << "#pragma once\n#include <cstdint>\nint helper_value();\nstruct FooDecl { int x; };\n";
    }
    ToolRegistry tools_s;
    tools_s.register_tool("get_code_of", "stub", [](const std::string& arg) {
      if (arg.find(".hpp") != std::string::npos) {
        return AiToolResult{true, "src/foo.hpp:1-20 (FooDecl)\n#pragma once\n#include <cstdint>\n"
                                  "int helper_value();\nstruct FooDecl { int x; };\n"};
      }
      return AiToolResult{true, "src/foo.cpp:1-10 (helper_value)\nint helper_value() { return 1; }\n"};
    });
    tools_s.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + "\nhelper_value\nFooDecl\n"};
    });
    tools_s.register_tool("headers_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "headers_of: " + arg + "\n#include <cstdint>\nsibling: src/foo.hpp\n"};
    });
    Level2Session sesss(Level2SessionDeps{&tools_s, {}, {}});
    Level2BootstrapOpts optss;
    optss.workspace_root = roots.string();
    optss.query = "helper_value";
    optss.instruction = "bump helper_value declaration";
    expect(sesss.bootstrap(optss, &err), "bootstrap sibling " + err);
    const auto tr = sesss.apply_plan(roots.string(), {"src/foo.cpp:helper_value"}, "sib");
    expect(tr.ok, "plan sibling: " + tr.error);
    const std::string pack = read_all(Level2Session::pack_path(roots.string()));
    expect(pack.find("src/foo.hpp") != std::string::npos, "pack mentions sibling hpp");
    expect(pack.find("sibling header") != std::string::npos, "normalize note for sibling");
    expect(pack.find("FooDecl") != std::string::npos || pack.find("#include") != std::string::npos,
           "pack keeps decl/include");
    expect(pack.find("## Headers") != std::string::npos, "Headers section present");
    fs::remove_all(roots, ec);
  }

  // Post-pack tool dumps are capped per turn (not 200 lines).
  {
    const fs::path rooto = fs::temp_directory_path() / "tuide_l2_obs_packed";
    fs::remove_all(rooto, ec);
    fs::create_directories(rooto / ".tuide" / "ai", ec);
    fs::create_directories(rooto / "src", ec);
    {
      std::ofstream map(rooto / ".tuide" / "ai" / "map_last.md");
      map << "query: helper_value\n";
    }
    {
      std::ofstream foo(rooto / "src" / "foo.cpp");
      foo << "int helper_value = 1;\n";
    }
    ToolRegistry toolso;
    toolso.register_tool("get_code_of", "stub", [](const std::string& arg) {
      if (arg.find("fat") != std::string::npos) {
        std::ostringstream o;
        for (int i = 1; i <= 200; ++i) {
          o << "FATLINE_" << i << " helper_value padding\n";
        }
        return AiToolResult{true, o.str()};
      }
      return AiToolResult{true, "int helper_value = 1;\n"};
    });
    toolso.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + "\nhelper_value\n"};
    });
    Level2Session sesso(Level2SessionDeps{&toolso, {}, {}});
    Level2BootstrapOpts optso;
    optso.workspace_root = rooto.string();
    optso.query = "helper_value";
    optso.instruction = "touch helper_value";
    expect(sesso.bootstrap(optso, &err), "bootstrap obs packed " + err);
    expect(sesso.apply_plan(rooto.string(), {"src/foo.cpp:helper_value"}, "p").ok, "plan obs");
    const auto tr = sesso.apply_tool(rooto.string(), "get_code_of", "src/foo.cpp:fat");
    expect(tr.ok, "fat tool ok");
    const std::string session = read_all(Level2Session::session_path(rooto.string()));
    expect(session.find("FATLINE_1 ") != std::string::npos, "keeps start of fat obs");
    expect(session.find("FATLINE_80 ") == std::string::npos, "drops far fat lines");
    expect(session.find("observation truncated") != std::string::npos ||
               session.find("packed observations truncated") != std::string::npos,
           "truncation marker post-pack");
    expect(session.size() < 20000, "session stays well under pre-pack 51k dumps");
    fs::remove_all(rooto, ec);
  }

  // Compile undeclared identifier → sibling-header hint.
  {
    const fs::path rootu = fs::temp_directory_path() / "tuide_l2_undecl_hint";
    fs::remove_all(rootu, ec);
    fs::create_directories(rootu / ".tuide" / "ai", ec);
    fs::create_directories(rootu / "src", ec);
    {
      std::ofstream map(rootu / ".tuide" / "ai" / "map_last.md");
      map << "query: undecl\n";
    }
    {
      std::ofstream foo(rootu / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    Level2SessionDeps depsu{&tools, {}, [](std::string* out) {
      if (out) {
        *out = "foo.cpp:3:10: error: 'ToggleLineMark' was not declared in this scope\n";
      }
      return 1;
    }};
    depsu.pack_incomplete_pushback_max = 0;
    Level2Session sessu(depsu);
    Level2BootstrapOpts optsu;
    optsu.workspace_root = rootu.string();
    optsu.query = "fix undecl";
    expect(sessu.bootstrap(optsu, &err), "bootstrap undecl " + err);
    expect(sessu.mark_done(rootu.string(), "ready", "edit").ok, "to edit undecl");
    SearchReplaceHunk h;
    h.path = "src/foo.cpp";
    h.search = "int value = 1;\n";
    h.replace = "int value = 4;\n";
    const auto tr = sessu.apply_edit(rootu.string(), {h});
    expect(!tr.ok || tr.phase == "edit", "stays edit after undecl");
    const std::string session = read_all(Level2Session::session_path(rootu.string()));
    expect(session.find("compile_feedback") != std::string::npos, "compile_feedback undecl");
    expect(session.find("Símbolos no declarados") != std::string::npos, "undeclared banner");
    expect(session.find("ToggleLineMark") != std::string::npos, "keeps ident");
    expect(session.find("header hermano") != std::string::npos, "sibling header hint");
    fs::remove_all(rootu, ec);
  }

  fs::remove_all(root, ec);
  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level2_session_test OK\n";
  return 0;
}
