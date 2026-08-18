#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ai/coding_stem_embed_index.hpp"
#include "indexer/symbol_workspace_indexer.hpp"

namespace {

int g_fails = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_fails;
  }
}

tuide::IndexedSymbol make_sym(const char* file, const char* name, tuide::SymbolKind kind, int line,
                              const char* sig) {
  tuide::IndexedSymbol s;
  s.file = file;
  s.name = name;
  s.display_name = name;
  s.kind = kind;
  s.line = line;
  s.signature = sig;
  return s;
}

}  // namespace

int main() {
  using tuide::StemPassageBuildInput;
  using tuide::StemPassageProfileId;
  using tuide::StemPassageSymbol;
  using tuide::build_coding_stem_index_passage;
  using tuide::stem_passage_profile_name;

  expect(std::string(stem_passage_profile_name(StemPassageProfileId::Baseline)) == "baseline",
         "baseline name");
  expect(tuide::parse_stem_passage_profile("rich_720") == StemPassageProfileId::Rich720,
         "parse rich_720");

  // Legacy API matches baseline shape.
  {
    const std::string p = build_coding_stem_index_passage(
        "performance_panel", {"src/ui/performance_panel.cpp"},
        {"update_threads", "PerformancePanelState"});
    expect(p.find("performance_panel") == 0, "starts with stem");
    expect(p.find("performance") != std::string::npos, "underscore tokens");
    expect(p.find("update_threads") != std::string::npos, "name included");
    expect(p.size() <= 480, "baseline cap 480");
  }

  StemPassageBuildInput in;
  in.stem = "performance_panel";
  in.paths = {"src/ui/performance_panel.cpp", "src/ui/performance_panel.hpp"};
  {
    StemPassageSymbol a;
    a.name = "helper_local";
    a.kind = tuide::SymbolKind::kFunction;
    a.file = "src/ui/performance_panel.cpp";
    a.line = 40;
    a.signature = "void helper_local() {";
    in.symbols.push_back(a);
  }
  {
    StemPassageSymbol b;
    b.name = "PerformancePanelState";
    b.kind = tuide::SymbolKind::kStruct;
    b.file = "src/ui/performance_panel.hpp";
    b.line = 18;
    b.signature = "struct PerformancePanelState {";
    in.symbols.push_back(b);
  }
  {
    StemPassageSymbol c;
    c.name = "RenderPerformancePanel";
    c.kind = tuide::SymbolKind::kFunction;
    c.file = "src/ui/performance_panel.hpp";
    c.line = 24;
    c.signature = "ftxui::Element RenderPerformancePanel(...);";
    in.symbols.push_back(c);
  }

  {
    const std::string base = build_coding_stem_index_passage(in, StemPassageProfileId::Baseline);
    const std::string typed = build_coding_stem_index_passage(in, StemPassageProfileId::TypeFirst);
    expect(typed.find("class:PerformancePanelState") != std::string::npos,
           "type_first labels class");
    const auto class_pos = typed.find("class:PerformancePanelState");
    const auto helper_pos = typed.find("helper_local");
    expect(class_pos != std::string::npos && helper_pos != std::string::npos && class_pos < helper_pos,
           "type_first puts types before helpers");
    expect(base.find("class:") == std::string::npos, "baseline has no class: label");
  }

  {
    const std::string sig = build_coding_stem_index_passage(in, StemPassageProfileId::SigSnip);
    expect(sig.find("PerformancePanelState") != std::string::npos, "sig has type name");
    expect(sig.find("struct PerformancePanelState") != std::string::npos ||
               sig.find("RenderPerformancePanel") != std::string::npos,
           "sig_snip includes a signature snippet");
  }

  {
    const std::string rich = build_coding_stem_index_passage(in, StemPassageProfileId::Rich720);
    expect(rich.size() <= 720, "rich_720 cap");
    const std::string sink = build_coding_stem_index_passage(in, StemPassageProfileId::KitchenSink);
    expect(sink.size() <= 960, "kitchen_sink cap");
  }

  // Prefer header path ordering when prefer_hpp is on.
  {
    const std::string typed = build_coding_stem_index_passage(in, StemPassageProfileId::TypeFirst);
    const std::string snip = build_coding_stem_index_passage(in, StemPassageProfileId::SigSnip);
    const auto hpp_t = typed.find("performance_panel.hpp");
    const auto cpp_t = typed.find("performance_panel.cpp");
    // type_first does not reorder paths; sig_snip does.
    (void)hpp_t;
    (void)cpp_t;
    const auto hpp_s = snip.find("performance_panel.hpp");
    const auto cpp_s = snip.find("performance_panel.cpp");
    expect(hpp_s != std::string::npos && cpp_s != std::string::npos && hpp_s < cpp_s,
           "sig_snip prefers hpp path first");
  }

  // Snapshot → passages for multiple stems.
  {
    tuide::SymbolIndexSnapshot snap;
    snap.workspace_root = ".";
    snap.symbols.push_back(make_sym("src/ui/panel.hpp", "MakePanel", tuide::SymbolKind::kFunction, 10,
                                    "Element MakePanel();"));
    snap.symbols.push_back(make_sym("src/ui/performance_panel.hpp", "PerformancePanelState",
                                    tuide::SymbolKind::kStruct, 18,
                                    "struct PerformancePanelState {"));
    auto rows = tuide::CodingStemEmbedIndex::build_passages(&snap, StemPassageProfileId::TypeFirst);
    expect(rows.size() == 2, "two stems");
    bool saw_perf = false;
    for (const auto& r : rows) {
      if (r.stem == "performance_panel") {
        saw_perf = true;
        expect(r.passage.find("class:PerformancePanelState") != std::string::npos,
               "snapshot type_first passage");
      }
    }
    expect(saw_perf, "performance_panel row present");
  }

  if (g_fails) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "stem_passage_test ok\n";
  return 0;
}
