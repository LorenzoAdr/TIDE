#include <cassert>
#include <iostream>
#include <string>

#include "ai/level0_router.hpp"
#include "ai/search_needles.hpp"

using tuide::AiRouteKind;
using tuide::route_level0;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    const auto r = route_level0("/build");
    expect(r.kind == AiRouteKind::ResolveTask && r.task_name == "compile", "/build");
  }
  {
    const auto r = route_level0("compila");
    expect(r.kind == AiRouteKind::ResolveTask && r.task_name == "compile", "compila");
  }
  {
    const auto r = route_level0("busca PayloadBuilder");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "search", "busca literal");
  }
  {
    const auto r = route_level0("lista errores");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "diagnostics", "lista errores");
  }
  {
    const auto r = route_level0("git status");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status", "git status");
  }
  {
    const auto r = route_level0(
        "oye búscame dónde se haga la construcción de la estructura que enviamos al sistema");
    expect(r.kind == AiRouteKind::EscalateLevel1, "NL conceptual escala");
  }
  {
    const auto r = route_level0("/explain esto");
    expect(r.kind == AiRouteKind::ForceLevel1 && r.arg == "esto", "/explain");
  }
  {
    const auto r = route_level0("/l1 busca callers de Foo");
    expect(r.kind == AiRouteKind::ForceLevel1, "/l1");
  }
  {
    const auto r = route_level0("/cancel");
    expect(r.kind == AiRouteKind::CancelAgent, "/cancel");
  }
  {
    const auto r = route_level0("/l2_turn");
    expect(r.kind == AiRouteKind::Level2Harness && r.arg == "turn", "/l2_turn");
  }
  {
    const auto r = route_level0("/l2_tool get_code_of src/ai/x.cpp:Foo");
    expect(r.kind == AiRouteKind::Level2Harness &&
               r.arg == "tool get_code_of src/ai/x.cpp:Foo",
           "/l2_tool");
  }
  {
    const auto r = route_level0("/l2_session status");
    expect(r.kind == AiRouteKind::Level2Harness && r.arg == "status", "/l2_session");
  }
  {
    const auto r = route_level0("como está el repo, hay mucho cambio?");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status" && r.arg.empty(),
           "como está el repo");
  }
  {
    const auto r = route_level0("y que cambios hay dentro de tools o en src?", "git_status",
                                "examples");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_diff" &&
               r.arg == "tools,src",
           "diff tools o src");
  }
  {
    const auto r = route_level0("como está el git? tengo muchos cambios?");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status",
           "como está el git");
  }
  {
    const auto r = route_level0("y en tools? tengo muchos?", "git_status", "examples");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status" && r.arg == "tools",
           "follow-up y en tools con muchos");
  }
  {
    const auto r = route_level0("y dentro de src?", "git_status");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status" && r.arg == "src",
           "follow-up y dentro de src");
  }
  {
    const auto r = route_level0("y dentro de src?");
    expect(r.kind == AiRouteKind::EscalateLevel1, "follow-up sin prev → escalate");
  }
  {
    const auto r =
        route_level0("que tengo cambiado dentro de esos archivos?", "git_status", "tests");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_diff" && r.arg == "tests",
           "diff de esos → prev arg");
  }
  {
    const auto r = route_level0(
        "dime que cambios tiene cada uno de los archivos que hay modificados dentro de src");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_diff" && r.arg == "src",
           "diff cada archivo en src");
  }
  {
    const auto r = route_level0("listame los archivos que hay dentro de src", "git_status", "tests");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "list_files" && r.arg == "src",
           "listame dir → list_files");
  }
  {
    const auto r = route_level0("que archviso modificados tengo en src");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status" && r.arg == "src",
           "typo archviso");
  }
  {
    const auto r = route_level0("que archivos tengo modificados, listamelos");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status",
           "archivos tengo modificados");
  }
  {
    const auto r = route_level0("tengo algun archivo modificado dentro de tools?");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status" &&
               r.arg == "tools",
           "modificado en tools");
  }
  {
    const auto r = route_level0("tengo archivos modificados dentro de examples?");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status" &&
               r.arg == "examples",
           "modificados en examples");
  }
  {
    const auto r = route_level0("dime las ramas que hay en remoto activas");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_branches",
           "ramas remotas");
  }
  {
    const auto r = route_level0("cuales son los últimos commits de main");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_log" &&
               r.arg.find("main") != std::string::npos,
           "últimos commits de main → git_log");
  }
  {
    const auto r = route_level0("cual fue el último commit de main?");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_log" &&
               r.arg.find("main") != std::string::npos && r.arg.find("1") != std::string::npos,
           "último commit de main → git_log n=1");
  }
  {
    const auto r = route_level0("/git log main");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_log", "/git log");
  }
  {
    const auto r =
        route_level0("y en concreto, el último que archivos cambio??", "git_log", "12");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show" && r.arg == "HEAD",
           "follow-up archivos del último → git_show");
  }
  {
    const auto r = route_level0(
        "y que archivos modifico el penúltimo commit y cual fue su mensaje?");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show" && r.arg == "HEAD~1",
           "penúltimo commit archivos → git_show HEAD~1");
  }
  {
    const auto r = route_level0("y que archivos cambio?", "git_log", "1");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show",
           "y que archivos cambio tras git_log → git_show");
  }
  {
    const auto r = route_level0("que archivos cambió ese commit?", "git_log", "main 1");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show" && r.arg == "main",
           "ese commit tras git_log main → git_show main");
  }
  {
    const auto r = route_level0("y el penúltimo?", "git_show", "main");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show" && r.arg == "main~1",
           "y el penúltimo tras git_show main → main~1");
  }
  {
    const auto r = route_level0("y que cambios metió?", "git_log", "1");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show" && r.arg == "HEAD",
           "y que cambios metió → git_show");
  }
  {
    const auto r = route_level0("y que cambios en concreto cambio?", "git_show", "main");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show" &&
               r.arg.find("main") != std::string::npos &&
               r.arg.find("patch") != std::string::npos,
           "cambios en concreto tras git_show → patch");
  }
  {
    const auto r = route_level0("cambios de cada archivo", "git_show", "main");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_show" &&
               r.arg.find("patch") != std::string::npos,
           "cambios de cada archivo tras git_show → patch");
  }
  {
    const auto r =
        route_level0("dime el histórico del archivo que tengo abierto, hace cuanto se cambio?");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_log" &&
               r.arg.find("@active") != std::string::npos,
           "histórico archivo abierto → git_log @active");
  }
  {
    const auto r = route_level0("actualiza el git");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_pull",
           "actualiza el git → pull");
  }
  {
    const auto r = route_level0("archivos modificados del proyecto");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status",
           "archivos modificados → status");
  }
  {
    const auto r = route_level0("hazte un git pulll a ver si hay cambios");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_pull",
           "git pulll typo → pull");
  }
  {
    const auto r = route_level0("/git pull");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_pull", "/git pull");
  }
  {
    const auto r = route_level0("/help");
    expect(r.kind == AiRouteKind::Help, "/help");
  }
  {
    const auto r = route_level0("/repomap panel performance");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "repo_map" &&
               r.arg.find("panel") != std::string::npos,
           "/repomap query");
  }
  {
    const auto r = route_level0("/map");
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "repo_map", "/map alias");
  }
  {
    const auto r =
        route_level0("donde está el codigo que gestiona el git en la aplicación");
    expect(r.kind == AiRouteKind::EscalateLevel1, "código de git → escalate");
  }
  {
    const auto r = route_level0("dónde está el código del gutter de git");
    expect(r.kind == AiRouteKind::EscalateLevel1, "gutter → escalate (no L0 glossary)");
  }
  {
    const auto r = route_level0("dónde está la funcionalidad del busy strip");
    expect(r.kind == AiRouteKind::EscalateLevel1, "funcionalidad → escalate L1");
  }
  {
    const auto r = route_level0("investiga dónde se gestiona el mapa del repo");
    expect(r.kind == AiRouteKind::EscalateLevel1, "investiga → escalate L1");
  }
  {
    const auto r = route_level0(
        "busca en el código donde está la gestión del panel del editor de la aplicación");
    expect(r.kind == AiRouteKind::EscalateLevel1, "panel editor → escalate");
  }
  {
    const auto r = route_level0("y la gestion del gutter?", "search", "editor_panel");
    expect(r.kind == AiRouteKind::EscalateLevel1, "follow-up código → escalate");
  }
  {
    const auto r = route_level0(
        "busca en el codigo donde se hace la gestion del panel de rendimiento y como se rellena");
    expect(r.kind == AiRouteKind::EscalateLevel1, "performance panel → escalate");
  }
  {
    const auto needles = tuide::expand_search_needles("panel_performance", 12);
    bool has_rev = false;
    bool has_camel = false;
    for (const auto& n : needles) {
      if (n == "performance_panel") {
        has_rev = true;
      }
      if (n == "PerformancePanel" || n == "PanelPerformance") {
        has_camel = true;
      }
    }
    expect(has_rev, "expand reverses snake");
    expect(has_camel, "expand adds CamelCase");
  }
  {
    const std::vector<std::string> seeds = {"file_tree", "FileTree", "explorer", "file_explorer"};
    const int named = tuide::filename_seed_match_score("src/ui/file_tree_panel.cpp", seeds);
    const int other = tuide::filename_seed_match_score("src/ui/console_panel.cpp", seeds);
    expect(named > 0, "file_tree_panel matches seed file_tree");
    expect(named > other, "name match beats unrelated file");
    const int via_explorer =
        tuide::score_search_hit("src/ui/file_tree_panel.cpp", "explorer", seeds);
    const int via_console =
        tuide::score_search_hit("src/ui/console_panel.cpp", "explorer", seeds);
    expect(via_explorer > via_console, "seed-in-filename outranks body-only hit");
  }
  {
    const char* q =
        "donde se hace en el código la gestión de la jerarquía y las peticiones LSP "
        "realcionadas con el call_hierarchy";
    expect(tuide::query_asks_code_location(q), "call_hierarchy → code locate");
    expect(!tuide::query_asks_git_repo(q), "call_hierarchy ≠ git repo");
    expect(tuide::is_git_repo_tool_name("git_status"), "git_status name");
    expect(!tuide::is_git_repo_tool_name("search"), "search not git");
    const auto tokens = tuide::extract_code_tokens(q, 8);
    bool has_ch = false;
    bool has_lsp = false;
    for (const auto& t : tokens) {
      if (t == "call_hierarchy") {
        has_ch = true;
      }
      if (t == "LSP") {
        has_lsp = true;
      }
    }
    expect(has_ch, "extract call_hierarchy");
    expect(has_lsp, "extract LSP");
  }
  {
    const char* q =
        "añade en el panel de terminal, una nueva pestaña que se llame tem y que "
        "dentro tenga el texto X";
    expect(tuide::query_asks_code_edit(q), "añade pestaña → code_edit");
    expect(!tuide::query_asks_git_repo(q), "añade pestaña ≠ git");
    expect(!tuide::query_asks_code_edit("muéstrame el git diff"), "git diff ≠ code_edit");
    expect(!tuide::query_asks_code_edit("qué archivos he modificado"), "modified ≠ code_edit");
    const auto r = tuide::route_level0(q);
    expect(r.kind == tuide::AiRouteKind::EscalateLevel1, "code_edit → escalate L1");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level0_router_test ok\n";
  return 0;
}
