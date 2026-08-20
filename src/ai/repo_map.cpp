#include "ai/repo_map.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "ai/ai_path_scope.hpp"
#include "ai/search_needles.hpp"
#include "ai/coding_embed_rerank.hpp"
#include "ai/coding_stem_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "git/git_command.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "symbols/symbol_utils.hpp"

#include <cstdio>

namespace tuide {
namespace {

std::string ascii_lower_copy(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

// Fold Spanish/Latin accents so "búsqueda" → "busqueda" (tokenizer used to drop
// non-ASCII bytes and produce "bsqueda", killing query_hits).
std::string ascii_fold_query(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(std::tolower(c)));
      ++i;
      continue;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
      const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
      const unsigned cp = (static_cast<unsigned>(c & 0x1F) << 6) | (c1 & 0x3F);
      char mapped = 0;
      switch (cp) {
        case 0xE1:
        case 0xC1:
        case 0xE0:
        case 0xC0:
        case 0xE2:
        case 0xC2:
        case 0xE3:
        case 0xC3:
        case 0xE4:
        case 0xC4:
          mapped = 'a';
          break;
        case 0xE9:
        case 0xC9:
        case 0xE8:
        case 0xC8:
        case 0xEA:
        case 0xCA:
        case 0xEB:
        case 0xCB:
          mapped = 'e';
          break;
        case 0xED:
        case 0xCD:
        case 0xEC:
        case 0xCC:
        case 0xEE:
        case 0xCE:
        case 0xEF:
        case 0xCF:
          mapped = 'i';
          break;
        case 0xF3:
        case 0xD3:
        case 0xF2:
        case 0xD2:
        case 0xF4:
        case 0xD4:
        case 0xF5:
        case 0xD5:
        case 0xF6:
        case 0xD6:
          mapped = 'o';
          break;
        case 0xFA:
        case 0xDA:
        case 0xF9:
        case 0xD9:
        case 0xFB:
        case 0xDB:
        case 0xFC:
        case 0xDC:
          mapped = 'u';
          break;
        case 0xF1:
        case 0xD1:
          mapped = 'n';
          break;
        case 0xE7:
        case 0xC7:
          mapped = 'c';
          break;
        default:
          break;
      }
      if (mapped != 0) {
        out.push_back(mapped);
      }
      i += 2;
      continue;
    }
    // Skip longer UTF-8 sequences.
    if ((c & 0xF0) == 0xE0) {
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      i += 4;
    } else {
      ++i;
    }
  }
  return out;
}

bool is_stopword(const std::string& w) {
  static const std::unordered_set<std::string> kStop = {
      "a",           "al",           "an",          "and",         "archivo",     "archivos",
      "as",          "at",           "be",          "busca",       "buscar",      "by",
      "code",        "codigo",       "código",      "como",        "cómo",        "con",
      "cual",        "cuál",         "de",          "del",         "donde",       "dónde",
      "el",          "en",           "encuentra",   "es",          "esta",        "está",
      "estan",       "están",        "este",        "feature",     "find",        "for",
      "from",        "fuente",       "funcionalidad","funcinoalidad","functionality","gestion",
      "gestión",     "gestiona",     "hay",         "how",         "in",          "investiga",
      "investigar",  "investigate",  "is",          "la",          "las",         "lo",
      "localiza",    "localizar",    "los",         "me",          "mi",          "my",
      "of",          "on",           "or",          "para",        "por",         "proceso",
      "permite",     "hacer",        "lanzar",      "dentro",      "entero",      "palabras",
      "genera",      "global",       "aplicacion",  "aplicación",  "proyecto",
      "que",         "qué",         "se",          "si",          "sin",         "sobre",
      "source",      "the",          "this",        "to",          "un",          "una",
      "what",        "where",        "which",       "with",        "y",
  };
  return kStop.count(w) > 0;
}

const char* kind_label(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::kNamespace:
      return "ns";
    case SymbolKind::kClass:
      return "class";
    case SymbolKind::kStruct:
      return "struct";
    case SymbolKind::kFunction:
      return "fn";
    case SymbolKind::kMethod:
      return "method";
    case SymbolKind::kVariable:
      return "var";
  }
  return "sym";
}

int kind_boost(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::kClass:
    case SymbolKind::kStruct:
      return 45;
    case SymbolKind::kFunction:
    case SymbolKind::kMethod:
      return 35;
    case SymbolKind::kNamespace:
      return 12;
    case SymbolKind::kVariable:
      return 0;
  }
  return 0;
}

int path_boost(const std::string& file) {
  if (file.rfind("src/", 0) == 0) {
    return 28;
  }
  if (file.rfind("include/", 0) == 0) {
    return 22;
  }
  if (file.rfind("tools/", 0) == 0 || file.rfind("scripts/", 0) == 0) {
    return 14;
  }
  if (file.rfind("tests/", 0) == 0 || file.rfind("test/", 0) == 0) {
    return 8;
  }
  if (file.rfind("docs/", 0) == 0 || file.rfind("third_party/", 0) == 0) {
    return -30;
  }
  return 0;
}

std::string path_basename(const std::string& file) {
  const auto slash = file.find_last_of('/');
  return slash == std::string::npos ? file : file.substr(slash + 1);
}

std::string path_stem(const std::string& file) {
  const std::string base = path_basename(file);
  const auto dot = base.find_last_of('.');
  if (dot == std::string::npos || dot == 0) {
    return base;
  }
  return base.substr(0, dot);
}

std::string path_entry_signature(const std::string& file) {
  const std::string base = path_basename(file);
  const std::string base_l = ascii_lower_copy(base);
  if (base_l.size() >= 3 &&
      (base_l.compare(base_l.size() - 3, 3, ".sh") == 0 ||
       base_l.compare(base_l.size() - 5, 5, ".bash") == 0)) {
    return "script " + file;
  }
  if (base == "CMakeLists.txt" || base_l.size() >= 6 &&
                                      base_l.compare(base_l.size() - 6, 6, ".cmake") == 0) {
    return "cmake " + file;
  }
  if (base_l.find("makefile") != std::string::npos ||
      (base_l.size() >= 3 && base_l.compare(base_l.size() - 3, 3, ".mk") == 0)) {
    return "make " + file;
  }
  return "file " + file;
}

IndexedSymbol make_path_map_symbol(const std::string& file) {
  IndexedSymbol syn;
  syn.file = file;
  syn.name = path_stem(file);
  if (syn.name.empty()) {
    syn.name = path_basename(file);
  }
  syn.display_name = syn.name;
  syn.kind = SymbolKind::kFunction;
  syn.line = 1;
  syn.signature = path_entry_signature(file);
  return syn;
}

bool tokens_look_buildish(const std::vector<std::string>& tokens) {
  for (const auto& t : tokens) {
    if (t == "compile" || t == "build" || t == "cmake" || t == "makefile" || t == "ninja" ||
        t == "compilacion" || t == "compilation" || t == "compilar" ||
        t.find("compile") != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Lightweight path filter for git path-stem injection (avoid linking index_rules into
// every consumer of repo_map). Mirrors the build/script subset of is_indexed_source_path.
bool looks_like_map_path_candidate(const std::string& path) {
  const std::string base = path_basename(path);
  const std::string base_l = ascii_lower_copy(base);
  if (base == "CMakeLists.txt" || base_l == "makefile" || base_l == "gnumakefile") {
    return true;
  }
  auto has_ext = [&](std::string_view ext) {
    return base_l.size() > ext.size() &&
           base_l.compare(base_l.size() - ext.size(), ext.size(), ext) == 0;
  };
  return has_ext(".sh") || has_ext(".bash") || has_ext(".cmake") || has_ext(".mk");
}

int token_hit_score(const std::string& hay_lower, const std::string& token_lower) {
  if (token_lower.size() < 3 || hay_lower.empty()) {
    return 0;
  }
  if (hay_lower == token_lower) {
    return 520 + static_cast<int>(token_lower.size()) * 12;
  }
  const auto pos = hay_lower.find(token_lower);
  if (pos == std::string::npos) {
    return 0;
  }
  int score = 180 + static_cast<int>(token_lower.size()) * 9;
  if (pos == 0) {
    score += 40;
  }
  return score;
}

std::string bare_name(const IndexedSymbol& sym) {
  if (!sym.name.empty()) {
    return sym.name;
  }
  return symbol_insert_name(sym.display_name);
}

std::string path_file_stem(const std::string& file) {
  const auto slash = file.find_last_of('/');
  std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos) {
    base = base.substr(0, dot);
  }
  return base;
}

bool is_impl_source_file(const std::string& file) {
  const std::string low = ascii_lower_copy(file);
  auto has_ext = [&](std::string_view ext) {
    return low.size() > ext.size() && low.compare(low.size() - ext.size(), ext.size(), ext) == 0;
  };
  return has_ext(".cpp") || has_ext(".cc") || has_ext(".cxx") || has_ext(".c") || has_ext(".mm");
}

bool is_header_source_file(const std::string& file) {
  const std::string low = ascii_lower_copy(file);
  auto has_ext = [&](std::string_view ext) {
    return low.size() > ext.size() && low.compare(low.size() - ext.size(), ext.size(), ext) == 0;
  };
  return has_ext(".hpp") || has_ext(".hh") || has_ext(".h") || has_ext(".hxx");
}

bool entry_is_junk(const RepoMapEntry& e) {
  if (e.signature.find("= delete") != std::string::npos) {
    return true;
  }
  if (!e.name.empty() && e.name[0] == '~') {
    return true;
  }
  if (e.signature.find("operator=") != std::string::npos) {
    return true;
  }
  return false;
}

bool entry_is_file_or_type_anchor(const RepoMapEntry& e) {
  if (e.kind == SymbolKind::kClass || e.kind == SymbolKind::kStruct) {
    return true;
  }
  if (e.signature.rfind("script ", 0) == 0 || e.signature.rfind("file ", 0) == 0 ||
      e.signature.rfind("cmake ", 0) == 0 || e.signature.rfind("make ", 0) == 0) {
    return true;
  }
  if (e.signature.rfind("class ", 0) == 0 || e.signature.rfind("struct ", 0) == 0) {
    return true;
  }
  return false;
}

bool entry_is_functionish(const RepoMapEntry& e) {
  return e.kind == SymbolKind::kFunction || e.kind == SymbolKind::kMethod;
}

bool actionable_coding_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  static const char* kKeys[] = {"tab",     "panel",  "modal",  "setting", "config", "open",
                                "close",   "make",   "handle", "switch",  "append", "overlay",
                                "apply",   "render", "build",  "create",  "show",   "hide",
                                "toggle",  "select", "scroll", "click",   "key",    "mouse"};
  for (const char* k : kKeys) {
    if (low.find(k) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Lifecycle / shell of a UI module: open/close/Make*Overlay — high value for coding packs.
bool coding_lifecycle_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  if (low.find("overlay") != std::string::npos) {
    return true;
  }
  if (low.rfind("open_", 0) == 0 || low.rfind("close_", 0) == 0) {
    return true;
  }
  if (low.rfind("make", 0) == 0 &&
      (low.find("modal") != std::string::npos || low.find("settings") != std::string::npos ||
       low.find("dialog") != std::string::npos)) {
    return true;
  }
  return false;
}

// Wire/I/O path: message read loops, notification dispatch (vs provider lifecycle).
bool coding_io_wire_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  static const char* kKeys[] = {"read_message",     "write_message",      "send_message",
                                "notification_handler", "on_notification", "set_notification",
                                "dispatch_message", "handle_message",     "parse_message",
                                "reader_loop",      "read_loop",          "recv_",
                                "incoming_",        "on_lsp_notification"};
  for (const char* k : kKeys) {
    if (low.find(k) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool coding_startup_noise_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  static const char* kKeys[] = {"ensure_", "configure_", "join_", "finish_", "startup",
                                "starting", "_ready", "companion_sources"};
  for (const char* k : kKeys) {
    if (low.find(k) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool query_wants_io_wire(const std::vector<std::string>& facets) {
  for (const auto& f : facets) {
    if (f.find("recep") != std::string::npos || f.find("paquet") != std::string::npos ||
        f.find("packet") != std::string::npos || f.find("recv") != std::string::npos ||
        f == "receive" || f == "incoming" || f == "transport" || f == "payload") {
      return true;
    }
    const auto expanded = expand_nl_retrieval_tokens({f}, 12);
    for (const auto& ex : expanded) {
      if (ex == "transport" || ex == "notification" || ex == "incoming" || ex == "payload" ||
          ex == "receive" || ex == "reader") {
        return true;
      }
    }
  }
  return false;
}

bool coding_highlight_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  static const char* kKeys[] = {"highlight", "highlighter", "highlights_for", "highlight_query",
                                "highlight_code", "syntax_highlight", "line_highlight"};
  for (const char* k : kKeys) {
    if (low.find(k) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool coding_provider_api_noise_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  static const char* kKeys[] = {"hover_at", "supports_hover", "completions_at", "symbols_for",
                                "local_completions", "workspace_symbols"};
  for (const char* k : kKeys) {
    if (low.find(k) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool query_wants_highlight(const std::vector<std::string>& facets) {
  for (const auto& f : facets) {
    if (f.find("color") != std::string::npos || f.find("resalt") != std::string::npos ||
        f.find("highlight") != std::string::npos || f == "syntax" || f == "highlighter") {
      return true;
    }
    const auto expanded = expand_nl_retrieval_tokens({f}, 12);
    for (const auto& ex : expanded) {
      if (ex == "highlight" || ex == "highlighter" || ex == "highlights" || ex == "syntax") {
        return true;
      }
    }
  }
  return false;
}

// Weak helpers that often crowd out lifecycle when every function inherits path facets.
bool coding_peripheral_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  static const char* kWeak[] = {"export", "portable", "color_field", "ui_color", "clamp_shortcut",
                                "toolpack", "docker", "format_", "clang_format", "path_browser"};
  for (const char* k : kWeak) {
    if (low.find(k) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool looks_primary_type_name(const std::string& name) {
  const std::string low = ascii_lower_copy(name);
  if (low.size() < 10) {
    return false;
  }
  return low.find("modal") != std::string::npos || low.find("settings") != std::string::npos ||
         low.find("state") != std::string::npos || low.find("panel") != std::string::npos ||
         low.find("manager") != std::string::npos || low.find("controller") != std::string::npos ||
         low.find("overlay") != std::string::npos;
}

// 0=none, 1=via short/generic NL expand (≤6 chars: tab, config, …),
// 2=via longer synonym expand (settings, preferences, …), 3=raw facet substring.
int facet_hit_strength(const std::string& hay_lower, const std::string& facet) {
  if (facet.size() < 3 || hay_lower.empty()) {
    return 0;
  }
  if (hay_lower.find(facet) != std::string::npos) {
    return 3;
  }
  const auto expanded = expand_nl_retrieval_tokens({facet}, 12);
  int best = 0;
  for (const auto& ex : expanded) {
    if (ex == facet || ex.size() < 3) {
      continue;
    }
    if (hay_lower.find(ex) == std::string::npos) {
      continue;
    }
    // Short bridges are too common across the codebase (config, tab, shell, …).
    // Longer synonyms discriminate modules (settings, preferences, overlay, …).
    const int w = ex.size() <= 6 ? 1 : 2;
    best = std::max(best, w);
  }
  return best;
}

int entry_facet_hit_strength(const RepoMapEntry& e, const std::string& facet) {
  const std::string hay =
      ascii_lower_copy(e.file) + " " + ascii_lower_copy(e.name) + " " + ascii_lower_copy(e.signature);
  return facet_hit_strength(hay, facet);
}

// How many distinct query facets this entry hits (any strength > 0).
int entry_distinct_facets(const RepoMapEntry& e, const std::vector<std::string>& facets) {
  int n = 0;
  for (const auto& f : facets) {
    if (entry_facet_hit_strength(e, f) > 0) {
      ++n;
    }
  }
  return n;
}

// Higher = better for coding pack selection / outline.
int coding_entry_rank(const RepoMapEntry& e, const std::vector<std::string>& facets) {
  if (entry_is_junk(e)) {
    return -100000;
  }
  const int cov = facet_coverage_score(e.file, e.name, e.signature, facets);
  int rank = cov;
  const int distinct = entry_distinct_facets(e, facets);
  // Prefer symbols that touch several query facets over many single-token lookalikes.
  if (distinct >= 2) {
    rank += distinct * distinct * 250;
  } else if (distinct == 1) {
    // Sole hit only via short NL expand (e.g. tab) while the query has longer facets → weak.
    bool only_short = true;
    bool query_has_long = false;
    for (const auto& f : facets) {
      if (f.size() > 4) {
        query_has_long = true;
      }
      const int s = entry_facet_hit_strength(e, f);
      if (s >= 2) {
        only_short = false;
      }
    }
    if (only_short && query_has_long) {
      rank -= 800;
    }
  }
  if (entry_is_functionish(e)) {
    rank += 400;
    const bool io_query = query_wants_io_wire(facets);
    const bool hl_query = query_wants_highlight(facets);
    if (coding_io_wire_name(e.name)) {
      rank += io_query ? 2000 : 700;
    }
    if (coding_highlight_name(e.name)) {
      rank += hl_query ? 2000 : 700;
    }
    if (coding_lifecycle_name(e.name)) {
      rank += (io_query || hl_query) ? 200 : 1200;
    } else if (actionable_coding_name(e.name)) {
      rank += 600;
    }
    if (io_query && coding_startup_noise_name(e.name) && !coding_io_wire_name(e.name)) {
      rank -= 700;
    }
    if (hl_query && coding_provider_api_noise_name(e.name) && !coding_highlight_name(e.name)) {
      rank -= 900;
    }
    if (coding_peripheral_name(e.name) && !coding_lifecycle_name(e.name)) {
      rank -= 500;
    }
    if (is_impl_source_file(e.file)) {
      rank += 80;
    } else if (is_header_source_file(e.file)) {
      rank -= 40;  // prefer definition bodies over decls
    }
  } else if (e.kind == SymbolKind::kClass || e.kind == SymbolKind::kStruct ||
             e.signature.rfind("class ", 0) == 0 || e.signature.rfind("struct ", 0) == 0) {
    if (looks_primary_type_name(e.name) || cov >= 2000) {
      rank += 280;
    } else {
      rank += 20;  // tiny / incidental types
    }
  } else if (e.signature.rfind("file ", 0) == 0 || e.signature.rfind("script ", 0) == 0) {
    rank += 50;  // weak file anchor — prefer real types/funcs
  }
  return rank;
}

// Prefer .cpp definition over .hpp declaration for the same symbol name.
void dedup_coding_by_name(std::vector<RepoMapEntry>* ranked) {
  if (ranked == nullptr || ranked->empty()) {
    return;
  }
  std::unordered_map<std::string, std::size_t> best;
  std::vector<RepoMapEntry> kept;
  kept.reserve(ranked->size());
  for (std::size_t i = 0; i < ranked->size(); ++i) {
    const auto& e = (*ranked)[i];
    const std::string key = ascii_lower_copy(e.name);
    if (key.empty()) {
      kept.push_back(e);
      continue;
    }
    auto it = best.find(key);
    if (it == best.end()) {
      best[key] = kept.size();
      kept.push_back(e);
      continue;
    }
    auto& prev = kept[it->second];
    const bool cur_impl = is_impl_source_file(e.file);
    const bool prev_impl = is_impl_source_file(prev.file);
    if ((cur_impl && !prev_impl) || (cur_impl == prev_impl && e.score > prev.score)) {
      prev = e;
    }
  }
  *ranked = std::move(kept);
}

// Pick coding/context stem: locked context_stem wins; else maximize *distinct facet
// coverage* (and match strength), not raw symbol mass on one ambiguous expand.
//
// mass_entries: contribute capped mass (+ facet strengths).
// Lexical stem candidates (facet coverage + mass). Shared by auto-pick and L1 shortlist.
std::vector<CodingStemCandidate> collect_coding_stem_candidates(
    const std::vector<RepoMapEntry>& mass_entries, const std::vector<std::string>& facets,
    const SymbolIndexSnapshot* snapshot) {
  std::vector<CodingStemCandidate> ranked_stems;
  if (mass_entries.empty() && (snapshot == nullptr || snapshot->symbols.empty())) {
    return ranked_stems;
  }

  struct StemAgg {
    int best_strength[32]{};       // max over path+symbols
    int best_path_strength[32]{};  // file path / stem only (module identity)
    int mass = 0;                  // capped per-symbol contribution
    int symbols = 0;
    std::string sample_path;
    std::vector<std::string> sample_names;
  };
  const std::size_t nfacets = std::min<std::size_t>(facets.size(), 32);
  std::unordered_map<std::string, StemAgg> aggs;
  std::unordered_map<std::string, int> facet_stem_df;  // stems with path-strong hit
  std::unordered_map<std::string, int> facet_any_df;   // stems with any strong hit

  auto remember_sample = [](StemAgg& agg, const RepoMapEntry& e) {
    if (agg.sample_path.empty() && !e.file.empty()) {
      agg.sample_path = e.file;
    }
    if (agg.sample_names.size() >= 3 || e.name.empty()) {
      return;
    }
    for (const auto& n : agg.sample_names) {
      if (n == e.name) {
        return;
      }
    }
    agg.sample_names.push_back(e.name);
  };

  auto apply_strengths = [&](StemAgg& agg, const RepoMapEntry& e) {
    const std::string path_l = ascii_lower_copy(e.file);
    const std::string stem_l = ascii_lower_copy(path_file_stem(e.file));
    for (std::size_t fi = 0; fi < nfacets; ++fi) {
      const int s_path =
          std::max(facet_hit_strength(path_l, facets[fi]), facet_hit_strength(stem_l, facets[fi]));
      if (s_path > agg.best_path_strength[fi]) {
        agg.best_path_strength[fi] = s_path;
      }
      if (s_path > agg.best_strength[fi]) {
        agg.best_strength[fi] = s_path;
      }
      const int s = entry_facet_hit_strength(e, facets[fi]);
      if (s > agg.best_strength[fi]) {
        agg.best_strength[fi] = s;
      }
    }
    remember_sample(agg, e);
  };

  auto entry_only_short_expand_hits = [&](const RepoMapEntry& e) {
    bool any_short = false;
    for (std::size_t fi = 0; fi < nfacets; ++fi) {
      const int s = std::max(entry_facet_hit_strength(e, facets[fi]),
                             facet_hit_strength(ascii_lower_copy(e.file), facets[fi]));
      if (s >= 2) {
        return false;
      }
      if (s == 1) {
        any_short = true;
      }
    }
    return any_short;
  };

  // 1) Facet coverage from the full index (strength only).
  if (snapshot != nullptr) {
    for (const auto& sym : snapshot->symbols) {
      const std::string stem = path_file_stem(sym.file);
      if (stem.empty()) {
        continue;
      }
      RepoMapEntry e;
      e.file = sym.file;
      e.name = bare_name(sym);
      e.kind = sym.kind;
      e.line = sym.line;
      e.signature = sym.signature;
      if (entry_is_junk(e)) {
        continue;
      }
      apply_strengths(aggs[stem], e);
    }
  }

  // 2) Mass (+ strengths) from the ranked/map pool.
  for (const auto& e : mass_entries) {
    if (entry_is_junk(e)) {
      continue;
    }
    const std::string stem = path_file_stem(e.file);
    if (stem.empty()) {
      continue;
    }
    auto& agg = aggs[stem];
    ++agg.symbols;
    int local = coding_entry_rank(e, facets);
    if (local > 4000) {
      local = 4000;
    }
    // Symbols that only match via short NL expands (e.g. tab) must not flood mass.
    if (entry_only_short_expand_hits(e)) {
      local = std::min(local, 80);
    }
    if (local > 0) {
      agg.mass += local;
    }
    apply_strengths(agg, e);
  }

  if (aggs.empty()) {
    return ranked_stems;
  }

  // DF: prefer path-anchored hits for rarity; fall back to any-strong.
  for (const auto& kv : aggs) {
    for (std::size_t fi = 0; fi < nfacets; ++fi) {
      if (kv.second.best_path_strength[fi] >= 2) {
        ++facet_stem_df[facets[fi]];
      }
      if (kv.second.best_strength[fi] >= 2) {
        ++facet_any_df[facets[fi]];
      }
    }
  }

  ranked_stems.reserve(aggs.size());
  for (const auto& kv : aggs) {
    const auto& agg = kv.second;
    int path_strong = 0;
    int covered_strong = 0;
    int covered_weak = 0;
    int strength_strong = 0;
    int strength_weak = 0;
    int rarity = 0;
    for (std::size_t fi = 0; fi < nfacets; ++fi) {
      const int s_path = agg.best_path_strength[fi];
      const int s = agg.best_strength[fi];
      if (s_path >= 2) {
        ++path_strong;
      }
      if (s <= 0) {
        continue;
      }
      if (s >= 2) {
        ++covered_strong;
        strength_strong += s;
        int df = facet_stem_df[facets[fi]];
        if (df <= 0) {
          df = facet_any_df[facets[fi]];
        }
        rarity += (s * 100) / std::max(1, df);
      } else {
        ++covered_weak;
        strength_weak += s;
      }
    }
    if (path_strong == 0 && covered_strong == 0 && covered_weak == 0 && agg.mass == 0) {
      continue;
    }
    // Path/stem hits beat symbol-only false friends (*_from_settings, *Config*).
    const int score = path_strong * 5000000 + covered_strong * 1000000 + strength_strong * 50000 +
                      rarity * 200 + covered_weak * 3000 + strength_weak * 200 + agg.mass;
    CodingStemCandidate c;
    c.stem = kv.first;
    c.lexical_score = score;
    c.path_strong = path_strong;
    c.passage = coding_stem_passage(kv.first, agg.sample_path, agg.sample_names);
    ranked_stems.push_back(std::move(c));
  }
  std::stable_sort(ranked_stems.begin(), ranked_stems.end(),
                   [](const CodingStemCandidate& a, const CodingStemCandidate& b) {
                     if (a.lexical_score != b.lexical_score) {
                       return a.lexical_score > b.lexical_score;
                     }
                     return a.stem < b.stem;
                   });
  return ranked_stems;
}

// snapshot (optional): full index scan updates facet strengths only (no mass), so a stem
// is not under-counted when only a few samples were loaded into mass_entries.
std::string pick_coding_context_stem(const std::string& locked,
                                     const std::vector<RepoMapEntry>& mass_entries,
                                     const std::vector<std::string>& facets,
                                     const SymbolIndexSnapshot* snapshot,
                                     const std::string& query,
                                     EmbeddingBackend* embed,
                                     const CodingEmbedFn& embed_fn,
                                     CodingStemEmbedIndex* stem_index,
                                     bool* embed_used_out,
                                     float* embed_cos_out) {
  if (embed_used_out != nullptr) {
    *embed_used_out = false;
  }
  if (embed_cos_out != nullptr) {
    *embed_cos_out = 0.0f;
  }
  if (!locked.empty()) {
    return locked;
  }
  auto ranked_stems = collect_coding_stem_candidates(mass_entries, facets, snapshot);
  if (ranked_stems.empty()) {
    return {};
  }

  std::string best = ranked_stems.front().stem;
  if (!query.empty()) {
    auto items = build_fused_stem_shortlist(ranked_stems, query, stem_index, embed, embed_fn, 8);
    if (!items.empty()) {
      best = items.front().stem;
      bool any_sem = false;
      for (const auto& it : items) {
        if (it.semantic_cos > 0.0f) {
          any_sem = true;
          break;
        }
      }
      if (embed_used_out != nullptr) {
        *embed_used_out = any_sem;
      }
      if (embed_cos_out != nullptr) {
        *embed_cos_out = items.front().semantic_cos;
      }
    } else if (embed != nullptr || embed_fn || (stem_index != nullptr && stem_index->ready())) {
      const auto rr = fuse_coding_stems(query, ranked_stems, stem_index, embed, embed_fn);
      if (!rr.stem.empty()) {
        best = rr.stem;
      }
      if (embed_used_out != nullptr) {
        *embed_used_out = rr.used_embed;
      }
      if (embed_cos_out != nullptr) {
        *embed_cos_out = rr.best_cosine;
      }
    }
  }
  return best;
}

std::string pick_coding_context_stem(const std::string& locked,
                                     const std::vector<RepoMapEntry>& ranked,
                                     const std::vector<std::string>& facets) {
  return pick_coding_context_stem(locked, ranked, facets, nullptr, {}, nullptr, {}, nullptr, nullptr,
                                  nullptr);
}

// Prefer real implementation symbols over ctors / deleted special members / bare type decls.
int boilerplate_penalty(const IndexedSymbol& sym) {
  const std::string name = bare_name(sym);
  const std::string& sig = sym.signature;
  if (sig.find("= delete") != std::string::npos) {
    return -900;
  }
  if (!name.empty() && name[0] == '~') {
    return -700;
  }
  if (sig.find("operator=") != std::string::npos || name == "operator=") {
    return -650;
  }
  if (sig.rfind("struct ", 0) == 0 || sig.rfind("class ", 0) == 0 ||
      sig.rfind("enum ", 0) == 0) {
    return -180;
  }
  // Default / copy ctor: "Foo();" / "Foo(const Foo&)"
  if (!name.empty() && sig.find(name + "(") != std::string::npos) {
    const auto paren = sig.find('(');
    if (paren != std::string::npos && paren + 1 < sig.size() &&
        (sig[paren + 1] == ')' || sig.find("const " + name, paren) != std::string::npos ||
         sig.find(name + "&", paren) != std::string::npos)) {
      // Only penalize if it looks like a ctor (starts with name), not a free function call in body.
      if (sig.find(name) == 0 || sig.find(name) <= 2) {
        return -420;
      }
    }
  }
  return 0;
}

void add_domain_alias_tokens(std::vector<std::string>& tokens, std::string_view query_folded) {
  tokens = expand_nl_retrieval_tokens(tokens,
                                      std::max<std::size_t>(tokens.size() + 16, 32), query_folded);
}

int lexical_score(const IndexedSymbol& sym, const std::vector<std::string>& tokens_lower,
                  const std::string& active_file,
                  const std::unordered_set<std::string>& chat_files) {
  const std::string name_l = ascii_lower_copy(bare_name(sym));
  const std::string file_l = ascii_lower_copy(sym.file);
  int score = kind_boost(sym.kind) + path_boost(sym.file);

  // Soft context boosts — never swamp query token hits when investigating.
  const bool has_query = !tokens_lower.empty();
  if (!active_file.empty() && sym.file == active_file) {
    score += has_query ? 40 : 140;
  } else if (chat_files.count(sym.file)) {
    score += has_query ? 25 : 90;
  }

  int best_name = 0;
  int best_file = 0;
  int hits = 0;
  for (const auto& t : tokens_lower) {
    const int n = token_hit_score(name_l, t);
    const int f = token_hit_score(file_l, t);
    if (n > 0 || f > 0) {
      ++hits;
    }
    best_name = std::max(best_name, n);
    best_file = std::max(best_file, f);
  }
  score += best_name;
  score += best_file / 2;
  if (hits >= 2) {
    score += 120 * (hits - 1);
  }
  if (bare_name(sym).size() >= 12) {
    score += 8;
  }
  score += boilerplate_penalty(sym);
  return score;
}

// Token hits only (no path/active boosts) — used to decide if a symbol answers the query.
int query_hit_score(const IndexedSymbol& sym, const std::vector<std::string>& tokens_lower) {
  if (tokens_lower.empty()) {
    return 0;
  }
  const std::string name_l = ascii_lower_copy(bare_name(sym));
  const std::string file_l = ascii_lower_copy(sym.file);
  int best = 0;
  int hits = 0;
  for (const auto& t : tokens_lower) {
    const int n = token_hit_score(name_l, t);
    const int f = token_hit_score(file_l, t);
    if (n > 0 || f > 0) {
      ++hits;
    }
    best = std::max(best, std::max(n, f));
  }
  if (hits >= 2) {
    best += 100 * (hits - 1);
  }
  return best;
}

int estimate_tokens(const std::string& text) {
  // Cheap proxy; good enough for map budgeting.
  return std::max(1, static_cast<int>((text.size() + 3) / 4));
}

std::unordered_map<std::string, double> personalized_pagerank(
    const std::unordered_map<std::string, std::unordered_map<std::string, double>>& edges,
    const std::unordered_map<std::string, double>& personalization, int iters = 25) {
  std::unordered_set<std::string> nodes;
  for (const auto& [u, outs] : edges) {
    nodes.insert(u);
    for (const auto& [v, w] : outs) {
      (void)w;
      nodes.insert(v);
    }
  }
  for (const auto& [n, p] : personalization) {
    (void)p;
    nodes.insert(n);
  }
  if (nodes.empty()) {
    return {};
  }

  std::unordered_map<std::string, double> pers;
  double pers_sum = 0.0;
  for (const auto& n : nodes) {
    const auto it = personalization.find(n);
    const double p = it == personalization.end() ? 0.0 : std::max(0.0, it->second);
    pers[n] = p;
    pers_sum += p;
  }
  if (pers_sum <= 1e-12) {
    const double u = 1.0 / static_cast<double>(nodes.size());
    for (const auto& n : nodes) {
      pers[n] = u;
    }
  } else {
    for (auto& [n, p] : pers) {
      p /= pers_sum;
    }
  }

  std::unordered_map<std::string, double> out_w;
  for (const auto& [u, outs] : edges) {
    double s = 0.0;
    for (const auto& [v, w] : outs) {
      (void)v;
      s += w;
    }
    out_w[u] = s;
  }

  const double damping = 0.85;
  std::unordered_map<std::string, double> rank;
  const double init = 1.0 / static_cast<double>(nodes.size());
  for (const auto& n : nodes) {
    rank[n] = init;
  }

  for (int it = 0; it < iters; ++it) {
    std::unordered_map<std::string, double> next;
    for (const auto& n : nodes) {
      next[n] = (1.0 - damping) * pers[n];
    }
    for (const auto& [u, outs] : edges) {
      const double ow = out_w[u];
      if (ow <= 1e-12) {
        // Dangling: distribute by personalization.
        for (const auto& n : nodes) {
          next[n] += damping * rank[u] * pers[n];
        }
        continue;
      }
      for (const auto& [v, w] : outs) {
        next[v] += damping * rank[u] * (w / ow);
      }
    }
    rank.swap(next);
  }
  return rank;
}

}  // namespace

std::vector<CodingStemShortlistItem> RepoMap::coding_stem_shortlist(
    const SymbolIndexSnapshot* snapshot, const std::string& query, std::size_t max_n) const {
  const std::vector<std::string> facets = extract_query_facets(query, 12);
  std::vector<RepoMapEntry> seed = entries;
  for (auto& e : seed) {
    e.score = coding_entry_rank(e, facets);
  }
  auto ranked = collect_coding_stem_candidates(seed, facets, snapshot);
  return build_fused_stem_shortlist(std::move(ranked), query, coding_stem_index, coding_embed,
                                    coding_embed_fn, max_n);
}

std::vector<std::string> repo_map_query_tokens(const std::string& text, std::size_t max_n) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  const std::string folded = ascii_fold_query(text);

  auto push = [&](std::string s) {
    s = ascii_lower_copy(std::move(s));
    if (s.size() < 3 || is_stopword(s)) {
      return;
    }
    if (!seen.insert(s).second) {
      return;
    }
    out.push_back(std::move(s));
  };

  for (const auto& tok : extract_code_tokens(folded, max_n)) {
    push(tok);
    if (out.size() >= max_n) {
      add_domain_alias_tokens(out, folded);
      return out;
    }
  }

  std::string cur;
  auto flush = [&] {
    if (!cur.empty()) {
      push(cur);
      cur.clear();
    }
  };
  for (char ch : folded) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || ch == '_') {
      cur.push_back(static_cast<char>(std::tolower(c)));
    } else {
      flush();
    }
    if (out.size() >= max_n) {
      add_domain_alias_tokens(out, folded);
      return out;
    }
  }
  flush();

  // Compound consecutive NL tokens: "busy strip" → busy_strip (matches file/symbol stems).
  {
    std::vector<std::string> words;
    std::string w;
    auto flush_w = [&] {
      if (w.size() >= 3 && !is_stopword(w)) {
        words.push_back(w);
      }
      w.clear();
    };
    for (char ch : folded) {
      const unsigned char c = static_cast<unsigned char>(ch);
      if (std::isalnum(c)) {
        w.push_back(static_cast<char>(std::tolower(c)));
      } else {
        flush_w();
      }
    }
    flush_w();
    for (std::size_t i = 0; i + 1 < words.size() && out.size() < max_n; ++i) {
      push(words[i] + "_" + words[i + 1]);
      push(words[i] + words[i + 1]);
    }
  }

  std::vector<std::string> extras;
  for (const auto& t : out) {
    for (const auto& v : expand_identifier_variants(t)) {
      const std::string vl = ascii_lower_copy(v);
      if (vl.size() >= 3 && !is_stopword(vl) && seen.insert(vl).second) {
        extras.push_back(vl);
      }
    }
  }
  for (auto& e : extras) {
    out.push_back(std::move(e));
    if (out.size() >= max_n) {
      break;
    }
  }
  add_domain_alias_tokens(out, folded);
  return out;
}

std::vector<std::string> repo_map_git_tracked_files(const std::string& workspace_root) {
  std::vector<std::string> out;
  if (workspace_root.empty()) {
    return out;
  }
  // NOTE: do NOT use -z here. run_git() reads via fgets + string+=(const char*),
  // which truncates at the first NUL and would keep only one path.
  const auto result = run_git(workspace_root, {"ls-files"});
  if (result.exit_code != 0 || result.stdout_text.empty()) {
    return out;
  }
  std::string cur;
  auto flush = [&] {
    while (!cur.empty() && (cur.back() == '\r' || cur.back() == ' ' || cur.back() == '\t')) {
      cur.pop_back();
    }
    while (cur.rfind("./", 0) == 0) {
      cur = cur.substr(2);
    }
    if (!cur.empty()) {
      out.push_back(cur);
    }
    cur.clear();
  };
  for (char ch : result.stdout_text) {
    if (ch == '\n') {
      flush();
    } else {
      cur.push_back(ch);
    }
  }
  flush();
  return out;
}

std::string RepoMap::render_text() const {
  std::ostringstream out;
  out << "REPO_MAP (firmas reales"
      << (used_pagerank ? "; PageRank" : "")
      << "; CITA estos métodos en la respuesta final):\n";
  if (!note.empty()) {
    out << "note: " << note << '\n';
  }
  if (entries.empty()) {
    out << "(vacío — índice de símbolos aún no listo o sin matches)\n";
    return out.str();
  }

  std::vector<RepoMapEntry> ordered = entries;
  std::stable_sort(ordered.begin(), ordered.end(), [](const RepoMapEntry& a, const RepoMapEntry& b) {
    if (a.file != b.file) {
      return a.file < b.file;
    }
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.line < b.line;
  });

  std::string cur_file;
  for (const auto& e : ordered) {
    if (e.file != cur_file) {
      cur_file = e.file;
      out << cur_file << ":\n";
    }
    out << "⋮...\n";
    if (!e.signature.empty()) {
      out << "│" << e.signature << '\n';
    } else {
      out << "│" << kind_label(e.kind) << ' ' << e.name;
      if (e.line > 0) {
        out << " L" << e.line;
      }
      out << '\n';
    }
  }
  out << "⋮...\n";
  return out.str();
}

std::vector<RepoMapEntry> RepoMap::ranked_investigate_entries(std::size_t max_n) const {
  std::vector<RepoMapEntry> out;
  if (entries.empty()) {
    return out;
  }
  std::vector<RepoMapEntry> ranked = entries;
  std::stable_sort(ranked.begin(), ranked.end(), [](const RepoMapEntry& a, const RepoMapEntry& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    if (a.file != b.file) {
      return a.file < b.file;
    }
    return a.line < b.line;
  });

  auto is_noisy = [](const RepoMapEntry& e) {
    if (e.signature.find("= delete") != std::string::npos) {
      return true;
    }
    if (!e.name.empty() && e.name[0] == '~') {
      return true;
    }
    if (e.signature.find("operator=") != std::string::npos) {
      return true;
    }
    // Investigate lists methods; bare type decls are low-signal in the signature list.
    if (e.signature.rfind("struct ", 0) == 0 || e.signature.rfind("class ", 0) == 0) {
      return true;
    }
    return false;
  };

  const std::size_t limit = max_n == 0 ? ranked.size() : std::min(max_n, ranked.size());
  std::unordered_map<std::string, int> per_file;
  for (int pass = 0; pass < 2 && out.size() < limit; ++pass) {
    for (const auto& e : ranked) {
      if (out.size() >= limit) {
        break;
      }
      if (pass == 0 && is_noisy(e)) {
        continue;
      }
      if (per_file[e.file] >= 2) {
        continue;
      }
      ++per_file[e.file];
      out.push_back(e);
    }
  }
  return out;
}

int RepoMap::enrich_dominant_stem_from_snapshot(const SymbolIndexSnapshot* snapshot,
                                                const std::string& query,
                                                std::size_t max_extra) {
  if (snapshot == nullptr || entries.empty()) {
    return 0;
  }

  const std::vector<std::string> facets = extract_query_facets(query, 12);
  // Mass pool = current map entries only. Facet coverage for stem pick uses the full
  // snapshot inside pick_coding_context_stem (avoids under-counting from tiny samples).
  std::vector<RepoMapEntry> seed = entries;
  for (auto& e : seed) {
    e.score = coding_entry_rank(e, facets);
  }
  std::stable_sort(seed.begin(), seed.end(), [&](const RepoMapEntry& a, const RepoMapEntry& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    const int fa = facet_coverage_score(a.file, a.name, a.signature, facets);
    const int fb = facet_coverage_score(b.file, b.name, b.signature, facets);
    if (fa != fb) {
      return fa > fb;
    }
    return a.file < b.file;
  });

  std::string dominant_stem =
      pick_coding_context_stem(context_stem, seed, facets, snapshot, query, coding_embed,
                               coding_embed_fn, coding_stem_index, &embed_rerank_used,
                               &embed_stem_cos);
  if (dominant_stem.empty()) {
    for (const auto& e : seed) {
      if (entry_is_junk(e)) {
        continue;
      }
      dominant_stem = path_file_stem(e.file);
      break;
    }
  }
  if (dominant_stem.empty()) {
    return 0;
  }
  context_stem = dominant_stem;
  if (embed_rerank_used) {
    if (!note.empty()) {
      note += "; ";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "embed_rerank=1; embed_stem_cos=%.3f",
                  static_cast<double>(embed_stem_cos));
    note += buf;
  } else if (coding_embed != nullptr || coding_embed_fn) {
    if (!note.empty()) {
      note += "; ";
    }
    note += "embed_rerank=0";
  }
  if (max_extra == 0) {
    return 0;
  }

  std::unordered_set<std::string> seen;
  seen.reserve(entries.size() * 2 + 8);
  auto key_of = [](const std::string& file, const std::string& name, int line) {
    return file + "\n" + name + "\n" + std::to_string(line);
  };
  for (const auto& e : entries) {
    seen.insert(key_of(e.file, e.name, e.line));
  }

  struct Cand {
    RepoMapEntry e;
    int rank = 0;
  };
  std::vector<Cand> cands;
  for (const auto& sym : snapshot->symbols) {
    if (path_file_stem(sym.file) != dominant_stem) {
      continue;
    }
    const std::string nm = bare_name(sym);
    if (seen.count(key_of(sym.file, nm, sym.line)) > 0) {
      continue;
    }
    RepoMapEntry e;
    e.file = sym.file;
    e.name = nm;
    e.kind = sym.kind;
    e.line = sym.line;
    e.signature = sym.signature;
    if (entry_is_junk(e)) {
      continue;
    }
    const int cov = facet_coverage_score(e.file, e.name, e.signature, facets);
    // Coding-oriented enrich: actionable functions beat tiny structs.
    int rank = coding_entry_rank(e, facets);
    if (rank < cov) {
      rank = cov;  // never below raw facet coverage
    }
    e.score = rank;
    cands.push_back(Cand{std::move(e), rank});
  }
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.rank != b.rank) {
      return a.rank > b.rank;
    }
    if (a.e.file != b.e.file) {
      return a.e.file < b.e.file;
    }
    return a.e.line < b.e.line;
  });

  int added = 0;
  for (auto& c : cands) {
    if (static_cast<std::size_t>(added) >= max_extra) {
      break;
    }
    if (!seen.insert(key_of(c.e.file, c.e.name, c.e.line)).second) {
      continue;
    }
    entries.push_back(std::move(c.e));
    ++added;
  }
  if (added > 0) {
    if (!note.empty()) {
      note += "; ";
    }
    note += "enrich_stem=" + dominant_stem + " +" + std::to_string(added);
  }
  return added;
}

std::vector<RepoMapEntry> RepoMap::ranked_context_entries(std::size_t max_n,
                                                         const std::string& query) const {
  std::vector<RepoMapEntry> out;
  if (entries.empty()) {
    return out;
  }
  std::vector<RepoMapEntry> ranked = entries;
  const std::vector<std::string> facets = extract_query_facets(query, 12);

  auto is_junk = [](const RepoMapEntry& e) {
    if (e.signature.find("= delete") != std::string::npos) {
      return true;
    }
    if (!e.name.empty() && e.name[0] == '~') {
      return true;
    }
    if (e.signature.find("operator=") != std::string::npos) {
      return true;
    }
    return false;
  };

  auto is_preferred = [](const RepoMapEntry& e) {
    if (e.kind == SymbolKind::kClass || e.kind == SymbolKind::kStruct) {
      return true;
    }
    if (e.signature.rfind("script ", 0) == 0 || e.signature.rfind("file ", 0) == 0 ||
        e.signature.rfind("cmake ", 0) == 0 || e.signature.rfind("make ", 0) == 0) {
      return true;
    }
    if (e.signature.rfind("class ", 0) == 0 || e.signature.rfind("struct ", 0) == 0) {
      return true;
    }
    return false;
  };

  // Basename without extension: settings_modal.hpp → settings_modal.
  auto file_stem = [](const std::string& file) {
    const auto slash = file.find_last_of('/');
    std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) {
      base = base.substr(0, dot);
    }
    return base;
  };

  // Family for diversity caps: editor_tab_bar → editor_tab; settings_modal → settings_modal.
  auto path_cluster = [&](const std::string& file) {
    const std::string base = file_stem(file);
    const auto u1 = base.find('_');
    if (u1 == std::string::npos) {
      return base;
    }
    const auto u2 = base.find('_', u1 + 1);
    if (u2 == std::string::npos) {
      return base;
    }
    return base.substr(0, u2);
  };

  auto facet_of = [&](const RepoMapEntry& e) {
    return facet_coverage_score(e.file, e.name, e.signature, facets);
  };

  std::stable_sort(ranked.begin(), ranked.end(),
                   [&](const RepoMapEntry& a, const RepoMapEntry& b) {
                     const int fa = facet_of(a);
                     const int fb = facet_of(b);
                     if (fa != fb) {
                       return fa > fb;
                     }
                     if (a.score != b.score) {
                       return a.score > b.score;
                     }
                     if (a.file != b.file) {
                       return a.file < b.file;
                     }
                     return a.line < b.line;
                   });

  // Dominant module = stem of the strongest non-junk seed (prefer class/file-level).
  std::string dominant_stem;
  std::string dominant_file;
  for (const auto& e : ranked) {
    if (is_junk(e) || !is_preferred(e)) {
      continue;
    }
    dominant_stem = file_stem(e.file);
    dominant_file = e.file;
    break;
  }
  if (dominant_stem.empty()) {
    for (const auto& e : ranked) {
      if (is_junk(e)) {
        continue;
      }
      dominant_stem = file_stem(e.file);
      dominant_file = e.file;
      break;
    }
  }

  auto affinity_of = [&](const RepoMapEntry& e) {
    if (dominant_stem.empty()) {
      return 0;
    }
    const std::string stem = file_stem(e.file);
    if (stem == dominant_stem) {
      // Same basename family (.hpp/.cpp): strongest cohesion signal.
      return e.file == dominant_file ? 3 : 2;
    }
    if (path_cluster(e.file) == path_cluster(dominant_file)) {
      return 1;
    }
    return 0;
  };

  std::stable_sort(ranked.begin(), ranked.end(),
                   [&](const RepoMapEntry& a, const RepoMapEntry& b) {
                     const int aa = affinity_of(a);
                     const int ab = affinity_of(b);
                     if (aa != ab) {
                       return aa > ab;
                     }
                     const int fa = facet_of(a);
                     const int fb = facet_of(b);
                     if (fa != fb) {
                       return fa > fb;
                     }
                     if (a.score != b.score) {
                       return a.score > b.score;
                     }
                     if (a.file != b.file) {
                       return a.file < b.file;
                     }
                     return a.line < b.line;
                   });

  const std::size_t limit = max_n == 0 ? ranked.size() : std::min(max_n, ranked.size());
  constexpr int kMaxPerOtherCluster = 2;
  constexpr int kMaxPerDominantCluster = 6;
  constexpr int kMaxPerOtherFile = 2;
  constexpr int kMaxPerDominantFile = 4;
  std::unordered_map<std::string, int> per_file;
  std::unordered_map<std::string, int> per_cluster;
  auto already = [&](const RepoMapEntry& e) {
    for (const auto& prev : out) {
      if (prev.file == e.file && prev.name == e.name && prev.line == e.line) {
        return true;
      }
    }
    return false;
  };

  auto try_push = [&](const RepoMapEntry& e, bool enforce_cluster) {
    if (out.size() >= limit || is_junk(e) || already(e)) {
      return;
    }
    const std::string stem = file_stem(e.file);
    const bool dominant = !dominant_stem.empty() && stem == dominant_stem;
    const int file_cap = dominant ? kMaxPerDominantFile : kMaxPerOtherFile;
    if (per_file[e.file] >= file_cap) {
      return;
    }
    const std::string cluster = path_cluster(e.file);
    const int cluster_cap =
        (!dominant_stem.empty() && cluster == path_cluster(dominant_file))
            ? kMaxPerDominantCluster
            : kMaxPerOtherCluster;
    if (enforce_cluster && per_cluster[cluster] >= cluster_cap) {
      return;
    }
    ++per_file[e.file];
    ++per_cluster[cluster];
    out.push_back(e);
  };

  // Pass 0: preferred in dominant stem/file. Pass 1: other preferred. Pass 2: rest dominant.
  // Pass 3: remaining with cluster caps. Pass 4: fill holes.
  for (int pass = 0; pass < 5 && out.size() < limit; ++pass) {
    for (const auto& e : ranked) {
      const std::string stem = file_stem(e.file);
      const bool dominant = !dominant_stem.empty() && stem == dominant_stem;
      if (pass == 0) {
        if (!dominant || !is_preferred(e)) {
          continue;
        }
      } else if (pass == 1) {
        if (dominant || !is_preferred(e)) {
          continue;
        }
      } else if (pass == 2) {
        if (!dominant) {
          continue;
        }
      } else if (pass == 3) {
        // all remaining under cluster caps
      }
      const bool enforce = pass < 4;
      try_push(e, enforce);
      if (out.size() >= limit) {
        break;
      }
    }
  }
  return out;
}

std::vector<RepoMapEntry> RepoMap::ranked_coding_entries(std::size_t max_n,
                                                        const std::string& query) const {
  std::vector<RepoMapEntry> out;
  if (entries.empty()) {
    return out;
  }
  const std::vector<std::string> facets = extract_query_facets(query, 12);
  std::vector<RepoMapEntry> ranked = entries;
  for (auto& e : ranked) {
    e.score = coding_entry_rank(e, facets);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const RepoMapEntry& a, const RepoMapEntry& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    if (a.file != b.file) {
      return a.file < b.file;
    }
    return a.line < b.line;
  });

  // Dominant stem: enrich lock (context_stem) or actionable/UI mass.
  std::string dominant_stem = pick_coding_context_stem(context_stem, ranked, facets);
  if (dominant_stem.empty()) {
    for (const auto& e : ranked) {
      if (entry_is_junk(e)) {
        continue;
      }
      dominant_stem = path_file_stem(e.file);
      break;
    }
  }

  // Soft semantic boost within/near the dominant stem (does not flip stem lock).
  if (coding_embed != nullptr || coding_embed_fn) {
    std::vector<RepoMapEntry> boost_pool;
    boost_pool.reserve(std::min<std::size_t>(ranked.size(), 24));
    for (const auto& e : ranked) {
      if (boost_pool.size() >= 24) {
        break;
      }
      if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
        continue;
      }
      boost_pool.push_back(e);
    }
    // Allow one high-facet foreign for boost consideration.
    for (const auto& e : ranked) {
      if (boost_pool.size() >= 24) {
        break;
      }
      if (!dominant_stem.empty() && path_file_stem(e.file) == dominant_stem) {
        continue;
      }
      boost_pool.push_back(e);
      break;
    }
    if (soft_boost_coding_entries(query, &boost_pool, coding_embed, coding_embed_fn, 24)) {
      std::unordered_map<std::string, int> boosted;
      auto key_of = [](const RepoMapEntry& e) {
        return e.file + "\n" + e.name + "\n" + std::to_string(e.line);
      };
      for (const auto& e : boost_pool) {
        boosted[key_of(e)] = e.score;
      }
      for (auto& e : ranked) {
        auto it = boosted.find(key_of(e));
        if (it != boosted.end()) {
          e.score = it->second;
        }
      }
      std::stable_sort(ranked.begin(), ranked.end(),
                       [](const RepoMapEntry& a, const RepoMapEntry& b) {
                         if (a.score != b.score) {
                           return a.score > b.score;
                         }
                         if (a.file != b.file) {
                           return a.file < b.file;
                         }
                         return a.line < b.line;
                       });
    }
  }

  dedup_coding_by_name(&ranked);

  bool stem_has_primary_type = false;
  for (const auto& e : ranked) {
    if (entry_is_junk(e)) {
      continue;
    }
    if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
      continue;
    }
    if ((e.kind == SymbolKind::kClass || e.kind == SymbolKind::kStruct ||
         e.signature.rfind("class ", 0) == 0 || e.signature.rfind("struct ", 0) == 0) &&
        looks_primary_type_name(e.name)) {
      stem_has_primary_type = true;
      break;
    }
  }

  const std::size_t limit = max_n == 0 ? ranked.size() : std::min(max_n, ranked.size());
  auto already = [&](const RepoMapEntry& e) {
    for (const auto& prev : out) {
      if (ascii_lower_copy(prev.name) == ascii_lower_copy(e.name) && !e.name.empty()) {
        return true;
      }
      if (prev.file == e.file && prev.name == e.name && prev.line == e.line) {
        return true;
      }
    }
    return false;
  };

  auto try_push = [&](const RepoMapEntry& e) {
    if (out.size() >= limit || entry_is_junk(e) || already(e)) {
      return;
    }
    // Never keep bare file synthetics once we have a real type anchor on the stem.
    if (stem_has_primary_type && e.signature.rfind("file ", 0) == 0) {
      return;
    }
    // Tiny incidental structs are outline-only when we already have anchors/funcs.
    if (!out.empty() && (e.kind == SymbolKind::kStruct || e.kind == SymbolKind::kClass) &&
        !looks_primary_type_name(e.name)) {
      return;
    }
    out.push_back(e);
  };

  // Pass 0: up to 2 primary type anchors from dominant stem (skip bare file if types exist).
  int anchors = 0;
  for (const auto& e : ranked) {
    if (out.size() >= limit || anchors >= 2) {
      break;
    }
    if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
      continue;
    }
    if (!entry_is_file_or_type_anchor(e)) {
      continue;
    }
    if (e.signature.rfind("file ", 0) == 0 && stem_has_primary_type) {
      continue;  // file synthetic is low-signal when we have SettingsModalState etc.
    }
    // Skip tiny incidental types unless they are the only anchor.
    if ((e.kind == SymbolKind::kStruct || e.kind == SymbolKind::kClass) &&
        !looks_primary_type_name(e.name) &&
        facet_coverage_score(e.file, e.name, e.signature, facets) < 2000 && anchors >= 1) {
      continue;
    }
    const std::size_t before = out.size();
    try_push(e);
    if (out.size() > before) {
      ++anchors;
    }
  }

  // Pass 1a0: wire/I/O handlers when the query is about reception/packets.
  if (query_wants_io_wire(facets)) {
    for (const auto& e : ranked) {
      if (out.size() >= limit) {
        break;
      }
      if (!entry_is_functionish(e) || !coding_io_wire_name(e.name)) {
        continue;
      }
      if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
        continue;
      }
      try_push(e);
    }
  }

  // Pass 1a0b: highlight helpers when the query is about coloring/syntax.
  if (query_wants_highlight(facets)) {
    for (const auto& e : ranked) {
      if (out.size() >= limit) {
        break;
      }
      if (!entry_is_functionish(e) || !coding_highlight_name(e.name)) {
        continue;
      }
      if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
        continue;
      }
      try_push(e);
    }
  }

  // Pass 1a: lifecycle (Make*Overlay / open_ / close_) on dominant stem.
  for (const auto& e : ranked) {
    if (out.size() >= limit) {
      break;
    }
    if (!entry_is_functionish(e) || !coding_lifecycle_name(e.name)) {
      continue;
    }
    if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
      continue;
    }
    try_push(e);
  }

  // Pass 1b: other actionable functions on dominant stem.
  for (const auto& e : ranked) {
    if (out.size() >= limit) {
      break;
    }
    if (!entry_is_functionish(e)) {
      continue;
    }
    if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
      continue;
    }
    if (coding_peripheral_name(e.name) && !actionable_coding_name(e.name)) {
      continue;
    }
    try_push(e);
  }

  // Pass 2: remaining dominant-stem symbols (types that help).
  for (const auto& e : ranked) {
    if (out.size() >= limit) {
      break;
    }
    if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
      continue;
    }
    try_push(e);
  }

  // Pass 3: at most one high-facet foreign entry.
  int foreign = 0;
  for (const auto& e : ranked) {
    if (out.size() >= limit || foreign >= 1) {
      break;
    }
    if (!dominant_stem.empty() && path_file_stem(e.file) == dominant_stem) {
      continue;
    }
    const int cov = facet_coverage_score(e.file, e.name, e.signature, facets);
    if (cov / 1000 < 2) {
      continue;
    }
    const std::size_t before = out.size();
    try_push(e);
    if (out.size() > before) {
      ++foreign;
    }
  }

  return out;
}

std::vector<RepoMapEntry> RepoMap::coding_outline_entries(std::size_t max_n,
                                                         const std::string& query) const {
  std::vector<RepoMapEntry> out;
  if (entries.empty()) {
    return out;
  }
  const std::vector<std::string> facets = extract_query_facets(query, 12);
  std::vector<RepoMapEntry> ranked = entries;
  for (auto& e : ranked) {
    e.score = coding_entry_rank(e, facets);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const RepoMapEntry& a, const RepoMapEntry& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return a.line < b.line;
  });

  std::string dominant_stem = pick_coding_context_stem(context_stem, ranked, facets);
  if (dominant_stem.empty()) {
    for (const auto& e : ranked) {
      if (!entry_is_junk(e)) {
        dominant_stem = path_file_stem(e.file);
        break;
      }
    }
  }

  dedup_coding_by_name(&ranked);

  const std::size_t limit = max_n == 0 ? ranked.size() : std::min(max_n, ranked.size());
  for (const auto& e : ranked) {
    if (out.size() >= limit) {
      break;
    }
    if (entry_is_junk(e)) {
      continue;
    }
    if (!dominant_stem.empty() && path_file_stem(e.file) != dominant_stem) {
      continue;
    }
    // Outline: functions + primary types (skip tiny structs / bare file if we have funcs).
    if (entry_is_functionish(e) || looks_primary_type_name(e.name) ||
        e.signature.rfind("file ", 0) == 0 || e.signature.rfind("script ", 0) == 0) {
      out.push_back(e);
    }
  }
  return out;
}

std::string RepoMap::format_investigate_answer(std::size_t max_n) const {
  std::ostringstream out;
  out << "Resultados más probables:\n";
  if (entries.empty()) {
    out << "(ninguno — índice vacío o sin matches para la consulta)\n";
    if (!note.empty()) {
      out << "note: " << note << '\n';
    }
    return out.str();
  }
  const auto shown_entries = ranked_investigate_entries(max_n);
  std::size_t shown = 0;
  for (const auto& e : shown_entries) {
    ++shown;
    out << shown << ". " << e.file;
    if (e.line > 0) {
      out << ':' << e.line;
    }
    out << '\n';
    out << "    ";
    if (!e.signature.empty()) {
      out << e.signature;
    } else {
      out << kind_label(e.kind) << ' ' << e.name;
    }
    out << '\n';
  }
  if (shown == 0) {
    out << "(sin firmas útiles tras filtrar ruido)\n";
  }
  return out.str();
}

std::vector<std::string> RepoMap::suggested_needles(std::size_t max_n) const {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const auto& e : entries) {
    if (e.name.size() < 2) {
      continue;
    }
    if (!seen.insert(e.name).second) {
      continue;
    }
    out.push_back(e.name);
    if (out.size() >= max_n) {
      break;
    }
  }
  if (out.size() < max_n) {
    for (const auto& e : entries) {
      const auto slash = e.file.find_last_of('/');
      const std::string base = slash == std::string::npos ? e.file : e.file.substr(slash + 1);
      const auto dot = base.find_last_of('.');
      const std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
      if (stem.size() < 3) {
        continue;
      }
      if (!seen.insert(stem).second) {
        continue;
      }
      out.push_back(stem);
      if (out.size() >= max_n) {
        break;
      }
    }
  }
  return out;
}

RepoMap build_repo_map(const SymbolIndexSnapshot* snapshot, const RepoMapOptions& opts) {
  RepoMap map;
  if (snapshot == nullptr || snapshot->symbols.empty()) {
    map.note = "sin snapshot de símbolos";
    return map;
  }

  auto tokens = repo_map_query_tokens(opts.query, 16);
  {
    std::unordered_set<std::string> seen(tokens.begin(), tokens.end());
    auto push_tok = [&](std::string s) {
      s = ascii_lower_copy(std::move(s));
      if (s.size() < 3 || is_stopword(s) || !seen.insert(s).second) {
        return;
      }
      tokens.push_back(std::move(s));
    };
    for (const auto& n : opts.extra_needles) {
      push_tok(n);
      for (const auto& v : expand_identifier_variants(n)) {
        push_tok(v);
      }
    }
  }
  const std::vector<std::string> facets = extract_query_facets(opts.query, 12);
  std::unordered_set<std::string> chat_set(opts.chat_files.begin(), opts.chat_files.end());

  std::unordered_set<std::string> git_ok;
  bool git_boost = false;
  if (opts.prefer_git_tracked && !snapshot->workspace_root.empty()) {
    const auto tracked = repo_map_git_tracked_files(snapshot->workspace_root);
    if (!tracked.empty()) {
      git_ok.insert(tracked.begin(), tracked.end());
      git_boost = true;
    }
  }

  // name -> defining files
  std::unordered_map<std::string, std::vector<std::string>> defines;
  std::vector<const IndexedSymbol*> usable;
  usable.reserve(snapshot->symbols.size());
  std::size_t tracked_syms = 0;
  std::size_t untracked_syms = 0;
  for (const auto& sym : snapshot->symbols) {
    if (sym.file.empty()) {
      continue;
    }
    if (!ai_path_in_scope(snapshot->workspace_root, sym.file, opts.path_scope)) {
      continue;
    }
    const std::string name = bare_name(sym);
    if (name.size() < 2) {
      continue;
    }
    if (sym.kind == SymbolKind::kVariable && name.size() < 4) {
      continue;
    }
    usable.push_back(&sym);
    defines[name].push_back(sym.file);
    if (git_boost) {
      if (git_ok.count(sym.file)) {
        ++tracked_syms;
      } else {
        ++untracked_syms;
      }
    }
  }

  // Path-stem hits: scripts/CMake/Makefile often have weak or zero useful defs, but the
  // filename itself answers "dónde se compila". Inject file-level synthetics when tokens
  // strongly match the path stem/base (not weak path substring).
  std::vector<IndexedSymbol> path_owned;
  std::unordered_set<const IndexedSymbol*> path_syms;
  if (!tokens.empty()) {
    std::unordered_set<std::string> considered;
    auto consider = [&](const std::string& file) {
      if (file.empty() || file.rfind("third_party/", 0) == 0 || !considered.insert(file).second) {
        return;
      }
      if (!ai_path_in_scope(snapshot->workspace_root, file, opts.path_scope)) {
        return;
      }
      if (filename_seed_match_score(file, tokens) < 130) {
        return;
      }
      path_owned.push_back(make_path_map_symbol(file));
    };
    for (const auto* sym : usable) {
      consider(sym->file);
    }
    if (git_boost) {
      for (const auto& file : git_ok) {
        if (!looks_like_map_path_candidate(file)) {
          continue;
        }
        consider(file);
      }
    }
    for (auto& syn : path_owned) {
      usable.push_back(&syn);
      path_syms.insert(&syn);
    }
  }

  if (usable.empty()) {
    map.note = "índice sin símbolos útiles (symbols=" + std::to_string(snapshot->symbols.size()) +
               ")";
    return map;
  }

  if (snapshot->partial) {
    map.note = "índice parcial (scan en curso)";
  }
  if (git_boost) {
    if (!map.note.empty()) {
      map.note += "; ";
    }
    map.note += "git boost tracked=" + std::to_string(tracked_syms) +
                " untracked=" + std::to_string(untracked_syms);
  }

  // --- PageRank over file graph (Aider-style) ---
  std::unordered_map<std::string, double> file_rank;
  bool have_pr = false;
  if (opts.use_pagerank && !snapshot->refs.empty()) {
    std::unordered_map<std::string, std::unordered_map<std::string, double>> edges;

    auto mentioned = [&](const std::string& name) {
      const std::string nl = ascii_lower_copy(name);
      for (const auto& t : tokens) {
        if (nl == t || nl.find(t) != std::string::npos) {
          return true;
        }
      }
      return false;
    };

    for (const auto& ref : snapshot->refs) {
      const auto dit = defines.find(ref.name);
      if (dit == defines.end()) {
        continue;
      }
      double w = static_cast<double>(std::max(1, ref.count));
      if (ref.name.size() >= 12) {
        w *= 10.0;
      }
      if (mentioned(ref.name)) {
        w *= 10.0;
      }
      // Mild boost from open/active (Aider's 50x collapses the map to open tabs).
      if (chat_set.count(ref.file) || ref.file == opts.active_file) {
        w *= 4.0;
      }
      if (git_boost && git_ok.count(ref.file)) {
        w *= 1.5;
      }
      for (const auto& def_file : dit->second) {
        if (def_file == ref.file) {
          continue;
        }
        edges[ref.file][def_file] += w;
      }
    }

    std::unordered_map<std::string, double> personalization;
    if (!opts.active_file.empty()) {
      personalization[opts.active_file] = 12.0;
    }
    for (const auto& f : opts.chat_files) {
      personalization[f] += 6.0;
    }
    for (const auto& sym : usable) {
      const std::string fl = ascii_lower_copy(sym->file);
      for (const auto& t : tokens) {
        if (fl.find(t) != std::string::npos) {
          personalization[sym->file] += 8.0;
          break;
        }
      }
      if (git_boost && git_ok.count(sym->file)) {
        personalization[sym->file] += 2.0;
      }
    }
    for (const auto& [name, files] : defines) {
      if (!mentioned(name)) {
        continue;
      }
      for (const auto& f : files) {
        personalization[f] += 12.0;
      }
    }

    if (!edges.empty()) {
      file_rank = personalized_pagerank(edges, personalization);
      have_pr = !file_rank.empty();
      map.used_pagerank = have_pr;
      if (have_pr) {
        if (!map.note.empty()) {
          map.note += "; ";
        }
        map.note += "PageRank (" + std::to_string(edges.size()) + " nodos con aristas)";
      }
    } else {
      if (!map.note.empty()) {
        map.note += "; ";
      }
      map.note += "PageRank off (sin aristas cross-file; índice pequeño o solo tabs)";
    }
  } else if (opts.use_pagerank && snapshot->refs.empty()) {
    if (!map.note.empty()) {
      map.note += "; ";
    }
    map.note += "PageRank off (sin refs en el índice)";
  }

  struct Scored {
    const IndexedSymbol* sym = nullptr;
    int score = 0;
    double pr = 0.0;
  };
  std::vector<Scored> scored;
  scored.reserve(usable.size());

  // Incoming ref weight per (file,name) for distributing file rank.
  std::unordered_map<std::string, int> symbol_ref_weight;
  for (const auto& ref : snapshot->refs) {
    symbol_ref_weight[ref.name] += ref.count;
  }

  for (const auto* sym : usable) {
    if (sym->file.rfind("third_party/", 0) == 0) {
      continue;
    }
    const int qhit = query_hit_score(*sym, tokens);
    const int path_hit = filename_seed_match_score(sym->file, tokens);
    const int lex = lexical_score(*sym, tokens, opts.active_file, chat_set);
    double pr = 0.0;
    if (have_pr) {
      const auto it = file_rank.find(sym->file);
      if (it != file_rank.end()) {
        pr = it->second;
      }
      const int rw = symbol_ref_weight[bare_name(*sym)];
      pr *= (1.0 + 0.15 * static_cast<double>(std::min(rw, 40)));
    }
    // With a real query, lexical/query hits dominate. PageRank only reorders among matches.
    // Without query tokens, PageRank drives the structural outline (Aider-style).
    int score = 0;
    if (!tokens.empty()) {
      score = qhit * 2000 + lex * 10 + static_cast<int>(pr * 1000.0);
      // Strong path/stem matches (compile.sh ↔ compile) count as first-class hits.
      if (path_hit >= 130) {
        score += path_hit * 25;
      }
      // Prefer the file-level synthetic over incidental helper functions in the same script.
      if (path_syms.count(sym) > 0 && path_hit >= 130) {
        score += 400000;
      }
      // Multi-facet coverage: reward entries that match several query facets (not one ambiguous word).
      if (!facets.empty()) {
        const int cov =
            facet_coverage_score(sym->file, bare_name(*sym), sym->signature, facets);
        const int covered = cov / 1000;
        if (covered >= 2) {
          score += covered * covered * 120000;
        } else if (covered == 1) {
          score += 8000;
        }
      }
    } else if (have_pr) {
      score = static_cast<int>(pr * 1'000'000.0) + lex;
    } else {
      score = lex;
    }
    scored.push_back(Scored{sym, score, pr});
  }

  std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    return bare_name(*a.sym) < bare_name(*b.sym);
  });

  map.best_score = scored.empty() ? 0 : scored.front().score;

  // Prefer symbols that actually hit the query; fall back to outline if none.
  const bool has_query = !tokens.empty();
  const bool buildish_query = tokens_look_buildish(tokens);
  int query_match_count = 0;
  if (has_query) {
    for (const auto& s : scored) {
      if (query_hit_score(*s.sym, tokens) > 0 ||
          filename_seed_match_score(s.sym->file, tokens) >= 130) {
        ++query_match_count;
      }
    }
  }
  const bool weak = has_query && query_match_count == 0;
  if (weak) {
    if (!map.note.empty()) {
      map.note += "; ";
    }
    map.note += "pocos matches léxicos; outline ampliado";
  } else if (has_query) {
    if (!map.note.empty()) {
      map.note += "; ";
    }
    map.note += "query_hits=" + std::to_string(query_match_count);
  }

  // Select with token/char budget (binary search when max_map_tokens > 0).
  auto try_build = [&](std::size_t take) -> RepoMap {
    RepoMap trial = map;
    trial.entries.clear();
    std::unordered_set<std::string> files_used;
    std::unordered_map<std::string, int> per_file;
    std::size_t chars = 80;
    constexpr int kMaxPerFile = 6;
    for (std::size_t i = 0; i < take && i < scored.size(); ++i) {
      const auto& s = scored[i];
      if (s.sym->file.rfind("third_party/", 0) == 0) {
        continue;
      }
      if (has_query && !weak && query_hit_score(*s.sym, tokens) <= 0 &&
          filename_seed_match_score(s.sym->file, tokens) < 130) {
        // Keep the map on-topic: skip PR-hot symbols that ignore the query.
        continue;
      }
      if (weak) {
        const bool active = !opts.active_file.empty() && s.sym->file == opts.active_file;
        const bool chat = chat_set.count(s.sym->file) > 0;
        const bool srcish =
            s.sym->file.rfind("src/", 0) == 0 || s.sym->file.rfind("include/", 0) == 0;
        const bool toolish = s.sym->file.rfind("tools/", 0) == 0 ||
                             s.sym->file.rfind("scripts/", 0) == 0 ||
                             path_basename(s.sym->file) == "CMakeLists.txt";
        const bool typeish = s.sym->kind == SymbolKind::kClass || s.sym->kind == SymbolKind::kStruct ||
                             s.sym->kind == SymbolKind::kFunction || s.sym->kind == SymbolKind::kMethod;
        if (!active && !chat && !(srcish && typeish) && !(buildish_query && toolish)) {
          continue;
        }
      }
      if (per_file[s.sym->file] >= kMaxPerFile) {
        continue;
      }
      if (files_used.find(s.sym->file) == files_used.end() && files_used.size() >= opts.max_files) {
        continue;
      }
      const std::string sig =
          s.sym->signature.empty()
              ? (std::string(kind_label(s.sym->kind)) + " " + bare_name(*s.sym))
              : s.sym->signature;
      const std::size_t line_chars = s.sym->file.size() + sig.size() + 16;
      if (trial.entries.size() >= opts.max_symbols || chars + line_chars > opts.max_chars) {
        break;
      }
      files_used.insert(s.sym->file);
      ++per_file[s.sym->file];
      chars += line_chars;
      RepoMapEntry e;
      e.file = s.sym->file;
      e.name = bare_name(*s.sym);
      e.kind = s.sym->kind;
      e.line = s.sym->line;
      e.score = s.score;
      e.signature = s.sym->signature;
      trial.entries.push_back(std::move(e));
    }
    return trial;
  };

  if (opts.max_map_tokens > 0) {
    std::size_t lo = 1;
    std::size_t hi = std::min(scored.size(), opts.max_symbols);
    RepoMap best = try_build(std::min<std::size_t>(16, hi));
    while (lo <= hi) {
      const std::size_t mid = (lo + hi) / 2;
      RepoMap trial = try_build(mid);
      const int toks = estimate_tokens(trial.render_text());
      if (toks <= opts.max_map_tokens) {
        best = std::move(trial);
        lo = mid + 1;
      } else {
        if (mid == 0) {
          break;
        }
        hi = mid - 1;
      }
    }
    map = std::move(best);
  } else {
    map = try_build(opts.max_symbols);
  }

  return map;
}

}  // namespace tuide
