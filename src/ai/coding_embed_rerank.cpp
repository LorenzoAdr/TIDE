#include "ai/coding_embed_rerank.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include "ai/coding_stem_embed_index.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/get_code_of.hpp"
#include "ai/repo_map.hpp"
#include "ai/search_needles.hpp"
#include "ai/vector_math.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "symbols/symbol_kind.hpp"

#include <filesystem>
#include <fstream>

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
// Soft boost when underscore tokens of the stem appear in the query (module identity).
constexpr long long kStemTokenHitWeight = 550000;
// Prefer ui_wake_policy over ui_wake when the query mentions the extra token.
constexpr long long kStemSpecificityBonus = 350000;

std::string ascii_lower_copy_local(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

int stem_query_token_hits(const std::string& stem, const std::string& query_l) {
  if (stem.empty() || query_l.empty()) {
    return 0;
  }
  int hits = 0;
  std::string tok;
  auto flush = [&] {
    if (tok.size() >= 3 && query_l.find(tok) != std::string::npos) {
      ++hits;
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
  return hits;
}

bool stem_extends_other(const std::string& longer, const std::string& shorter) {
  if (longer.size() <= shorter.size() + 1) {
    return false;
  }
  if (longer.compare(0, shorter.size(), shorter) != 0) {
    return false;
  }
  return longer[shorter.size()] == '_' || longer[shorter.size()] == '-';
}

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
  // Expand NL synonyms into the query haystack so stem tokens like "policy" /
  // "performance" match Spanish prompts (politica / rendimiento).
  std::string query_l = ascii_lower_copy_local(query);
  for (const auto& fac : extract_query_facets(query, 16)) {
    for (const auto& ex : expand_nl_retrieval_tokens({fac}, 12)) {
      if (ex.size() >= 3 && query_l.find(ex) == std::string::npos) {
        query_l.push_back(' ');
        query_l += ex;
      }
    }
  }
  for (const auto& kv : by_stem) {
    const auto& c = kv.second;
    CodingStemShortlistItem item;
    item.stem = c.stem;
    item.lexical_score = c.lexical_score;
    item.semantic_cos = c.semantic_cos;
    item.fused_score =
        static_cast<long long>(c.lexical_score) +
        static_cast<long long>(std::lround(static_cast<double>(c.semantic_cos) * kSemanticWeight));
    const int hits = stem_query_token_hits(c.stem, query_l);
    item.fused_score += static_cast<long long>(hits) * kStemTokenHitWeight;
    item.hint = c.passage.empty() ? c.stem : c.passage;
    if (item.hint.size() > 180) {
      item.hint.resize(180);
      item.hint += "…";
    }
    out.push_back(std::move(item));
  }
  // Specificity: prefer ui_wake_policy over ui_wake when both are candidates and the
  // query mentions the extra segment (policy / politica already folded into query_l).
  for (auto& item : out) {
    for (const auto& other : out) {
      if (other.stem == item.stem) {
        continue;
      }
      if (!stem_extends_other(item.stem, other.stem)) {
        continue;
      }
      const std::string extra = item.stem.substr(other.stem.size() + 1);
      if (extra.size() >= 3 && query_l.find(ascii_lower_copy_local(extra)) != std::string::npos) {
        item.fused_score += kStemSpecificityBonus;
        break;
      }
    }
  }
  // Bare/generic panel layout: prefer stem "panel" over *_panel when the query is
  // about a generic layout panel (not performance/search/git/…).
  {
    const bool mentions_panel = query_l.find("panel") != std::string::npos;
    const bool mentions_generic =
        query_l.find("generico") != std::string::npos || query_l.find("generic") != std::string::npos ||
        query_l.find("layout") != std::string::npos;
    const bool mentions_specific =
        query_l.find("performance") != std::string::npos || query_l.find("rendimiento") != std::string::npos ||
        query_l.find("search") != std::string::npos || query_l.find("git") != std::string::npos ||
        query_l.find("diagnost") != std::string::npos || query_l.find("packet") != std::string::npos ||
        query_l.find("paquete") != std::string::npos;
    if (mentions_panel && mentions_generic && !mentions_specific) {
      for (auto& item : out) {
        if (item.stem == "panel") {
          item.fused_score += kStemSpecificityBonus;
        }
      }
    }
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

std::string diversify_stem_key(const RepoMapEntry& e) {
  std::string stem = e.stem;
  if (stem.empty()) {
    stem = e.file;
    const auto slash = stem.find_last_of("/\\");
    if (slash != std::string::npos) {
      stem = stem.substr(slash + 1);
    }
    const auto dot = stem.find_last_of('.');
    if (dot != std::string::npos) {
      stem = stem.substr(0, dot);
    }
  }
  const auto sharp = stem.find('#');
  if (sharp != std::string::npos) {
    stem = stem.substr(0, sharp);
  }
  return stem;
}

std::string diversify_dir_key(const RepoMapEntry& e) {
  const std::string& file = e.file;
  const auto slash = file.find('/');
  if (slash == std::string::npos) {
    return file;
  }
  const auto slash2 = file.find('/', slash + 1);
  if (slash2 == std::string::npos) {
    return file;
  }
  return file.substr(0, slash2);
}

std::vector<std::size_t> select_diverse_entry_indices(const std::vector<RepoMapEntry>& ranked,
                                                      std::size_t max_n, int max_per_file,
                                                      int max_per_stem, int max_per_dir) {
  std::vector<std::size_t> out;
  if (max_n == 0 || ranked.empty()) {
    return out;
  }
  out.reserve(std::min(max_n, ranked.size()));
  std::unordered_map<std::string, int> file_count;
  std::unordered_map<std::string, int> stem_count;
  std::unordered_map<std::string, int> dir_count;
  for (std::size_t i = 0; i < ranked.size() && out.size() < max_n; ++i) {
    const auto& e = ranked[i];
    if (max_per_file > 0 && file_count[e.file] >= max_per_file) {
      continue;
    }
    const std::string stem = diversify_stem_key(e);
    if (max_per_stem > 0 && !stem.empty() && stem_count[stem] >= max_per_stem) {
      continue;
    }
    const std::string dir = diversify_dir_key(e);
    if (max_per_dir > 0 && !dir.empty() && dir_count[dir] >= max_per_dir) {
      continue;
    }
    ++file_count[e.file];
    if (!stem.empty()) {
      ++stem_count[stem];
    }
    if (!dir.empty()) {
      ++dir_count[dir];
    }
    out.push_back(i);
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

  for (auto& e : candidates) {
    e.score_base = e.score;
    e.sig_cos = -1.0f;
    e.body_cos = -1.0f;
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
        candidates[i].sig_cos = c;
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
        e.sig_cos = c;
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
  if (opts.max_per_file > 0 || opts.max_per_stem > 0 || opts.max_per_dir > 0) {
    const auto keep = select_diverse_entry_indices(candidates, candidates.size(), opts.max_per_file,
                                                   opts.max_per_stem, opts.max_per_dir);
    std::vector<RepoMapEntry> filtered;
    filtered.reserve(keep.size());
    for (std::size_t idx : keep) {
      filtered.push_back(std::move(candidates[idx]));
    }
    candidates = std::move(filtered);
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
        candidates[i].body_cos = c;
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
        candidates[i].body_cos = c;
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
  if (opts.max_per_file <= 0 && opts.max_per_stem <= 0 && opts.max_per_dir <= 0) {
    out.entries.assign(candidates.begin(),
                      candidates.begin() + static_cast<std::ptrdiff_t>(final_n));
    if (do_bodies) {
      out.body_texts.assign(bodies.begin(), bodies.begin() + static_cast<std::ptrdiff_t>(final_n));
    }
  } else {
    out.entries.reserve(final_n);
    out.body_texts.reserve(final_n);
    const auto keep = select_diverse_entry_indices(candidates, final_n, opts.max_per_file,
                                                   opts.max_per_stem, opts.max_per_dir);
    for (std::size_t i : keep) {
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

namespace {

std::string file_stem_of(const std::string& file) {
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

std::string trim_hint(std::string s, std::size_t max_n) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.pop_back();
  }
  if (s.size() > max_n) {
    s.resize(max_n);
    s += "…";
  }
  return s;
}

bool looks_like_doc_comment(const std::string& line) {
  std::size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    ++i;
  }
  if (i >= line.size()) {
    return false;
  }
  if (line.compare(i, 3, "///") == 0 || line.compare(i, 3, "//!") == 0) {
    return true;
  }
  if (line.compare(i, 2, "//") == 0) {
    return true;
  }
  if (line.compare(i, 2, "/*") == 0 || line.compare(i, 1, "*") == 0) {
    return true;
  }
  return false;
}

struct FileLinesCache {
  std::vector<std::string> lines;  // 1-based content in [1..] with [0] unused
  bool loaded = false;
};

FileLinesCache& load_file_lines(std::unordered_map<std::string, FileLinesCache>* cache,
                                const std::string& abs_path) {
  auto& slot = (*cache)[abs_path];
  if (slot.loaded) {
    return slot;
  }
  slot.loaded = true;
  std::ifstream in(abs_path);
  if (!in) {
    return slot;
  }
  slot.lines.emplace_back();
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    slot.lines.push_back(std::move(line));
  }
  return slot;
}

std::string extract_doc_near(const FileLinesCache& fl, int line) {
  if (line <= 1 || fl.lines.size() <= 1) {
    return {};
  }
  const int start = std::max(1, line - 4);
  for (int i = line - 1; i >= start; --i) {
    if (i <= 0 || static_cast<std::size_t>(i) >= fl.lines.size()) {
      continue;
    }
    const std::string& L = fl.lines[static_cast<std::size_t>(i)];
    if (L.find_first_not_of(" \t") == std::string::npos) {
      continue;
    }
    if (looks_like_doc_comment(L)) {
      return trim_hint(L, 140);
    }
    // Stop at first non-comment code above the def (other than blank).
    break;
  }
  return {};
}

std::string extract_snippet(const FileLinesCache& fl, int line, int max_lines) {
  if (line <= 0 || fl.lines.size() <= 1) {
    return {};
  }
  std::ostringstream out;
  int taken = 0;
  for (int i = line; static_cast<std::size_t>(i) < fl.lines.size() && taken < max_lines; ++i) {
    const std::string& L = fl.lines[static_cast<std::size_t>(i)];
    if (taken > 0 && L.find_first_not_of(" \t") == std::string::npos) {
      break;
    }
    if (taken) {
      out << '\n';
    }
    out << trim_hint(L, 120);
    ++taken;
  }
  return out.str();
}

std::string snippet_from_body(const std::string& body, int max_lines) {
  if (body.empty()) {
    return {};
  }
  std::istringstream iss(body);
  std::string line;
  std::ostringstream out;
  int taken = 0;
  while (std::getline(iss, line) && taken < max_lines) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (taken > 0 && line.find_first_not_of(" \t") == std::string::npos) {
      break;
    }
    if (taken) {
      out << '\n';
    }
    out << trim_hint(line, 120);
    ++taken;
  }
  return out.str();
}

}  // namespace

std::string format_entry_hints_line(const RepoMapEntry& e) {
  std::ostringstream why;
  why << "why:";
  bool any = false;
  auto add = [&](const std::string& part) {
    if (part.empty()) {
      return;
    }
    why << (any ? " · " : " ") << part;
    any = true;
  };

  {
    std::string sc = "base=" + std::to_string(e.score_base);
    char buf[32];
    if (e.sig_cos >= 0.0f) {
      std::snprintf(buf, sizeof(buf), " sig=%.2f", static_cast<double>(e.sig_cos));
      sc += buf;
    }
    if (e.body_cos >= 0.0f) {
      std::snprintf(buf, sizeof(buf), " body=%.2f", static_cast<double>(e.body_cos));
      sc += buf;
    }
    add(sc);
  }
  if (!e.stem.empty()) {
    std::string s = "stem=" + e.stem;
    if (e.stem_sem_rank > 0) {
      s += "#" + std::to_string(e.stem_sem_rank);
    }
    if (e.dup_stem) {
      s += " dup_stem";
    }
    add(s);
  }
  if (e.file_rank > 0 && e.file_count > 0) {
    add("file_rank=" + std::to_string(e.file_rank) + "/" + std::to_string(e.file_count));
  }
  if (e.refs_in > 0) {
    add("refs≈" + std::to_string(e.refs_in));
  }
  if (!e.related_names.empty()) {
    std::string r = "related=";
    for (std::size_t i = 0; i < e.related_names.size(); ++i) {
      if (i) {
        r += ',';
      }
      r += e.related_names[i];
    }
    add(r);
  }
  if (!e.role_hint.empty()) {
    add("role=" + e.role_hint);
  }
  if (!any) {
    return {};
  }
  return why.str();
}

namespace {

std::string ascii_lower_hint(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool query_has_any(const std::string& q, std::initializer_list<const char*> words) {
  for (const char* w : words) {
    if (q.find(w) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string path_stem_hint(const std::string& file) {
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

bool ends_with_ci(const std::string& s, const char* suf) {
  const std::string lower = ascii_lower_hint(s);
  const std::string suffix = ascii_lower_hint(suf);
  return lower.size() >= suffix.size() &&
         lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

void apply_ranked_map_priors(const std::string& query, std::vector<RepoMapEntry>* entries,
                             CodingStemEmbedIndex* stem_index, EmbeddingBackend* embed) {
  if (entries == nullptr || entries->empty()) {
    return;
  }
  const std::string q = ascii_lower_hint(query);
  const bool wants_ui_wake =
      query_has_any(q, {"wake", "repaint", "actualizar", "refresh", "dirty", "invalidat"}) &&
      query_has_any(q, {"ui", "panel", "console", "terminal", "pty", "texto", "salida", "output",
                        "stream", "saca"});
  const bool wants_pty_out =
      query_has_any(q, {"pty", "terminal"}) &&
      query_has_any(q, {"texto", "salida", "output", "saca", "bytes", "stream", "repaint", "wake",
                        "gestion", "gestión"});
  const bool mentions_ai =
      query_has_any(q, {" ai", "ia ", "agent", "llm", "nivel 1", "level1", "embed"});

  std::unordered_map<std::string, int> stem_sem_rank;
  if (stem_index != nullptr && stem_index->ready() && embed != nullptr && embed->ready() &&
      !query.empty()) {
    std::vector<float> qvec;
    std::string err;
    if (stem_index->embed_query_vec(query, embed, {}, &qvec, &err) && !qvec.empty()) {
      const auto top = stem_index->top_k(qvec, 16);
      for (std::size_t i = 0; i < top.size(); ++i) {
        stem_sem_rank[top[i].first] = static_cast<int>(i + 1);
      }
    }
  }

  // Prefer .cpp definition over .hpp declaration for the same symbol name.
  std::unordered_map<std::string, int> name_cpp_count;
  std::unordered_map<std::string, int> name_hpp_count;
  for (const auto& e : *entries) {
    if (e.name.empty()) {
      continue;
    }
    if (ends_with_ci(e.file, ".cpp") || ends_with_ci(e.file, ".cc") || ends_with_ci(e.file, ".cxx")) {
      ++name_cpp_count[e.name];
    } else if (ends_with_ci(e.file, ".hpp") || ends_with_ci(e.file, ".h") ||
               ends_with_ci(e.file, ".hh")) {
      ++name_hpp_count[e.name];
    }
  }

  for (auto& e : *entries) {
    const std::string name_l = ascii_lower_hint(e.name);
    const std::string file_l = ascii_lower_hint(e.file);
    const std::string stem = path_stem_hint(e.file);
    int delta = 0;
    std::string role;

    const bool is_tool_script =
        (file_l.find("tools/") != std::string::npos || file_l.find("/tools/") != std::string::npos) &&
        (ends_with_ci(e.file, ".sh") || ends_with_ci(e.file, ".py") || name_l.find("check_") == 0);
    const bool is_fd_wake =
        name_l.find("wake_fd") != std::string::npos || name_l.find("drain_wake") != std::string::npos ||
        name_l.find("signal_reader_wake") != std::string::npos ||
        name_l.find("ensure_wake_fd") != std::string::npos ||
        (name_l.find("wake") != std::string::npos && file_l.find("session") != std::string::npos &&
         (name_l.find("_fd") != std::string::npos || name_l.find("reader") != std::string::npos));
    const bool is_host_nudge =
        name_l.find("nudge_terminal_repaint") != std::string::npos ||
        (name_l.find("nudge") != std::string::npos && name_l.find("repaint") != std::string::npos);
    const bool is_input_dir =
        name_l.find("event_to_pty") != std::string::npos || name_l.find("pty_input") != std::string::npos ||
        name_l.find("looks_like_terminal_mouse") != std::string::npos ||
        (name_l.find("input_active") != std::string::npos && name_l.find("filter") == std::string::npos);
    const bool is_ai_wake =
        (name_l == "wake" || name_l.rfind("::wake") != std::string::npos) &&
        (file_l.find("ai_controller") != std::string::npos || file_l.find("/ai/") != std::string::npos);
    const bool is_bridge =
        name_l.find("request_terminal_repaint") != std::string::npos ||
        name_l.find("on_pty_output") != std::string::npos ||
        name_l.find("tick_terminal_shell") != std::string::npos;
    const bool is_ui_wake =
        name_l.find("wake_console") != std::string::npos || name_l == "ui_wake" ||
        name_l.find("wake_console_panel") != std::string::npos ||
        name_l.find("emit_terminal") != std::string::npos ||
        name_l.find("ui_wake_correlated") != std::string::npos;
    const bool is_pty_out =
        name_l.find("on_pty_bytes") != std::string::npos ||
        name_l.find("feed_pty_bytes") != std::string::npos ||
        name_l.find("pty_output") != std::string::npos;

    if (is_tool_script) {
      delta -= 2'500'000;
      role = "tool";
    } else if (is_fd_wake && wants_ui_wake) {
      delta -= 2'000'000;
      role = "fd-wake";
    } else if (is_host_nudge && (wants_ui_wake || wants_pty_out)) {
      delta -= 1'800'000;
      role = "host-nudge";
    } else if (is_ai_wake && !mentions_ai) {
      delta -= 1'600'000;
      role = "ai-wake";
    } else if (is_input_dir && wants_pty_out) {
      delta -= 1'200'000;
      role = "input";
    } else if (is_bridge) {
      delta += 1'500'000;
      role = "bridge";
    } else if (is_ui_wake && (wants_ui_wake || wants_pty_out)) {
      delta += 1'200'000;
      role = "ui-wake";
    } else if (is_pty_out && wants_pty_out) {
      delta += 1'200'000;
      role = "pty-out";
    }

    if (!name_l.empty() && name_hpp_count[e.name] > 0 && name_cpp_count[e.name] > 0) {
      if (ends_with_ci(e.file, ".hpp") || ends_with_ci(e.file, ".h") || ends_with_ci(e.file, ".hh")) {
        delta -= 80'000;
      } else if (ends_with_ci(e.file, ".cpp") || ends_with_ci(e.file, ".cc")) {
        delta += 120'000;
      }
    }

    if (!stem.empty()) {
      auto it = stem_sem_rank.find(stem);
      if (it != stem_sem_rank.end()) {
        // Higher boost for better stem ranks (#1 → +900k … #16 → +50k).
        delta += 950'000 - (it->second - 1) * 60'000;
        e.stem_sem_rank = it->second;
        if (role.empty()) {
          role = "stem-hit";
        }
      }
    }

    e.score += delta;
    if (!role.empty()) {
      e.role_hint = role;
    }
  }

  std::stable_sort(entries->begin(), entries->end(),
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

void enrich_ranked_map_hints(std::vector<RepoMapEntry>* entries, const std::string& workspace_root,
                             const std::string& query, const SymbolIndexSnapshot* snapshot,
                             CodingStemEmbedIndex* stem_index, EmbeddingBackend* embed,
                             const std::vector<std::string>* body_texts,
                             std::size_t snippet_top_n) {
  if (entries == nullptr || entries->empty()) {
    return;
  }

  std::unordered_map<std::string, int> ref_count;
  std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> refs_by_file;
  if (snapshot != nullptr) {
    for (const auto& r : snapshot->refs) {
      if (r.name.empty()) {
        continue;
      }
      ref_count[r.name] += std::max(1, r.count);
      if (!r.file.empty()) {
        refs_by_file[r.file].push_back({r.name, std::max(1, r.count)});
      }
    }
  }

  std::unordered_map<std::string, int> stem_sem;
  if (stem_index != nullptr && stem_index->ready() && embed != nullptr && embed->ready() &&
      !query.empty()) {
    std::vector<float> qvec;
    std::string err;
    if (stem_index->embed_query_vec(query, embed, {}, &qvec, &err) && !qvec.empty()) {
      const auto top = stem_index->top_k(qvec, 16);
      for (std::size_t i = 0; i < top.size(); ++i) {
        stem_sem[top[i].first] = static_cast<int>(i + 1);
      }
    }
  }

  std::unordered_map<std::string, int> file_totals;
  std::unordered_map<std::string, int> file_seen;
  std::unordered_map<std::string, std::vector<std::string>> stem_files;
  for (const auto& e : *entries) {
    ++file_totals[e.file];
    const std::string st = file_stem_of(e.file);
    if (!st.empty()) {
      auto& paths = stem_files[st];
      bool found = false;
      for (const auto& p : paths) {
        if (p == e.file) {
          found = true;
          break;
        }
      }
      if (!found) {
        paths.push_back(e.file);
      }
    }
  }

  namespace fs = std::filesystem;
  std::unordered_map<std::string, FileLinesCache> file_cache;

  for (std::size_t idx = 0; idx < entries->size(); ++idx) {
    auto& e = (*entries)[idx];
    e.stem = file_stem_of(e.file);
    if (!e.stem.empty()) {
      auto it = stem_sem.find(e.stem);
      if (it != stem_sem.end()) {
        e.stem_sem_rank = it->second;
      }
      auto sf = stem_files.find(e.stem);
      e.dup_stem = sf != stem_files.end() && sf->second.size() > 1;
    }
    e.file_count = file_totals[e.file];
    e.file_rank = ++file_seen[e.file];
    if (!e.name.empty()) {
      auto rc = ref_count.find(e.name);
      if (rc != ref_count.end()) {
        e.refs_in = rc->second;
      }
    }
    e.related_names.clear();
    auto rf = refs_by_file.find(e.file);
    if (rf != refs_by_file.end()) {
      auto names = rf->second;
      std::stable_sort(names.begin(), names.end(),
                       [](const auto& a, const auto& b) { return a.second > b.second; });
      for (const auto& kv : names) {
        if (kv.first == e.name) {
          continue;
        }
        bool seen = false;
        for (const auto& n : e.related_names) {
          if (n == kv.first) {
            seen = true;
            break;
          }
        }
        if (seen) {
          continue;
        }
        e.related_names.push_back(kv.first);
        if (e.related_names.size() >= 3) {
          break;
        }
      }
    }

    const bool want_snippet = idx < snippet_top_n;
    if ((want_snippet || e.doc_line.empty()) && !workspace_root.empty() && !e.file.empty()) {
      fs::path abs = fs::path(e.file);
      if (!abs.is_absolute()) {
        abs = fs::path(workspace_root) / e.file;
      }
      const auto& fl = load_file_lines(&file_cache, abs.lexically_normal().string());
      if (e.doc_line.empty()) {
        e.doc_line = extract_doc_near(fl, e.line);
      }
      if (want_snippet && e.snippet.empty()) {
        if (body_texts != nullptr && idx < body_texts->size() && !(*body_texts)[idx].empty()) {
          e.snippet = snippet_from_body((*body_texts)[idx], 5);
        } else {
          e.snippet = extract_snippet(fl, e.line, 5);
        }
      }
    }
  }
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
    out << "  [" << kind_label_short(e.kind) << "]";
    out << "  [score=" << e.score << "]\n";
    out << "    ";
    if (!e.signature.empty()) {
      out << e.signature;
    } else {
      out << kind_label_short(e.kind) << ' ' << e.name;
    }
    out << '\n';
    const std::string why = format_entry_hints_line(e);
    if (!why.empty()) {
      out << "    " << why << '\n';
    }
    if (!e.doc_line.empty()) {
      out << "    doc: " << e.doc_line << '\n';
    }
    if (!e.snippet.empty() && shown <= 12) {
      std::istringstream sn(e.snippet);
      std::string sl;
      while (std::getline(sn, sl)) {
        out << "    | " << sl << '\n';
      }
    }
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
