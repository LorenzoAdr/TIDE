// Stem-boost quality battery: compares ranking with vs without stem index boost.
// Metrics per prompt: shortlist hit@1/@3, map stem hit@5, trap-above, lift vs no-stem.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/ai_types.hpp"
#include "ai/coding_embed_rerank.hpp"
#include "ai/coding_stem_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/model_store.hpp"
#include "ai/repo_map.hpp"
#include "ai/search_needles.hpp"
#include "indexer/index_rules.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "parser/tree_sitter_tags.hpp"

namespace fs = std::filesystem;

namespace tuide {
std::string join_editor_lines_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}
}  // namespace tuide

namespace {

struct Case {
  std::string id;
  std::string prompt;
  std::vector<std::string> expected;
  std::vector<std::string> traps;
};

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool write_file(const std::string& path, const std::string& body) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

std::string path_stem_of(const std::string& file) {
  if (file.empty()) {
    return {};
  }
  std::string base = file;
  const auto slash = base.find_last_of("/\\");
  if (slash != std::string::npos) {
    base = base.substr(slash + 1);
  }
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return base;
}

std::vector<Case> load_cases(const std::string& path, std::string* err) {
  const std::string raw = read_file(path);
  if (raw.empty()) {
    if (err) {
      *err = "empty " + path;
    }
    return {};
  }
  std::vector<Case> out;
  try {
    const auto doc = nlohmann::json::parse(raw);
    for (const auto& j : doc) {
      Case c;
      c.id = j.value("id", "");
      c.prompt = j.value("prompt", "");
      if (j.contains("expected_stems")) {
        for (const auto& s : j["expected_stems"]) {
          c.expected.push_back(s.get<std::string>());
        }
      }
      if (j.contains("trap_stems")) {
        for (const auto& s : j["trap_stems"]) {
          c.traps.push_back(s.get<std::string>());
        }
      }
      if (!c.id.empty() && !c.prompt.empty() && !c.expected.empty()) {
        out.push_back(std::move(c));
      }
    }
  } catch (const std::exception& e) {
    if (err) {
      *err = e.what();
    }
  }
  return out;
}

tuide::SymbolIndexSnapshot build_snapshot(const std::string& root, std::size_t* files_n) {
  tuide::SymbolIndexSnapshot snap;
  snap.workspace_root = root;
  std::size_t n = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec || !it->is_regular_file(ec)) {
      continue;
    }
    std::string rel = fs::relative(it->path(), root, ec).generic_string();
    if (ec || rel.empty() || !tuide::should_index_relative_path(rel)) {
      continue;
    }
    const std::string abs = it->path().string();
    const std::string source = read_file(abs);
    if (source.empty()) {
      continue;
    }
    ++n;
    for (const auto& tag : tuide::extract_repo_map_tags(abs, rel, source)) {
      if (tag.tag_kind != tuide::RepoMapTagKind::Def) {
        continue;
      }
      tuide::IndexedSymbol entry;
      entry.display_name = tag.name;
      entry.name = tag.name;
      entry.kind = tag.symbol_kind;
      entry.line = tag.line;
      entry.file = rel;
      entry.signature = tag.signature;
      snap.symbols.push_back(std::move(entry));
    }
  }
  if (files_n) {
    *files_n = n;
  }
  return snap;
}

int first_expected_rank(const std::vector<std::string>& ordered,
                        const std::vector<std::string>& expected) {
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    for (const auto& e : expected) {
      if (ordered[i] == e) {
        return static_cast<int>(i + 1);
      }
    }
  }
  return 0;
}

bool trap_above_expected(const std::vector<std::string>& ordered,
                         const std::vector<std::string>& expected,
                         const std::vector<std::string>& traps) {
  const int hit = first_expected_rank(ordered, expected);
  if (hit <= 0) {
    return false;
  }
  for (std::size_t i = 0; i < static_cast<std::size_t>(hit - 1) && i < ordered.size(); ++i) {
    for (const auto& t : traps) {
      if (ordered[i] == t) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> unique_stems_from_map(const std::vector<tuide::RepoMapEntry>& entries,
                                               std::size_t max_n) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const auto& e : entries) {
    std::string stem = e.stem.empty() ? path_stem_of(e.file) : e.stem;
    if (stem.empty() || !seen.insert(stem).second) {
      continue;
    }
    out.push_back(std::move(stem));
    if (out.size() >= max_n) {
      break;
    }
  }
  return out;
}

std::vector<std::string> stems_from_shortlist(const std::vector<tuide::CodingStemShortlistItem>& sl) {
  std::vector<std::string> out;
  out.reserve(sl.size());
  for (const auto& i : sl) {
    out.push_back(i.stem);
  }
  return out;
}

nlohmann::json json_string_array(const std::vector<std::string>& v) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : v) {
    arr.push_back(s);
  }
  return arr;
}

nlohmann::json json_shortlist_detail(const std::vector<tuide::CodingStemShortlistItem>& sl,
                                     std::size_t max_n) {
  nlohmann::json arr = nlohmann::json::array();
  std::vector<tuide::CodingStemShortlistItem> sorted = sl;
  std::sort(sorted.begin(), sorted.end(),
            [](const tuide::CodingStemShortlistItem& a, const tuide::CodingStemShortlistItem& b) {
              return a.fused_score > b.fused_score;
            });
  for (std::size_t i = 0; i < sorted.size() && i < max_n; ++i) {
    const auto& item = sorted[i];
    arr.push_back({{"rank", i + 1},
                   {"stem", item.stem},
                   {"lexical_score", item.lexical_score},
                   {"semantic_cos", item.semantic_cos},
                   {"fused_score", item.fused_score},
                   {"hint", item.hint}});
  }
  return arr;
}

nlohmann::json json_stem_semantic_top(tuide::CodingStemEmbedIndex* stem_index,
                                      tuide::EmbeddingBackend* backend,
                                      const std::string& query, std::size_t max_n) {
  nlohmann::json arr = nlohmann::json::array();
  if (stem_index == nullptr || !stem_index->ready() || backend == nullptr || !backend->ready() ||
      query.empty()) {
    return arr;
  }
  std::vector<float> qvec;
  std::string err;
  if (!stem_index->embed_query_vec(query, backend, {}, &qvec, &err) || qvec.empty()) {
    return arr;
  }
  const auto top = stem_index->top_k(qvec, max_n);
  for (std::size_t i = 0; i < top.size(); ++i) {
    arr.push_back(
        {{"rank", i + 1}, {"stem", top[i].first}, {"cosine", top[i].second}});
  }
  return arr;
}

nlohmann::json json_map_entries_top(const std::vector<tuide::RepoMapEntry>& entries,
                                    std::size_t max_n) {
  nlohmann::json arr = nlohmann::json::array();
  for (std::size_t i = 0; i < entries.size() && i < max_n; ++i) {
    const auto& e = entries[i];
    arr.push_back({{"rank", i + 1},
                   {"file", e.file},
                   {"name", e.name},
                   {"stem", e.stem.empty() ? path_stem_of(e.file) : e.stem},
                   {"score", e.score},
                   {"score_base", e.score_base}});
  }
  return arr;
}

nlohmann::json build_lexical_diagnostics(const std::string& prompt) {
  const auto facets = tuide::extract_query_facets(prompt, 16);
  const auto tokens = tuide::repo_map_query_tokens(prompt, 24);
  std::vector<std::string> nl_expanded;
  for (const auto& fac : facets) {
    for (const auto& ex : tuide::expand_nl_retrieval_tokens({fac}, 12)) {
      nl_expanded.push_back(ex);
    }
  }
  std::unordered_set<std::string> seen;
  std::vector<std::string> nl_unique;
  for (auto s : nl_expanded) {
    if (seen.insert(s).second) {
      nl_unique.push_back(std::move(s));
    }
  }
  return {{"facets", json_string_array(facets)},
          {"tokens", json_string_array(tokens)},
          {"nl_expanded", json_string_array(nl_unique)}};
}

void print_verbose_case(const Case& c, const nlohmann::json& row) {
  std::cerr << "\n========== " << c.id << " ==========\n";
  std::cerr << "prompt: " << c.prompt << "\n\n";
  if (row.contains("lexical")) {
    const auto& lx = row["lexical"];
    std::cerr << "[Léxico] facets: ";
    for (const auto& f : lx["facets"]) {
      std::cerr << f.get<std::string>() << " ";
    }
    std::cerr << "\n[Léxico] tokens: ";
    for (const auto& t : lx["tokens"]) {
      std::cerr << t.get<std::string>() << " ";
    }
    std::cerr << "\n[Léxico] NL→EN expandido: ";
    for (const auto& t : lx["nl_expanded"]) {
      std::cerr << t.get<std::string>() << " ";
    }
    std::cerr << "\n\n";
  }
  if (row.contains("shortlist_off_detail")) {
    std::cerr << "--- Shortlist SIN stem boost (top 8) ---\n";
    for (const auto& item : row["shortlist_off_detail"]) {
      std::cerr << "  #" << item["rank"].get<int>() << " " << item["stem"].get<std::string>()
                << " lex=" << item["lexical_score"].get<int>()
                << " cos=" << item["semantic_cos"].get<float>()
                << " fused=" << item["fused_score"].get<long long>() << "\n";
    }
  }
  if (row.contains("shortlist_on_detail")) {
    std::cerr << "--- Shortlist CON stem boost (top 8) ---\n";
    for (const auto& item : row["shortlist_on_detail"]) {
      std::cerr << "  #" << item["rank"].get<int>() << " " << item["stem"].get<std::string>()
                << " lex=" << item["lexical_score"].get<int>()
                << " cos=" << item["semantic_cos"].get<float>()
                << " fused=" << item["fused_score"].get<long long>() << "\n";
    }
  }
  if (row.contains("stem_semantic_top")) {
    std::cerr << "--- Stem index semántico (top-K por cosine) ---\n";
    for (const auto& item : row["stem_semantic_top"]) {
      std::cerr << "  #" << item["rank"].get<int>() << " " << item["stem"].get<std::string>()
                << " cos=" << item["cosine"].get<float>() << "\n";
    }
  }
  if (row.contains("map_top_on")) {
    std::cerr << "--- Mapa rankeado (top 12, con priors) ---\n";
    for (const auto& item : row["map_top_on"]) {
      std::cerr << "  #" << item["rank"].get<int>() << " " << item["stem"].get<std::string>()
                << " " << item["file"].get<std::string>() << ":" << item["name"].get<std::string>()
                << " score=" << item["score"].get<int>() << "\n";
    }
  }
  std::cerr << "\nMétricas: shortlist " << row["rank_sl_off"].get<int>() << "->"
            << row["rank_sl_on"].get<int>() << "  map " << row["rank_map_off"].get<int>() << "->"
            << row["rank_map_on"].get<int>() << "  context_stem="
            << row["context_stem"].get<std::string>()
            << (row["trap_above"].get<bool>() ? "  TRAP" : "")
            << (row["enrich_hit"].get<bool>() ? "  ENRICH_OK" : "") << "\n";
  std::cerr << "Esperado: ";
  for (const auto& e : c.expected) {
    std::cerr << e << " ";
  }
  std::cerr << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string workspace = fs::current_path().string();
  std::string prompts_path;
  std::string out_dir;
  std::string cache_dir = tuide::ModelStore::default_cache_dir();
  std::string label = "baseline";
  std::string case_filter;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&](const char* f) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing " << f << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--workspace") {
      workspace = need("--workspace");
    } else if (a == "--prompts") {
      prompts_path = need("--prompts");
    } else if (a == "--out") {
      out_dir = need("--out");
    } else if (a == "--cache") {
      cache_dir = need("--cache");
    } else if (a == "--label") {
      label = need("--label");
    } else if (a == "--case") {
      case_filter = need("--case");
    } else if (a == "--verbose" || a == "-v") {
      verbose = true;
    } else {
      std::cerr << "unknown " << a << "\n";
      return 2;
    }
  }
  if (prompts_path.empty()) {
    prompts_path =
        (fs::path(workspace) / "tests/fixtures/stem_boost_battery/prompts.json").string();
  }
  if (out_dir.empty()) {
    out_dir = (fs::path(workspace) / ".tuide/ai/stem_boost_battery" / ("round_" + label)).string();
  }

  std::string err;
  auto cases = load_cases(prompts_path, &err);
  if (cases.empty()) {
    std::cerr << "no cases: " << err << "\n";
    return 1;
  }
  if (!case_filter.empty()) {
    std::vector<Case> filtered;
    for (const auto& c : cases) {
      if (c.id == case_filter || c.id.find(case_filter) == 0) {
        filtered.push_back(c);
      }
    }
    cases = std::move(filtered);
    if (cases.empty()) {
      std::cerr << "no cases match --case " << case_filter << "\n";
      return 1;
    }
  }

  std::cerr << "stem_boost_battery label=" << label << " cases=" << cases.size() << "\n";
  std::size_t files_n = 0;
  const auto snap = build_snapshot(workspace, &files_n);
  std::cerr << "indexed files=" << files_n << " symbols=" << snap.symbols.size() << "\n";

  tuide::AiSettings settings;
  settings.level0.embeddings.model_path =
      tuide::ModelStore(cache_dir).intent_embed_model_path(tuide::default_intent_embed_model());
  tuide::EmbeddingBackend backend;
  auto progress = [](const std::string& line) { std::cerr << line << "\n"; };
  if (!backend.ensure_ready(settings, progress, &err)) {
    std::cerr << "embed backend: " << err << "\n";
    return 1;
  }

  tuide::CodingStemEmbedIndex stem_index;
  if (!stem_index.ensure(&snap, &backend, cache_dir, settings.level0.embeddings.model_id, progress,
                         &err, tuide::default_stem_passage_profile())) {
    std::cerr << "stem ensure: " << err << "\n";
    return 1;
  }

  fs::create_directories(out_dir);
  const std::string results_path = (fs::path(out_dir) / "results.jsonl").string();
  std::ofstream results(results_path, std::ios::trunc);

  int sl_hit1 = 0, sl_hit3 = 0, map_hit5 = 0, trap_bad = 0;
  int lift_pos = 0, lift_neg = 0, lift_zero = 0;
  double mrr_sl = 0.0, mrr_map = 0.0;
  int enrich_hit = 0;

  for (const auto& c : cases) {
    tuide::RepoMapOptions opts;
    opts.query = c.prompt;
    opts.max_symbols = 96;
    opts.max_files = 32;

    // Without stem boost.
    tuide::RepoMap map_off = tuide::build_repo_map(&snap, opts);
    map_off.coding_embed = &backend;
    map_off.coding_stem_index = nullptr;
    auto sl_off = map_off.coding_stem_shortlist(&snap, c.prompt, 8);
    auto entries_off = map_off.entries;
    for (auto& e : entries_off) {
      e.stem = path_stem_of(e.file);
      e.score_base = e.score;
    }
    tuide::apply_ranked_map_priors(c.prompt, &entries_off, nullptr, nullptr);
    const auto stems_off = unique_stems_from_map(entries_off, 12);
    const int rank_sl_off = first_expected_rank(stems_from_shortlist(sl_off), c.expected);
    const int rank_map_off = first_expected_rank(stems_off, c.expected);

    // With stem boost.
    tuide::RepoMap map_on = tuide::build_repo_map(&snap, opts);
    map_on.coding_embed = &backend;
    map_on.coding_stem_index = &stem_index;
    auto sl_on = map_on.coding_stem_shortlist(&snap, c.prompt, 8);
    auto entries_on = map_on.entries;
    for (auto& e : entries_on) {
      e.stem = path_stem_of(e.file);
      e.score_base = e.score;
    }
    tuide::apply_ranked_map_priors(c.prompt, &entries_on, &stem_index, &backend);
    map_on.entries = entries_on;
    map_on.enrich_dominant_stem_from_snapshot(&snap, c.prompt, 24);
    const auto stems_on = unique_stems_from_map(map_on.entries, 12);
    const int rank_sl_on = first_expected_rank(stems_from_shortlist(sl_on), c.expected);
    const int rank_map_on = first_expected_rank(stems_on, c.expected);

    const bool trap = trap_above_expected(stems_from_shortlist(sl_on), c.expected, c.traps) ||
                      trap_above_expected(stems_on, c.expected, c.traps);
    if (trap) {
      ++trap_bad;
    }
    if (rank_sl_on == 1) {
      ++sl_hit1;
    }
    if (rank_sl_on > 0 && rank_sl_on <= 3) {
      ++sl_hit3;
    }
    if (rank_map_on > 0 && rank_map_on <= 5) {
      ++map_hit5;
    }
    if (rank_sl_on > 0) {
      mrr_sl += 1.0 / static_cast<double>(rank_sl_on);
    }
    if (rank_map_on > 0) {
      mrr_map += 1.0 / static_cast<double>(rank_map_on);
    }

    // Lift: lower rank number is better; 0 = miss (treat as 99).
    const int off = rank_sl_off > 0 ? rank_sl_off : 99;
    const int on = rank_sl_on > 0 ? rank_sl_on : 99;
    int lift = off - on;
    if (lift > 0) {
      ++lift_pos;
    } else if (lift < 0) {
      ++lift_neg;
    } else {
      ++lift_zero;
    }

    bool enrich_ok = false;
    for (const auto& e : c.expected) {
      if (map_on.context_stem == e) {
        enrich_ok = true;
        break;
      }
    }
    if (enrich_ok) {
      ++enrich_hit;
    }

    nlohmann::json row = {
        {"id", c.id},
        {"prompt", c.prompt},
        {"expected", c.expected},
        {"traps", c.traps},
        {"lexical", build_lexical_diagnostics(c.prompt)},
        {"shortlist_off", stems_from_shortlist(sl_off)},
        {"shortlist_on", stems_from_shortlist(sl_on)},
        {"shortlist_off_detail", json_shortlist_detail(sl_off, 8)},
        {"shortlist_on_detail", json_shortlist_detail(sl_on, 8)},
        {"stem_semantic_top", json_stem_semantic_top(&stem_index, &backend, c.prompt, 12)},
        {"map_stems_off", stems_off},
        {"map_stems_on", stems_on},
        {"map_top_off", json_map_entries_top(entries_off, 12)},
        {"map_top_on", json_map_entries_top(map_on.entries, 12)},
        {"rank_sl_off", rank_sl_off},
        {"rank_sl_on", rank_sl_on},
        {"rank_map_off", rank_map_off},
        {"rank_map_on", rank_map_on},
        {"lift_sl", lift},
        {"trap_above", trap},
        {"context_stem", map_on.context_stem},
        {"enrich_hit", enrich_ok},
        {"embed_rerank_used", map_on.embed_rerank_used},
        {"embed_stem_cos", map_on.embed_stem_cos},
    };
    results << row.dump() << "\n";
    if (verbose) {
      print_verbose_case(c, row);
    }
    std::cerr << c.id << " sl " << rank_sl_off << "->" << rank_sl_on << " map " << rank_map_off
              << "->" << rank_map_on << " ctx=" << map_on.context_stem
              << (trap ? " TRAP" : "") << "\n";
  }

  const double n = static_cast<double>(cases.size());
  nlohmann::json summary = {
      {"label", label},
      {"cases", cases.size()},
      {"stems_indexed", stem_index.size()},
      {"shortlist_hit_at_1", sl_hit1 / n},
      {"shortlist_hit_at_3", sl_hit3 / n},
      {"shortlist_mrr", mrr_sl / n},
      {"map_stem_hit_at_5", map_hit5 / n},
      {"map_mrr", mrr_map / n},
      {"enrich_context_hit", enrich_hit / n},
      {"trap_above_count", trap_bad},
      {"lift_positive", lift_pos},
      {"lift_negative", lift_neg},
      {"lift_zero", lift_zero},
  };
  write_file((fs::path(out_dir) / "summary.json").string(), summary.dump(2));
  std::cout << summary.dump(2) << "\n";
  backend.stop();
  return 0;
}
