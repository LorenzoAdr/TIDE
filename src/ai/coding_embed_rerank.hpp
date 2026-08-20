#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tuide {

struct RepoMapEntry;
struct SymbolIndexSnapshot;
class EmbeddingBackend;
class CodingStemEmbedIndex;

// is_query=true → query embedding; false → passage. Returns false on failure.
using CodingEmbedFn =
    std::function<bool(bool is_query, const std::string& text, std::vector<float>* out)>;

struct CodingStemCandidate {
  std::string stem;
  int lexical_score = 0;
  int path_strong = 0;
  std::string passage;
  float semantic_cos = 0.0f;
};

struct CodingEmbedRerankResult {
  std::string stem;
  bool used_embed = false;
  float best_cosine = 0.0f;
};

std::string coding_stem_passage(const std::string& stem, const std::string& sample_path,
                                const std::vector<std::string>& sample_names);
std::string coding_entry_passage(const RepoMapEntry& e);

// Fuse lexical candidates with stem-index recall. Injects semantic top-K even if
// lexical score is weak; picks max(lexical + cosine * 4e6).
CodingEmbedRerankResult fuse_coding_stems(const std::string& query,
                                          std::vector<CodingStemCandidate> lexical_ranked,
                                          CodingStemEmbedIndex* stem_index,
                                          EmbeddingBackend* backend,
                                          const CodingEmbedFn& test_embed = {});

// Legacy shortlist-only rerank (kept for tests). Prefer fuse_coding_stems.
CodingEmbedRerankResult rerank_coding_stems(const std::string& query,
                                            const std::vector<CodingStemCandidate>& lexical_ranked,
                                            EmbeddingBackend* backend,
                                            const CodingEmbedFn& test_embed = {});

// Soft boost scores (cosine * 400, capped) for semantic tie-break within a stem.
// Returns true if any embedding was applied.
bool soft_boost_coding_entries(const std::string& query, std::vector<RepoMapEntry>* entries,
                               EmbeddingBackend* backend, const CodingEmbedFn& test_embed = {},
                               std::size_t max_n = 24);

struct CodingStemShortlistItem {
  std::string stem;
  std::string hint;  // path + sample symbols for the LLM
  int lexical_score = 0;
  float semantic_cos = 0.0f;
  long long fused_score = 0;
};

// Rank lexical candidates with stem-index fusion; return top max_n for L1 to pick from.
std::vector<CodingStemShortlistItem> build_fused_stem_shortlist(
    std::vector<CodingStemCandidate> lexical_ranked, const std::string& query,
    CodingStemEmbedIndex* stem_index, EmbeddingBackend* backend,
    const CodingEmbedFn& test_embed = {}, std::size_t max_n = 8);

// Exact / case-insensitive match against shortlist; empty if invalid.
std::string validate_coding_stem_pick(const std::string& raw,
                                      const std::vector<CodingStemShortlistItem>& shortlist);

// Build a single embed_query string: user prompt + L1 needles.
std::string enrich_query_for_embed(const std::string& query,
                                   const std::vector<std::string>& needles);

// Body semantic rerank: loose tokens only (no compound seeds).
// Caps to max_tokens (0 = uncapped) preferring longer, unique tokens.
std::string build_semantic_embed_query(const std::string& query,
                                       const std::vector<std::string>& semantic_tokens,
                                       std::size_t max_tokens = 12);

struct BodySemanticRerankOptions {
  std::string query;
  std::vector<std::string> semantic_tokens;
  std::string workspace_root;
  std::size_t body_pool = 40;
  std::size_t final_top = 120;
  int body_max_lines = 80;
  // Soft cap before embed backend truncate (raise with embed n_ctx).
  std::size_t body_max_embed_chars = 1200;
  // Max tokens appended to the embed query (noisy L1 lists dilute cosine).
  std::size_t max_semantic_tokens = 12;
  // Additive body_cos * scale on score_base (keeps lexical order among peers).
  int cos_boost_scale = 1000000;
  int max_per_file = 8;
  int max_per_stem = 2;
  int max_per_dir = 12;
};

struct BodySemanticRerankResult {
  std::vector<RepoMapEntry> entries;
  std::vector<std::string> body_texts;
  bool used_body_embed = false;
  std::size_t candidates_in = 0;
  std::size_t body_pool = 0;
  int64_t body_fetch_ms = 0;
  int64_t query_embed_ms = 0;
  int64_t body_embed_ms = 0;
  int64_t total_ms = 0;
  std::string note;
};

// Lexical order → top body_pool → embed bodies → hybrid: score_base + cos*boost.
BodySemanticRerankResult rerank_map_body_semantic(std::vector<RepoMapEntry> candidates,
                                                  const BodySemanticRerankOptions& opts,
                                                  EmbeddingBackend* backend,
                                                  const CodingEmbedFn& test_embed = {});

struct TwoStageRerankOptions {
  std::string query;
  std::vector<std::string> needles;
  std::string workspace_root;  // required for phase B body fetch
  // 0 = no truncate (embed the full candidate list).
  std::size_t phase_a_pool = 48;
  std::size_t phase_a_top = 16;
  std::size_t final_top = 12;
  bool fetch_bodies = true;
  int body_max_lines = 80;
  // <=0 disables per-file diversification (keep full list order).
  int max_per_file = 3;
  // <=0 disables per-stem diversification.
  int max_per_stem = 0;
  // <=0 disables per top-level directory diversification (e.g. src/ui, src/ai).
  int max_per_dir = 0;
  // Candidates already ranked (e.g. from CodingSymbolEmbedIndex); skip signature embed.
  bool skip_phase_a = false;
};

struct TwoStageRerankResult {
  std::vector<RepoMapEntry> entries;
  // Parallel to entries when phase B ran; empty string if fetch/embed failed.
  std::vector<std::string> body_texts;
  bool used_phase_a = false;
  bool used_phase_b = false;
  std::size_t candidates_in = 0;
  int64_t phase_a_ms = 0;
  int64_t phase_b_ms = 0;
  int64_t total_ms = 0;
  std::string note;
};

// Lexical candidates → signature embed (A) → body embed on top-N (B). Degrades to
// lexical order when embed backend is unavailable.
TwoStageRerankResult rerank_map_two_stage(std::vector<RepoMapEntry> candidates,
                                          const TwoStageRerankOptions& opts,
                                          EmbeddingBackend* backend,
                                          const CodingEmbedFn& test_embed = {});

// Fill medium/low-priority L2 hints (stem, doc, refs, diversity, snippet, …).
// snapshot/stem_index/embed optional; snippet_top_n caps expensive body/file reads.
void enrich_ranked_map_hints(std::vector<RepoMapEntry>* entries,
                             const std::string& workspace_root, const std::string& query,
                             const SymbolIndexSnapshot* snapshot = nullptr,
                             CodingStemEmbedIndex* stem_index = nullptr,
                             EmbeddingBackend* embed = nullptr,
                             const std::vector<std::string>* body_texts = nullptr,
                             std::size_t snippet_top_n = 40);

// Soft re-score for L2: demote wake_fd / host-nudge / input-direction / tools noise;
// boost PTY-output→UI bridge; optional stem-index recall. Re-sorts by score.
void apply_ranked_map_priors(const std::string& query, std::vector<RepoMapEntry>* entries,
                             CodingStemEmbedIndex* stem_index = nullptr,
                             EmbeddingBackend* embed = nullptr);

// Compact one-line "why" for map dump / transcript.
std::string format_entry_hints_line(const RepoMapEntry& e);

// Human-readable ranked list for investigate / L1 fallback.
std::string format_ranked_map_answer(const std::vector<RepoMapEntry>& entries,
                                     std::size_t max_n = 16, const std::string& note = {});

}  // namespace tuide
