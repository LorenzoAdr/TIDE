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
    Level2Session sess4(Level2SessionDeps{&tools, {}, [](std::string*) { return 0; }});
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
    }
    expect(sessp.mark_done(rootp.string(), "ready", "edit").ok, "to edit after plan");
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

  fs::remove_all(root, ec);
  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level2_session_test OK\n";
  return 0;
}
