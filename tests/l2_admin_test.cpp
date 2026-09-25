#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ai/l2_admin.hpp"

namespace fs = std::filesystem;

using tuide::AdminClarifyTurn;
using tuide::AdminDo;
using tuide::AdminEvidenceItem;
using tuide::AdminJob;
using tuide::AdminJobResult;
using tuide::AdminLoopOpts;
using tuide::AdminOla;
using tuide::AdminOps;
using tuide::AdminScriptedBrain;
using tuide::AdminSpawn;
using tuide::AdminSpawnTipo;
using tuide::AdminState;
using tuide::admin_apply;
using tuide::admin_begin_consulta_budgets;
using tuide::admin_clip_output;
using tuide::admin_explore_stub;
using tuide::admin_explores_used_this_consulta;
using tuide::admin_run_explore_lite;
using tuide::admin_legal;
using tuide::admin_legal_dos;
using tuide::admin_notebook_append;
using tuide::admin_notebook_has_path;
using tuide::admin_notebook_markdown;
using tuide::admin_parse;
using tuide::admin_run_read_file;
using tuide::admin_split_read_args;
using tuide::admin_run_search_rg;
using tuide::admin_run_shell_safe;
using tuide::admin_verify_context_prompt;
using tuide::admin_verify_readable_paths;
using tuide::admin_run_web_fetch;
using tuide::admin_run_web_fetch_stub;
using tuide::admin_run_web_search;
using tuide::admin_run_web_search_stub;
using tuide::admin_is_continuable;
using tuide::admin_load_state;
using tuide::admin_path_inside_workspace;
using tuide::admin_resolve_in_workspace;
using tuide::admin_save_state;
using tuide::admin_shell_cmd_allowed;
using tuide::admin_shell_enrich_result;
using tuide::admin_shell_stays_in_workspace;
using tuide::admin_url_fetch_allowed;
using tuide::admin_user_prompt;
using tuide::kAdminMaxExplores;
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
    // Modelo confunde action/do: pone el gesto en action.
    const auto ola = admin_parse(
        R"({"action":"ask_user","why":"saludo sin tarea","reply":"¡Hola! ¿En qué ayudo?"})");
    expect(ola.ok && ola.do_kind == AdminDo::AskUser, "heal action=ask_user as do");
    expect(ola.reply.find("Hola") != std::string::npos, "heal keeps reply");
  }
  {
    const auto ola = admin_parse(
        R"({"action":"cerrar","why":"basta saludar","reply":"Hola, aquí estoy."})");
    expect(ola.ok && ola.do_kind == AdminDo::Cerrar, "heal action=cerrar as do");
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
    const fs::path root = fs::temp_directory_path() / "tuide_admin_ws_perimeter";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "src", ec);
    {
      std::ofstream out((root / "src" / "a.cpp").string());
      out << "int main(){}\n";
    }
    expect(admin_path_inside_workspace(root.string(), "src/a.cpp"), "rel inside");
    expect(admin_path_inside_workspace(root.string(), (root / "src" / "a.cpp").string()),
           "abs inside");
    expect(!admin_path_inside_workspace(root.string(), "../outside.cpp"), "rel escape");
    expect(!admin_path_inside_workspace(root.string(), "/etc/passwd"), "abs escape");
    std::string abs;
    std::string rel;
    std::string err;
    expect(admin_resolve_in_workspace(root.string(), "src/a.cpp", &abs, &rel, &err), "resolve ok");
    expect(rel.find("a.cpp") != std::string::npos, "resolve rel");
    expect(!admin_resolve_in_workspace(root.string(), "../../etc/passwd", &abs, &rel, &err),
           "resolve escape");
    expect(admin_shell_stays_in_workspace("ls src", root.string(), &err), "shell ls ok");
    expect(!admin_shell_stays_in_workspace("ls ..", root.string(), &err), "shell .. denied");
    expect(!admin_shell_stays_in_workspace("cat /etc/passwd", root.string(), &err),
           "shell abs denied");
    const auto denied = admin_run_read_file("/etc/passwd", root.string());
    expect(!denied.ok, "read escape denied");
    const auto ok = admin_run_read_file("src/a.cpp", root.string());
    expect(ok.ok && ok.summary.find("a.cpp") != std::string::npos, "read inside ok");
    // Write multi-line file to test path:line / path:N:M
    {
      std::ofstream mf(root / "src" / "b.cpp");
      for (int i = 1; i <= 30; ++i) {
        mf << "line_" << i << "\n";
      }
    }
    const auto around = admin_run_read_file("src/b.cpp:10", root.string());
    expect(around.ok && around.log_tail.find("line_10") != std::string::npos, "read :line");
    expect(around.log_tail.find("line_1\n") == std::string::npos ||
               around.log_tail.find("10:line_10") != std::string::npos,
           "read :line not only head");
    const auto ranged = admin_run_read_file("src/b.cpp:5:7", root.string());
    expect(ranged.ok && ranged.log_tail.find("line_5") != std::string::npos &&
               ranged.log_tail.find("line_7") != std::string::npos,
           "read :N:M");
    expect(ranged.log_tail.find("line_9") == std::string::npos, "read :N:M clipped");
    const auto dash = admin_run_read_file("src/b.cpp:10-12", root.string());
    expect(dash.ok && dash.log_tail.find("line_10") != std::string::npos &&
               dash.log_tail.find("line_12") != std::string::npos,
           "read :N-M");
    expect(dash.log_tail.find("line_14") == std::string::npos, "read :N-M clipped");
    const auto multi = admin_run_read_file("src/b.cpp:5-7,15-17", root.string());
    expect(multi.ok && multi.log_tail.find("line_5") != std::string::npos &&
               multi.log_tail.find("line_15") != std::string::npos &&
               multi.log_tail.find("line_17") != std::string::npos,
           "read multi :N-M,a-b");
    expect(multi.log_tail.find("line_9") == std::string::npos, "read multi skips gap");
    {
      std::ofstream mf(root / "src" / "c.cpp");
      mf << "namespace X {\nstruct Foo {\n  void bar() { int x = 1; }\n};\n}\n";
    }
    const auto sym = admin_run_read_file("src/c.cpp:Foo::bar", root.string());
    expect(sym.ok && sym.log_tail.find("bar") != std::string::npos, "read Class::method");
    expect(sym.summary.find("Foo::bar") != std::string::npos ||
               std::find(sym.facts.begin(), sym.facts.end(), "read:symbol=Foo::bar") !=
                   sym.facts.end(),
           "read Class::method notes symbol");
    const auto multi_paths =
        admin_run_read_file("src/a.cpp,src/b.cpp", root.string());
    expect(multi_paths.ok && multi_paths.paths.size() >= 2, "read path,path both");
    expect(multi_paths.log_tail.find("int main") != std::string::npos ||
               multi_paths.summary.find("read×") != std::string::npos,
           "read path,path has content");
    const auto still_ranges = admin_split_read_args("src/b.cpp:5-7,15-17");
    expect(still_ranges.size() == 1 && still_ranges[0].find("5-7,15-17") != std::string::npos,
           "split keeps range commas");
    const auto two = admin_split_read_args("src/a.cpp,src/b.cpp");
    expect(two.size() == 2, "split two paths");
    const std::string prompt =
        admin_user_prompt(AdminState{}, kAdminMaxProposes, kAdminMaxSpawns, root.string());
    expect(prompt.find("WORKSPACE_ROOT") != std::string::npos, "prompt has workspace root");
    expect(prompt.find(root.string()) != std::string::npos, "prompt shows root path");
    fs::remove_all(root, ec);
  }
  {
    // search_rg: encuentra FileTree en src/ y no envenena con .tuide/
    fs::path root = fs::current_path();
    if (!fs::exists(root / "src" / "ui" / "file_tree_panel.cpp") &&
        fs::exists(root / ".." / "src" / "ui" / "file_tree_panel.cpp")) {
      root = root / "..";
    }
    if (fs::exists(root / "src" / "ui" / "file_tree_panel.cpp")) {
      const auto hit = admin_run_search_rg("MakeFileTreePanel", root.string());
      expect(hit.ok && !hit.paths.empty(), "search finds MakeFileTreePanel");
      bool in_src = false;
      bool in_tuide = false;
      for (const auto& p : hit.paths) {
        if (p.find("file_tree") != std::string::npos || p.find("main_layout") != std::string::npos) {
          in_src = true;
        }
        if (p.find(".tuide") != std::string::npos) {
          in_tuide = true;
        }
      }
      expect(in_src, "search path in src ui");
      expect(!in_tuide, "search excludes .tuide");

      // El explorador a menudo emite "class.*Tab" creyendo que es regex; el
      // runtime es literal, pero .* se trata como hueco en la misma línea.
      const auto gap = admin_run_search_rg("class.*Tab", root.string());
      expect(gap.ok && !gap.paths.empty() && gap.summary.find("(0 hits)") == std::string::npos,
             "search class.*Tab finds Tab types");
      bool tabish = false;
      for (const auto& p : gap.paths) {
        if (p.find("tab") != std::string::npos || p.find("Tab") != std::string::npos) {
          tabish = true;
        }
      }
      expect(tabish || gap.log_tail.find("Tab") != std::string::npos, "search .* gap hits Tab");
      {
        const std::string miss_a = "ZzNoSuchStem";
        const std::string miss_b = "QqNoSuchTail";
        const auto lit_miss = admin_run_search_rg(miss_a + ".*" + miss_b, root.string());
        expect(lit_miss.ok && lit_miss.paths.empty(), "search .* still requires parts");
      }

      // Explore lite conservador: varios patterns en una ola + cerrar.
      AdminScriptedBrain explore_brain({
          R"({"do":"grep","patterns":["MakeFileTreePanel","FileTreeNode"],"why":"batch"})",
          R"({"do":"cerrar","veredicto":"encontrado","simbolos":["src/ui/file_tree_panel.cpp:MakeFileTreePanel"],"evidencia":[],"falta":[],"why":"panel localizado"})",
      });
      AdminSpawn esp;
      esp.tipo = AdminSpawnTipo::Explore;
      esp.brief = "localiza FileTree";
      AdminLoopOpts eopts;
      eopts.workspace_root = root.string();
      const auto er = admin_run_explore_lite(esp, explore_brain, root.string(), eopts);
      expect(er.ok && er.veredicto == "encontrado", "explore multi-grep cierra encontrado");
      expect(!er.paths.empty() || !er.simbolos.empty(), "explore deja anclas");

      // Read sin grep previo → no anclado (script: read primero, luego cerrar).
      AdminScriptedBrain bad_read({
          R"({"do":"read","paths":["src/ui/file_tree_panel.cpp"],"why":"sin ancla"})",
          R"({"do":"cerrar","veredicto":"parcial","simbolos":[],"evidencia":[],"falta":["ancla"],"why":"sin read"})",
      });
      const auto er2 = admin_run_explore_lite(esp, bad_read, root.string(), eopts);
      expect(er2.ok, "explore sobrevivió read no anclado");
    }
  }
  {
    // Parser ToolRegistry: top_files + hits (antes solo leía 10 líneas de metadatos).
    AdminJobResult r;
    const std::string sample =
        "needles_tried: 8  hits: 2  files: 2  name_files: 2  quality:ok\n"
        "needles:\n"
        "  explorer: 195\n"
        "  file_tree: 124\n"
        "hint: prioriza top_files (nombre contiene seed).\n"
        "top_files:\n"
        "  src/ui/file_tree_panel.cpp  [name]\n"
        "  src/ui/file_tree_sticky.hpp  [name]\n"
        "src/ui/file_tree_panel.cpp:1:14  [file_tree] [name]  #include \"ui/file_tree_panel.hpp\"\n"
        "src/ui/main_layout.cpp:989:7  [FileTree]  MakeFileTreePanel(\n";
    tuide::admin_collect_search_hits(sample, &r);
    expect(r.paths.size() >= 2, "collect paths from tool output");
    expect(std::find(r.paths.begin(), r.paths.end(), "src/ui/file_tree_panel.cpp") != r.paths.end(),
           "collect top_files path");
    expect(!r.facts.empty(), "collect hit facts");
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
    // Verificador: contexto factual engordado (evidencia + reads), sin tesis del piloto.
    AdminState vs;
    vs.consulta = "dónde se crean las pestañas de consola";
    AdminJob ex;
    ex.id = 8;
    ex.tipo = "explore";
    ex.veredicto = "encontrado";
    ex.summary =
        "build_console_panel_view ancla la creación de tabs en console_panel.cpp:3292";
    ex.simbolos.push_back("build_console_panel_view");
    ex.evidencia.push_back("src/ui/console_panel.cpp:3292-3293");
    vs.jobs.push_back(ex);
    AdminJob rd;
    rd.id = 9;
    rd.tipo = "read";
    rd.veredicto = "parcial";
    rd.summary = "lectura de rangos en console_panel";
    rd.evidencia.push_back("src/ui/console_panel.cpp:3280");
    vs.jobs.push_back(rd);
    AdminEvidenceItem nb;
    nb.job_id = 8;
    nb.tipo = "explore";
    nb.summary = ex.summary;
    nb.paths.push_back("src/ui/console_panel.cpp");
    nb.simbolos.push_back("build_console_panel_view");
    nb.facts.push_back("hit:build_console_panel_view @ console_panel.cpp:3292");
    vs.notebook.push_back(nb);
    const std::string blob = admin_verify_context_prompt(vs);
    expect(blob.find("job8 (explore)") != std::string::npos, "verify ctx explore job");
    expect(blob.find("job9 (read)") != std::string::npos, "verify ctx incluye read");
    expect(blob.find("src/ui/console_panel.cpp:3292-3293") != std::string::npos,
           "verify ctx evidencia tipada");
    expect(blob.find("hit:build_console_panel_view") != std::string::npos,
           "verify ctx hechos notebook");
    expect(blob.find("build_console_panel_view ancla la creación") != std::string::npos,
           "verify ctx summary >140");
    {
      // Summaries largos no se deben partir a ~400 (bug previo del verificador).
      AdminState long_st;
      long_st.consulta = "arco A→B";
      AdminJob long_job;
      long_job.id = 1;
      long_job.tipo = "explore";
      long_job.veredicto = "encontrado";
      long_job.summary = std::string(500, 'x') + " FINAL_MARKER_OK";
      long_st.jobs.push_back(long_job);
      const std::string long_blob = admin_verify_context_prompt(long_st);
      expect(long_blob.find("FINAL_MARKER_OK") != std::string::npos,
             "verify ctx no corta summary a 400");
    }
    {
      AdminState read_st;
      read_st.consulta = "lee rango";
      AdminJob rj;
      rj.id = 2;
      rj.tipo = "read";
      rj.veredicto = "parcial";
      rj.summary = "read console_panel bytes=900";
      rj.log_tail = "3290| void build_console_panel_view() {\n3291|   tabs.push_back(...);\n";
      read_st.jobs.push_back(rj);
      const std::string rb = admin_verify_context_prompt(read_st);
      expect(rb.find("build_console_panel_view") != std::string::npos,
             "verify ctx incluye extracto log_tail de read");
    }
    const auto vpaths = admin_verify_readable_paths(vs);
    expect(std::find(vpaths.begin(), vpaths.end(), "src/ui/console_panel.cpp") != vpaths.end(),
           "verify paths desde evidencia");
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
    expect(tuide::admin_shell_cmd_allowed("find . -maxdepth 2 -type f -name '*.cpp' | head -50"),
           "allow find|head");
    expect(tuide::admin_shell_cmd_allowed("ls -la && ls -la src/"), "allow ls && ls");
    expect(!tuide::admin_shell_cmd_allowed("ls &"), "deny background &");
    expect(!tuide::admin_shell_cmd_allowed("find . | grep secret"), "deny find|grep");
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
  {
    AdminState st;
    st.consulta = "arregla lo del margen";
    AdminScriptedBrain brain({
        R"({"action":"admin_v1","do":"ask_user","why":"vago","reply":"¿Qué margen: gutter, overview ruler o panel?"})",
    });
    AdminOps ops;
    AdminLoopOpts opts;
    const auto res = run_admin_loop(&st, brain, ops, opts);
    expect(res.ok && res.clarify, "ask_user pauses as clarify");
    expect(!st.done && st.clarify, "ask_user does not close session");
    expect(st.pending_question.find("margen") != std::string::npos, "pending_question set");
    // Simula respuesta del panel y reanudación.
    tuide::AdminClarifyTurn turn;
    turn.question = st.pending_question;
    turn.answer = "el gutter izquierdo del editor";
    st.clarifies.push_back(std::move(turn));
    st.clarify = false;
    st.pending_question.clear();
    st.reply.clear();
    expect(st.consulta == "arregla lo del margen", "consulta original intacta");
    AdminScriptedBrain brain2({
        R"({"action":"admin_v1","do":"cerrar","why":"ya concreto","reply":"Voy a mirar el gutter izquierdo."})",
    });
    const auto res2 = run_admin_loop(&st, brain2, ops, opts);
    expect(res2.ok && !res2.clarify && st.done, "resume after answer closes");
    expect(st.clarifies.size() == 1, "clarify turn persisted in state");
    expect(st.episodes.size() == 1, "cerrar archives episode");
    expect(st.episodes[0].consulta == "arregla lo del margen", "episode consulta");
    expect(st.episodes[0].reply.find("gutter") != std::string::npos, "episode reply");
  }
  {
    // A+B: tras cerrar, sesión continuable; save/load conserva episodios; prompt los muestra.
    const fs::path root = fs::temp_directory_path() / "tuide_l2_admin_episodes";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / ".tuide" / "ai", ec);
    AdminState st;
    st.consulta = "dónde está el gutter";
    AdminEvidenceItem ev;
    ev.paths = {"src/ui/editor.cpp"};
    ev.facts = {"gutter izquierdo"};
    st.notebook.push_back(ev);
    AdminScriptedBrain brain({
        R"({"action":"admin_v1","do":"cerrar","why":"localizado","reply":"El gutter está en editor.cpp."})",
    });
    AdminOps ops;
    AdminLoopOpts opts;
    opts.workspace_root = root.string();
    const auto res = run_admin_loop(&st, brain, ops, opts);
    expect(res.ok && st.done, "cerrar ok");
    expect(st.episodes.size() == 1, "episode on cerrar");
    expect(admin_save_state(root.string(), st, nullptr), "save with episodes");
    expect(admin_is_continuable(root.string()), "continuable after done+episodes");
    AdminState loaded;
    std::string err;
    expect(admin_load_state(root.string(), &loaded, &err), "load episodes");
    expect(loaded.episodes.size() == 1 && loaded.episodes[0].reply.find("gutter") != std::string::npos,
           "episodes roundtrip");
    expect(loaded.notebook.size() == 1, "notebook kept with episodes");
    // Follow-up: nueva consulta, mismos anclajes (como ai_controller).
    loaded.consulta = "aplica el fix ahí";
    loaded.done = false;
    loaded.reply.clear();
    loaded.proposes = 0;
    loaded.spawns = 0;
    const std::string prompt = admin_user_prompt(loaded, kAdminMaxProposes, kAdminMaxSpawns);
    expect(prompt.find("Episodios anteriores") != std::string::npos, "prompt has episodes");
    expect(prompt.find("dónde está el gutter") != std::string::npos, "prompt lists prior consulta");
    expect(prompt.find("aplica el fix ahí") != std::string::npos, "prompt has new consulta");

    AdminState topic = loaded;
    expect(tuide::admin_should_keep_session(topic, "y qué hace con eso del gutter"),
           "keep: related follow-up");
    expect(tuide::admin_should_keep_session(topic, "más detalle del gutter izquierdo"),
           "keep: same topic words");
    expect(tuide::admin_should_keep_session(
               topic, "explícame cómo funciona el embebido de stems y el índice de símbolos"),
           "keep: even if wording differs (no auto-clear mid-run)");
    topic.clarify = true;
    expect(tuide::admin_should_keep_session(topic, "el margen izquierdo del editor"),
           "keep: clarify answer always");

    // Presupuesto de explore fresco en follow-up aunque jobs viejos estén llenos.
    AdminState capped = loaded;
    capped.done = false;
    capped.reply.clear();
    for (int i = 0; i < kAdminMaxExplores; ++i) {
      AdminJob j;
      j.id = i + 1;
      j.tipo = "explore";
      j.ok = true;
      j.summary = "old";
      capped.jobs.push_back(j);
    }
    capped.explore_jobs_baseline = 0;
    expect(admin_explores_used_this_consulta(capped) >= kAdminMaxExplores, "cap full before");
    AdminOla explore_ola = admin_parse(
        R"({"action":"admin_v1","do":"spawn","why":"hace falta otro polo","spawn":{"tipo":"explore","brief":"otro polo concreto a cazar"}})");
    expect(explore_ola.ok, "explore ola parses");
    std::string cap_err;
    expect(!admin_legal(capped, explore_ola, kAdminMaxProposes, kAdminMaxSpawns, &cap_err),
           "blocked when cap exhausted");
    admin_begin_consulta_budgets(&capped);
    expect(admin_explores_used_this_consulta(capped) == 0, "budget reset keeps jobs");
    expect(capped.jobs.size() == static_cast<std::size_t>(kAdminMaxExplores),
           "jobs preserved after budget reset");
    expect(admin_legal(capped, explore_ola, kAdminMaxProposes, kAdminMaxSpawns, &cap_err),
           "explore legal after begin_consulta_budgets");
    // Prompt marca explore agotado cuando el cupo de esta consulta está lleno.
    {
      AdminState full = capped;
      // Tras begin_consulta_budgets used=0; forzar baseline 0 → used == nº explores.
      full.explore_jobs_baseline = 0;
      const std::string pfull =
          admin_user_prompt(full, kAdminMaxProposes, kAdminMaxSpawns);
      expect(pfull.find("Explore agotado") != std::string::npos ||
                 pfull.find("AGOTADO") != std::string::npos,
             "prompt marks explore exhausted");
    }
    fs::remove_all(root, ec);
  }

  if (failures) {
    std::cerr << "l2_admin_test failures=" << failures << '\n';
    return 1;
  }
  std::cout << "l2_admin_test ok\n";
  return 0;
}
