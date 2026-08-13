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
    expect(tr.phase == "done" || tr.action == "done", "ended done");
    expect(read_all(root / "src" / "foo.cpp").find("value = 2") != std::string::npos, "file edited");
  }

  // Ambiguous / unique parser
  {
    tuide::SearchReplaceSpan sp;
    std::string e;
    expect(!tuide::find_unique_span("a a", "a", &sp, &e), "ambiguous reject");
  }

  // Explore fail → clarify (no edit)
  {
    const fs::path root2 = fs::temp_directory_path() / "tuide_l2_clarify_test";
    fs::remove_all(root2, ec);
    fs::create_directories(root2 / ".tuide" / "ai", ec);
    {
      std::ofstream map(root2 / ".tuide" / "ai" / "map_last.md");
      map << "query: vague\n";
    }
    Level2BootstrapOpts opts2;
    opts2.workspace_root = root2.string();
    opts2.query = "algo vago";
    expect(session.bootstrap(opts2, &err), "bootstrap2");
    const auto tr = session.mark_done(root2.string(), "no encontré el símbolo; ¿módulo?", "clarify");
    expect(tr.ok && tr.phase == "clarify", "clarify phase");
    expect(read_all(Level2Session::session_path(root2.string())).find("clarify") != std::string::npos,
           "clarify in session");
    fs::remove_all(root2, ec);
  }

  fs::remove_all(root, ec);
  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level2_session_test OK\n";
  return 0;
}
