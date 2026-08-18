#include <cassert>
#include <cstdlib>
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
    expect(sess.find("cierra la tarea") != std::string::npos ||
               sess.find("Instruction cubierta") != std::string::npos,
           "hint that compile is not task end or closeout done");
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
    expect(session.find("baseline pre-hunk") != std::string::npos, "rollback note");
    expect(read_all(root3 / "src" / "foo.cpp").find("value = 1") != std::string::npos,
           "file restored after compile fail");
    fs::remove_all(root3, ec);
  }

  // compile_ok then bad edit: fail restores to last good (keeps compile_ok bytes).
  {
    const fs::path rootg = fs::temp_directory_path() / "tuide_l2_compile_rollback_good";
    fs::remove_all(rootg, ec);
    fs::create_directories(rootg / ".tuide" / "ai", ec);
    fs::create_directories(rootg / "src", ec);
    {
      std::ofstream map(rootg / ".tuide" / "ai" / "map_last.md");
      map << "query: keep good\n";
    }
    {
      std::ofstream foo(rootg / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    int compiles = 0;
    Level2SessionDeps depsg{&tools, {}, [&](std::string* out) {
      ++compiles;
      if (out) {
        *out = compiles == 1 ? "ok\n" : "error: redeclared\n";
      }
      return compiles == 1 ? 0 : 1;
    }};
    depsg.pack_incomplete_pushback_max = 0;
    Level2Session sessg(depsg);
    Level2BootstrapOpts optsg;
    optsg.workspace_root = rootg.string();
    optsg.query = "keep good";
    expect(sessg.bootstrap(optsg, &err), "bootstrap good " + err);
    expect(sessg.mark_done(rootg.string(), "ready", "edit").ok, "to edit good");
    SearchReplaceHunk okh;
    okh.path = "src/foo.cpp";
    okh.search = "int value = 1;\n";
    okh.replace = "int value = 2;\n";
    expect(sessg.apply_edit(rootg.string(), {okh}).ok, "first compile ok");
    expect(read_all(rootg / "src" / "foo.cpp").find("value = 2") != std::string::npos,
           "good edit kept");
    SearchReplaceHunk badh;
    badh.path = "src/foo.cpp";
    badh.search = "int value = 2;\n";
    badh.replace = "int value = 2;\nint value = 2;\n";  // duplicate → "compile" fail
    const auto trb = sessg.apply_edit(rootg.string(), {badh});
    expect(!trb.ok && trb.phase == "edit", "second stays edit after fail");
    expect(read_all(rootg / "src" / "foo.cpp").find("value = 2") != std::string::npos,
           "still at compile_ok baseline");
    expect(read_all(rootg / "src" / "foo.cpp").find("value = 2;\nint value = 2") ==
               std::string::npos,
           "duplicate not left on disk");
    const std::string sess = read_all(Level2Session::session_path(rootg.string()));
    expect(sess.find("get_code_of") != std::string::npos ||
               sess.find("hunk Aider") != std::string::npos ||
               sess.find("Instruction cubierta") != std::string::npos,
           "compile_ok follow-up asks get_code_of, Aider hunk, or closeout done");
    fs::remove_all(rootg, ec);
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
    bad.search = "int this_string_does_not_exist_xyz = 0;\n";
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

  // Short/generic search rejected; identical hunk repeat → pushback then clarify.
  {
    const fs::path roote = fs::temp_directory_path() / "tuide_l2_edit_anti_loop";
    fs::remove_all(roote, ec);
    fs::create_directories(roote / ".tuide" / "ai", ec);
    fs::create_directories(roote / "src", ec);
    {
      std::ofstream map(roote / ".tuide" / "ai" / "map_last.md");
      map << "query: anti loop\n";
    }
    {
      std::ofstream foo(roote / "src" / "foo.cpp");
      foo << "void tick_terminal_shell() {}\nvoid other() { tick_terminal_shell(); }\n";
    }
    Level2SessionDeps depse{&tools, {}, [](std::string*) { return 0; }};
    depse.pack_incomplete_pushback_max = 0;
    Level2Session sesse(depse);
    Level2BootstrapOpts optse;
    optse.workspace_root = roote.string();
    optse.query = "anti loop";
    expect(sesse.bootstrap(optse, &err), "bootstrap anti " + err);
    expect(sesse.mark_done(roote.string(), "ready", "edit").ok, "to edit anti");
    SearchReplaceHunk short_h;
    short_h.path = "src/foo.cpp";
    short_h.search = "tick_terminal_shell";
    short_h.replace = "tick_terminal_shell\n  // bad\n";
    {
      const auto tr = sesse.apply_edit(roote.string(), {short_h});
      expect(!tr.ok && tr.phase == "edit", "short search stays edit");
      expect(tr.error.find("genérico") != std::string::npos ||
                 tr.error.find("corto") != std::string::npos,
             "short search error: " + tr.error);
    }
    {
      const auto tr = sesse.apply_edit(roote.string(), {short_h});
      expect(!tr.ok, "identical rejected");
      expect(tr.error.find("idéntico") != std::string::npos, "identical error: " + tr.error);
      const std::string sess = read_all(Level2Session::session_path(roote.string()));
      expect(sess.find("edit_repeat_pushback") != std::string::npos, "repeat pushback obs");
    }
    // One more identical → clarify (kMaxIdenticalEditRepeats=2).
    {
      const auto tr = sesse.apply_edit(roote.string(), {short_h});
      expect(tr.phase == "clarify", "clarify after identical repeats");
      expect(tr.ok, "clarify ends ok");
      expect(sesse.status_text(roote.string()).find("phase: clarify") != std::string::npos,
             "status clarify");
    }
    fs::remove_all(roote, ec);
  }

  // Reject opener-only search expanded into a full braced body (struct Foo { → whole type).
  {
    const fs::path roots = fs::temp_directory_path() / "tuide_l2_hunk_opener";
    fs::remove_all(roots, ec);
    fs::create_directories(roots / ".tuide" / "ai", ec);
    fs::create_directories(roots / "src", ec);
    {
      std::ofstream map(roots / ".tuide" / "ai" / "map_last.md");
      map << "query: opener\n";
    }
    {
      std::ofstream foo(roots / "src" / "panel.hpp");
      foo << "struct ConsolePanelTabs {\n"
             "  static constexpr int kShell = 0;\n"
             "  static constexpr int kOutput = 1;\n"
             "};\n";
    }
    Level2SessionDeps depss{&tools, {}, [](std::string*) { return 0; }};
    depss.pack_incomplete_pushback_max = 0;
    Level2Session sesss(depss);
    Level2BootstrapOpts optss;
    optss.workspace_root = roots.string();
    optss.query = "opener shape";
    expect(sesss.bootstrap(optss, &err), "bootstrap opener " + err);
    expect(sesss.mark_done(roots.string(), "ready", "edit").ok, "to edit opener");
    SearchReplaceHunk bad;
    bad.path = "src/panel.hpp";
    bad.search = "struct ConsolePanelTabs {";
    bad.replace =
        "struct ConsolePanelTabs {\n"
        "  static constexpr int kShell = 0;\n"
        "  static constexpr int kOutput = 1;\n"
        "  static constexpr int kTemp = 2;\n"
        "};\n";
    {
      const auto tr = sesss.apply_edit(roots.string(), {bad});
      expect(!tr.ok && tr.phase == "edit", "opener hunk rejected");
      expect(tr.error.find("opener") != std::string::npos ||
                 tr.error.find("mal formado") != std::string::npos,
             "opener error: " + tr.error);
      const std::string body = read_all((roots / "src" / "panel.hpp").string());
      expect(body.find("kTemp") == std::string::npos, "file unchanged after reject");
    }
    // Valid: search covers full struct span.
    SearchReplaceHunk good;
    good.path = "src/panel.hpp";
    good.search =
        "struct ConsolePanelTabs {\n"
        "  static constexpr int kShell = 0;\n"
        "  static constexpr int kOutput = 1;\n"
        "};\n";
    good.replace =
        "struct ConsolePanelTabs {\n"
        "  static constexpr int kShell = 0;\n"
        "  static constexpr int kOutput = 1;\n"
        "  static constexpr int kTemp = 2;\n"
        "};\n";
    {
      const auto tr = sesss.apply_edit(roots.string(), {good});
      expect(tr.ok, "full-span hunk ok: " + tr.error);
      const std::string body = read_all((roots / "src" / "panel.hpp").string());
      expect(body.find("kTemp") != std::string::npos, "kTemp applied");
    }
    fs::remove_all(roots, ec);
  }

  // Function-opener prefix insert → rewrite to after the full function (not inside `{`).
  {
    const fs::path rooti = fs::temp_directory_path() / "tuide_l2_insert_after_fn";
    fs::remove_all(rooti, ec);
    fs::create_directories(rooti / ".tuide" / "ai", ec);
    fs::create_directories(rooti / "src" / "util", ec);
    {
      std::ofstream map(rooti / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream cpp(rooti / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n"
             "bool command_exists(const std::string& c) {\n  return !c.empty();\n}\n}\n";
    }
    {
      std::ofstream hpp(rooti / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    Level2SessionDeps depsi{&tools, {}, [](std::string*) { return 0; }};
    depsi.pack_incomplete_pushback_max = 0;
    Level2Session sessi(depsi);
    Level2BootstrapOpts optsi;
    optsi.workspace_root = rooti.string();
    optsi.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y bool always_true() { return "
        "true; } en src/util/shell_utils.cpp.";
    expect(sessi.bootstrap(optsi, &err), "bootstrap insert-fn " + err);
    expect(sessi.mark_done(rooti.string(), "ready", "edit").ok, "to edit insert-fn");
    SearchReplaceHunk ins;
    ins.path = "src/util/shell_utils.cpp";
    ins.search = "bool command_exists(const std::string& c) {";
    ins.replace =
        "bool command_exists(const std::string& c) {\n\nbool always_true() {\n  return true;\n}\n";
    const auto tr = sessi.apply_edit(rooti.string(), {ins});
    expect(tr.ok, "function opener insert ok: " + tr.error);
    const std::string body = read_all((rooti / "src" / "util" / "shell_utils.cpp").string());
    expect(body.find("always_true") != std::string::npos, "always_true present");
    expect(body.find("return !c.empty();") != std::string::npos, "keeps command_exists body");
    const auto pos_close = body.find("return !c.empty();");
    const auto pos_new = body.find("bool always_true()");
    expect(pos_close != std::string::npos && pos_new != std::string::npos && pos_close < pos_new,
           "always_true after function, not inside: " + body);
    expect(body.find("command_exists") < body.find("always_true"), "order");
    fs::remove_all(rooti, ec);
  }

  // Mixed decl+def on header → split definition onto sibling .cpp.
  {
    const fs::path rootm = fs::temp_directory_path() / "tuide_l2_split_sibling";
    fs::remove_all(rootm, ec);
    fs::create_directories(rootm / ".tuide" / "ai", ec);
    fs::create_directories(rootm / "src" / "util", ec);
    {
      std::ofstream map(rootm / ".tuide" / "ai" / "map_last.md");
      map << "query: l2_ps_noop\n";
    }
    {
      std::ofstream hpp(rootm / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootm / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n"
             "bool command_exists(const std::string& c) { return !c.empty(); }\n}\n";
    }
    Level2SessionDeps depsm{&tools, {}, [](std::string*) { return 0; }};
    depsm.pack_incomplete_pushback_max = 0;
    Level2Session sessm(depsm);
    Level2BootstrapOpts optsm;
    optsm.workspace_root = rootm.string();
    optsm.query =
        "Añade void l2_ps_noop(); en src/util/shell_utils.hpp y void l2_ps_noop() {} en "
        "src/util/shell_utils.cpp. Ambos archivos.";
    expect(sessm.bootstrap(optsm, &err), "bootstrap split " + err);
    expect(sessm.mark_done(rootm.string(), "ready", "edit").ok, "to edit split");
    SearchReplaceHunk mix;
    mix.path = "src/util/shell_utils.hpp";
    mix.search = "namespace tuide {";
    mix.replace =
        "namespace tuide {\n\nvoid l2_ps_noop();\n\nvoid l2_ps_noop() {}\n";
    const auto tr = sessm.apply_edit(rootm.string(), {mix});
    expect(tr.ok, "mixed split apply ok: " + tr.error);
    const std::string hpp = read_all((rootm / "src" / "util" / "shell_utils.hpp").string());
    const std::string cpp = read_all((rootm / "src" / "util" / "shell_utils.cpp").string());
    expect(hpp.find("void l2_ps_noop();") != std::string::npos, "decl in hpp");
    expect(hpp.find("void l2_ps_noop() {}") == std::string::npos, "def not in hpp");
    expect(cpp.find("void l2_ps_noop() {}") != std::string::npos, "def in cpp");
    fs::remove_all(rootm, ec);
  }

  // Compile fail after one-file edit → sibling path excerpt.
  {
    const fs::path rootf = fs::temp_directory_path() / "tuide_l2_compile_sibling_nudge";
    fs::remove_all(rootf, ec);
    fs::create_directories(rootf / ".tuide" / "ai", ec);
    fs::create_directories(rootf / "src" / "util", ec);
    {
      std::ofstream map(rootf / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootf / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootf / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n"
             "bool command_exists(const std::string& c) { return !c.empty(); }\n}\n";
    }
    Level2SessionDeps depsf{&tools, {}, [](std::string* out) {
      if (out) {
        *out = "error: 'always_true' was not declared in this scope\n";
      }
      return 1;
    }};
    depsf.pack_incomplete_pushback_max = 0;
    Level2Session sessf(depsf);
    Level2BootstrapOpts optsf;
    optsf.workspace_root = rootf.string();
    optsf.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y bool always_true() { return "
        "true; } en src/util/shell_utils.cpp. Ambos archivos.";
    expect(sessf.bootstrap(optsf, &err), "bootstrap compile-sib " + err);
    expect(sessf.mark_done(rootf.string(), "ready", "edit").ok, "to edit compile-sib");
    SearchReplaceHunk h;
    h.path = "src/util/shell_utils.hpp";
    h.search = "bool command_exists(const std::string& c);\n";
    h.replace = "bool command_exists(const std::string& c);\nbool always_true();\n";
    const auto tr = sessf.apply_edit(rootf.string(), {h});
    expect(!tr.ok || tr.phase == "edit", "stays edit after compile fail");
    const std::string session = read_all(Level2Session::session_path(rootf.string()));
    expect(session.find("shell_utils.cpp") != std::string::npos, "mentions missing cpp");
    expect(session.find("Instruction también pide") != std::string::npos, "instruction gap banner");
    fs::remove_all(rootf, ec);
  }

  // Escape noise (\s*) + wrong path auto-corrected from watchlist/pack.
  {
    const fs::path rootp = fs::temp_directory_path() / "tuide_l2_path_escape";
    fs::remove_all(rootp, ec);
    fs::create_directories(rootp / ".tuide" / "ai", ec);
    fs::create_directories(rootp / "src", ec);
    {
      std::ofstream map(rootp / ".tuide" / "ai" / "map_last.md");
      map << "query: path escape\n\n## Ranked entries\n\n1. src/panel.hpp:1 — `ConsolePanelTabs`\n";
    }
    {
      std::ofstream panel(rootp / "src" / "panel.hpp");
      panel << "struct ConsolePanelTabs {\n"
               "  static constexpr int kShell = 0;\n"
               "  static constexpr int kOutput = 1;\n"
               "};\n";
    }
    {
      std::ofstream wrong(rootp / "src" / "wrong.hpp");
      wrong << "// empty-ish\n";
    }
    ToolRegistry toolsp;
    toolsp.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true,
                          "src/panel.hpp:1-5 (ConsolePanelTabs)\nstruct ConsolePanelTabs {\n"
                          "  static constexpr int kShell = 0;\n"
                          "  static constexpr int kOutput = 1;\n"
                          "};\n" +
                              arg};
    });
    toolsp.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + "\nstruct ConsolePanelTabs :1\n"};
    });
    toolsp.register_tool("headers_of", "stub",
                         [](const std::string& arg) { return AiToolResult{true, "hdr " + arg}; });
    Level2SessionDeps depsp{&toolsp, {}, [](std::string*) { return 0; }};
    depsp.pack_incomplete_pushback_max = 0;
    Level2Session sessp(depsp);
    Level2BootstrapOpts optsp;
    optsp.workspace_root = rootp.string();
    optsp.query = "add kTemp";
    optsp.instruction = "add kTemp to ConsolePanelTabs";
    expect(sessp.bootstrap(optsp, &err), "bootstrap path " + err);
    expect(sessp.apply_plan(rootp.string(), {"src/panel.hpp:ConsolePanelTabs"}, "tabs").ok,
           "plan path");
    expect(sessp.mark_done(rootp.string(), "ready", "edit").ok, "to edit path");
    SearchReplaceHunk bad_path;
    bad_path.path = "src/wrong.hpp";
    // literal \s* noise (as if JSON had \\s*)
    bad_path.search = std::string("struct ConsolePanelTabs {") + "\\s*" +
                      "  static constexpr int kShell = 0;" + "\\s*" +
                      "  static constexpr int kOutput = 1;" + "\\s*" + "};";
    bad_path.replace =
        "struct ConsolePanelTabs {\n"
        "  static constexpr int kShell = 0;\n"
        "  static constexpr int kOutput = 1;\n"
        "  static constexpr int kTemp = 2;\n"
        "};\n";
    {
      const auto tr = sessp.apply_edit(rootp.string(), {bad_path});
      expect(tr.ok, "path+escape edit ok: " + tr.error);
      const std::string sess = read_all(Level2Session::session_path(rootp.string()));
      expect(sess.find("path auto-corregido") != std::string::npos, "path correction noted");
      expect(sess.find("wrong.hpp") != std::string::npos &&
                 sess.find("panel.hpp") != std::string::npos,
             "shows wrong→panel");
      const std::string body = read_all((rootp / "src" / "panel.hpp").string());
      expect(body.find("kTemp") != std::string::npos, "applied on correct file");
      expect(read_all((rootp / "src" / "wrong.hpp").string()).find("kTemp") == std::string::npos,
             "wrong file untouched");
    }
    fs::remove_all(rootp, ec);
  }

  // Repeated covering plans emit edit nudge.
  {
    const fs::path rootn2 = fs::temp_directory_path() / "tuide_l2_plan_repeat_nudge";
    fs::remove_all(rootn2, ec);
    fs::create_directories(rootn2 / ".tuide" / "ai", ec);
    fs::create_directories(rootn2 / "src", ec);
    {
      std::ofstream map(rootn2 / ".tuide" / "ai" / "map_last.md");
      map << "query: helper_value\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `helper_value`\n";
    }
    {
      std::ofstream foo(rootn2 / "src" / "foo.cpp");
      foo << "int helper_value = 1;\n";
    }
    ToolRegistry toolsn2;
    toolsn2.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "src/foo.cpp:1-1 (helper_value)\nint helper_value = 1;\n" + arg};
    });
    toolsn2.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + "\nfn helper_value :1\n"};
    });
    Level2Session sessn2(Level2SessionDeps{&toolsn2, {}, [](std::string*) { return 0; }});
    Level2BootstrapOpts optsn2;
    optsn2.workspace_root = rootn2.string();
    optsn2.query = "bump helper_value";
    optsn2.instruction = "change helper_value";
    expect(sessn2.bootstrap(optsn2, &err), "bootstrap plan nudge2 " + err);
    expect(sessn2.apply_plan(rootn2.string(), {"src/foo.cpp:helper_value"}, "p1").ok, "plan1");
    {
      const auto tr2 = sessn2.apply_plan(rootn2.string(), {"src/foo.cpp:helper_value"}, "p2");
      expect(tr2.ok, "plan2");
      expect(tr2.error == "repeated_plan_pushback",
             "second covering plan is pushback got=" + tr2.error);
    }
    const std::string sess = read_all(Level2Session::session_path(rootn2.string()));
    expect(sess.find("_nudge:_") != std::string::npos, "repeat-plan edit nudge");
    expect(sess.find("done next=edit") != std::string::npos, "nudge asks edit");
    expect(sess.find("repeated_plan_pushback") != std::string::npos ||
               sess.find("Runtime pasa a phase=edit") != std::string::npos,
           "obs notes plan pushback");
    fs::remove_all(rootn2, ec);
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
                 sess.find("Algo más") != std::string::npos ||
                 sess.find("Instruction cubierta") != std::string::npos,
             "algo más or lean closeout hint in session");
      expect(sess.find("score=9") != std::string::npos ||
                 sess.find("`value`") != std::string::npos ||
                 sess.find("Instruction cubierta") != std::string::npos,
             "initial map restored, or lean closeout without ranked map");
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

  // Regression: plan telemetry in Observations must not poison pack needles
  // (false pack_incomplete → endless re-plan). Second plan with covering pack
  // must stay complete.
  {
    const fs::path rootr = root.parent_path() / "tuide_l2_pack_telem";
    fs::remove_all(rootr, ec);
    fs::create_directories(rootr / ".tuide" / "ai", ec);
    fs::create_directories(rootr / "src", ec);
    {
      std::ofstream map(rootr / ".tuide" / "ai" / "map_last.md");
      map << "query: helper_value\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `helper_value`\n";
    }
    {
      std::ofstream foo(rootr / "src" / "foo.cpp");
      foo << "int helper_value = 1;\n";
    }
    ToolRegistry toolsr;
    toolsr.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "src/foo.cpp:1-1 (helper_value)\nint helper_value = 1;\n" + arg};
    });
    toolsr.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + " symbols=1\nfn helper_value :1\n"};
    });
    Level2Session sessr(Level2SessionDeps{&toolsr, {}, [](std::string*) { return 0; }});
    Level2BootstrapOpts optsr;
    optsr.workspace_root = rootr.string();
    optsr.query = "cambia helper_value";
    optsr.instruction = "bump helper_value";
    expect(sessr.bootstrap(optsr, &err), "bootstrap telem " + err);
    {
      const auto tr = sessr.apply_plan(rootr.string(), {"src/foo.cpp:helper_value"}, "first");
      expect(tr.ok, "plan1 telem ok: " + tr.error);
      expect(sessr.status_text(rootr.string()).find("pack_incomplete: no") != std::string::npos,
             "plan1 pack complete");
    }
    {
      const auto tr = sessr.apply_plan(rootr.string(), {"src/foo.cpp:helper_value"}, "second");
      expect(tr.ok, "plan2 telem ok: " + tr.error);
      const std::string st = sessr.status_text(rootr.string());
      expect(st.find("pack_incomplete: no") != std::string::npos,
             "plan2 still complete (no telemetry poison)");
      expect(tr.summary.find("incomplete=1") == std::string::npos, "plan2 summary not incomplete");
      const std::string sess = read_all(Level2Session::session_path(rootr.string()));
      expect(sess.find("target_count:") != std::string::npos, "obs uses target_count label");
      expect(sess.find("fragments_ok:") != std::string::npos, "obs still has fragments_ok stats");
      // Must not claim Instruction gaps are telemetry keys.
      expect(sess.find("`fragments_ok:`") == std::string::npos &&
                 sess.find("`pack_chars:`") == std::string::npos,
             "gaps must not list plan telemetry tokens");
    }
    fs::remove_all(rootr, ec);
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
      const std::string arg = std::string("src/foo.cpp:helper_value#w") + std::to_string(i);
      expect(sesse.apply_tool(roote.string(), "get_code_of", arg).ok,
             "post-pack tool " + std::to_string(i));
    }
    const std::string sess = read_all(Level2Session::session_path(roote.string()));
    expect(sess.find("_nudge:_") != std::string::npos, "session contains edit nudge");
    expect(sess.find("done next=edit") != std::string::npos, "nudge asks for edit");
    fs::remove_all(roote, ec);
  }

  // Same get_code_of arg after covering pack is rejected (anti-loop).
  {
    const fs::path rootd = root.parent_path() / "tuide_l2_repeat_tool";
    fs::remove_all(rootd, ec);
    fs::create_directories(rootd / ".tuide" / "ai", ec);
    fs::create_directories(rootd / "src", ec);
    {
      std::ofstream map(rootd / ".tuide" / "ai" / "map_last.md");
      map << "query: bump helper_value\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `helper_value`\n";
    }
    {
      std::ofstream foo(rootd / "src" / "foo.cpp");
      foo << "int helper_value = 1;\n";
    }
    ToolRegistry toolsd;
    int fetches = 0;
    toolsd.register_tool("get_code_of", "stub", [&](const std::string& arg) {
      ++fetches;
      return AiToolResult{true, "src/foo.cpp:1-1 (helper_value)\nint helper_value = 1;\n" + arg};
    });
    toolsd.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + " symbols=1\n  helper_value\n"};
    });
    Level2Session sessd(Level2SessionDeps{&toolsd, {}, {}});
    Level2BootstrapOpts optsd;
    optsd.workspace_root = rootd.string();
    optsd.query = "bump helper_value";
    optsd.instruction = "change helper_value";
    expect(sessd.bootstrap(optsd, &err), "bootstrap repeat " + err);
    expect(sessd.apply_plan(rootd.string(), {"src/foo.cpp:helper_value"}, "ready").ok,
           "plan for repeat");
    expect(sessd.status_text(rootd.string()).find("pack_incomplete: no") != std::string::npos,
           "covering pack for repeat");
    const int after_plan = fetches;
    const auto t1 =
        sessd.apply_tool(rootd.string(), "get_code_of", "src/foo.cpp:helper_value#tail");
    expect(t1.ok && t1.error.empty(), "first fetch ok");
    expect(fetches == after_plan + 1, "invoked once more after plan");
    const auto t2 =
        sessd.apply_tool(rootd.string(), "get_code_of", "src/foo.cpp:helper_value#tail");
    expect(t2.ok, "dup turn accepted");
    expect(t2.error == "post_pack_tool_pushback", "dup after pack is pushback got=" + t2.error);
    expect(fetches == after_plan + 1, "second fetch not invoked");
    fs::remove_all(rootd, ec);
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

  // Bare Instruction path is packed as a file window, not omitted.
  {
    const fs::path rootb = fs::temp_directory_path() / "tuide_l2_bare_instr_window";
    fs::remove_all(rootb, ec);
    fs::create_directories(rootb / ".tuide" / "ai", ec);
    fs::create_directories(rootb / "src" / "util", ec);
    {
      std::ofstream map(rootb / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootb / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nbool command_exists(const std::string& c);\n";
    }
    {
      std::ofstream cpp(rootb / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nbool command_exists(const std::string& c) "
             "{ return !c.empty(); }\n";
    }
    ToolRegistry toolsb;
    toolsb.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "body " + arg + "\nbool command_exists(const std::string& c);\n"};
    });
    toolsb.register_tool("file_outline", "stub", [](const std::string&) {
      return AiToolResult{true, "outline: x symbols=0\n"};
    });
    Level2Session sessb(Level2SessionDeps{&toolsb, {}, {}});
    Level2BootstrapOpts optsb;
    optsb.workspace_root = rootb.string();
    optsb.query =
        "Añade always_true en src/util/shell_utils.hpp y src/util/shell_utils.cpp";
    optsb.instruction = "edit both files";
    expect(sessb.bootstrap(optsb, &err), "bootstrap bare instr " + err);
    const auto tr = sessb.apply_plan(rootb.string(), {"src/util/shell_utils.cpp"}, "bare");
    expect(tr.ok, "plan bare instr: " + tr.error);
    const std::string pack = read_all(Level2Session::pack_path(rootb.string()));
    expect(pack.find("file window") != std::string::npos, "normalize uses file window");
    expect(pack.find("omitido: bare path") == std::string::npos, "does not omit Instruction cpp");
    expect(pack.find("shell_utils.cpp") != std::string::npos, "pack names cpp");
    expect(pack.find("command_exists") != std::string::npos, "pack has cpp body");
    expect(pack.find("shell_utils.cpp:1-141") != std::string::npos,
           "file window fetches far enough to include last function");
    fs::remove_all(rootb, ec);
  }

  // Plan-only hpp still packs Instruction-named sibling .cpp.
  {
    const fs::path rootw = fs::temp_directory_path() / "tuide_l2_sibling_src";
    fs::remove_all(rootw, ec);
    fs::create_directories(rootw / ".tuide" / "ai", ec);
    fs::create_directories(rootw / "src" / "util", ec);
    {
      std::ofstream map(rootw / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootw / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nbool command_exists(const std::string& c);\n";
    }
    {
      std::ofstream cpp(rootw / "src" / "util" / "shell_utils.cpp");
      cpp << "bool command_exists(const std::string& c) { return !c.empty(); }\n";
    }
    ToolRegistry toolsw;
    toolsw.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "body " + arg + "\nbool command_exists();\n"};
    });
    toolsw.register_tool("file_outline", "stub", [](const std::string& arg) {
      return AiToolResult{true, "outline " + arg + "\ncommand_exists\n"};
    });
    Level2Session sessw(Level2SessionDeps{&toolsw, {}, {}});
    Level2BootstrapOpts optsw;
    optsw.workspace_root = rootw.string();
    optsw.query =
        "Añade always_true en src/util/shell_utils.hpp y src/util/shell_utils.cpp";
    optsw.instruction = "edit both";
    expect(sessw.bootstrap(optsw, &err), "bootstrap sibling src " + err);
    const auto tr = sessw.apply_plan(rootw.string(), {"src/util/shell_utils.hpp:command_exists"},
                                     "hdr");
    expect(tr.ok, "plan hpp-only: " + tr.error);
    const std::string pack = read_all(Level2Session::pack_path(rootw.string()));
    expect(pack.find("sibling source") != std::string::npos, "normalize sibling source");
    expect(pack.find("shell_utils.cpp") != std::string::npos, "pack includes cpp twin");
    fs::remove_all(rootw, ec);
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

  // After kMaxCompileAttempts, stay in edit (do not leave phase=compile).
  {
    const fs::path rootm = fs::temp_directory_path() / "tuide_l2_compile_max_edit";
    fs::remove_all(rootm, ec);
    fs::create_directories(rootm / ".tuide" / "ai", ec);
    fs::create_directories(rootm / "src", ec);
    {
      std::ofstream map(rootm / ".tuide" / "ai" / "map_last.md");
      map << "query: bump\n";
    }
    {
      std::ofstream foo(rootm / "src" / "foo.cpp");
      foo << "int value = 1;\n";
    }
    int compiles = 0;
    Level2SessionDeps depsm{&tools, {}, [&compiles](std::string* out) {
      ++compiles;
      if (out) {
        *out = "error: boom\n";
      }
      return 1;
    }};
    depsm.pack_incomplete_pushback_max = 0;
    Level2Session sessm(depsm);
    Level2BootstrapOpts optsm;
    optsm.workspace_root = rootm.string();
    optsm.query = "bump value";
    expect(sessm.bootstrap(optsm, &err), "bootstrap compile-max " + err);
    expect(sessm.mark_done(rootm.string(), "ready", "edit").ok, "to edit compile-max");
    SearchReplaceHunk hm;
    hm.path = "src/foo.cpp";
    hm.search = "int value = 1;\n";
    hm.replace = "int value = 9;\n";
    auto last = sessm.apply_edit(rootm.string(), {hm});
    expect(!last.ok, "compile fail 1");
    expect(last.phase == "edit", "phase edit after compile fail 0 got=" + last.phase);
    for (int i = 1; i < Level2Session::kMaxCompileAttempts; ++i) {
      last = sessm.apply_edit(rootm.string(), {hm});
      expect(!last.ok, "compile fail " + std::to_string(i + 1));
      expect(last.phase == "edit", "phase edit after compile fail " + std::to_string(i) +
                                       " got=" + last.phase);
    }
    expect(compiles == Level2Session::kMaxCompileAttempts, "compiled max times");
    expect(last.summary.find("rollback") != std::string::npos, "max fail mentions rollback");
    const std::string stxt = sessm.status_text(rootm.string());
    expect(stxt.find("phase: edit") != std::string::npos, "status stays edit not compile");
    expect(stxt.find("phase: compile") == std::string::npos, "status not compile");
    {
      std::ifstream in(rootm / "src" / "foo.cpp");
      std::ostringstream ss;
      ss << in.rdbuf();
      expect(ss.str().find("int value = 1;") != std::string::npos, "disk restored after rollback");
    }
    // Fresh budget: another edit still compiles instead of spinning in phase=compile.
    const auto again = sessm.apply_edit(rootm.string(), {hm});
    expect(again.phase == "edit", "still edit after extra compile fail got=" + again.phase);
    expect(compiles == Level2Session::kMaxCompileAttempts + 1, "fresh compile budget after rollback");
    fs::remove_all(rootm, ec);
  }

  // Phase A: POST_EDIT_COVERAGE — reject done when Instruction markers/paths missing.
  {
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    const fs::path rootc = fs::temp_directory_path() / "tuide_l2_coverage_gate";
    fs::remove_all(rootc, ec);
    fs::create_directories(rootc / ".tuide" / "ai", ec);
    fs::create_directories(rootc / "src" / "util", ec);
    fs::create_directories(rootc / "src" / "ui", ec);
    {
      std::ofstream map(rootc / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true shell_utils\n\n## Ranked entries\n\n"
             "1. src/ui/console_panel.cpp:1 — `tick` score=99\n";
    }
    {
      std::ofstream decoy(rootc / "src" / "ui" / "console_panel.cpp");
      decoy << "void tick() {}\n";
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
    Level2SessionDeps depsc{&tools, {}, [](std::string* out) {
      if (out) {
        *out = "ok\n";
      }
      return 0;
    }};
    depsc.pack_incomplete_pushback_max = 0;
    Level2Session sessc(depsc);
    Level2BootstrapOpts optsc;
    optsc.workspace_root = rootc.string();
    optsc.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y bool always_true() { return "
        "true; } en src/util/shell_utils.cpp. Ambos archivos.";
    optsc.instruction = "edit both files";
    expect(sessc.bootstrap(optsc, &err), "bootstrap coverage " + err);
    expect(sessc.mark_done(rootc.string(), "ready", "edit").ok, "to edit coverage");
    SearchReplaceHunk h;
    h.path = "src/util/shell_utils.hpp";
    h.search = "bool command_exists(const std::string& c);\n";
    h.replace =
        "bool command_exists(const std::string& c);\nbool always_true();\n";
    const auto er = sessc.apply_edit(rootc.string(), {h});
    expect(er.ok, "partial edit hpp ok");
    const std::string sess1 = read_all(Level2Session::session_path(rootc.string()));
    expect(sess1.find("post_edit_coverage") != std::string::npos, "coverage after partial edit");
    const auto cov_at = sess1.find("post_edit_coverage");
    const std::string cov =
        cov_at == std::string::npos ? std::string() : sess1.substr(cov_at, 2500);
    expect(cov.find("shell_utils.cpp") != std::string::npos, "coverage names missing cpp");
    expect(cov.find("command_exists") != std::string::npos,
           "coverage tail includes last cpp function");
    {
      const auto fresh_cpp = cov.find("### fresh `src/util/shell_utils.cpp`");
      const auto firma_hpp = cov.find("firma (referencia, NO edites) `src/util/shell_utils.hpp`");
      expect(fresh_cpp != std::string::npos, "coverage has cpp as SEARCH source");
      expect(firma_hpp != std::string::npos, "coverage has hpp signature after");
      expect(fresh_cpp < firma_hpp, "cpp fresh before hpp firma");
    }
    expect(cov.find("- `shell_utils`") == std::string::npos,
           "filename stem is not a coverage marker");
    expect(cov.find("console_panel") == std::string::npos,
           "coverage ignores ranked-map paths");
    expect(cov.find("<<<<<<< SEARCH") != std::string::npos, "lean coverage asks Aider");
    expect(cov.find("\"hunks\"") == std::string::npos, "lean coverage not JSON hunks");
    expect(sessc.status_text(rootc.string()).find("map_review: yes") == std::string::npos,
           "no map_review while coverage gaps");
    const auto done = sessc.mark_done(rootc.string(), "all done");
    expect(done.error == "done_coverage_gate" || done.summary == "done_coverage_gate",
           "done rejected by coverage gate");
    const std::string sess2 = read_all(Level2Session::session_path(rootc.string()));
    expect(sess2.find("done_coverage_gate") != std::string::npos, "gate observation");
    expect(sess2.find("shell_utils.cpp") != std::string::npos ||
               sess2.find("always_true") != std::string::npos,
           "mentions missing cpp or marker");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    fs::remove_all(rootc, ec);
  }

  // Coverage tail keeps the last function of a long .cpp (not just the file head).
  {
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    const fs::path roott = fs::temp_directory_path() / "tuide_l2_coverage_tail";
    fs::remove_all(roott, ec);
    fs::create_directories(roott / ".tuide" / "ai", ec);
    fs::create_directories(roott / "src" / "util", ec);
    {
      std::ofstream map(roott / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(roott / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(roott / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n";
      for (int i = 0; i < 50; ++i) {
        cpp << "int pad_" << i << "() { return " << i << "; }\n";
      }
      cpp << "bool command_exists(const std::string& c) { return !c.empty(); }\n}\n";
    }
    Level2SessionDeps depst{&tools, {}, [](std::string* out) {
      if (out) {
        *out = "ok\n";
      }
      return 0;
    }};
    depst.pack_incomplete_pushback_max = 0;
    Level2Session sesst(depst);
    Level2BootstrapOpts optst;
    optst.workspace_root = roott.string();
    optst.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y bool always_true() { return "
        "true; } en src/util/shell_utils.cpp.";
    optst.instruction = "edit both";
    expect(sesst.bootstrap(optst, &err), "bootstrap coverage tail " + err);
    expect(sesst.mark_done(roott.string(), "ready", "edit").ok, "to edit coverage tail");
    SearchReplaceHunk ht;
    ht.path = "src/util/shell_utils.hpp";
    ht.search = "bool command_exists(const std::string& c);\n";
    ht.replace =
        "bool command_exists(const std::string& c);\nbool always_true();\n";
    expect(sesst.apply_edit(roott.string(), {ht}).ok, "partial hpp for tail");
    const std::string sesstxt = read_all(Level2Session::session_path(roott.string()));
    const auto covt = sesstxt.find("post_edit_coverage");
    const std::string covb =
        covt == std::string::npos ? std::string() : sesstxt.substr(covt, 3500);
    expect(covb.find("command_exists") != std::string::npos,
           "long cpp coverage still shows command_exists");
    expect(covb.find("pad_0") == std::string::npos,
           "coverage does not keep the file head padding");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    fs::remove_all(roott, ec);
  }

  // Failed hunk on an already-edited path does not re-inject the stale pack span.
  {
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    const fs::path roots = fs::temp_directory_path() / "tuide_l2_stale_pack_span";
    fs::remove_all(roots, ec);
    fs::create_directories(roots / ".tuide" / "ai", ec);
    fs::create_directories(roots / "src" / "util", ec);
    {
      std::ofstream map(roots / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(roots / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(roots / "src" / "util" / "shell_utils.cpp");
      cpp << "bool command_exists(const std::string& c) { return !c.empty(); }\n";
    }
    ToolRegistry toolss;
    toolss.register_tool("get_code_of", "stub", [](const std::string& arg) {
      return AiToolResult{true, "body " + arg + "\nbool command_exists(const std::string& c);\n"};
    });
    toolss.register_tool("file_outline", "stub", [](const std::string&) {
      return AiToolResult{true, "outline: x symbols=1\ncommand_exists\n"};
    });
    Level2SessionDeps depss{&toolss, {}, [](std::string* out) {
      if (out) {
        *out = "ok\n";
      }
      return 0;
    }};
    depss.pack_incomplete_pushback_max = 0;
    Level2Session sesss(depss);
    Level2BootstrapOpts optss;
    optss.workspace_root = roots.string();
    optss.query =
        "Añade always_true en src/util/shell_utils.hpp y src/util/shell_utils.cpp";
    optss.instruction = "edit both";
    expect(sesss.bootstrap(optss, &err), "bootstrap stale span " + err);
    expect(sesss.apply_plan(roots.string(), {"src/util/shell_utils.hpp:command_exists"}, "p").ok,
           "plan for stale span");
    expect(sesss.mark_done(roots.string(), "ready", "edit").ok, "to edit stale span");
    SearchReplaceHunk okh;
    okh.path = "src/util/shell_utils.hpp";
    okh.search = "bool command_exists(const std::string& c);\n";
    okh.replace =
        "bool command_exists(const std::string& c);\nbool always_true();\n";
    expect(sesss.apply_edit(roots.string(), {okh}).ok, "first hpp edit ok");
    SearchReplaceHunk stale;
    stale.path = "src/util/shell_utils.hpp";
    stale.search =
        "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    stale.replace =
        "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n"
        "bool always_true();\n}\n";
    const auto bad = sesss.apply_edit(roots.string(), {stale});
    expect(bad.ok && bad.phase == "edit", "covered-path nudge stays edit");
    expect(bad.summary.find("edit_covered_path") != std::string::npos ||
               bad.error.find("edit_covered_path") != std::string::npos,
           "summary is edit_covered_path: " + bad.summary + " / " + bad.error);
    const std::string sessx = read_all(Level2Session::session_path(roots.string()));
    const auto covp = sessx.rfind("edit_covered_path");
    const std::string tail =
        covp == std::string::npos ? std::string() : sessx.substr(covp, 2500);
    expect(tail.find("ya está cubierto") != std::string::npos, "notes path already covered");
    expect(tail.find("shell_utils.cpp") != std::string::npos, "points at missing cpp");
    expect(tail.find("span sugerido desde pack") == std::string::npos,
           "does not re-inject stale pack span");
    expect(read_all(roots / "src" / "util" / "shell_utils.hpp").find("always_true") !=
               std::string::npos,
           "hpp keep first edit");
    expect(read_all(roots / "src" / "util" / "shell_utils.cpp").find("always_true") ==
               std::string::npos,
           "cpp not touched by covered-path hunk");
    SearchReplaceHunk both_ok;
    both_ok.path = "src/util/shell_utils.cpp";
    both_ok.search = "bool command_exists(const std::string& c) { return !c.empty(); }\n";
    both_ok.replace =
        "bool command_exists(const std::string& c) { return !c.empty(); }\n"
        "bool always_true() { return true; }\n";
    expect(sesss.apply_edit(roots.string(), {both_ok}).ok, "cpp gap hunk is allowed");
    expect(read_all(roots / "src" / "util" / "shell_utils.cpp").find("always_true") !=
               std::string::npos,
           "cpp received always_true");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    fs::remove_all(roots, ec);
  }

  // N covered-path rejects → stop without rolling back the compiled hpp.
  {
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    setenv("L2_FEAT_EDIT_LEAN_PROMPT", "1", 1);
    const fs::path rootl = fs::temp_directory_path() / "tuide_l2_covered_path_limit";
    fs::remove_all(rootl, ec);
    fs::create_directories(rootl / ".tuide" / "ai", ec);
    fs::create_directories(rootl / "src" / "util", ec);
    {
      std::ofstream map(rootl / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootl / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootl / "src" / "util" / "shell_utils.cpp");
      cpp << "bool command_exists(const std::string& c) { return !c.empty(); }\n";
    }
    Level2SessionDeps depsl{&tools, {}, [](std::string* out) {
      if (out) {
        *out = "ok\n";
      }
      return 0;
    }};
    depsl.pack_incomplete_pushback_max = 0;
    Level2Session sessl(depsl);
    Level2BootstrapOpts optsl;
    optsl.workspace_root = rootl.string();
    optsl.query =
        "Añade always_true en src/util/shell_utils.hpp y src/util/shell_utils.cpp";
    optsl.instruction = "edit both";
    expect(sessl.bootstrap(optsl, &err), "bootstrap covered limit " + err);
    expect(sessl.mark_done(rootl.string(), "ready", "edit").ok, "to edit covered limit");
    SearchReplaceHunk okh;
    okh.path = "src/util/shell_utils.hpp";
    okh.search = "bool command_exists(const std::string& c);\n";
    okh.replace =
        "bool command_exists(const std::string& c);\nbool always_true();\n";
    expect(sessl.apply_edit(rootl.string(), {okh}).ok, "hpp first edit");
    SearchReplaceHunk again;
    again.path = "src/util/shell_utils.hpp";
    again.search =
        "bool command_exists(const std::string& c);\nbool always_true();\n";
    again.replace =
        "bool command_exists(const std::string& c);\nbool always_true();\n";
    tuide::Level2TurnResult last;
    for (int i = 0; i < Level2Session::kMaxCoveredPathRejects; ++i) {
      last = sessl.apply_edit(rootl.string(), {again});
    }
    expect(last.ok && last.phase == "done", "limit closes session");
    expect(last.summary == "covered_path_limit" || last.error == "covered_path_limit",
           "summary covered_path_limit: " + last.summary + " / " + last.error);
    expect(read_all(rootl / "src" / "util" / "shell_utils.hpp").find("always_true") !=
               std::string::npos,
           "hpp kept after limit");
    expect(read_all(rootl / "src" / "util" / "shell_utils.cpp").find("always_true") ==
               std::string::npos,
           "cpp untouched after limit");
    expect(sessl.status_text(rootl.string()).find("phase: done") != std::string::npos,
           "status done after limit");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    unsetenv("L2_FEAT_EDIT_LEAN_PROMPT");
    fs::remove_all(rootl, ec);
  }

  // SEARCH miss on missing sibling .cpp → re-anchor to disk and keep the new definition.
  {
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "1", 1);
    const fs::path rootr = fs::temp_directory_path() / "tuide_l2_sibling_search_repair";
    fs::remove_all(rootr, ec);
    fs::create_directories(rootr / ".tuide" / "ai", ec);
    fs::create_directories(rootr / "src" / "util", ec);
    {
      std::ofstream map(rootr / ".tuide" / "ai" / "map_last.md");
      map << "query: always_true\n";
    }
    {
      std::ofstream hpp(rootr / "src" / "util" / "shell_utils.hpp");
      hpp << "#pragma once\nnamespace tuide {\nbool command_exists(const std::string& c);\n}\n";
    }
    {
      std::ofstream cpp(rootr / "src" / "util" / "shell_utils.cpp");
      cpp << "#include \"util/shell_utils.hpp\"\nnamespace tuide {\n"
             "bool command_exists(const std::string& c) { return !c.empty(); }\n}\n";
    }
    Level2SessionDeps depsr{&tools, {}, [](std::string* out) {
      if (out) {
        *out = "ok\n";
      }
      return 0;
    }};
    depsr.pack_incomplete_pushback_max = 0;
    Level2Session sessr(depsr);
    Level2BootstrapOpts optsr;
    optsr.workspace_root = rootr.string();
    optsr.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y bool always_true() { return "
        "true; } en src/util/shell_utils.cpp. Ambos archivos.";
    expect(sessr.bootstrap(optsr, &err), "bootstrap search repair " + err);
    expect(sessr.mark_done(rootr.string(), "ready", "edit").ok, "to edit search repair");
    SearchReplaceHunk hpph;
    hpph.path = "src/util/shell_utils.hpp";
    hpph.search = "bool command_exists(const std::string& c);\n";
    hpph.replace =
        "bool command_exists(const std::string& c);\nbool always_true();\n";
    expect(sessr.apply_edit(rootr.string(), {hpph}).ok, "hpp before cpp repair");
    SearchReplaceHunk bad;
    bad.path = "src/util/shell_utils.cpp";
    bad.search = "void not_on_disk_xyz_12345(); // unique miss\n";
    bad.replace = "bool always_true() { return true; }\n";
    const auto repaired = sessr.apply_edit(rootr.string(), {bad});
    expect(repaired.ok, "cpp SEARCH repair applies: " + repaired.error);
    const std::string cpp = read_all(rootr / "src" / "util" / "shell_utils.cpp");
    expect(cpp.find("always_true") != std::string::npos, "cpp got always_true via repair");
    expect(cpp.find("return !c.empty()") != std::string::npos, "keeps command_exists");
    expect(cpp.find("not_on_disk_xyz_12345") == std::string::npos, "does not insert failed SEARCH");
    const std::string sessmd = read_all(Level2Session::session_path(rootr.string()));
    expect(sessmd.find("search reescrito a ancla de disco") != std::string::npos,
           "notes SEARCH repair in session");
    fs::remove_all(rootr, ec);

    const fs::path rootd = fs::temp_directory_path() / "tuide_l2_sibling_search_repair_decl";
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
    Level2Session sessd(depsr);
    Level2BootstrapOpts optsd;
    optsd.workspace_root = rootd.string();
    optsd.query =
        "Añade bool always_true(); en src/util/shell_utils.hpp y bool always_true() { return "
        "true; } en src/util/shell_utils.cpp. Ambos archivos.";
    expect(sessd.bootstrap(optsd, &err), "bootstrap decl-only " + err);
    expect(sessd.mark_done(rootd.string(), "ready", "edit").ok, "to edit decl-only");
    expect(sessd.apply_edit(rootd.string(), {hpph}).ok, "hpp before decl-only");
    SearchReplaceHunk decl_only;
    decl_only.path = "src/util/shell_utils.cpp";
    decl_only.search = "void not_on_disk_xyz_12345(); // unique miss\n";
    decl_only.replace = "bool always_true();\n";
    const auto skipped = sessd.apply_edit(rootd.string(), {decl_only});
    expect(!skipped.ok, "decl-only SEARCH miss is not stuffed into cpp");
    expect(read_all(rootd / "src" / "util" / "shell_utils.cpp").find("always_true") ==
               std::string::npos,
           "cpp unchanged on decl-only miss");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    fs::remove_all(rootd, ec);
  }

  // Continuable session: follow-up accumulates; Reset clears.
  {
    setenv("L2_FEAT_POST_EDIT_COVERAGE", "0", 1);
    const fs::path rootf = fs::temp_directory_path() / "tuide_l2_followup_test";
    fs::remove_all(rootf, ec);
    fs::create_directories(rootf / ".tuide" / "ai", ec);
    fs::create_directories(rootf / "src", ec);
    {
      std::ofstream map(rootf / ".tuide" / "ai" / "map_last.md");
      map << "query: explain foo\n\n## Ranked entries\n\n1. src/foo.cpp:1 — `foo`\n";
    }
    {
      std::ofstream foo(rootf / "src" / "foo.cpp");
      foo << "int foo() { return 1; }\n";
    }
    Level2SessionDeps depsf{&tools, {}, {}};
    depsf.pack_incomplete_pushback_max = 0;
    Level2Session sessf(depsf);
    Level2BootstrapOpts optsf;
    optsf.workspace_root = rootf.string();
    optsf.query = "cómo funciona foo";
    optsf.instruction = "explica foo";
    optsf.workflow = "ask";
    expect(sessf.bootstrap(optsf, &err), "bootstrap ask followup " + err);
    expect(!Level2Session::is_continuable(rootf.string()), "not continuable mid-run");
    {
      const auto tr = sessf.apply_plan(rootf.string(), {"src/foo.cpp:foo"}, "pack");
      expect(tr.ok, "plan ask: " + tr.error);
    }
    {
      const auto tr = sessf.apply_synthesize(rootf.string(), "foo devuelve 1");
      expect(tr.ok && tr.phase == "done", "synthesize done");
    }
    expect(Level2Session::is_continuable(rootf.string()), "continuable after synthesize");
    const std::string pack_before = read_all(Level2Session::pack_path(rootf.string()));
    const std::string answer_before = read_all(Level2Session::answer_path(rootf.string()));
    expect(!answer_before.empty(), "answer.md written");
    expect(Level2Session::reopen_for_followup(rootf.string(), "arrégalo para devolver 2", "agent",
                                              &err),
           "reopen followup " + err);
    expect(!Level2Session::is_continuable(rootf.string()),
           "not continuable while follow-up run active");
    {
      const std::string sess = read_all(Level2Session::session_path(rootf.string()));
      expect(sess.find("## Follow-ups") != std::string::npos, "has Follow-ups");
      expect(sess.find("arrégalo para devolver 2") != std::string::npos, "follow-up text");
      expect(sess.find("follow-up 1") != std::string::npos, "follow-up obs");
      expect(sess.find("workflow: agent") != std::string::npos, "workflow switched to agent");
    }
    expect(read_all(Level2Session::pack_path(rootf.string())) == pack_before, "pack preserved");
    expect(read_all(Level2Session::answer_path(rootf.string())) == answer_before,
           "answer preserved");
    {
      const std::string prior = Level2Session::resume_context_markdown(rootf.string());
      expect(prior.find("Prior answer") != std::string::npos, "prior answer in resume ctx");
      expect(prior.find("foo devuelve 1") != std::string::npos, "answer body in resume ctx");
    }
    // Finish follow-up and clear.
    expect(sessf.mark_done(rootf.string(), "fixed", "").ok, "done after followup");
    expect(Level2Session::is_continuable(rootf.string()), "continuable again");
    expect(Level2Session::clear_session(rootf.string(), &err), "clear_session " + err);
    expect(!Level2Session::is_continuable(rootf.string()), "not continuable after clear");
    expect(!fs::exists(Level2Session::session_path(rootf.string())), "session wiped");
    unsetenv("L2_FEAT_POST_EDIT_COVERAGE");
    fs::remove_all(rootf, ec);
  }

  fs::remove_all(root, ec);
  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level2_session_test OK\n";
  return 0;
}
