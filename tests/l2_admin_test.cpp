#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ai/l2_admin.hpp"

using tuide::AdminDo;
using tuide::AdminJobResult;
using tuide::AdminLoopOpts;
using tuide::AdminOla;
using tuide::AdminOps;
using tuide::AdminScriptedBrain;
using tuide::AdminSpawn;
using tuide::AdminSpawnTipo;
using tuide::AdminState;
using tuide::admin_apply;
using tuide::admin_clip_output;
using tuide::admin_explore_stub;
using tuide::admin_legal;
using tuide::admin_legal_dos;
using tuide::admin_notebook_append;
using tuide::admin_notebook_has_path;
using tuide::admin_notebook_markdown;
using tuide::admin_parse;
using tuide::admin_run_read_file;
using tuide::admin_run_search_rg;
using tuide::admin_run_shell_safe;
using tuide::admin_run_web_fetch;
using tuide::admin_run_web_fetch_stub;
using tuide::admin_run_web_search;
using tuide::admin_run_web_search_stub;
using tuide::admin_shell_cmd_allowed;
using tuide::admin_shell_enrich_result;
using tuide::admin_url_fetch_allowed;
using tuide::kAdminMaxProposes;
using tuide::kAdminMaxSpawns;
using tuide::run_admin_loop;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    const auto ola = admin_parse(
        R"({"action":"admin_v1","do":"cerrar","why":"basta","reply":"hola"})");
    expect(ola.ok && ola.do_kind == AdminDo::Cerrar, "parse cerrar");
  }
  {
    const auto ola = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"buscar","spawn":{"tipo":"search","brief":"q","arg":"busy_strip"}})");
    expect(ola.ok && ola.spawn.tipo == AdminSpawnTipo::Search, "parse search");
  }
  {
    const auto ola = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"leer","spawn":{"tipo":"read","brief":"f","arg":"src/ai/l2_admin.hpp"}})");
    expect(ola.ok && ola.spawn.tipo == AdminSpawnTipo::Read, "parse read");
  }
  {
    const auto ola = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"buscar en red","spawn":{"tipo":"web","brief":"q","arg":"nlohmann json latest release"}})");
    expect(ola.ok && ola.spawn.tipo == AdminSpawnTipo::Web, "parse web");
  }
  {
    expect(admin_url_fetch_allowed("https://example.com/a"), "allow https");
    expect(!admin_url_fetch_allowed("http://127.0.0.1/x"), "deny localhost");
    expect(!admin_url_fetch_allowed("ftp://example.com/x"), "deny ftp");
  }
  {
    setenv("TUIDE_WEB_SEARCH_STUB", "1", 1);
    setenv("TUIDE_WEB_FETCH_STUB", "1", 1);
    AdminState web_st;
    web_st.consulta =
        "Investiga en internet la release de nlohmann/json, abre la URL del notebook y resume";
    AdminScriptedBrain brain({
        R"({"action":"admin_v1","do":"spawn","why":"buscar en la red","spawn":{"tipo":"web","brief":"nlohmann","arg":"nlohmann json latest release github"}})",
        R"({"action":"admin_v1","do":"spawn","why":"leer cuerpo url","spawn":{"tipo":"web_fetch","brief":"fetch","arg":"https://duckduckgo.com/?q=nlohmann+json+latest+release+github"}})",
        R"({"action":"admin_v1","do":"cerrar","why":"fetch en notebook","reply":"Tras web+web_fetch, el notebook tiene fetch:text con Stub fetch / nlohmann json releases."})",
    });
    AdminOps ops;
    ops.run_web = [](const AdminSpawn& s) { return admin_run_web_search(s.arg); };
    ops.run_web_fetch = [&web_st](const AdminSpawn& s) {
      return admin_run_web_fetch(s.arg, web_st);
    };
    AdminLoopOpts opts;
    const auto res = run_admin_loop(&web_st, brain, ops, opts);
    unsetenv("TUIDE_WEB_SEARCH_STUB");
    unsetenv("TUIDE_WEB_FETCH_STUB");
    expect(res.ok && web_st.notebook.size() >= 2, "web+fetch notebook");
    bool has_fetch = false;
    for (const auto& e : web_st.notebook) {
      for (const auto& f : e.facts) {
        if (f.find("fetch:") != std::string::npos) {
          has_fetch = true;
        }
      }
    }
    expect(has_fetch, "fetch facts in notebook");
    // fetch sin ancla debe ser ilegal
    AdminState empty;
    empty.consulta = "x";
    AdminOla bad = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"fetch suelto","spawn":{"tipo":"web_fetch","brief":"f","arg":"https://example.com/"}})");
    std::string err;
    expect(!admin_legal(empty, bad, kAdminMaxProposes, kAdminMaxSpawns, &err),
           "web_fetch ilegal sin ancla");
  }

  AdminState st;
  st.consulta = "acumular evidencias";
  st.ui.active_path = "src/ai/l2_admin.hpp";
  {
    AdminOla ola = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"search first","spawn":{"tipo":"search","brief":"s","arg":"AdminState"}})");
    AdminOps ops;
    ops.run_search = [](const AdminSpawn& s) {
      AdminJobResult r;
      r.ok = true;
      r.summary = "search hits";
      r.paths.push_back("src/ai/l2_admin.hpp");
      r.facts.push_back("hit:AdminState");
      r.log_tail = "src/ai/l2_admin.hpp:1:struct AdminState";
      return r;
    };
    std::string err;
    expect(admin_apply(&st, ola, ops, &err), "apply search");
    expect(!st.notebook.empty() && admin_notebook_has_path(st, "src/ai/l2_admin.hpp"),
           "notebook has path");
    expect(admin_notebook_markdown(st).find("Evidencias") != std::string::npos, "notebook md");
  }
  {
    AdminState empty;
    empty.consulta = "x";
    AdminOla ola = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"edit","spawn":{"tipo":"edit","brief":"e","arg":"nope.cpp","search":"a","replace":"b"}})");
    std::string err;
    expect(!admin_legal(empty, ola, kAdminMaxProposes, kAdminMaxSpawns, &err),
           "edit ilegal sin ancla");
  }
  {
    // edit exige confirmación previa aunque el path esté anclado
    std::string err;
    AdminOla raw_edit = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"edit","spawn":{"tipo":"edit","brief":"e","arg":"src/ai/l2_admin.hpp","search":"a","replace":"b"}})");
    expect(!admin_legal(st, raw_edit, kAdminMaxProposes, kAdminMaxSpawns, &err),
           "edit ilegal sin confirmar_editar");
    AdminOla ped = admin_parse(R"({"action":"admin_v1","do":"editar","why":"listo para parche"})");
    expect(ped.ok && ped.do_kind == AdminDo::Editar, "parse editar");
    expect(admin_legal(st, ped, kAdminMaxProposes, kAdminMaxSpawns, &err), "editar legal");
    expect(admin_apply(&st, ped, {}, &err), "apply editar");
    expect(st.awaiting_edit_confirm, "awaiting after editar");
    const auto during = admin_legal_dos(st, kAdminMaxProposes, kAdminMaxSpawns);
    expect(std::find(during.begin(), during.end(), "confirmar_editar") != during.end(),
           "confirm legal during await");
    expect(std::find(during.begin(), during.end(), "spawn") == during.end(),
           "spawn ilegal during await");
    AdminOla conf = admin_parse(
        R"({"action":"admin_v1","do":"confirmar_editar","why":"cubre polo A","cubre":"render de diagnósticos en editor","falta":"arco consola→margen"})");
    expect(conf.ok && conf.do_kind == AdminDo::ConfirmarEditar, "parse confirmar");
    expect(admin_legal(st, conf, kAdminMaxProposes, kAdminMaxSpawns, &err), "confirmar legal");
    expect(admin_apply(&st, conf, {}, &err), "apply confirmar");
    expect(st.edit_confirmed && !st.awaiting_edit_confirm, "edit_confirmed");
    expect(st.edit_falta.find("arco") != std::string::npos, "falta stored");
    expect(admin_legal(st, raw_edit, kAdminMaxProposes, kAdminMaxSpawns, &err),
           "edit legal after confirmar");
  }
  {
    expect(tuide::admin_shell_cmd_allowed("ls -la .tuide"), "allow ls");
    expect(!tuide::admin_shell_cmd_allowed("rm -rf /tmp"), "deny rm");
    bool trunc = false;
    int raw = 0;
    const std::string big(5000, 'x');
    const std::string clipped = tuide::admin_clip_output(big, &trunc, &raw);
    expect(trunc && clipped.find("--- head ---") != std::string::npos, "clip");
  }
  {
    // Enrichment unit (no popen): ls-like listing → typed paths.
    AdminJobResult fake;
    fake.ok = true;
    admin_shell_enrich_result(
        "ls tests/fixtures/l2_admin/battery",
        "cases.json\ncases_chain.json\nround_07\nround_08\n", ".", &fake);
    expect(!fake.paths.empty(), "enrich ls paths");
    expect(std::find(fake.paths.begin(), fake.paths.end(),
                     "tests/fixtures/l2_admin/battery/cases.json") != fake.paths.end() ||
               std::find(fake.paths.begin(), fake.paths.end(), "cases.json") != fake.paths.end(),
           "enrich ls cases.json");
    expect(!fake.facts.empty() && fake.facts[0].find("list:dir=") != std::string::npos,
           "enrich ls list fact");
  }
  {
    AdminJobResult fake;
    admin_shell_enrich_result("wc -l tools/l2_wave/admin_battery.py",
                              "443 tools/l2_wave/admin_battery.py\n", ".", &fake);
    expect(std::find(fake.paths.begin(), fake.paths.end(),
                     "tools/l2_wave/admin_battery.py") != fake.paths.end(),
           "enrich wc path");
    bool has_wc = false;
    for (const auto& f : fake.facts) {
      if (f.find("wc:path=") != std::string::npos && f.find("lines=") != std::string::npos) {
        has_wc = true;
      }
    }
    expect(has_wc, "enrich wc lines fact");
  }
  {
    AdminJobResult fake;
    admin_shell_enrich_result(
        "head -5 docs/ai/l2-admin.md",
        "# Administrador LLM único (chat)\n\nSustituye el hot path\n", ".", &fake);
    expect(std::find(fake.paths.begin(), fake.paths.end(), "docs/ai/l2-admin.md") !=
               fake.paths.end(),
           "enrich head path");
    bool peek = false;
    for (const auto& f : fake.facts) {
      if (f.find("peek:path=") != std::string::npos && f.find("kind=head") != std::string::npos) {
        peek = true;
      }
    }
    expect(peek, "enrich head peek fact");
  }
  // Live shell against repo (cwd = process cwd when test run from build/ or root).
  {
    const std::string root = [] {
      // Prefer repo root detected via this source relative marker.
      if (tuide::admin_shell_cmd_allowed("ls tests/fixtures/l2_admin/battery")) {
        auto r = admin_run_shell_safe("ls tests/fixtures/l2_admin/battery", ".");
        if (r.ok && !r.paths.empty()) {
          return std::string(".");
        }
      }
      return std::string("..");
    }();
    auto ls = admin_run_shell_safe("ls tests/fixtures/l2_admin/battery", root);
    expect(ls.ok && !ls.paths.empty(), "live ls paths");
    bool has_cases = false;
    for (const auto& p : ls.paths) {
      if (p.find("cases.json") != std::string::npos) {
        has_cases = true;
      }
    }
    expect(has_cases, "live ls cases.json path");
    auto wc = admin_run_shell_safe("wc -l tools/l2_wave/admin_battery.py", root);
    expect(wc.ok, "live wc ok");
    bool has_lines = false;
    for (const auto& f : wc.facts) {
      if (f.find("lines=") != std::string::npos) {
        has_lines = true;
      }
    }
    expect(has_lines && !wc.paths.empty(), "live wc typed fact+path");
  }
  {
    AdminState loop_st;
    loop_st.consulta = "busca AdminEvidenceItem y resume";
    loop_st.ui.active_path = "src/ai/l2_admin.hpp";
    AdminScriptedBrain brain({
        R"({"action":"admin_v1","do":"spawn","why":"search","spawn":{"tipo":"search","brief":"s","arg":"AdminEvidenceItem"}})",
        R"({"action":"admin_v1","do":"spawn","why":"read","spawn":{"tipo":"read","brief":"r","arg":"src/ai/l2_admin.hpp"}})",
        R"({"action":"admin_v1","do":"cerrar","why":"notebook basta","reply":"AdminEvidenceItem está en l2_admin.hpp según search+read."})",
    });
    AdminOps ops;
    ops.run_search = [](const AdminSpawn&) {
      AdminJobResult r;
      r.ok = true;
      r.summary = "hits";
      r.paths = {"src/ai/l2_admin.hpp"};
      r.facts = {"AdminEvidenceItem"};
      r.log_tail = "src/ai/l2_admin.hpp:10:struct AdminEvidenceItem";
      return r;
    };
    ops.run_read = [](const AdminSpawn& s) {
      AdminJobResult r;
      r.ok = true;
      r.summary = "read ok";
      r.paths = {s.arg};
      r.facts = {"leído:" + s.arg};
      r.log_tail = "struct AdminEvidenceItem {";
      return r;
    };
    AdminLoopOpts opts;
    opts.ui = loop_st.ui;
    const auto res = run_admin_loop(&loop_st, brain, ops, opts);
    expect(res.ok && loop_st.notebook.size() >= 2, "accumulate notebook");
    expect(loop_st.reply.find("AdminEvidenceItem") != std::string::npos, "grounded reply");
  }

  if (failures) {
    std::cerr << "l2_admin_test failures=" << failures << '\n';
    return 1;
  }
  std::cout << "l2_admin_test ok\n";
  return 0;
}
