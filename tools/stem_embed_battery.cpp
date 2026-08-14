// Offline IR battery for CodingStemEmbedIndex passage profiles.
// Usage:
//   stem_embed_battery --profile baseline [--workspace ROOT] [--qrels PATH]
//                      [--out DIR] [--passages-only] [--top-k 10]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/ai_types.hpp"
#include "ai/coding_stem_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/model_store.hpp"
#include "indexer/index_rules.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "parser/tree_sitter_tags.hpp"

// Stub: tree_sitter_tags.o references this for extract_repo_map_tags_for_file;
// the battery uses extract_repo_map_tags with an in-memory source instead.
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

namespace fs = std::filesystem;

namespace {

struct Qrel {
  std::string id;
  std::string query;
  std::vector<std::string> expected;
  std::vector<std::string> hard_negatives;
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

std::vector<Qrel> load_qrels(const std::string& path, std::string* err) {
  const std::string raw = read_file(path);
  if (raw.empty()) {
    if (err) {
      *err = "no se pudo leer " + path;
    }
    return {};
  }
  std::vector<Qrel> out;
  try {
    const auto doc = nlohmann::json::parse(raw);
    if (!doc.is_array()) {
      if (err) {
        *err = "qrels no es array";
      }
      return {};
    }
    for (const auto& j : doc) {
      Qrel q;
      q.id = j.value("id", "");
      q.query = j.value("query", "");
      if (j.contains("expected_stems") && j["expected_stems"].is_array()) {
        for (const auto& s : j["expected_stems"]) {
          q.expected.push_back(s.get<std::string>());
        }
      }
      if (j.contains("hard_negatives") && j["hard_negatives"].is_array()) {
        for (const auto& s : j["hard_negatives"]) {
          q.hard_negatives.push_back(s.get<std::string>());
        }
      }
      if (!q.id.empty() && !q.query.empty() && !q.expected.empty()) {
        out.push_back(std::move(q));
      }
    }
  } catch (const std::exception& e) {
    if (err) {
      *err = e.what();
    }
    return {};
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
    if (ec) {
      break;
    }
    if (!it->is_regular_file(ec)) {
      continue;
    }
    const fs::path& p = it->path();
    std::string rel = fs::relative(p, root, ec).generic_string();
    if (ec || rel.empty()) {
      continue;
    }
    if (!tuide::should_index_relative_path(rel)) {
      continue;
    }
    const std::string abs = p.string();
    const std::string source = read_file(abs);
    if (source.empty()) {
      continue;
    }
    const auto tags = tuide::extract_repo_map_tags(abs, rel, source);
    ++n;
    for (const auto& tag : tags) {
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

double percentile(std::vector<double> xs, double p) {
  if (xs.empty()) {
    return 0.0;
  }
  std::sort(xs.begin(), xs.end());
  const double idx = p * static_cast<double>(xs.size() - 1);
  const std::size_t lo = static_cast<std::size_t>(idx);
  const std::size_t hi = std::min(lo + 1, xs.size() - 1);
  const double frac = idx - static_cast<double>(lo);
  return xs[lo] * (1.0 - frac) + xs[hi] * frac;
}

int rank_of_first_hit(const std::vector<std::pair<std::string, float>>& ranked,
                      const std::vector<std::string>& expected) {
  for (std::size_t i = 0; i < ranked.size(); ++i) {
    for (const auto& e : expected) {
      if (ranked[i].first == e) {
        return static_cast<int>(i + 1);
      }
    }
  }
  return 0;
}

bool has_hard_neg_above(const std::vector<std::pair<std::string, float>>& ranked,
                        const std::vector<std::string>& expected,
                        const std::vector<std::string>& hard_negatives) {
  const int hit = rank_of_first_hit(ranked, expected);
  if (hit <= 0) {
    return false;
  }
  for (std::size_t i = 0; i < static_cast<std::size_t>(hit - 1) && i < ranked.size(); ++i) {
    for (const auto& n : hard_negatives) {
      if (ranked[i].first == n) {
        return true;
      }
    }
  }
  return false;
}

void print_usage() {
  std::cerr
      << "Usage: stem_embed_battery --profile NAME [options]\n"
      << "  --workspace DIR   (default: cwd)\n"
      << "  --qrels PATH\n"
      << "  --out DIR\n"
      << "  --cache DIR\n"
      << "  --top-k N          (default 10)\n"
      << "  --passages-only\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string profile_name = "baseline";
  std::string workspace = fs::current_path().string();
  std::string qrels_path;
  std::string out_dir;
  std::string cache_dir = tuide::ModelStore::default_cache_dir();
  std::size_t top_k = 10;
  bool passages_only = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << flag << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--profile") {
      profile_name = need("--profile");
    } else if (a == "--workspace") {
      workspace = need("--workspace");
    } else if (a == "--qrels") {
      qrels_path = need("--qrels");
    } else if (a == "--out") {
      out_dir = need("--out");
    } else if (a == "--cache") {
      cache_dir = need("--cache");
    } else if (a == "--top-k") {
      top_k = static_cast<std::size_t>(std::stoul(need("--top-k")));
    } else if (a == "--passages-only") {
      passages_only = true;
    } else if (a == "-h" || a == "--help") {
      print_usage();
      return 0;
    } else {
      std::cerr << "unknown arg: " << a << "\n";
      print_usage();
      return 2;
    }
  }

  const auto profile = tuide::parse_stem_passage_profile(profile_name);
  profile_name = tuide::stem_passage_profile_name(profile);

  if (qrels_path.empty()) {
    qrels_path = (fs::path(workspace) / "tests/fixtures/stem_embed_battery/qrels.json").string();
  }
  if (out_dir.empty()) {
    out_dir =
        (fs::path(workspace) / ".tuide/ai/stem_embed_battery" / ("round_" + profile_name)).string();
  }

  std::string qerr;
  const auto qrels = load_qrels(qrels_path, &qerr);
  if (qrels.empty()) {
    std::cerr << "qrels vacíos: " << qerr << "\n";
    return 1;
  }

  std::cerr << "stem_embed_battery profile=" << profile_name << " workspace=" << workspace << "\n";
  std::cerr << "indexing symbols…\n";
  const auto t0 = std::chrono::steady_clock::now();
  std::size_t files_n = 0;
  const auto snap = build_snapshot(workspace, &files_n);
  const auto t1 = std::chrono::steady_clock::now();
  std::cerr << "indexed files=" << files_n << " symbols=" << snap.symbols.size() << " in "
            << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << "ms\n";

  auto passages = tuide::CodingStemEmbedIndex::build_passages(&snap, profile);
  std::vector<double> chars;
  chars.reserve(passages.size());
  double total_chars = 0.0;
  for (const auto& row : passages) {
    const double c = static_cast<double>(row.passage.size());
    chars.push_back(c);
    total_chars += c;
  }
  const double mean_chars = chars.empty() ? 0.0 : total_chars / static_cast<double>(chars.size());
  const double p95_chars = percentile(chars, 0.95);

  nlohmann::json summary = nlohmann::json::object();
  summary["profile"] = profile_name;
  summary["workspace"] = workspace;
  summary["files_indexed"] = files_n;
  summary["symbols"] = snap.symbols.size();
  summary["stems"] = passages.size();
  summary["passage_chars_mean"] = mean_chars;
  summary["passage_chars_p95"] = p95_chars;
  summary["passage_chars_total"] = total_chars;
  summary["index_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  summary["qrels"] = qrels.size();
  summary["passages_only"] = passages_only;

  fs::create_directories(out_dir);
  {
    nlohmann::json dump = nlohmann::json::array();
    for (const auto& row : passages) {
      dump.push_back({{"stem", row.stem}, {"passage", row.passage}, {"chars", row.passage.size()}});
    }
    write_file((fs::path(out_dir) / "passages.json").string(), dump.dump(2));
  }

  if (passages_only) {
    summary["hit_at_1"] = nullptr;
    summary["hit_at_3"] = nullptr;
    summary["hit_at_5"] = nullptr;
    summary["mrr"] = nullptr;
    write_file((fs::path(out_dir) / "summary.json").string(), summary.dump(2));
    std::cout << summary.dump(2) << "\n";
    return 0;
  }

  tuide::AiSettings settings;
  settings.level0.embeddings.model_path =
      tuide::ModelStore(cache_dir).intent_embed_model_path(tuide::default_intent_embed_model());
  tuide::EmbeddingBackend backend;
  std::string emb_err;
  auto progress = [](const std::string& line) { std::cerr << line << "\n"; };
  if (!backend.ensure_ready(settings, progress, &emb_err)) {
    std::cerr << "embedding backend failed: " << emb_err << "\n";
    return 1;
  }

  tuide::CodingStemEmbedIndex index;
  const auto te0 = std::chrono::steady_clock::now();
  if (!index.ensure(&snap, &backend, cache_dir, settings.level0.embeddings.model_id, progress,
                    &emb_err, profile)) {
    std::cerr << "ensure failed: " << emb_err << "\n";
    return 1;
  }
  const auto te1 = std::chrono::steady_clock::now();
  const auto embed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(te1 - te0).count();
  summary["embed_ms"] = embed_ms;
  summary["content_hash"] = index.content_hash();

  const std::string results_path = (fs::path(out_dir) / "results.jsonl").string();
  write_file(results_path, "");

  int hit1 = 0, hit3 = 0, hit5 = 0;
  double mrr_sum = 0.0;
  int hard_neg_bad = 0;
  std::ofstream results(results_path, std::ios::trunc);

  for (const auto& q : qrels) {
    std::vector<float> qvec;
    std::string qe;
    if (!index.embed_query_vec(q.query, &backend, {}, &qvec, &qe)) {
      nlohmann::json row = {{"id", q.id}, {"ok", false}, {"error", qe}};
      results << row.dump() << "\n";
      continue;
    }
    const auto ranked = index.top_k(qvec, top_k);
    const int rank = rank_of_first_hit(ranked, q.expected);
    if (rank == 1) {
      ++hit1;
    }
    if (rank > 0 && rank <= 3) {
      ++hit3;
    }
    if (rank > 0 && rank <= 5) {
      ++hit5;
    }
    if (rank > 0) {
      mrr_sum += 1.0 / static_cast<double>(rank);
    }
    const bool neg = has_hard_neg_above(ranked, q.expected, q.hard_negatives);
    if (neg) {
      ++hard_neg_bad;
    }

    nlohmann::json tops = nlohmann::json::array();
    for (const auto& p : ranked) {
      tops.push_back({{"stem", p.first}, {"cos", p.second}});
    }
    nlohmann::json row = {{"id", q.id},
                          {"query", q.query},
                          {"expected", q.expected},
                          {"rank", rank},
                          {"hit_at_1", rank == 1},
                          {"hit_at_3", rank > 0 && rank <= 3},
                          {"hit_at_5", rank > 0 && rank <= 5},
                          {"rr", rank > 0 ? 1.0 / static_cast<double>(rank) : 0.0},
                          {"hard_neg_above", neg},
                          {"top", tops}};
    results << row.dump() << "\n";
    std::cerr << q.id << " rank=" << rank << "\n";
  }

  const double nq = static_cast<double>(qrels.size());
  summary["hit_at_1"] = hit1 / nq;
  summary["hit_at_3"] = hit3 / nq;
  summary["hit_at_5"] = hit5 / nq;
  summary["mrr"] = mrr_sum / nq;
  summary["hit_at_1_count"] = hit1;
  summary["hit_at_3_count"] = hit3;
  summary["hit_at_5_count"] = hit5;
  summary["hard_neg_above_count"] = hard_neg_bad;
  summary["top_k"] = top_k;

  write_file((fs::path(out_dir) / "summary.json").string(), summary.dump(2));
  std::cout << summary.dump(2) << "\n";
  backend.stop();
  return 0;
}
