#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

#include "ai/repo_map.hpp"
#include "indexer/symbol_workspace_indexer.hpp"

using tuide::IndexedRef;
using tuide::IndexedSymbol;
using tuide::RepoMapOptions;
using tuide::SymbolIndexSnapshot;
using tuide::SymbolKind;
using tuide::build_repo_map;
using tuide::repo_map_query_tokens;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

IndexedSymbol make_sym(const char* file, const char* name, SymbolKind kind, int line,
                       const char* sig = "") {
  IndexedSymbol s;
  s.file = file;
  s.display_name = name;
  s.name = name;
  s.kind = kind;
  s.line = line;
  s.signature = sig;
  return s;
}

int main() {
  {
    const auto toks = repo_map_query_tokens("dónde está el código del panel de performance", 16);
    bool has_perf = false;
    bool has_panel = false;
    for (const auto& t : toks) {
      if (t.find("performance") != std::string::npos) {
        has_perf = true;
      }
      if (t == "panel") {
        has_panel = true;
      }
    }
    expect(has_perf, "token performance from NL");
    expect(has_panel, "token panel from NL");
    expect(std::find(toks.begin(), toks.end(), "donde") == toks.end(), "stopword donde filtered");
    expect(std::find(toks.begin(), toks.end(), "codigo") == toks.end(), "stopword codigo filtered");
  }

  SymbolIndexSnapshot snap;
  snap.workspace_root = "/tmp/ws";
  snap.symbols.push_back(
      make_sym("src/ui/performance_panel.cpp", "MakePerformancePanel", SymbolKind::kFunction, 800,
               "Element MakePerformancePanel(PerformanceSampler* sampler) {"));
  snap.symbols.push_back(
      make_sym("src/ui/performance_panel.cpp", "PerformancePanelState", SymbolKind::kStruct, 40,
               "struct PerformancePanelState {"));
  snap.symbols.push_back(
      make_sym("src/ui/file_tree_panel.cpp", "MakeFileTreePanel", SymbolKind::kFunction, 100));
  snap.symbols.push_back(
      make_sym("src/ai/level1_agent.cpp", "Level1Agent", SymbolKind::kClass, 30));
  snap.symbols.push_back(make_sym("docs/readme.md", "Overview", SymbolKind::kFunction, 1));
  snap.symbols.push_back(make_sym("src/util/misc.cpp", "helper", SymbolKind::kFunction, 10));
  snap.symbols.push_back(
      make_sym("src/ui/console_panel.cpp", "MakeConsolePanel", SymbolKind::kFunction, 2000));

  // Refs: console + file_tree reference MakePerformancePanel → should boost performance_panel via PR.
  snap.refs.push_back(IndexedRef{"src/ui/console_panel.cpp", "MakePerformancePanel", 3});
  snap.refs.push_back(IndexedRef{"src/ui/file_tree_panel.cpp", "MakePerformancePanel", 1});
  snap.refs.push_back(IndexedRef{"src/ui/console_panel.cpp", "PerformancePanelState", 2});
  snap.refs.push_back(IndexedRef{"src/ai/level1_agent.cpp", "MakeFileTreePanel", 1});

  {
    RepoMapOptions opts;
    opts.query = "dónde está el panel de performance";
    opts.active_file = "src/ui/console_panel.cpp";
    opts.chat_files = {"src/ui/console_panel.cpp"};
    opts.max_symbols = 12;
    opts.max_map_tokens = 800;
    opts.prefer_git_tracked = false;  // synthetic paths
    opts.use_pagerank = true;
    const auto map = build_repo_map(&snap, opts);
    expect(!map.entries.empty(), "map not empty");
    expect(map.used_pagerank, "PageRank engaged");
    const auto text = map.render_text();
    expect(text.find("MakePerformancePanel") != std::string::npos ||
               text.find("PerformancePanel") != std::string::npos,
           "render mentions performance symbols");
    expect(text.find("⋮...") != std::string::npos, "elision markers");
    expect(text.find("│") != std::string::npos, "signature pipe");

    bool top_is_perf = false;
    if (!map.entries.empty()) {
      const auto& e = map.entries.front();
      top_is_perf = e.name.find("Performance") != std::string::npos ||
                    e.file.find("performance_panel") != std::string::npos;
    }
    expect(top_is_perf, "top entry related to performance (PR from chat refs)");

    const auto needles = map.suggested_needles(4);
    expect(!needles.empty(), "suggested needles");

    const auto answer = map.format_investigate_answer(8);
    expect(answer.find("Resultados más probables") != std::string::npos,
           "investigate answer header");
    expect(answer.find("1. ") != std::string::npos, "numbered list");
    expect(answer.find("src/") != std::string::npos, "investigate answer has paths");
  }
  {
    // Query-first: "busy strip" must surface busy_strip symbols, not only PR-hot files.
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp/ws";
    snap.symbols.push_back(
        make_sym("src/ui/busy_strip.cpp", "set_busy_spinner", SymbolKind::kFunction, 210));
    snap.symbols.push_back(
        make_sym("src/ui/busy_strip.cpp", "refresh_ai_mapping_busy", SymbolKind::kFunction, 295));
    snap.symbols.push_back(
        make_sym("src/ui/busy_strip.hpp", "BusyActivity", SymbolKind::kClass, 21));
    snap.symbols.push_back(
        make_sym("src/ai/level0_router.cpp", "route_level0", SymbolKind::kFunction, 800));
    snap.symbols.push_back(
        make_sym("third_party/nlohmann/json.hpp", "emplace_back", SymbolKind::kMethod, 10));
    snap.refs.push_back(IndexedRef{"src/app/application.cpp", "route_level0", 5});
    snap.refs.push_back(IndexedRef{"src/ui/console_panel.cpp", "route_level0", 3});

    RepoMapOptions opts;
    opts.query = "investiga donde esta la funcionalidad de busy strip";
    opts.active_file = "src/ai/level0_router.cpp";
    opts.prefer_git_tracked = false;
    opts.use_pagerank = true;
    opts.max_symbols = 12;
    const auto map = build_repo_map(&snap, opts);
    expect(!map.entries.empty(), "busy query map not empty");
    expect(map.note.find("query_hits=") != std::string::npos, "query_hits note");
    bool has_busy = false;
    for (const auto& e : map.entries) {
      if (e.file.find("busy_strip") != std::string::npos ||
          e.name.find("busy") != std::string::npos || e.name.find("Busy") != std::string::npos) {
        has_busy = true;
        break;
      }
    }
    expect(has_busy, "map contains busy_strip symbols for busy query");
    bool has_third = false;
    for (const auto& e : map.entries) {
      if (e.file.find("third_party/") != std::string::npos) {
        has_third = true;
      }
    }
    expect(!has_third, "third_party excluded from query map");
    const auto answer = map.format_investigate_answer(8);
    expect(answer.find("busy_strip") != std::string::npos, "investigate answer cites busy_strip");
  }
  {
    // L1-style needles applied to the map (no domain synonym tables).
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp/ws";
    snap.symbols.push_back(
        make_sym("src/ui/file_tree_panel.cpp", "MakeFileTreePanel", SymbolKind::kFunction, 900));
    snap.symbols.push_back(
        make_sym("src/ui/file_tree_panel.cpp", "build_file_tree_from_paths", SymbolKind::kFunction,
                 340));
    snap.symbols.push_back(
        make_sym("src/ui/editor_panel.cpp", "panel_diagnostics_match_doc", SymbolKind::kFunction,
                 803));
    snap.symbols.push_back(
        make_sym("src/ui/diagnostics_panel.cpp", "panel_content_width", SymbolKind::kFunction, 195));
    snap.symbols.push_back(
        make_sym("src/ui/busy_strip.hpp", "BusyStripState", SymbolKind::kClass, 37));
    snap.symbols.back().signature = "struct BusyStripState {";
    snap.symbols.push_back(
        make_sym("src/ui/busy_strip.hpp", "BusyStripState", SymbolKind::kMethod, 44));
    snap.symbols.back().signature = "BusyStripState(const BusyStripState&) = delete;";

    RepoMapOptions opts;
    opts.query = "y la gestión del panel del explorador de archivos?";
    opts.extra_needles = {"FileTree", "file_tree_panel", "MakeFileTreePanel"};
    opts.active_file = "src/ai/level0_router.cpp";
    opts.prefer_git_tracked = false;
    opts.use_pagerank = false;
    opts.max_symbols = 12;
    const auto map = build_repo_map(&snap, opts);
    expect(!map.entries.empty(), "explorer+needles map not empty");
    expect(map.note.find("query_hits=") != std::string::npos, "needles produce query_hits");
    bool top_tree = false;
    if (!map.entries.empty()) {
      top_tree = map.entries.front().file.find("file_tree") != std::string::npos;
    }
    expect(top_tree, "top entry is file_tree when L1 needles applied");
    const auto answer = map.format_investigate_answer(8);
    expect(answer.find("file_tree") != std::string::npos, "answer cites file_tree");
    expect(answer.find("= delete") == std::string::npos, "answer skips deleted ctors");
  }

  {
    // NL alone is weak; extra_needles (as L1 would propose) re-rank onto search_panel.
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp/ws";
    snap.symbols.push_back(
        make_sym("src/ui/search_panel.cpp", "MakeSearchPanel", SymbolKind::kFunction, 2000));
    snap.symbols.push_back(
        make_sym("src/ui/search_panel.cpp", "run_project_search", SymbolKind::kFunction, 380));
    snap.symbols.push_back(
        make_sym("src/ui/editor_panel.cpp", "panel_diagnostics_match_doc", SymbolKind::kFunction,
                 803));
    snap.symbols.push_back(
        make_sym("src/ui/console_panel.cpp", "MakeConsolePanel", SymbolKind::kFunction, 1800));
    snap.symbols.push_back(
        make_sym("src/terminal/pty_session.cpp", "PtySession", SymbolKind::kClass, 40));

    RepoMapOptions weak;
    weak.query = "y la gestión del buscador en el proyecto?";
    weak.active_file = "src/ui/console_panel.cpp";
    weak.chat_files = {"src/ui/console_panel.cpp", "src/terminal/pty_session.cpp"};
    weak.prefer_git_tracked = false;
    weak.use_pagerank = false;
    weak.max_symbols = 12;
    const auto weak_map = build_repo_map(&snap, weak);
    // NL lexicon maps buscador→search; path-stem hits surface search_panel without L1 needles.
    expect(weak_map.note.find("query_hits=") != std::string::npos,
           "NL buscador expands to search and yields query_hits");
    bool weak_has_search = false;
    for (const auto& e : weak_map.entries) {
      if (e.file.find("search_panel") != std::string::npos) {
        weak_has_search = true;
      }
    }
    expect(weak_has_search, "NL buscador map cites search_panel");

    RepoMapOptions opts = weak;
    opts.extra_needles = {"SearchPanel", "search_panel", "project_search"};
    const auto map = build_repo_map(&snap, opts);
    expect(map.note.find("query_hits=") != std::string::npos, "L1 needles yield query_hits");
    bool top_search = !map.entries.empty() &&
                      map.entries.front().file.find("search_panel") != std::string::npos;
    bool has_terminal = false;
    for (const auto& e : map.entries) {
      if (e.file.find("console_panel") != std::string::npos ||
          e.file.find("terminal/") != std::string::npos ||
          e.file.find("pty") != std::string::npos) {
        has_terminal = true;
      }
    }
    expect(top_search, "top entry is search_panel after needles");
    expect(!has_terminal, "needles keep map on-topic vs prior terminal tabs");
  }

  {
    RepoMapOptions opts;
    opts.query = "xyzzy_no_such_thing_abc";
    opts.max_symbols = 8;
    opts.prefer_git_tracked = false;
    opts.use_pagerank = true;
    const auto map = build_repo_map(&snap, opts);
    expect(!map.entries.empty(), "PR still yields map without lexical query");
    expect(map.used_pagerank, "PR without query tokens");
  }

  {
    RepoMapOptions opts;
    opts.query = "performance";
    opts.prefer_git_tracked = true;
    opts.use_pagerank = false;
    snap.workspace_root = "/tmp/ws";
    const auto map = build_repo_map(&snap, opts);
    expect(!map.entries.empty(), "lexical without pagerank");
    expect(!map.used_pagerank, "pagerank disabled");
  }

  {
    // Build/compile NL → path-stem hits for scripts (not only C++ under src/).
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp/ws";
    snap.symbols.push_back(
        make_sym("tools/compile.sh", "log", SymbolKind::kFunction, 40, "log() {"));
    snap.symbols.push_back(
        make_sym("tools/compile.sh", "cmake_extra_args", SymbolKind::kFunction, 200,
                 "cmake_extra_args() {"));
    snap.symbols.push_back(
        make_sym("src/app/application.cpp", "Application", SymbolKind::kClass, 80));
    snap.symbols.push_back(
        make_sym("src/ui/console_panel.cpp", "MakeConsolePanel", SymbolKind::kFunction, 2000));

    RepoMapOptions opts;
    opts.query = "dónde se hace la compilación";
    opts.active_file = "src/app/application.cpp";
    opts.prefer_git_tracked = false;
    opts.use_pagerank = false;
    opts.max_symbols = 12;
    const auto map = build_repo_map(&snap, opts);
    expect(map.note.find("query_hits=") != std::string::npos, "compilación yields query_hits");
    bool has_compile_sh = false;
    bool top_is_compile = false;
    if (!map.entries.empty()) {
      top_is_compile = map.entries.front().file.find("compile.sh") != std::string::npos;
    }
    for (const auto& e : map.entries) {
      if (e.file.find("compile.sh") != std::string::npos) {
        has_compile_sh = true;
      }
    }
    expect(has_compile_sh, "map cites tools/compile.sh for compilacion query");
    expect(top_is_compile, "top entry is compile.sh (path-stem synthetic)");
    const auto answer = map.format_investigate_answer(8);
    expect(answer.find("compile.sh") != std::string::npos, "investigate answer cites compile.sh");
  }

  {
    const SymbolIndexSnapshot* null_snap = nullptr;
    expect(build_repo_map(null_snap, RepoMapOptions{}).entries.empty(), "null snapshot");
  }

  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "repo_map_test ok\n";
  return 0;
}
