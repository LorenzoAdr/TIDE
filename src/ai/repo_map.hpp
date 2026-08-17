#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ai/coding_embed_rerank.hpp"
#include "symbols/symbol_kind.hpp"

namespace tuide {

struct SymbolIndexSnapshot;
class EmbeddingBackend;
class CodingStemEmbedIndex;

struct RepoMapEntry {
  std::string file;
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  int score = 0;  // scaled rank (PageRank * 1e6 + lexical + embed boosts)
  std::string signature;

  // L2 selection hints (optional; filled by enrich / two-stage rerank).
  int score_base = 0;       // score before embed boosts
  float sig_cos = -1.0f;    // signature cosine; <0 = n/a
  float body_cos = -1.0f;   // body cosine; <0 = n/a
  std::string stem;         // basename stem of file
  int stem_sem_rank = 0;    // 1-based rank in stem-index top-K; 0 = n/a
  std::string doc_line;     // nearby // or /// comment
  int refs_in = 0;          // incoming IndexedRef count for name
  std::vector<std::string> related_names;  // up to 3 co-file ref names
  int file_rank = 0;        // 1-based among entries sharing this file
  int file_count = 0;       // how many entries from this file in the list
  bool dup_stem = false;    // same stem appears under >1 path in the list
  std::string snippet;      // 3–5 source lines (top-N only)
  std::string role_hint;    // pty-out | ui-wake | bridge | fd-wake | input | host-nudge | tool | …
};

struct RepoMapOptions {
  std::string query;
  // Extra ranking needles (e.g. L1-proposed identifiers). Merged into query tokens
  // after stopword filtering — project-agnostic; no domain synonym tables.
  std::vector<std::string> extra_needles;
  std::string active_file;                 // relative when possible
  std::vector<std::string> chat_files;     // open tabs / chat context (relative)
  std::size_t max_symbols = 64;
  std::size_t max_files = 24;
  std::size_t max_chars = 3200;
  int max_map_tokens = 1024;               // Aider-style budget; 0 = use max_chars only
  bool prefer_git_tracked = true;          // drop symbols not in git ls-files when available
  bool use_pagerank = true;
  // Empty = unrestricted. Relative directory prefixes (recursive).
  std::vector<std::string> path_scope;
};

struct RepoMap {
  std::vector<RepoMapEntry> entries;
  int best_score = 0;
  std::string note;
  bool used_pagerank = false;
  // Locked by enrich_dominant_stem_from_snapshot (coding pack / context dump).
  // When set, ranked_coding_entries / outline prefer this basename stem.
  std::string context_stem;

  // Optional semantic rerank / recall for coding pack (not owned).
  EmbeddingBackend* coding_embed = nullptr;
  CodingStemEmbedIndex* coding_stem_index = nullptr;
  std::function<bool(bool is_query, const std::string& text, std::vector<float>* out)>
      coding_embed_fn;
  bool embed_rerank_used = false;
  float embed_stem_cos = 0.0f;

  // Shortlist of stems for L1 to pick from (lexical + semantic fusion).
  std::vector<CodingStemShortlistItem> coding_stem_shortlist(const SymbolIndexSnapshot* snapshot,
                                                            const std::string& query,
                                                            std::size_t max_n = 8) const;

  std::string render_text() const;
  // Investigate: filters noisy specials (incl. bare class/struct decl lines), diversifies files.
  std::vector<RepoMapEntry> ranked_investigate_entries(std::size_t max_n = 16) const;
  // Context dump (orientation): facets + stem/file affinity + diversity.
  std::vector<RepoMapEntry> ranked_context_entries(std::size_t max_n = 10,
                                                   const std::string& query = {}) const;
  // Coding pack bodies: 1–2 type anchors + lifecycle/actionable funcs of dominant stem.
  std::vector<RepoMapEntry> ranked_coding_entries(std::size_t max_n = 14,
                                                 const std::string& query = {}) const;
  // Coding pack outline: firmas del stem dominante (sin cuerpos), dedup hpp/cpp.
  std::vector<RepoMapEntry> coding_outline_entries(std::size_t max_n = 20,
                                                  const std::string& query = {}) const;
  // Pull more symbols from the index that share the dominant file stem (hpp/cpp depth).
  // Returns how many entries were appended.
  int enrich_dominant_stem_from_snapshot(const SymbolIndexSnapshot* snapshot,
                                         const std::string& query,
                                         std::size_t max_extra = 32);
  // Ranked method/location list for investigate answers (no ripgrep).
  std::string format_investigate_answer(std::size_t max_n = 16) const;
  std::vector<std::string> suggested_needles(std::size_t max_n = 8) const;
};

std::vector<std::string> repo_map_query_tokens(const std::string& text, std::size_t max_n = 16);

// Optional: files tracked by git (empty if not a repo / git fails).
std::vector<std::string> repo_map_git_tracked_files(const std::string& workspace_root);

RepoMap build_repo_map(const SymbolIndexSnapshot* snapshot, const RepoMapOptions& opts);

}  // namespace tuide
