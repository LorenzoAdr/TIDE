#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ai/coding_embed_rerank.hpp"
#include "ai/coding_stem_embed_index.hpp"
#include "ai/coding_symbol_embed_index.hpp"
#include "ai/get_code_of.hpp"
#include "ai/repo_map.hpp"
#include "ai/search_needles.hpp"
#include "indexer/symbol_workspace_indexer.hpp"

using tuide::GetCodeOfRequest;
using tuide::IndexedSymbol;
using tuide::RepoMap;
using tuide::RepoMapEntry;
using tuide::RepoMapOptions;
using tuide::SymbolIndexSnapshot;
using tuide::SymbolKind;
using tuide::build_repo_map;
using tuide::dump_context_last_md;
using tuide::extract_query_facets;
using tuide::facet_coverage_score;
using tuide::get_code_of;
using tuide::parse_get_code_of_arg;
using tuide::query_asks_context_dump;

namespace fs = std::filesystem;

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
    // Generic: late symbols that add a second facet must count even if early samples miss them.
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    for (int i = 0; i < 15; ++i) {
      const std::string name = "narrow_only_" + std::to_string(i);
      snap.symbols.push_back(make_sym("src/util/narrow_kit.cpp", name.c_str(), SymbolKind::kFunction,
                                      10 + i, "void narrow_only() {"));
      snap.symbols.back().name = name;
      snap.symbols.back().display_name = name;
      snap.symbols.back().signature = "void " + name + "() {";
    }
    // First symbols on wide_kit only cover "wide"; facet "tip" appears late.
    snap.symbols.push_back(
        make_sym("src/ui/wide_kit.cpp", "wide_kit_init", SymbolKind::kFunction, 5,
                 "void wide_kit_init() {"));
    snap.symbols.push_back(
        make_sym("src/ui/wide_kit.cpp", "WideKitState", SymbolKind::kStruct, 20,
                 "struct WideKitState {"));
    snap.symbols.push_back(
        make_sym("src/ui/wide_kit.cpp", "wide_kit_tip_handler", SymbolKind::kFunction, 400,
                 "void wide_kit_tip_handler() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/util/narrow_kit.cpp";
      e.name = "narrow_kit";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 200;
      e.signature = "file src/util/narrow_kit.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/wide_kit.cpp";
      e.name = "wide_kit";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 40;
      e.signature = "file src/ui/wide_kit.cpp";
      sparse.entries.push_back(e);
    }
    const std::string q = "wide kit with tip and narrow";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "wide_kit",
           "full-index facet scan finds late tip on wide_kit (not narrow mass)");
  }

  {
    // Generic stem pick: many single-facet lookalikes lose to a stem covering more facets.
    // (No product-specific words in the scoring rules — only in this fixture.)
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    for (int i = 0; i < 12; ++i) {
      const std::string name = "parse_red_" + std::to_string(i);
      snap.symbols.push_back(make_sym("src/util/red_tint.cpp", name.c_str(), SymbolKind::kFunction,
                                      10 + i, "void parse_red_x() {"));
      snap.symbols.back().name = name;
      snap.symbols.back().display_name = name;
      snap.symbols.back().signature = "void " + name + "() {";
    }
    snap.symbols.push_back(
        make_sym("src/ui/alpha_panel.cpp", "AlphaPanelState", SymbolKind::kStruct, 20,
                 "struct AlphaPanelState {"));
    snap.symbols.push_back(
        make_sym("src/ui/alpha_panel.cpp", "open_alpha_panel", SymbolKind::kFunction, 40,
                 "void open_alpha_panel() {"));
    snap.symbols.push_back(
        make_sym("src/ui/alpha_panel.cpp", "handle_alpha_panel_keys", SymbolKind::kFunction, 60,
                 "bool handle_alpha_panel_keys() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/util/red_tint.cpp";
      e.name = "red_tint";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/util/red_tint.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/alpha_panel.cpp";
      e.name = "alpha_panel";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 50;
      e.signature = "file src/ui/alpha_panel.cpp";
      sparse.entries.push_back(e);
    }
    const std::string q = "alpha panel with red marks";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "alpha_panel",
           "multi-facet stem beats single-facet mass (alpha_panel vs red_tint)");
    const auto coding = sparse.ranked_coding_entries(10, q);
    expect(!coding.empty() && coding.front().file.find("alpha_panel") != std::string::npos,
           "coding bodies stay on multi-facet stem");
  }

  {
    // Short NL expand ("pestanas"→tab) must not let a *tab*-name flood win over a stem
    // that covers the longer facets (generic — no product stems in the scorer).
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    for (int i = 0; i < 20; ++i) {
      const std::string name = "open_terminal_tab_" + std::to_string(i);
      snap.symbols.push_back(make_sym("src/ui/tab_flood.cpp", name.c_str(), SymbolKind::kFunction,
                                      10 + i, "void open_terminal_tab_x() {"));
      snap.symbols.back().name = name;
      snap.symbols.back().display_name = name;
      snap.symbols.back().signature = "void " + name + "() {";
    }
    snap.symbols.push_back(
        make_sym("src/ui/modal_kit.cpp", "ModalKitState", SymbolKind::kStruct, 20,
                 "struct ModalKitState {"));
    snap.symbols.push_back(
        make_sym("src/ui/modal_kit.cpp", "open_modal_kit", SymbolKind::kFunction, 40,
                 "void open_modal_kit() {"));
    snap.symbols.push_back(
        make_sym("src/ui/modal_kit.cpp", "handle_modal_kit_config", SymbolKind::kFunction, 60,
                 "bool handle_modal_kit_config() {"));
    snap.symbols.push_back(
        make_sym("src/ui/modal_kit.cpp", "render_modal_kit_tabs", SymbolKind::kFunction, 80,
                 "void render_modal_kit_tabs() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/ui/tab_flood.cpp";
      e.name = "tab_flood";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/ui/tab_flood.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/modal_kit.cpp";
      e.name = "modal_kit";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 50;
      e.signature = "file src/ui/modal_kit.cpp";
      sparse.entries.push_back(e);
    }
    const std::string q = "modal kit configuration and its pestanas";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "modal_kit",
           "short expand pestanas→tab does not let tab_flood beat multi-facet modal_kit");
  }

  {
    // Generic bridge "config" (≤6) must not let *Config*-named symbols steal the stem from
    // a module that matches longer synonyms like "settings".
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    for (int i = 0; i < 12; ++i) {
      const std::string name = "ShellLaunchConfig_" + std::to_string(i);
      snap.symbols.push_back(make_sym("src/ui/launch_panel.cpp", name.c_str(), SymbolKind::kFunction,
                                      10 + i, "void ShellLaunchConfig_x() {"));
      snap.symbols.back().name = name;
      snap.symbols.back().display_name = name;
      snap.symbols.back().signature = "void " + name + "() {";
    }
    snap.symbols.push_back(
        make_sym("src/ui/prefs_box.cpp", "PrefsBoxState", SymbolKind::kStruct, 20,
                 "struct PrefsBoxState {"));
    snap.symbols.push_back(
        make_sym("src/ui/prefs_box.cpp", "open_prefs_box_settings", SymbolKind::kFunction, 40,
                 "void open_prefs_box_settings() {"));
    snap.symbols.push_back(
        make_sym("src/ui/prefs_box.cpp", "handle_prefs_box_settings_keys", SymbolKind::kFunction, 60,
                 "bool handle_prefs_box_settings_keys() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/ui/launch_panel.cpp";
      e.name = "launch_panel";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/ui/launch_panel.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/prefs_box.cpp";
      e.name = "prefs_box";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 50;
      e.signature = "file src/ui/prefs_box.cpp";
      sparse.entries.push_back(e);
    }
    const std::string q = "menu de configuracion";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "prefs_box",
           "menu/configuracion prefer settings synonym over generic config flood");
  }

  {
    // Path/stem match beats symbol-only false friends (*_from_settings on another module).
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    for (int i = 0; i < 16; ++i) {
      const std::string name = "tint_config_from_settings_" + std::to_string(i);
      snap.symbols.push_back(make_sym("src/editor/tint_mark.cpp", name.c_str(), SymbolKind::kFunction,
                                      10 + i, "void tint_config_from_settings_x() {"));
      snap.symbols.back().name = name;
      snap.symbols.back().display_name = name;
      snap.symbols.back().signature = "void " + name + "(const AppSettings* settings) {";
    }
    snap.symbols.push_back(
        make_sym("src/ui/settings_host.cpp", "SettingsHostState", SymbolKind::kStruct, 20,
                 "struct SettingsHostState {"));
    snap.symbols.push_back(
        make_sym("src/ui/settings_host.cpp", "open_settings_host", SymbolKind::kFunction, 40,
                 "void open_settings_host() {"));
    snap.symbols.push_back(
        make_sym("src/ui/settings_host.cpp", "handle_settings_host_tabs", SymbolKind::kFunction, 60,
                 "bool handle_settings_host_tabs() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/editor/tint_mark.cpp";
      e.name = "tint_mark";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/editor/tint_mark.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/settings_host.cpp";
      e.name = "settings_host";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 40;
      e.signature = "file src/ui/settings_host.cpp";
      sparse.entries.push_back(e);
    }
    const std::string q = "menu de configuracion y sus pestanas";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "settings_host",
           "path-anchored settings_host beats *_from_settings mass on tint_mark");
  }

  {
    // I/O NL: recepción/paquetes → transport path beats a fat *provider* with the same domain token.
    const auto io_exp = tuide::expand_nl_retrieval_tokens({"recepciones", "paquetes"}, 16);
    bool has_transport = false;
    bool has_notification = false;
    for (const auto& t : io_exp) {
      if (t == "transport") {
        has_transport = true;
      }
      if (t == "notification") {
        has_notification = true;
      }
    }
    expect(has_transport && has_notification,
           "recepciones/paquetes expand to transport/notification");

    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    for (int i = 0; i < 18; ++i) {
      const std::string name = "ensure_protocol_lsp_" + std::to_string(i);
      snap.symbols.push_back(
          make_sym("src/net/protocol_provider.cpp", name.c_str(), SymbolKind::kFunction, 10 + i,
                   "void ensure_protocol_lsp_x() {"));
      snap.symbols.back().name = name;
      snap.symbols.back().display_name = name;
      snap.symbols.back().signature = "void " + name + "() {";
    }
    snap.symbols.push_back(
        make_sym("src/net/protocol_provider.cpp", "open_lazy_protocol_buffers", SymbolKind::kFunction,
                 200, "void open_lazy_protocol_buffers() {"));
    snap.symbols.push_back(
        make_sym("src/net/protocol_transport.cpp", "read_message", SymbolKind::kFunction, 40,
                 "std::optional<std::string> read_message() {"));
    snap.symbols.push_back(
        make_sym("src/net/protocol_transport.cpp", "set_notification_handler", SymbolKind::kFunction,
                 80, "void set_notification_handler() {"));
    snap.symbols.push_back(
        make_sym("src/net/protocol_transport.cpp", "write_message", SymbolKind::kFunction, 120,
                 "bool write_message() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/net/protocol_provider.cpp";
      e.name = "protocol_provider";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 120;
      e.signature = "file src/net/protocol_provider.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/net/protocol_transport.cpp";
      e.name = "protocol_transport";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 40;
      e.signature = "file src/net/protocol_transport.cpp";
      sparse.entries.push_back(e);
    }
    const std::string q =
        "dame contexto de donde se gestionan las recepciones de los paquetes de protocol";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "protocol_transport",
           "recepciones/paquetes prefer protocol_transport over protocol_provider mass");
    const auto coding = sparse.ranked_coding_entries(8, q);
    bool has_read = false;
    for (const auto& e : coding) {
      if (e.name.find("read_message") != std::string::npos) {
        has_read = true;
      }
    }
    expect(has_read, "coding pack surfaces read_message for reception query");
  }

  {
    // Coloreado/syntax → highlight path beats a thin *provider* that only shares the domain token.
    const auto hl_exp = tuide::expand_nl_retrieval_tokens({"coloreado", "syntax"}, 16);
    bool has_highlight = false;
    for (const auto& t : hl_exp) {
      if (t == "highlight" || t == "highlighter") {
        has_highlight = true;
      }
    }
    expect(has_highlight, "coloreado/syntax expand to highlight");

    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    snap.symbols.push_back(
        make_sym("src/parse/acme_sitter_provider.cpp", "symbols_for_file", SymbolKind::kFunction, 9,
                 "std::vector<SymbolInfo> symbols_for_file() {"));
    snap.symbols.push_back(
        make_sym("src/parse/acme_sitter_provider.cpp", "hover_at", SymbolKind::kFunction, 16,
                 "HoverInfo hover_at() {"));
    snap.symbols.push_back(
        make_sym("src/parse/acme_sitter_provider.cpp", "completions_at", SymbolKind::kFunction, 22,
                 "std::vector<CompletionItem> completions_at() {"));
    snap.symbols.push_back(
        make_sym("src/parse/acme_sitter_highlight.cpp", "highlights_for_document",
                 SymbolKind::kFunction, 40, "std::vector<LineHighlights> highlights_for_document() {"));
    snap.symbols.push_back(
        make_sym("src/parse/acme_sitter_highlight.cpp", "highlight_query_for_lang",
                 SymbolKind::kFunction, 80, "TSQuery* highlight_query_for_lang() {"));
    snap.symbols.push_back(
        make_sym("src/parse/acme_sitter_highlight.cpp", "highlights_for_line", SymbolKind::kFunction,
                 120, "LineHighlights highlights_for_line() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/parse/acme_sitter_provider.cpp";
      e.name = "acme_sitter_provider";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/parse/acme_sitter_provider.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/parse/acme_sitter_highlight.cpp";
      e.name = "acme_sitter_highlight";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 40;
      e.signature = "file src/parse/acme_sitter_highlight.cpp";
      sparse.entries.push_back(e);
    }
    const std::string q =
        "donde se hace la gestion del coloreado del codigo usando acme sitter";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "acme_sitter_highlight",
           "coloreado prefers *_highlight over thin *_provider");
    const auto coding = sparse.ranked_coding_entries(8, q);
    bool has_hl = false;
    for (const auto& e : coding) {
      if (e.name.find("highlights_for") != std::string::npos) {
        has_hl = true;
      }
    }
    expect(has_hl, "coding pack surfaces highlights_for_* for coloring query");
  }

  {
    using tuide::CodingStemCandidate;
    using tuide::rerank_coding_stems;

    auto unit_embed = [](bool /*is_query*/, const std::string& text, std::vector<float>* out) {
      out->assign(3, 0.0f);
      const bool hl = text.find("highlight") != std::string::npos ||
                      text.find("highlighting") != std::string::npos ||
                      text.find("coloreado") != std::string::npos;
      const bool prov = text.find("provider") != std::string::npos;
      if (hl) {
        (*out)[0] = 1.0f;
      } else if (prov) {
        (*out)[1] = 1.0f;
      } else {
        (*out)[2] = 1.0f;
      }
      return true;
    };

    // Lexical prefers provider; embed flips to highlight when scores/path allow.
    std::vector<CodingStemCandidate> cands;
    {
      CodingStemCandidate c;
      c.stem = "acme_provider";
      c.lexical_score = 1000000;
      c.path_strong = 2;
      c.passage = "acme_provider src/parse/acme_provider.cpp hover_at";
      cands.push_back(c);
    }
    {
      CodingStemCandidate c;
      c.stem = "acme_highlight";
      c.lexical_score = 900000;  // within 15%
      c.path_strong = 2;
      c.passage = "acme_highlight src/parse/acme_highlight.cpp highlights_for_document";
      cands.push_back(c);
    }
    const auto rr =
        rerank_coding_stems("code highlighting with acme", cands, nullptr, unit_embed);
    expect(rr.used_embed && rr.stem == "acme_highlight",
           "embed rerank flips provider→highlight on highlighting query");

    // Fallback: no embed → lexical winner.
    const auto rr0 = rerank_coding_stems("code highlighting with acme", cands, nullptr, {});
    expect(!rr0.used_embed && rr0.stem == "acme_provider",
           "null embed keeps lexical stem");
  }

  {
    // Path-anchor must not be overturned by embed preferring *_from_settings mass.
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    for (int i = 0; i < 16; ++i) {
      const std::string name = "tint_config_from_settings_" + std::to_string(i);
      snap.symbols.push_back(make_sym("src/editor/tint_mark.cpp", name.c_str(), SymbolKind::kFunction,
                                      10 + i, "void tint_config_from_settings_x() {"));
      snap.symbols.back().name = name;
      snap.symbols.back().display_name = name;
      snap.symbols.back().signature = "void " + name + "(const AppSettings* settings) {";
    }
    snap.symbols.push_back(
        make_sym("src/ui/settings_host.cpp", "SettingsHostState", SymbolKind::kStruct, 20,
                 "struct SettingsHostState {"));
    snap.symbols.push_back(
        make_sym("src/ui/settings_host.cpp", "open_settings_host", SymbolKind::kFunction, 40,
                 "void open_settings_host() {"));

    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/editor/tint_mark.cpp";
      e.name = "tint_mark";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/editor/tint_mark.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/settings_host.cpp";
      e.name = "settings_host";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 40;
      e.signature = "file src/ui/settings_host.cpp";
      sparse.entries.push_back(e);
    }
    sparse.coding_embed_fn = [](bool is_query, const std::string& text, std::vector<float>* out) {
      out->assign(2, 0.0f);
      if (is_query) {
        // Adversarial: query vector aligned with tint_mark passages.
        (*out)[0] = 1.0f;
        return true;
      }
      if (text.find("tint") != std::string::npos || text.find("from_settings") != std::string::npos) {
        (*out)[0] = 1.0f;
      } else {
        (*out)[1] = 1.0f;
      }
      return true;
    };
    const std::string q = "menu de configuracion";
    sparse.enrich_dominant_stem_from_snapshot(&snap, q, 32);
    expect(sparse.context_stem == "settings_host",
           "embed cannot overturn path-anchored settings_host");
    expect(sparse.embed_rerank_used, "embed_rerank attempted with test hook");
  }

  {
    // Stem-index recall: generic "panel" lexical winner loses to performance_panel via fused cosine.
    using tuide::CodingStemEmbedIndex;
    using tuide::CodingStemEmbedRow;
    using tuide::fuse_coding_stems;
    using tuide::CodingStemCandidate;

    CodingStemEmbedIndex idx;
    {
      std::vector<CodingStemEmbedRow> rows;
      CodingStemEmbedRow panel;
      panel.stem = "panel";
      panel.passage = "panel src/ui/panel.hpp MakePanel PanelTitle PanelBody";
      panel.embedding = {1.0f, 0.0f, 0.0f};
      rows.push_back(panel);
      CodingStemEmbedRow perf;
      perf.stem = "performance_panel";
      perf.passage =
          "performance_panel src/ui/performance_panel.cpp thread_rows update_threads render_threads";
      perf.embedding = {0.0f, 1.0f, 0.0f};
      rows.push_back(perf);
      idx.set_rows_for_test(std::move(rows));
    }

    auto q_embed = [](bool is_query, const std::string& /*text*/, std::vector<float>* out) {
      // Query about rendimiento/hilos → aligned with performance_panel.
      out->assign({0.05f, 0.95f, 0.0f});
      (void)is_query;
      return true;
    };

    std::vector<CodingStemCandidate> lex;
    {
      CodingStemCandidate c;
      c.stem = "panel";
      c.lexical_score = 5000000;  // path_strong=1
      c.path_strong = 1;
      c.passage = "panel src/ui/panel.hpp MakePanel";
      lex.push_back(c);
    }
    {
      CodingStemCandidate c;
      c.stem = "performance_panel";
      c.lexical_score = 5000500;
      c.path_strong = 1;
      c.passage = "performance_panel src/ui/performance_panel.cpp";
      lex.push_back(c);
    }
    const auto rr = fuse_coding_stems(
        "panel de rendimiento informacion de hilos", std::move(lex), &idx, nullptr, q_embed);
    expect(rr.used_embed && rr.stem == "performance_panel",
           "stem-index fusion prefers performance_panel over generic panel");

    // Full enrich path with index pointer.
    SymbolIndexSnapshot snap;
    snap.workspace_root = "/tmp";
    snap.symbols.push_back(
        make_sym("src/ui/panel.hpp", "MakePanel", SymbolKind::kFunction, 10, "Element MakePanel() {"));
    snap.symbols.push_back(make_sym("src/ui/performance_panel.cpp", "update_thread_rows",
                                    SymbolKind::kFunction, 40, "void update_thread_rows() {"));
    snap.symbols.push_back(make_sym("src/ui/performance_panel.cpp", "render_threads",
                                    SymbolKind::kFunction, 80, "Element render_threads() {"));
    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/ui/panel.hpp";
      e.name = "panel";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/ui/panel.hpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/performance_panel.cpp";
      e.name = "performance_panel";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 40;
      e.signature = "file src/ui/performance_panel.cpp";
      sparse.entries.push_back(e);
    }
    sparse.coding_stem_index = &idx;
    sparse.coding_embed_fn = q_embed;
    sparse.enrich_dominant_stem_from_snapshot(
        &snap, "panel de rendimiento y donde se gestiona la informacion de hilos", 32);
    expect(sparse.context_stem == "performance_panel",
           "enrich+stem-index picks performance_panel for rendimiento/hilos query");
  }

  {
    // Facets + no concrete stem expansion for ambiguous "pestaña".
    const auto facets = extract_query_facets(
        "dame contexto sobre el modal de configuracion y sus pestanas", 12);
    bool has_modal = false;
    bool has_config = false;
    bool has_tab = false;
    for (const auto& f : facets) {
      if (f == "modal") {
        has_modal = true;
      }
      if (f.find("config") != std::string::npos) {
        has_config = true;
      }
      if (f.find("pestan") != std::string::npos) {
        has_tab = true;
      }
    }
    expect(has_modal && has_config && has_tab, "facets capture modal/config/pestanas");

    const auto expanded = tuide::expand_nl_retrieval_tokens({"pestanas", "configuracion"}, 16);
    bool has_editor_tab = false;
    bool has_settings = false;
    bool has_generic_tab = false;
    for (const auto& t : expanded) {
      if (t == "editor_tab" || t == "file_tree") {
        has_editor_tab = true;
      }
      if (t == "settings" || t == "config") {
        has_settings = true;
      }
      if (t == "tab" || t == "tabs") {
        has_generic_tab = true;
      }
    }
    expect(!has_editor_tab, "NL expand avoids concrete editor_tab stem");
    expect(has_settings && has_generic_tab, "NL expand settings + generic tab");

    const auto menu_exp = tuide::expand_nl_retrieval_tokens({"menu", "configuracion"}, 16);
    bool menu_to_settings = false;
    bool menu_to_context = false;
    for (const auto& t : menu_exp) {
      if (t == "settings" || t == "config") {
        menu_to_settings = true;
      }
      if (t == "context_menu") {
        menu_to_context = true;
      }
    }
    expect(menu_to_settings, "menu expands to settings/config");
    expect(!menu_to_context, "menu does not expand to context_menu");

    const auto menu_facets = extract_query_facets(
        "dame contexto sobre el menu de configuracion y sus pestanas", 12);
    const int cov_settings_menu = facet_coverage_score(
        "src/ui/settings_modal.cpp", "SettingsModal", "class SettingsModal {", menu_facets);
    const int cov_context_menu = facet_coverage_score(
        "src/ui/context_menu.cpp", "ContextMenu", "class ContextMenu {", menu_facets);
    expect(cov_settings_menu > cov_context_menu, "settings beats context_menu for menu+config");

    const int cov_settings =
        facet_coverage_score("src/ui/settings_modal.cpp", "SettingsModal", "class SettingsModal {",
                             facets);
    const int cov_tabs =
        facet_coverage_score("src/ui/editor_tab_bar.cpp", "EditorTabBar", "struct EditorTabBar {",
                             facets);
    expect(cov_settings > cov_tabs, "settings_modal beats editor_tab_bar on multi-facet");
  }

  {
    expect(query_asks_context_dump("dame contexto de la compilación"), "dame contexto");
    expect(query_asks_context_dump("give me context of Foo"), "give me context");
    expect(query_asks_context_dump("dame codigo de MakeConsolePanel"), "dame codigo de");
    expect(query_asks_context_dump("código de MakeConsolePanel"), "codigo de + ident");
    expect(query_asks_context_dump(
               "ahora dame el conexto de donde se gestiona el resalte del gutter de git"),
           "typo conexto still dumps");
    expect(!query_asks_context_dump("dónde está el panel"), "locate alone is not dump");
    expect(!query_asks_context_dump("el contexto de la reunión"), "bare contexto de rejected");
    expect(!query_asks_context_dump("código de la"), "codigo de without ident rejected");

    using tuide::query_asks_git_repo;
    expect(!query_asks_git_repo(
               "ahora dame el conexto de donde se gestiona el resalte del gutter de git "
               "para que cuando edito código se realce la línea como editada"),
           "git gutter highlight ≠ git repo tools");
    expect(query_asks_git_repo("muéstrame el git status"), "git status is repo");
    expect(query_asks_git_repo("haz git pull"), "git pull is repo");
  }

  {
    const auto a = parse_get_code_of_arg("src/ui/foo.cpp:Bar", "/tmp/ws");
    expect(a.file == "src/ui/foo.cpp" && a.symbol == "Bar" && a.line == 0, "path:Symbol");
    const auto b = parse_get_code_of_arg("src/ui/foo.cpp:42", "/tmp/ws");
    expect(b.file == "src/ui/foo.cpp" && b.line == 42 && b.symbol.empty(), "path:line");
    const auto c = parse_get_code_of_arg("OnlySymbol", "/tmp/ws");
    expect(c.symbol == "OnlySymbol" && c.file.empty(), "bare symbol");
    const auto d = parse_get_code_of_arg("src/ui/foo.cpp:10-40", "/tmp/ws");
    expect(d.file == "src/ui/foo.cpp" && d.window == tuide::GetCodeOfWindow::Range &&
               d.range_start == 10 && d.range_end == 40,
           "path:A-B range");
    const auto e = parse_get_code_of_arg("src/ui/foo.cpp:Bar#tail", "/tmp/ws");
    expect(e.file == "src/ui/foo.cpp" && e.symbol == "Bar" &&
               e.window == tuide::GetCodeOfWindow::Tail,
           "path:Symbol#tail");
    const auto f = parse_get_code_of_arg("src/ui/foo.cpp:Bar#mid", "/tmp/ws");
    expect(f.window == tuide::GetCodeOfWindow::Mid && f.symbol == "Bar", "path:Symbol#mid");
  }

  fs::path tmp = fs::temp_directory_path() / "tuide_get_code_of_test";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  fs::create_directories(tmp, ec);
  const fs::path cpp = tmp / "sample.cpp";
  {
    std::ofstream out(cpp);
    out << "#include <string>\n"
           "\n"
           "class Widget {\n"
           " public:\n"
           "  void paint();\n"
           "  int size_ = 0;\n"
           "};\n"
           "\n"
           "void Widget::paint() {\n"
           "  size_ = 1;\n"
           "}\n"
           "\n"
           "int helper() { return 0; }\n";
  }

  {
    GetCodeOfRequest req;
    req.workspace_root = tmp.string();
    req.file = "sample.cpp";
    req.symbol = "Widget";
    req.max_lines = 80;
    const auto got = get_code_of(req);
    expect(got.ok, "get_code_of Widget ok");
    expect(got.text.find("class Widget") != std::string::npos, "body has class Widget");
    expect(got.text.find("size_") != std::string::npos, "body has class field");
    expect(got.start_line >= 1 && got.end_line >= got.start_line, "line range");
  }

  {
    GetCodeOfRequest req;
    req.workspace_root = tmp.string();
    req.file = cpp.string();
    req.line = 9;  // Widget::paint
    req.max_lines = 40;
    const auto got = get_code_of(req);
    expect(got.ok, "get_code_of by line ok");
    expect(got.text.find("paint") != std::string::npos, "line pick finds paint");
  }

  {
    const fs::path long_cpp = tmp / "long_fn.cpp";
    {
      std::ofstream out(long_cpp);
      out << "int long_fn() {\n";
      for (int i = 1; i <= 80; ++i) {
        out << "  int x" << i << " = " << i << ";\n";
      }
      out << "  return 99;\n}\n";
    }
    GetCodeOfRequest req;
    req.workspace_root = tmp.string();
    req.file = "long_fn.cpp";
    req.symbol = "long_fn";
    req.max_lines = 20;
    req.window = tuide::GetCodeOfWindow::Auto;
    const auto got = get_code_of(req);
    expect(got.ok, "long_fn ok");
    expect(got.truncated, "long_fn truncated");
    expect(got.omitted_start > 0 && got.omitted_end >= got.omitted_start, "omitted span");
    expect(got.text.find("int long_fn()") != std::string::npos, "keeps head/signature");
    expect(got.text.find("return 99") != std::string::npos, "keeps tail");
    expect(got.text.find("omitted lines") != std::string::npos, "omitted marker");
    expect(!got.refetch_hint.empty(), "refetch hint");
    const std::string formatted = tuide::format_get_code_of_result(got, "long_fn.cpp");
    expect(formatted.find("[TRUNCATED]") != std::string::npos, "format TRUNCATED");
    expect(formatted.find("missing_lines:") != std::string::npos, "format missing_lines");
    expect(formatted.find("refetch:") != std::string::npos, "format refetch");

    GetCodeOfRequest tail_req = req;
    tail_req.window = tuide::GetCodeOfWindow::Tail;
    const auto tail = get_code_of(tail_req);
    expect(tail.ok && tail.truncated, "tail window");
    expect(tail.text.find("return 99") != std::string::npos, "tail has return");
    expect(tail.text.find("int long_fn()") == std::string::npos, "tail skips signature");

    GetCodeOfRequest range_req;
    range_req.workspace_root = tmp.string();
    range_req.file = "long_fn.cpp";
    range_req.window = tuide::GetCodeOfWindow::Range;
    range_req.range_start = 40;
    range_req.range_end = 45;
    range_req.max_lines = 120;
    const auto ranged = get_code_of(range_req);
    expect(ranged.ok, "range ok");
    expect(ranged.text.find("x39") != std::string::npos ||
               ranged.text.find("x40") != std::string::npos,
           "range includes mid vars");
    expect(!ranged.truncated, "small range not truncated");

    // path:line inside a huge symbol → window around the line (not whole-method head+tail).
    GetCodeOfRequest line_win;
    line_win.workspace_root = tmp.string();
    line_win.file = "long_fn.cpp";
    line_win.line = 45;  // mid of long_fn
    line_win.max_lines = 20;
    line_win.window = tuide::GetCodeOfWindow::Auto;
    const auto around = get_code_of(line_win);
    expect(around.ok && around.truncated, "line-window truncated huge symbol");
    expect(around.text.find("line-window") != std::string::npos, "line-window marker");
    expect(around.text.find("x45") != std::string::npos ||
               around.text.find("x44") != std::string::npos,
           "line-window keeps mid vars");
    expect(around.text.find("int long_fn()") == std::string::npos, "line-window skips signature");
    expect(around.text.find("return 99") == std::string::npos, "line-window skips return");
    expect(!around.refetch_hint.empty(), "line-window refetch");
    expect(around.refetch_hint.find("long_fn.cpp:1-") == std::string::npos ||
               around.refetch_hint.find("-20") == std::string::npos,
           "refetch not forced to symbol head only");
    // Prefer adjacent window near the requested line.
    expect(around.refetch_hint.find(':') != std::string::npos, "refetch is path:range");
  }

  {
    // Multi-facet + stem/file affinity: settings modal beats editor tabs; same-stem helpers rise.
    SymbolIndexSnapshot snap;
    snap.workspace_root = tmp.string();
    snap.symbols.push_back(make_sym("src/ui/editor_tab_bar.cpp", "editor_tab_bar",
                                    SymbolKind::kFunction, 1, "file src/ui/editor_tab_bar.cpp"));
    snap.symbols.back().signature = "file src/ui/editor_tab_bar.cpp";
    snap.symbols.push_back(
        make_sym("src/ui/editor_tab_bar.cpp", "EditorTabBarState", SymbolKind::kStruct, 22,
                 "struct EditorTabBarState {"));
    snap.symbols.push_back(
        make_sym("src/ui/editor_tab_bar.cpp", "make_tabs_overflow_modal", SymbolKind::kFunction, 213,
                 "Component make_tabs_overflow_modal("));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.hpp", "settings_modal", SymbolKind::kFunction, 1,
                 "file src/ui/settings_modal.hpp"));
    snap.symbols.back().signature = "file src/ui/settings_modal.hpp";
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.hpp", "SettingsModalState", SymbolKind::kStruct, 50,
                 "struct SettingsModalState {"));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "settings_modal", SymbolKind::kFunction, 1,
                 "file src/ui/settings_modal.cpp"));
    snap.symbols.back().signature = "file src/ui/settings_modal.cpp";
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "append_top_level_tabs_header", SymbolKind::kFunction,
                 57, "void append_top_level_tabs_header("));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "switch_top_level_tab", SymbolKind::kFunction, 80,
                 "void switch_top_level_tab("));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "MakeSettingsModalOverlay", SymbolKind::kFunction, 120,
                 "Component MakeSettingsModalOverlay("));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "open_settings_modal", SymbolKind::kFunction, 200,
                 "void open_settings_modal("));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "close_settings_modal", SymbolKind::kFunction, 220,
                 "void close_settings_modal("));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.hpp", "ClickTarget", SymbolKind::kStruct, 119,
                 "struct ClickTarget {"));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "SettingsModalState", SymbolKind::kStruct, 40,
                 "struct SettingsModalState {"));
    snap.symbols.push_back(
        make_sym("src/ui/settings_modal.cpp", "start_export_portable", SymbolKind::kFunction, 61,
                 "void start_export_portable("));
    snap.symbols.push_back(
        make_sym("src/util/tabular_file.cpp", "tabular_file", SymbolKind::kFunction, 1,
                 "file src/util/tabular_file.cpp"));
    snap.symbols.back().signature = "file src/util/tabular_file.cpp";

    // Create minimal files so get_code_of / dump can read bodies.
    fs::create_directories(tmp / "src/ui", ec);
    {
      std::ofstream out(tmp / "src/ui/settings_modal.hpp");
      out << "struct SettingsModalState {\n  int tab_ = 0;\n};\n"
             "struct ClickTarget {\n  int row = -1;\n};\n";
    }
    {
      std::ofstream out(tmp / "src/ui/settings_modal.cpp");
      out << "struct SettingsModalState {\n  int tab_ = 0;\n};\n"
             "void append_top_level_tabs_header() {}\n"
             "void switch_top_level_tab() {}\n"
             "void start_export_portable() {}\n"
             "void MakeSettingsModalOverlay() {}\n"
             "void open_settings_modal() {}\n"
             "void close_settings_modal() {}\n";
    }
    {
      std::ofstream out(tmp / "src/ui/editor_tab_bar.cpp");
      out << "struct EditorTabBarState {\n  int x = 0;\n};\n"
             "void make_tabs_overflow_modal() {}\n";
    }

    RepoMapOptions opts;
    opts.query = "dame contexto sobre el modal de configuracion y sus pestanas";
    opts.prefer_git_tracked = false;
    opts.use_pagerank = false;
    opts.max_symbols = 24;
    const auto map = build_repo_map(&snap, opts);
    expect(map.note.find("query_hits=") != std::string::npos, "settings query has hits");

    RepoMap enriched = map;
    enriched.enrich_dominant_stem_from_snapshot(&snap, opts.query, 48);
    const auto coding = enriched.ranked_coding_entries(14, opts.query);
    expect(!coding.empty(), "coding ranked non-empty");
    bool top_is_settings =
        !coding.empty() && coding.front().file.find("settings_modal") != std::string::npos;
    expect(top_is_settings, "top coding entry is settings_modal");

    int settings_stem = 0;
    int editor_cluster = 0;
    int helper_pos = -1;
    int lifecycle_pos = -1;
    int overflow_pos = -1;
    int click_pos = -1;
    int export_pos = -1;
    int file_synth_pos = -1;
    bool has_state = false;
    for (std::size_t i = 0; i < coding.size(); ++i) {
      if (coding[i].file.find("settings_modal") != std::string::npos) {
        ++settings_stem;
      }
      if (coding[i].file.find("editor_tab") != std::string::npos) {
        ++editor_cluster;
      }
      if ((coding[i].name.find("append_top_level_tabs_header") != std::string::npos ||
           coding[i].name.find("switch_top_level_tab") != std::string::npos) &&
          helper_pos < 0) {
        helper_pos = static_cast<int>(i);
      }
      if ((coding[i].name.find("MakeSettingsModalOverlay") != std::string::npos ||
           coding[i].name.find("open_settings_modal") != std::string::npos ||
           coding[i].name.find("close_settings_modal") != std::string::npos) &&
          lifecycle_pos < 0) {
        lifecycle_pos = static_cast<int>(i);
      }
      if (coding[i].name.find("make_tabs_overflow_modal") != std::string::npos && overflow_pos < 0) {
        overflow_pos = static_cast<int>(i);
      }
      if (coding[i].name.find("ClickTarget") != std::string::npos && click_pos < 0) {
        click_pos = static_cast<int>(i);
      }
      if (coding[i].name.find("start_export_portable") != std::string::npos && export_pos < 0) {
        export_pos = static_cast<int>(i);
      }
      if (coding[i].signature.rfind("file ", 0) == 0 && file_synth_pos < 0) {
        file_synth_pos = static_cast<int>(i);
      }
      if (coding[i].name.find("SettingsModalState") != std::string::npos) {
        has_state = true;
      }
    }
    expect(settings_stem >= 3, "coding pack keeps settings_modal depth");
    expect(editor_cluster <= 1, "coding pack caps foreign editor_tab");
    expect(helper_pos >= 0, "coding pack includes tabs helper");
    expect(lifecycle_pos >= 0, "coding pack includes Make/open/close lifecycle");
    expect(has_state, "coding pack keeps SettingsModalState anchor");
    expect(file_synth_pos < 0, "bare file synthetic omitted when primary type exists");
    expect(export_pos < 0 || (lifecycle_pos >= 0 && lifecycle_pos < export_pos),
           "lifecycle ranks before peripheral export helper");
    expect(overflow_pos < 0 || (helper_pos >= 0 && helper_pos < overflow_pos),
           "settings helper before foreign overflow modal");
    expect(click_pos < 0 || (helper_pos >= 0 && click_pos > helper_pos),
           "ClickTarget does not dominate over helpers");

    const auto outline = enriched.coding_outline_entries(20, opts.query);
    expect(outline.size() >= 3, "outline has several stem symbols");

    std::string err;
    const std::string path = dump_context_last_md(tmp.string(), opts.query, map, 14, &err, &snap);
    expect(!path.empty(), "dump path ok: " + err);
    std::ifstream in(path);
    std::string md((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    expect(md.find("coding pack") != std::string::npos || md.find("coding_pack=1") != std::string::npos,
           "dump is coding pack");
    expect(md.find("## Outline") != std::string::npos, "dump has Outline section");
    expect(md.find("## Bodies") != std::string::npos, "dump has Bodies section");
    expect(md.find("query: query:") == std::string::npos, "dump does not double-prefix query");
    // Even if caller passes a prefixed query string:
    {
      std::string err_q;
      const std::string path_q =
          dump_context_last_md(tmp.string(), std::string("query: ") + opts.query, map, 14, &err_q,
                               &snap);
      expect(!path_q.empty(), "prefixed dump ok");
      std::ifstream in_q(path_q);
      std::string md_q((std::istreambuf_iterator<char>(in_q)), std::istreambuf_iterator<char>());
      expect(md_q.find("query: query:") == std::string::npos, "strips caller query: prefix");
    }
    expect(md.find("settings_modal") != std::string::npos, "dump mentions settings_modal");
    expect(md.find("MakeSettingsModalOverlay") != std::string::npos ||
               md.find("open_settings_modal") != std::string::npos,
           "dump lists overlay lifecycle");
    const auto settings_pos = md.find("settings_modal");
    const auto editor_pos = md.find("editor_tab_bar");
    expect(settings_pos != std::string::npos &&
               (editor_pos == std::string::npos || settings_pos < editor_pos),
           "settings appears before editor_tab in dump");
    const auto helper_md = md.find("append_top_level_tabs_header");
    const auto make_md = md.find("MakeSettingsModalOverlay");
    const auto switch_md = md.find("switch_top_level_tab");
    expect(helper_md != std::string::npos || make_md != std::string::npos ||
               switch_md != std::string::npos,
           "dump lists settings tabs/overlay helper");
    const auto overflow_md = md.find("make_tabs_overflow_modal");
    const auto first_helper =
        std::min({helper_md == std::string::npos ? md.size() : helper_md,
                  make_md == std::string::npos ? md.size() : make_md,
                  switch_md == std::string::npos ? md.size() : switch_md});
    expect(overflow_md == std::string::npos || first_helper < overflow_md,
           "dump lists settings helper before editor overflow modal");

    // Sparse map (only file synthetics) + enrich from full snapshot → depth on dominant stem.
    RepoMap sparse;
    {
      RepoMapEntry e;
      e.file = "src/ui/settings_modal.hpp";
      e.name = "settings_modal";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100;
      e.signature = "file src/ui/settings_modal.hpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/context_menu.cpp";
      e.name = "context_menu";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 90;
      e.signature = "file src/ui/context_menu.cpp";
      sparse.entries.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/app/editor_tabs.hpp";
      e.name = "editor_tabs";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 80;
      e.signature = "file src/app/editor_tabs.hpp";
      sparse.entries.push_back(e);
    }
    const std::string menu_q =
        "dame contexto sobre donde se hace la gestion del menu de configuracion y sus pestanas";
    // Dump enriches from the original sparse map (copy inside dump).
    std::string err2;
    RepoMap sparse_for_dump = sparse;
    const std::string path2 =
        dump_context_last_md(tmp.string(), menu_q, sparse_for_dump, 10, &err2, &snap);
    expect(!path2.empty(), "enriched dump path ok: " + err2);
    std::ifstream in2(path2);
    std::string md2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
    expect(md2.find("enrich_stem=settings_modal") != std::string::npos, "dump note has enrich");
    expect(md2.find("coding_pack=1") != std::string::npos, "enriched dump coding_pack note");
    expect(md2.find("## Outline (stem=settings_modal)") != std::string::npos,
           "outline locked to settings_modal not app_settings");
    expect(md2.find("## Outline") != std::string::npos, "enriched dump has Outline");
    expect(md2.find("SettingsModalState") != std::string::npos ||
               md2.find("append_top_level_tabs_header") != std::string::npos ||
               md2.find("MakeSettingsModalOverlay") != std::string::npos,
           "enriched dump includes settings depth");
    expect(md2.find("app_settings") == std::string::npos ||
               md2.find("settings_modal") < md2.find("app_settings"),
           "settings_modal before app_settings in menu query dump");

    const int added = sparse.enrich_dominant_stem_from_snapshot(&snap, menu_q, 32);
    expect(added >= 2, "enrich pulls extra settings_modal symbols");
    expect(sparse.context_stem == "settings_modal", "enrich locks context_stem");
    expect(sparse.note.find("enrich_stem=settings_modal") != std::string::npos, "enrich note");
    const auto deep = sparse.ranked_coding_entries(10, menu_q);
    int deep_settings = 0;
    bool deep_state = false;
    bool deep_helper = false;
    int ctx_menu_n = 0;
    int app_settings_n = 0;
    for (const auto& e : deep) {
      if (e.file.find("settings_modal") != std::string::npos) {
        ++deep_settings;
      }
      if (e.file.find("app_settings") != std::string::npos) {
        ++app_settings_n;
      }
      if (e.name.find("SettingsModalState") != std::string::npos) {
        deep_state = true;
      }
      if (e.name.find("append_top_level_tabs_header") != std::string::npos ||
          e.name.find("MakeSettingsModalOverlay") != std::string::npos ||
          e.name.find("switch_top_level_tab") != std::string::npos) {
        deep_helper = true;
      }
      if (e.file.find("context_menu") != std::string::npos) {
        ++ctx_menu_n;
      }
    }
    expect(deep_settings >= 3, "enriched coding ranking keeps depth on settings_modal");
    expect(deep_helper, "enriched coding includes tabs/overlay helper");
    expect(deep_state || deep_helper, "enriched coding includes state or tabs helper");
    expect(ctx_menu_n <= 1, "context_menu limited after menu→settings NL");
    expect(app_settings_n <= 1, "app_settings does not steal dominant stem");
  }

  {
    // Ranked map entries for compile-ish query include path-stem; get_code_of on script.
    const fs::path sh = tmp / "tools";
    fs::create_directories(sh, ec);
    const fs::path script = sh / "compile.sh";
    {
      std::ofstream out(script);
      out << "#!/usr/bin/env bash\n"
             "log() { echo hi; }\n"
             "main() { log; }\n";
    }

    SymbolIndexSnapshot snap;
    snap.workspace_root = tmp.string();
    snap.symbols.push_back(
        make_sym("tools/compile.sh", "log", SymbolKind::kFunction, 2, "log() {"));
    snap.symbols.push_back(
        make_sym("sample.cpp", "Widget", SymbolKind::kClass, 3, "class Widget {"));
    snap.symbols.push_back(
        make_sym("sample.cpp", "helper", SymbolKind::kFunction, 14, "int helper() {"));

    RepoMapOptions opts;
    opts.query = "dame contexto de Widget";
    opts.prefer_git_tracked = false;
    opts.use_pagerank = false;
    opts.max_symbols = 8;
    const auto map = build_repo_map(&snap, opts);
    expect(map.note.find("query_hits=") != std::string::npos, "Widget query has hits");

    const auto inv = map.ranked_investigate_entries(10);
    expect(!inv.empty(), "investigate entries non-empty");

    const auto ctx = map.ranked_context_entries(10, opts.query);
    expect(!ctx.empty(), "context entries");
    bool ctx_prefers_widget = false;
    if (!ctx.empty()) {
      ctx_prefers_widget = ctx.front().name.find("Widget") != std::string::npos ||
                           ctx.front().kind == SymbolKind::kClass;
    }
    expect(ctx_prefers_widget, "context dump prefers Widget class");
    // Class decls are first-class for context, not buried behind helpers.
    bool class_before_helper = false;
    int class_i = -1;
    int helper_i = -1;
    for (int i = 0; i < static_cast<int>(ctx.size()); ++i) {
      if (ctx[static_cast<std::size_t>(i)].kind == SymbolKind::kClass ||
          ctx[static_cast<std::size_t>(i)].name == "Widget") {
        class_i = i;
      }
      if (ctx[static_cast<std::size_t>(i)].name == "helper") {
        helper_i = i;
      }
    }
    class_before_helper = class_i >= 0 && (helper_i < 0 || class_i < helper_i);
    expect(class_before_helper, "Widget class ranked before helper in context entries");
    (void)inv;

    std::string err;
    const std::string path =
        tuide::dump_context_last_md(tmp.string(), opts.query, map, 10, &err);
    expect(!path.empty(), "context_last.md path: " + err);
    expect(fs::exists(path), "context_last.md exists");
    std::ifstream in(path);
    std::string md((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    expect(md.find("class Widget") != std::string::npos, "md contains class Widget body");
    expect(md.find("size_") != std::string::npos, "md contains class field");
    expect(md.find("```cpp") != std::string::npos, "md has cpp fence");
  }

  {
    // compile.sh path-stem still surfaces under build-ish context query.
    SymbolIndexSnapshot snap;
    snap.workspace_root = tmp.string();
    snap.symbols.push_back(
        make_sym("tools/compile.sh", "log", SymbolKind::kFunction, 2, "log() {"));
    snap.symbols.push_back(
        make_sym("sample.cpp", "Widget", SymbolKind::kClass, 3, "class Widget {"));

    RepoMapOptions opts;
    opts.query = "dame contexto de la compilación";
    opts.prefer_git_tracked = false;
    opts.use_pagerank = false;
    opts.max_symbols = 8;
    const auto map = build_repo_map(&snap, opts);
    expect(map.note.find("query_hits=") != std::string::npos, "compilacion query_hits");
    const auto ranked = map.ranked_context_entries(10, opts.query);
    bool has_compile = false;
    for (const auto& e : ranked) {
      if (e.file.find("compile.sh") != std::string::npos) {
        has_compile = true;
      }
    }
    expect(has_compile, "context entries include compile.sh");
  }

  {
    using tuide::CodingStemShortlistItem;
    using tuide::validate_coding_stem_pick;
    std::vector<CodingStemShortlistItem> list;
    {
      CodingStemShortlistItem a;
      a.stem = "performance_panel";
      list.push_back(a);
      CodingStemShortlistItem b;
      b.stem = "editor_panel";
      list.push_back(b);
    }
    expect(validate_coding_stem_pick("performance_panel", list) == "performance_panel",
           "validate exact stem");
    expect(validate_coding_stem_pick("  Performance_Panel  ", list) == "performance_panel",
           "validate case-insensitive trim");
    expect(validate_coding_stem_pick("\"editor_panel\"", list) == "editor_panel",
           "validate quoted stem");
    expect(validate_coding_stem_pick("settings_modal", list).empty(),
           "validate rejects invented stem");
    expect(validate_coding_stem_pick("", list).empty(), "validate rejects empty");
  }

  {
    // wakes → wake + ui_wake expand; stopwords drop cuando/llega; shortlist diversifies lsp_*.
    using tuide::CodingStemCandidate;
    using tuide::build_fused_stem_shortlist;
    using tuide::expand_nl_retrieval_tokens;

    const auto wake_exp = expand_nl_retrieval_tokens({"wakes", "despierta"}, 16);
    bool has_wake = false;
    bool has_ui_wake = false;
    for (const auto& t : wake_exp) {
      if (t == "wake") {
        has_wake = true;
      }
      if (t == "ui_wake") {
        has_ui_wake = true;
      }
    }
    expect(has_wake && has_ui_wake, "wake NL expand → wake + ui_wake");

    const auto facets = extract_query_facets(
        "gestion de los wakes cuando me llega una respuesta de LSP para actualizar la UI", 16);
    bool has_wake_facet = false;
    bool has_cuando = false;
    bool has_llega = false;
    for (const auto& f : facets) {
      if (f == "wake" || f == "wakes") {
        has_wake_facet = true;
      }
      if (f == "cuando") {
        has_cuando = true;
      }
      if (f == "llega") {
        has_llega = true;
      }
    }
    expect(has_wake_facet, "facets include wake/wakes from plural");
    expect(!has_cuando && !has_llega, "facet stopwords drop cuando/llega");

    std::vector<CodingStemCandidate> lex;
    const char* lsp_stems[] = {"lsp_symbol_provider", "lsp_client", "lsp_sync", "lsp_uri",
                               "lsp_probe_stubs",     "lsp_missing_toast", "lsp_position",
                               "lsp_missing_prompt"};
    for (const char* s : lsp_stems) {
      CodingStemCandidate c;
      c.stem = s;
      c.lexical_score = 9000000;
      c.path_strong = 1;
      c.passage = std::string(s) + " src/symbols/" + s + ".cpp";
      lex.push_back(c);
    }
    {
      CodingStemCandidate c;
      c.stem = "ui_wake_policy";
      c.lexical_score = 5000100;
      c.path_strong = 1;
      c.passage = "ui_wake_policy wake ui src/ui/ui_wake_policy.hpp UiWakeReason";
      lex.push_back(c);
    }
    {
      CodingStemCandidate c;
      c.stem = "application";
      c.lexical_score = 4000000;
      c.path_strong = 0;
      c.passage = "application src/app/application.cpp";
      lex.push_back(c);
    }
    const auto shortlist = build_fused_stem_shortlist(
        std::move(lex), "wakes LSP UI", nullptr, nullptr, {}, 8);
    expect(shortlist.size() >= 3 && shortlist.size() <= 8, "diversified shortlist bounded");
    int lsp_family = 0;
    bool has_ui_wake_policy = false;
    bool has_application = false;
    for (const auto& it : shortlist) {
      if (it.stem.rfind("lsp_", 0) == 0) {
        ++lsp_family;
      }
      if (it.stem == "ui_wake_policy") {
        has_ui_wake_policy = true;
      }
      if (it.stem == "application") {
        has_application = true;
      }
    }
    expect(lsp_family <= 2, "shortlist caps lsp_* family at 2");
    expect(has_ui_wake_policy, "shortlist keeps ui_wake_policy despite weaker lex");
    expect(has_application, "shortlist keeps application via diversity");
  }

  {
    using tuide::CodingSymbolEmbedIndex;
    using tuide::CodingSymbolEmbedRow;
    using tuide::coding_symbol_index_passage;

    expect(coding_symbol_index_passage("a.cpp", "foo", "void foo()")
               .find("foo") != std::string::npos,
           "symbol passage includes name");

    CodingSymbolEmbedIndex idx;
    std::vector<CodingSymbolEmbedRow> rows;
    {
      CodingSymbolEmbedRow r;
      r.file = "src/ui/editor_panel.cpp";
      r.name = "git_line_changed";
      r.line = 991;
      r.signature = "bool git_line_changed(...)";
      r.passage = coding_symbol_index_passage(r.file, r.name, r.signature);
      r.embedding = {1.0f, 0.0f, 0.0f};
      rows.push_back(r);
    }
    {
      CodingSymbolEmbedRow r;
      r.file = "src/ui/main_layout.cpp";
      r.name = "apply_editor_navigation";
      r.line = 73;
      r.signature = "void apply_editor_navigation(...)";
      r.passage = coding_symbol_index_passage(r.file, r.name, r.signature);
      r.embedding = {0.0f, 1.0f, 0.0f};
      rows.push_back(r);
    }
    {
      CodingSymbolEmbedRow r;
      r.file = "src/editor/visual_highlight.cpp";
      r.name = "build_git_marks_snapshot";
      r.line = 295;
      r.signature = "void build_git_marks_snapshot(...)";
      r.passage = coding_symbol_index_passage(r.file, r.name, r.signature);
      r.embedding = {0.9f, 0.1f, 0.0f};
      rows.push_back(r);
    }
    idx.set_rows_for_test(std::move(rows));
    expect(idx.ready() && idx.size() == 3, "symbol index test ready");
    const auto top = idx.top_entries({1.0f, 0.0f, 0.0f}, 2);
    expect(top.size() == 2, "top_entries size");
    expect(top.front().name == "git_line_changed", "cosine prefers git_line_changed");
    expect(top[1].name == "build_git_marks_snapshot", "second is git marks");
  }

  {
    using tuide::RepoMapEntry;
    using tuide::TwoStageRerankOptions;
    using tuide::enrich_query_for_embed;
    using tuide::format_ranked_map_answer;
    using tuide::rerank_map_two_stage;
    using tuide::dump_ranked_map_md;
    using tuide::RankedMapDumpOptions;
    using tuide::SymbolKind;

    const std::string enriched =
        enrich_query_for_embed("wakes de la UI", {"ui_wake", "pty"});
    expect(enriched.find("wakes de la UI") != std::string::npos, "enrich keeps query");
    expect(enriched.find("ui_wake") != std::string::npos && enriched.find("pty") != std::string::npos,
           "enrich appends needles");

    // Mock embed: dim-4 bag over keywords wake/pty/nudge/terminal.
    auto mock_embed = [](bool /*is_query*/, const std::string& text, std::vector<float>* out) {
      if (out == nullptr) {
        return false;
      }
      out->assign(4, 0.0f);
      std::string low = text;
      for (char& c : low) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
      if (has("wake")) {
        (*out)[0] = 1.0f;
      }
      if (has("pty")) {
        (*out)[1] = 1.0f;
      }
      if (has("nudge")) {
        (*out)[2] = 1.0f;
      }
      if (has("terminal_display") || has("repaint")) {
        (*out)[3] = 1.0f;
      }
      return true;
    };

    // Files for phase-B body fetch.
    const fs::path ui = tmp / "src" / "ui";
    fs::create_directories(ui, ec);
    {
      std::ofstream out(ui / "ui_wake.hpp");
      out << "inline void wake_console_panel() { /* wake UI after PTY */ }\n";
    }
    {
      std::ofstream out(ui / "terminal_display.cpp");
      out << "void nudge_terminal_repaint() { /* cursor blink only */ }\n";
    }

    std::vector<RepoMapEntry> cands;
    {
      RepoMapEntry e;
      e.file = "src/ui/terminal_display.cpp";
      e.name = "nudge_terminal_repaint";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 9000000;  // lexical winner (misleading name)
      e.signature = "void nudge_terminal_repaint()";
      cands.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ui/ui_wake.hpp";
      e.name = "wake_console_panel";
      e.kind = SymbolKind::kFunction;
      e.line = 1;
      e.score = 100000;  // weaker lexical
      e.signature = "inline void wake_console_panel()";
      cands.push_back(e);
    }

    TwoStageRerankOptions opts;
    opts.query = "gestion de wakes de terminal PTY y UI";
    opts.needles = {"ui_wake", "pty"};
    opts.workspace_root = tmp.string();
    opts.phase_a_pool = 8;
    opts.phase_a_top = 8;
    opts.final_top = 4;
    opts.fetch_bodies = true;
    opts.body_max_lines = 40;

    auto ranked = rerank_map_two_stage(cands, opts, nullptr, mock_embed);
    expect(ranked.used_phase_a, "two_stage used phase A");
    expect(ranked.used_phase_b, "two_stage used phase B");
    expect(!ranked.entries.empty(), "two_stage non-empty");
    expect(ranked.entries.front().name == "wake_console_panel",
           "body embed promotes wake over nudge lexical winner");

    // Without embed backend: lexical order preserved (nudge first).
    auto lex_only = rerank_map_two_stage(cands, opts, nullptr, {});
    expect(!lex_only.used_phase_a && !lex_only.used_phase_b, "no embed → no phases");
    expect(!lex_only.entries.empty() && lex_only.entries.front().name == "nudge_terminal_repaint",
           "degrades to lexical order");

    const std::string ans = format_ranked_map_answer(ranked.entries, 8, ranked.note);
    expect(ans.find("wake_console_panel") != std::string::npos, "format lists wake");
    expect(ans.find("score=") != std::string::npos, "format shows score");

    RankedMapDumpOptions dump;
    dump.workspace_root = tmp.string();
    dump.query = opts.query;
    dump.note = ranked.note;
    dump.entries = ranked.entries;
    dump.body_texts = ranked.body_texts;
    dump.include_bodies = false;
    dump.filename = "map_last.md";
    std::string dump_err;
    const std::string map_path = dump_ranked_map_md(dump, &dump_err);
    expect(!map_path.empty(), "dump_ranked_map_md path: " + dump_err);
    expect(fs::exists(map_path), "map_last.md exists");
    expect(fs::exists(tmp / ".tuide" / "ai" / "context_last.md"), "mirrors context_last.md");
    {
      std::ifstream in(map_path);
      std::string md((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      expect(md.find("Ranked map") != std::string::npos, "dump title");
      expect(md.find("ranked_map=1") != std::string::npos, "dump note flag");
      expect(md.find("## Ranked entries") != std::string::npos, "dump entries section");
      expect(md.find("## Bodies") == std::string::npos, "L2 payload omits bodies");
      expect(md.find("wake_console_panel") != std::string::npos, "dump lists wake");
    }

    dump.include_bodies = true;
    dump.filename = "map_last.md";
    const std::string map_path2 = dump_ranked_map_md(dump, &dump_err);
    expect(!map_path2.empty(), "dump with bodies ok");
    {
      std::ifstream in(map_path2);
      std::string md((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      expect(md.find("## Bodies") != std::string::npos, "fallback dump has bodies");
      expect(md.find("wake UI after PTY") != std::string::npos ||
                 md.find("wake_console_panel") != std::string::npos,
             "body content present");
    }
  }

  {
    using tuide::BodySemanticRerankOptions;
    using tuide::RepoMapEntry;
    using tuide::build_semantic_embed_query;
    using tuide::decompose_identifier_parts;
    using tuide::merge_semantic_tokens;
    using tuide::rerank_map_body_semantic;

    const auto parts = decompose_identifier_parts("update_console_drag_head");
    expect(parts.size() >= 3, "decompose snake parts");
    bool has_drag = false;
    for (const auto& p : parts) {
      if (p == "drag") {
        has_drag = true;
      }
    }
    expect(has_drag, "decompose includes drag");

    const auto merged = merge_semantic_tokens({"busy", "cancel"},
                                              {"set_busy_spinner", "agent_busy"},
                                              {"AI busy state"});
    expect(!merged.empty(), "merge semantic tokens non-empty");
    bool has_busy = false;
    for (const auto& t : merged) {
      if (t == "busy") {
        has_busy = true;
      }
    }
    expect(has_busy, "merge keeps busy");

    const std::string sem_q = build_semantic_embed_query("spinner stuck", {"busy", "agent"});
    expect(sem_q.find("spinner stuck") != std::string::npos, "semantic query keeps NL");
    expect(sem_q.find("busy") != std::string::npos, "semantic query has tokens");
    expect(sem_q.find("set_busy_spinner") == std::string::npos,
           "semantic query excludes compound seeds");

    auto mock_body_embed = [](bool is_query, const std::string& text, std::vector<float>* out) {
      if (out == nullptr) {
        return false;
      }
      out->assign(4, 0.0f);
      std::string low = text;
      for (char& c : low) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
      if (is_query) {
        if (has("busy") || has("agent")) {
          (*out)[0] = 1.0f;
        }
      } else if (has("agent_busy") || has("busactivity")) {
        (*out)[0] = 1.0f;
      }
      return (*out)[0] > 0.f;
    };

    fs::path body_tmp = tmp / "body_sem";
    fs::create_directories(body_tmp / "src/ai", ec);
    fs::create_directories(body_tmp / "src/ui", ec);
    {
      std::ofstream f(body_tmp / "src/ai/ai_controller.cpp");
      f << "bool agent_busy() { return agent_busy_.load(); }\n";
    }
    {
      std::ofstream f(body_tmp / "src/ui/console_panel.cpp");
      f << "void update_console_drag_head() { /* hover */ }\n";
    }

    std::vector<RepoMapEntry> body_cands;
    {
      RepoMapEntry e;
      e.file = "src/ui/console_panel.cpp";
      e.name = "update_console_drag_head";
      e.line = 1;
      e.score = 2000000;
      body_cands.push_back(e);
    }
    {
      RepoMapEntry e;
      e.file = "src/ai/ai_controller.cpp";
      e.name = "agent_busy";
      e.line = 1;
      e.score = 1500000;
      body_cands.push_back(e);
    }

    BodySemanticRerankOptions bs_opts;
    bs_opts.query = "spinner IA stuck";
    bs_opts.semantic_tokens = {"busy", "agent", "spinner", "cancel"};
    bs_opts.workspace_root = body_tmp.string();
    bs_opts.body_pool = 2;
    bs_opts.final_top = 2;
    bs_opts.max_per_file = 0;
    bs_opts.max_per_stem = 0;
    bs_opts.max_per_dir = 0;

    auto body_ranked = rerank_map_body_semantic(body_cands, bs_opts, nullptr, mock_body_embed);
    expect(body_ranked.used_body_embed, "body semantic used embed");
    expect(!body_ranked.entries.empty(), "body semantic non-empty");
    expect(body_ranked.note.find("body_semantic=1") != std::string::npos, "body semantic note");
    fs::remove_all(body_tmp, ec);
  }

  fs::remove_all(tmp, ec);

  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "get_code_of_test ok\n";
  return 0;
}
