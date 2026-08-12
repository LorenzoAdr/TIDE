#include "ai/coding_embed_rerank.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sstream>
#include <unordered_map>

#include "ai/coding_stem_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/get_code_of.hpp"
#include "ai/repo_map.hpp"
#include "ai/vector_math.hpp"
#include "symbols/symbol_kind.hpp"

namespace tuide {
namespace {

bool embed_text(bool is_query, const std::string& text, EmbeddingBackend* backend,
                const CodingEmbedFn& test_embed, std::vector<float>* out) {
  if (out == nullptr || text.empty()) {
    return false;
  }
  if (test_embed) {
    return test_embed(is_query, text, out);
  }
  if (backend == nullptr || !backend->ready()) {
    return false;
  }
  std::string err;
  if (is_query) {
    return backend->embed_query(text, out, &err);
  }
  return backend->embed_passage(text, out, &err);
}

constexpr int kSemanticWeight = 4000000;

std::string coding_stem_family_key(const std::string& stem) {
  const auto u = stem.find('_');
  if (u != std::string::npos && u >= 2 && u <= 16 && u + 2 <= stem.size()) {
    return stem.substr(0, u);
  }
  return stem;
}

// Keep fused order but cap stems that share a prefix family (lsp_*, ui_*, …).
// Prefer a shorter diverse list over padding with clones of the same family.
std::vector<CodingStemShortlistItem> diversify_stem_shortlist(
    const std::vector<CodingStemShortlistItem>& ranked, std::size_t max_n, int max_per_family) {
  std::vector<CodingStemShortlistItem> out;
  if (max_n == 0 || ranked.empty()) {
    return out;
  }
  if (max_per_family < 1) {
    max_per_family = 1;
  }
  out.reserve(std::min(max_n, ranked.size()));
  std::unordered_map<std::string, int> family_count;
  for (const auto& item : ranked) {
    if (out.size() >= max_n) {
      break;
    }
    const std::string fam = coding_stem_family_key(item.stem);
    if (family_count[fam] >= max_per_family) {
      continue;
    }
    ++family_count[fam];
    out.push_back(item);
  }
  return out;
}

}  // namespace

std::string coding_stem_passage(const std::string& stem, const std::string& sample_path,
                                const std::vector<std::string>& sample_names) {
  std::string out = stem;
  {
    std::string tok;
    auto flush = [&] {
      if (tok.size() >= 3) {
        out.push_back(' ');
        out += tok;
      }
      tok.clear();
    };
    for (char ch : stem) {
      if (ch == '_' || ch == '-') {
        flush();
      } else {
        tok.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      }
    }
    flush();
  }
  if (!sample_path.empty()) {
    out.push_back(' ');
    out += sample_path;
  }
  const std::size_t n = std::min<std::size_t>(sample_names.size(), 3);
  for (std::size_t i = 0; i < n; ++i) {
    if (sample_names[i].empty()) {
      continue;
    }
    out.push_back(' ');
    out += sample_names[i];
  }
  return out;
}

std::string coding_entry_passage(const RepoMapEntry& e) {
  std::string out = e.file;
  if (!e.name.empty()) {
    out.push_back(' ');
    out += e.name;
  }
  if (!e.signature.empty()) {
    out.push_back(' ');
    const std::size_t cap = 120;
    if (e.signature.size() <= cap) {
      out += e.signature;
    } else {
      out.append(e.signature.data(), cap);
    }
  }
  return out;
}

CodingEmbedRerankResult fuse_coding_stems(const std::string& query,
                                          std::vector<CodingStemCandidate> lexical_ranked,
                                          CodingStemEmbedIndex* stem_index,
                                          EmbeddingBackend* backend,
                                          const CodingEmbedFn& test_embed) {
  CodingEmbedRerankResult out;
  if (lexical_ranked.empty() && (stem_index == nullptr || !stem_index->ready())) {
    return out;
  }
  if (!lexical_ranked.empty()) {
    out.stem = lexical_ranked.front().stem;
  }
  if (query.empty()) {
    return out;
  }
  if (!test_embed && (backend == nullptr || !backend->ready()) &&
      (stem_index == nullptr || !stem_index->ready())) {
    return out;
  }

  std::vector<float> qvec;
  if (!embed_text(true, query, backend, test_embed, &qvec) || qvec.empty()) {
    return out;
  }

  std::unordered_map<std::string, CodingStemCandidate> by_stem;
  by_stem.reserve(lexical_ranked.size() + 16);
  for (auto& c : lexical_ranked) {
    by_stem[c.stem] = std::move(c);
  }

  if (stem_index != nullptr && stem_index->ready()) {
    const auto sem = stem_index->top_k(qvec, 12);
    for (const auto& kv : sem) {
      auto it = by_stem.find(kv.first);
      if (it == by_stem.end()) {
        CodingStemCandidate c;
        c.stem = kv.first;
        c.lexical_score = 0;
        c.path_strong = 0;
        c.semantic_cos = kv.second;
        if (const auto* p = stem_index->passage_for(kv.first)) {
          c.passage = *p;
        } else {
          c.passage = kv.first;
        }
        by_stem.emplace(kv.first, std::move(c));
      } else {
        it->second.semantic_cos = std::max(it->second.semantic_cos, kv.second);
        if (it->second.passage.empty()) {
          if (const auto* p = stem_index->passage_for(kv.first)) {
            it->second.passage = *p;
          }
        }
      }
    }
  }

  for (auto& kv : by_stem) {
    auto& c = kv.second;
    if (c.semantic_cos > 0.0f) {
      continue;
    }
    if (stem_index != nullptr && stem_index->ready()) {
      c.semantic_cos = stem_index->cosine_for_stem(qvec, c.stem);
      if (c.semantic_cos > 0.0f) {
        continue;
      }
    }
    std::vector<float> pvec;
    const std::string& passage = c.passage.empty() ? c.stem : c.passage;
    if (embed_text(false, passage, backend, test_embed, &pvec) && !pvec.empty()) {
      c.semantic_cos = cosine_similarity(qvec, pvec);
    }
  }

  out.used_embed = true;
  long long best_fused = -1;
  for (const auto& kv : by_stem) {
    const auto& c = kv.second;
    const long long fused =
        static_cast<long long>(c.lexical_score) +
        static_cast<long long>(std::lround(static_cast<double>(c.semantic_cos) * kSemanticWeight));
    if (fused > best_fused || (fused == best_fused && (out.stem.empty() || c.stem < out.stem))) {
      best_fused = fused;
      out.stem = c.stem;
      out.best_cosine = c.semantic_cos;
    }
  }
  return out;
}

std::vector<CodingStemShortlistItem> build_fused_stem_shortlist(
    std::vector<CodingStemCandidate> lexical_ranked, const std::string& query,
    CodingStemEmbedIndex* stem_index, EmbeddingBackend* backend, const CodingEmbedFn& test_embed,
    std::size_t max_n) {
  std::vector<CodingStemShortlistItem> out;
  if (max_n == 0) {
    return out;
  }
  // Reuse fuse path to inject semantic top-K and fill cosines, then score all.
  std::unordered_map<std::string, CodingStemCandidate> by_stem;
  for (auto& c : lexical_ranked) {
    by_stem[c.stem] = std::move(c);
  }

  std::vector<float> qvec;
  const bool can_embed = test_embed || (backend != nullptr && backend->ready()) ||
                         (stem_index != nullptr && stem_index->ready());
  if (!query.empty() && can_embed) {
    embed_text(true, query, backend, test_embed, &qvec);
  }
  if (!qvec.empty() && stem_index != nullptr && stem_index->ready()) {
    for (const auto& kv : stem_index->top_k(qvec, 16)) {
      auto it = by_stem.find(kv.first);
      if (it == by_stem.end()) {
        CodingStemCandidate c;
        c.stem = kv.first;
        c.semantic_cos = kv.second;
        if (const auto* p = stem_index->passage_for(kv.first)) {
          c.passage = *p;
        }
        by_stem.emplace(kv.first, std::move(c));
      } else {
        it->second.semantic_cos = std::max(it->second.semantic_cos, kv.second);
      }
    }
  }
  for (auto& kv : by_stem) {
    auto& c = kv.second;
    if (c.semantic_cos > 0.0f || qvec.empty()) {
      continue;
    }
    if (stem_index != nullptr && stem_index->ready()) {
      c.semantic_cos = stem_index->cosine_for_stem(qvec, c.stem);
      if (c.semantic_cos > 0.0f) {
        continue;
      }
    }
    std::vector<float> pvec;
    const std::string& passage = c.passage.empty() ? c.stem : c.passage;
    if (embed_text(false, passage, backend, test_embed, &pvec) && !pvec.empty()) {
      c.semantic_cos = cosine_similarity(qvec, pvec);
    }
  }

  out.reserve(by_stem.size());
  for (const auto& kv : by_stem) {
    const auto& c = kv.second;
    CodingStemShortlistItem item;
    item.stem = c.stem;
    item.lexical_score = c.lexical_score;
    item.semantic_cos = c.semantic_cos;
    item.fused_score =
        static_cast<long long>(c.lexical_score) +
        static_cast<long long>(std::lround(static_cast<double>(c.semantic_cos) * kSemanticWeight));
    item.hint = c.passage.empty() ? c.stem : c.passage;
    if (item.hint.size() > 180) {
      item.hint.resize(180);
      item.hint += "…";
    }
    out.push_back(std::move(item));
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const CodingStemShortlistItem& a, const CodingStemShortlistItem& b) {
                     if (a.fused_score != b.fused_score) {
                       return a.fused_score > b.fused_score;
                     }
                     return a.stem < b.stem;
                   });
  // Cap per family so L1 sees ui_wake alongside lsp_* instead of 8× lsp_*.
  const int max_per_family = max_n >= 6 ? 2 : 1;
  return diversify_stem_shortlist(out, max_n, max_per_family);
}

std::string validate_coding_stem_pick(const std::string& raw,
                                      const std::vector<CodingStemShortlistItem>& shortlist) {
  std::string s = raw;
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  // Strip accidental quotes / backticks.
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '`' && s.back() == '`'))) {
    s = s.substr(1, s.size() - 2);
  }
  if (s.empty()) {
    return {};
  }
  for (const auto& item : shortlist) {
    if (item.stem == s) {
      return item.stem;
    }
  }
  std::string low = s;
  for (char& c : low) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  for (const auto& item : shortlist) {
    std::string il = item.stem;
    for (char& c : il) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (il == low) {
      return item.stem;
    }
  }
  return {};
}

CodingEmbedRerankResult rerank_coding_stems(const std::string& query,
                                            const std::vector<CodingStemCandidate>& lexical_ranked,
                                            EmbeddingBackend* backend,
                                            const CodingEmbedFn& test_embed) {
  CodingEmbedRerankResult out;
  if (lexical_ranked.empty()) {
    return out;
  }
  out.stem = lexical_ranked.front().stem;
  if (query.empty() || lexical_ranked.size() < 2) {
    return out;
  }
  if (!test_embed && (backend == nullptr || !backend->ready())) {
    return out;
  }

  std::vector<float> qvec;
  if (!embed_text(true, query, backend, test_embed, &qvec) || qvec.empty()) {
    return out;
  }

  const int top_lex = lexical_ranked.front().lexical_score;
  const int top_path = lexical_ranked.front().path_strong;
  float best_cos = -1.0f;
  std::size_t best_i = 0;
  std::vector<float> cosines(lexical_ranked.size(), 0.0f);

  const std::size_t n = std::min<std::size_t>(lexical_ranked.size(), 6);
  for (std::size_t i = 0; i < n; ++i) {
    std::vector<float> pvec;
    const std::string& passage = lexical_ranked[i].passage.empty() ? lexical_ranked[i].stem
                                                                   : lexical_ranked[i].passage;
    if (!embed_text(false, passage, backend, test_embed, &pvec) || pvec.empty()) {
      continue;
    }
    const float c = cosine_similarity(qvec, pvec);
    cosines[i] = c;
    if (c > best_cos) {
      best_cos = c;
      best_i = i;
    }
  }
  if (best_cos < 0.0f) {
    return out;
  }

  out.used_embed = true;
  out.best_cosine = cosines[0];

  constexpr float kMargin = 0.04f;
  for (std::size_t i = 1; i < n; ++i) {
    const float margin = cosines[i] - cosines[0];
    if (margin < kMargin) {
      continue;
    }
    const auto& cand = lexical_ranked[i];
    const bool path_ok = cand.path_strong >= top_path;
    const bool lex_ok =
        top_lex <= 0 ||
        cand.lexical_score >= static_cast<int>(static_cast<long long>(top_lex) * 85 / 100);
    if (!(path_ok || lex_ok)) {
      continue;
    }
    if (cosines[i] > out.best_cosine) {
      out.stem = cand.stem;
      out.best_cosine = cosines[i];
    }
  }
  if (out.stem.empty()) {
    out.stem = lexical_ranked[best_i].stem;
    out.best_cosine = best_cos;
  }
  return out;
}

bool soft_boost_coding_entries(const std::string& query, std::vector<RepoMapEntry>* entries,
                               EmbeddingBackend* backend, const CodingEmbedFn& test_embed,
                               std::size_t max_n) {
  if (entries == nullptr || entries->empty() || query.empty()) {
    return false;
  }
  if (!test_embed && (backend == nullptr || !backend->ready())) {
    return false;
  }

  std::vector<float> qvec;
  if (!embed_text(true, query, backend, test_embed, &qvec) || qvec.empty()) {
    return false;
  }

  const std::size_t n = std::min(entries->size(), max_n);
  std::vector<std::string> passages;
  passages.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    passages.push_back(coding_entry_passage((*entries)[i]));
  }

  std::vector<std::vector<float>> pvecs;
  bool ok_batch = false;
  if (!test_embed && backend != nullptr && backend->ready()) {
    std::string emb_err;
    ok_batch = backend->embed_passages(passages, &pvecs, &emb_err) && pvecs.size() == n;
  }

  bool any = false;
  if (ok_batch) {
    for (std::size_t i = 0; i < n; ++i) {
      if (pvecs[i].empty()) {
        continue;
      }
      const float c = cosine_similarity(qvec, pvecs[i]);
      int boost = static_cast<int>(std::lround(static_cast<double>(c) * 400.0));
      if (boost > 400) {
        boost = 400;
      }
      if (boost < -100) {
        boost = -100;
      }
      (*entries)[i].score += boost;
      any = true;
    }
    return any;
  }

  for (std::size_t i = 0; i < n; ++i) {
    auto& e = (*entries)[i];
    std::vector<float> pvec;
    if (!embed_text(false, passages[i], backend, test_embed, &pvec) || pvec.empty()) {
      continue;
    }
    const float c = cosine_similarity(qvec, pvec);
    int boost = static_cast<int>(std::lround(static_cast<double>(c) * 400.0));
    if (boost > 400) {
      boost = 400;
    }
    if (boost < -100) {
      boost = -100;
    }
    e.score += boost;
    any = true;
  }
  return any;
}

std::string enrich_query_for_embed(const std::string& query,
                                   const std::vector<std::string>& needles) {
  std::ostringstream out;
  out << query;
  if (!needles.empty()) {
    out << "\nneedles:";
    for (const auto& n : needles) {
      if (n.empty()) {
        continue;
      }
      out << ' ' << n;
    }
  }
  return out.str();
}

namespace {

const char* kind_label_short(SymbolKind k) {
  switch (k) {
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
    case SymbolKind::kNamespace:
      return "ns";
  }
  return "sym";
}

std::vector<RepoMapEntry> diversify_entries_by_file(std::vector<RepoMapEntry> ranked,
                                                   std::size_t max_n, int max_per_file) {
  std::vector<RepoMapEntry> out;
  if (max_n == 0 || ranked.empty()) {
    return out;
  }
  if (max_per_file < 1) {
    max_per_file = 1;
  }
  out.reserve(std::min(max_n, ranked.size()));
  std::unordered_map<std::string, int> file_count;
  for (auto& e : ranked) {
    if (out.size() >= max_n) {
      break;
    }
    if (file_count[e.file] >= max_per_file) {
      continue;
    }
    ++file_count[e.file];
    out.push_back(std::move(e));
  }
  return out;
}

int clamp_boost(int boost, int lo, int hi) {
  if (boost > hi) {
    return hi;
  }
  if (boost < lo) {
    return lo;
  }
  return boost;
}

}  // namespace

TwoStageRerankResult rerank_map_two_stage(std::vector<RepoMapEntry> candidates,
                                          const TwoStageRerankOptions& opts,
                                          EmbeddingBackend* backend,
                                          const CodingEmbedFn& test_embed) {
  using clock = std::chrono::steady_clock;
  const auto t0 = clock::now();

  TwoStageRerankResult out;
  out.candidates_in = candidates.size();
  if (candidates.empty()) {
    out.note = "two_stage=0; empty_candidates";
    return out;
  }

  // Keep input order loosely by existing score only as a stable starting point; embed decides.
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const RepoMapEntry& a, const RepoMapEntry& b) {
                     if (a.score != b.score) {
                       return a.score > b.score;
                     }
                     if (a.file != b.file) {
                       return a.file < b.file;
                     }
                     return a.line < b.line;
                   });

  if (opts.phase_a_pool > 0 && candidates.size() > opts.phase_a_pool) {
    candidates.resize(opts.phase_a_pool);
  }

  const bool can_embed = test_embed || (backend != nullptr && backend->ready());
  const std::string enriched = enrich_query_for_embed(opts.query, opts.needles);
  std::vector<float> qvec;
  if (can_embed && !enriched.empty()) {
    embed_text(true, enriched, backend, test_embed, &qvec);
  }

  const auto t_a0 = clock::now();
  std::vector<float> sig_cos(candidates.size(), -1.0f);
  if (!qvec.empty() && !opts.skip_phase_a) {
    out.used_phase_a = true;
    std::vector<std::string> passages;
    passages.reserve(candidates.size());
    for (const auto& e : candidates) {
      passages.push_back(coding_entry_passage(e));
    }
    std::vector<std::vector<float>> pvecs;
    bool ok_batch = false;
    if (!test_embed && backend != nullptr && backend->ready()) {
      std::string emb_err;
      ok_batch = backend->embed_passages(passages, &pvecs, &emb_err) && pvecs.size() == passages.size();
    }
    if (ok_batch) {
      for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (pvecs[i].empty()) {
          continue;
        }
        const float c = cosine_similarity(qvec, pvecs[i]);
        sig_cos[i] = c;
        candidates[i].score +=
            clamp_boost(static_cast<int>(std::lround(static_cast<double>(c) * 400.0)), -100, 400);
      }
    } else {
      for (std::size_t i = 0; i < candidates.size(); ++i) {
        auto& e = candidates[i];
        std::vector<float> pvec;
        if (!embed_text(false, passages[i], backend, test_embed, &pvec) || pvec.empty()) {
          continue;
        }
        const float c = cosine_similarity(qvec, pvec);
        sig_cos[i] = c;
        e.score += clamp_boost(static_cast<int>(std::lround(static_cast<double>(c) * 400.0)), -100,
                               400);
      }
    }
    // Rank by signature cosine (primary); lexical score only as tie-break.
    std::vector<std::size_t> order(candidates.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t ia, std::size_t ib) {
      const float ca = sig_cos[ia];
      const float cb = sig_cos[ib];
      const bool a_ok = ca >= 0.0f;
      const bool b_ok = cb >= 0.0f;
      if (a_ok != b_ok) {
        return a_ok;
      }
      if (a_ok && b_ok && ca != cb) {
        return ca > cb;
      }
      if (candidates[ia].score != candidates[ib].score) {
        return candidates[ia].score > candidates[ib].score;
      }
      return candidates[ia].file < candidates[ib].file;
    });
    std::vector<RepoMapEntry> sorted;
    sorted.reserve(order.size());
    for (std::size_t idx : order) {
      sorted.push_back(std::move(candidates[idx]));
    }
    candidates = std::move(sorted);
  }
  out.phase_a_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t_a0).count();

  if (opts.phase_a_top > 0 && candidates.size() > opts.phase_a_top) {
    candidates.resize(opts.phase_a_top);
  }
  if (opts.max_per_file > 0) {
    candidates = diversify_entries_by_file(std::move(candidates), candidates.size(),
                                          opts.max_per_file);
  }

  std::vector<std::string> bodies(candidates.size());
  std::vector<float> body_cos(candidates.size(), -1.0f);
  const bool do_bodies =
      opts.fetch_bodies && !qvec.empty() && !opts.workspace_root.empty() && !candidates.empty();
  const auto t_b0 = clock::now();
  if (do_bodies) {
    out.used_phase_b = true;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      auto& e = candidates[i];
      GetCodeOfRequest req;
      req.workspace_root = opts.workspace_root;
      req.file = e.file;
      req.symbol = e.name;
      req.line = e.line;
      req.max_lines = std::max(20, opts.body_max_lines);
      const GetCodeOfResult got = get_code_of(req);
      if (!got.ok || got.text.empty()) {
        continue;
      }
      std::string body = got.text;
      constexpr std::size_t kBodyCap = 2500;
      if (body.size() > kBodyCap) {
        body.resize(kBodyCap);
        body += "\n…";
      }
      bodies[i] = std::move(body);
    }
    std::vector<std::string> body_passages;
    std::vector<std::size_t> body_idx;
    body_passages.reserve(candidates.size());
    body_idx.reserve(candidates.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
      if (!bodies[i].empty()) {
        body_passages.push_back(bodies[i]);
        body_idx.push_back(i);
      }
    }
    std::vector<std::vector<float>> pvecs;
    bool ok_batch = false;
    if (!body_passages.empty() && !test_embed && backend != nullptr && backend->ready()) {
      std::string emb_err;
      ok_batch =
          backend->embed_passages(body_passages, &pvecs, &emb_err) && pvecs.size() == body_passages.size();
    }
    if (ok_batch) {
      for (std::size_t j = 0; j < body_idx.size(); ++j) {
        const std::size_t i = body_idx[j];
        if (pvecs[j].empty()) {
          continue;
        }
        const float c = cosine_similarity(qvec, pvecs[j]);
        body_cos[i] = c;
        candidates[i].score +=
            clamp_boost(static_cast<int>(std::lround(static_cast<double>(c) * 2000.0)), -200, 2000);
      }
    } else {
      for (std::size_t j = 0; j < body_idx.size(); ++j) {
        const std::size_t i = body_idx[j];
        std::vector<float> pvec;
        if (!embed_text(false, bodies[i], backend, test_embed, &pvec) || pvec.empty()) {
          continue;
        }
        const float c = cosine_similarity(qvec, pvec);
        body_cos[i] = c;
        candidates[i].score +=
            clamp_boost(static_cast<int>(std::lround(static_cast<double>(c) * 2000.0)), -200, 2000);
      }
    }
    std::vector<std::size_t> order(candidates.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
    }
    // Phase B: body cosine is primary; lexical/signature score is tie-break.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t ia, std::size_t ib) {
      const float ca = body_cos[ia];
      const float cb = body_cos[ib];
      const bool a_ok = ca >= 0.0f;
      const bool b_ok = cb >= 0.0f;
      if (a_ok != b_ok) {
        return a_ok;
      }
      if (a_ok && b_ok && ca != cb) {
        return ca > cb;
      }
      const auto& a = candidates[ia];
      const auto& b = candidates[ib];
      if (a.score != b.score) {
        return a.score > b.score;
      }
      return a.file < b.file;
    });
    std::vector<RepoMapEntry> sorted_entries;
    std::vector<std::string> sorted_bodies;
    sorted_entries.reserve(order.size());
    sorted_bodies.reserve(order.size());
    for (std::size_t idx : order) {
      sorted_entries.push_back(std::move(candidates[idx]));
      sorted_bodies.push_back(std::move(bodies[idx]));
    }
    candidates = std::move(sorted_entries);
    bodies = std::move(sorted_bodies);
  }
  out.phase_b_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t_b0).count();

  const std::size_t final_n =
      opts.final_top == 0 ? candidates.size() : std::min(candidates.size(), opts.final_top);
  if (opts.max_per_file <= 0) {
    out.entries.assign(candidates.begin(),
                      candidates.begin() + static_cast<std::ptrdiff_t>(final_n));
    if (do_bodies) {
      out.body_texts.assign(bodies.begin(), bodies.begin() + static_cast<std::ptrdiff_t>(final_n));
    }
  } else {
    const int max_per_file = opts.max_per_file;
    out.entries.reserve(final_n);
    out.body_texts.reserve(final_n);
    std::unordered_map<std::string, int> file_count;
    for (std::size_t i = 0; i < candidates.size() && out.entries.size() < final_n; ++i) {
      if (file_count[candidates[i].file] >= max_per_file) {
        continue;
      }
      ++file_count[candidates[i].file];
      out.entries.push_back(std::move(candidates[i]));
      if (do_bodies) {
        if (i < bodies.size()) {
          out.body_texts.push_back(std::move(bodies[i]));
        } else {
          out.body_texts.emplace_back();
        }
      }
    }
  }
  if (!do_bodies) {
    out.body_texts.clear();
  }

  out.total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();

  std::ostringstream note;
  note << "two_stage=1";
  note << "; embed_sig=" << (out.used_phase_a ? 1 : 0);
  note << "; embed_body=" << (out.used_phase_b ? 1 : 0);
  note << "; cand_in=" << out.candidates_in;
  note << "; n=" << out.entries.size();
  note << "; phase_a_ms=" << out.phase_a_ms;
  note << "; phase_b_ms=" << out.phase_b_ms;
  note << "; total_ms=" << out.total_ms;
  if (opts.skip_phase_a) {
    note << "; skip_sig=1";
  }
  out.note = note.str();
  return out;
}

std::string format_ranked_map_answer(const std::vector<RepoMapEntry>& entries, std::size_t max_n,
                                     const std::string& note) {
  std::ostringstream out;
  out << "Resultados más probables:\n";
  if (entries.empty()) {
    out << "(ninguno — índice vacío o sin matches para la consulta)\n";
    if (!note.empty()) {
      out << "note: " << note << '\n';
    }
    return out.str();
  }
  std::size_t shown = 0;
  for (const auto& e : entries) {
    if (shown >= max_n) {
      break;
    }
    ++shown;
    out << shown << ". " << e.file;
    if (e.line > 0) {
      out << ':' << e.line;
    }
    out << "  [score=" << e.score << "]\n";
    out << "    ";
    if (!e.signature.empty()) {
      out << e.signature;
    } else {
      out << kind_label_short(e.kind) << ' ' << e.name;
    }
    out << '\n';
  }
  if (shown == 0) {
    out << "(sin firmas útiles)\n";
  }
  if (!note.empty()) {
    out << "note: " << note << '\n';
  }
  return out.str();
}

}  // namespace tuide
