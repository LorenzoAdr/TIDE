#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ai/level2_autonomous_loop.hpp"
#include "ai/level2_debrief.hpp"
#include "ai/level2_session.hpp"

namespace fs = std::filesystem;
using tuide::Level2AutonomousLoopResult;
using tuide::Level2Debrief;
using tuide::build_level2_debrief;
using tuide::format_level2_debrief;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

bool has_tag(const Level2Debrief& d, const std::string& tag) {
  for (const auto& f : d.facts) {
    if (f.tag == tag) {
      return true;
    }
  }
  return false;
}

bool detail_contains(const Level2Debrief& d, const std::string& tag, const std::string& needle) {
  for (const auto& f : d.facts) {
    if (f.tag == tag && f.detail.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

int main() {
  const fs::path root = fs::temp_directory_path() / "tuide_l2_debrief_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / ".tuide" / "ai" / "l2", ec);

  {
    std::ofstream st(root / ".tuide" / "ai" / "l2" / "state.json");
    st << R"({"turn":4,"done":true,"phase":"clarify","edit_attempt":0,"compile_attempt":0,"last_action":"need_clarification","last_op_id":0,"pending":[]})";
  }
  {
    std::ofstream sess(root / ".tuide" / "ai" / "l2" / "session.md");
    sess << R"(# L2 session

## Observations

### turn 2 — `file_outline` `src/ui/console_panel.cpp`

```
outline: src/ui/console_panel.cpp  symbols=0  (archivo OK; sin símbolos Tree-sitter — usa get_code_of/search)
```

### turn 3 — edit_feedback

error: search no encontrado en src/foo.cpp

path: `src/foo.cpp`

### turn 4 — clarify (arreglo cancelado)

No tengo contexto suficiente del tab temp.

_No se pasa a edit/compile. El usuario debe dar más detalle y relanzar._

)";
  }
  {
    std::ofstream tr(root / ".tuide" / "ai" / "l2" / "trace.ndjson");
    tr << R"({"ts":1,"event":"tool","turn":2,"name":"file_outline","ok":true,"ms":12,"phase":"explore"})" << '\n';
    tr << R"({"ts":2,"event":"edit_fail","turn":3,"ms":5,"error":"search no encontrado en src/foo.cpp"})" << '\n';
    tr << R"({"ts":3,"event":"need_clarification","turn":4})" << '\n';
  }

  Level2AutonomousLoopResult result;
  result.ok = true;
  result.phase = "clarify";
  result.steps = 4;
  result.summary = "clarify (hace falta más detalle)";

  const Level2Debrief d = build_level2_debrief(root.string(), result);
  expect(d.outcome_tag == "clarify", "outcome clarify");
  expect(has_tag(d, "outline_empty"), "outline_empty tag");
  expect(detail_contains(d, "outline_empty", "archivo OK"),
         "outline fact keeps archivo OK (no missing-file fiction)");
  expect(has_tag(d, "edit_fail"), "edit_fail tag");
  expect(detail_contains(d, "edit_fail", "search no encontrado"), "edit fail detail");
  expect(has_tag(d, "clarify"), "clarify tag");
  expect(has_tag(d, "tools"), "tools count");

  const std::string md = format_level2_debrief(d);
  expect(md.find("outcome: `clarify`") != std::string::npos, "markdown outcome");
  expect(md.find("no existe") == std::string::npos, "must not invent missing file");

  // Success + compile path
  {
    fs::remove_all(root, ec);
    fs::create_directories(root / ".tuide" / "ai" / "l2", ec);
    {
      std::ofstream st(root / ".tuide" / "ai" / "l2" / "state.json");
      st << R"({"turn":3,"done":true,"phase":"done","edit_attempt":1,"compile_attempt":1,"last_action":"compile_ok","last_op_id":0,"pending":[]})";
    }
    {
      std::ofstream sess(root / ".tuide" / "ai" / "l2" / "session.md");
      sess << "## Observations\n\n### turn 3 — done\n\nOK compile. edit_attempts=1 compile_attempts=1\n";
    }
    {
      std::ofstream tr(root / ".tuide" / "ai" / "l2" / "trace.ndjson");
      tr << R"({"ts":1,"event":"edit","turn":2,"ms":3})" << '\n';
      tr << R"({"ts":2,"event":"compile_ok","exit":0,"ms":218000})" << '\n';
    }
    Level2AutonomousLoopResult ok;
    ok.ok = true;
    ok.phase = "done";
    ok.steps = 3;
    ok.summary = "sesión terminada";
    const Level2Debrief d2 = build_level2_debrief(root.string(), ok);
    expect(d2.outcome_tag == "success", "success outcome");
    expect(has_tag(d2, "compile_ok"), "compile_ok");
    expect(detail_contains(d2, "compile_ok", "218000"), "compile duration from trace");
  }

  if (failures == 0) {
    std::cout << "OK level2_debrief_test\n";
    return 0;
  }
  std::cerr << failures << " failure(s)\n";
  return 1;
}
