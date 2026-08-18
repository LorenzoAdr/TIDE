#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "symbols/symbol_kind.hpp"

namespace tuide {

class EmbeddingBackend;
struct SymbolIndexSnapshot;

struct CodingStemEmbedRow {
  std::string stem;
  std::string passage;
  std::vector<float> embedding;
};

// Passage composition variants for coding-stem embeddings (battery + production).
enum class StemPassageProfileId {
  Baseline = 0,
  TypeFirst,
  SigSnip,
  HdrDoc,
  ModuleBlurb,
  Rich480,
  Rich720,
  KitchenSink,
};

struct StemPassageSymbol {
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  std::string signature;
  int line = 0;
  std::string file;  // workspace-relative
};

struct StemPassageBuildInput {
  std::string stem;
  std::vector<std::string> paths;
  std::vector<StemPassageSymbol> symbols;
  std::string workspace_root;
};

StemPassageProfileId parse_stem_passage_profile(const std::string& name);
const char* stem_passage_profile_name(StemPassageProfileId id);
// Default production profile (may change after battery).
StemPassageProfileId default_stem_passage_profile();

// Workspace stem/file passages embedded once and cached for coding-pack recall.
class CodingStemEmbedIndex {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;
  // Optional test embedder (same contract as CodingEmbedFn).
  using EmbedFn = std::function<bool(bool is_query, const std::string& text, std::vector<float>* out)>;

  bool ready() const { return ready_; }
  std::size_t size() const { return rows_.size(); }
  StemPassageProfileId profile() const { return profile_; }
  const std::string& content_hash() const { return content_hash_; }
  const std::vector<CodingStemEmbedRow>& rows() const { return rows_; }

  // Build/refresh from snapshot symbols. Caches under cache_dir/embed/coding_stems/.
  // Profile id is part of the content hash so variants never share cache rows.
  bool ensure(const SymbolIndexSnapshot* snapshot, EmbeddingBackend* backend,
              const std::string& cache_dir, const std::string& model_id,
              const ProgressFn& on_progress, std::string* error,
              StemPassageProfileId profile = StemPassageProfileId::Baseline);

  // Build passages only (no embed/cache). Useful for cost stats and unit tests.
  static std::vector<CodingStemEmbedRow> build_passages(const SymbolIndexSnapshot* snapshot,
                                                        StemPassageProfileId profile);

  // Precomputed rows for unit tests (no backend/cache).
  void set_rows_for_test(std::vector<CodingStemEmbedRow> rows);

  void invalidate();

  float cosine_for_stem(const std::vector<float>& query_embedding, const std::string& stem) const;
  const std::string* passage_for(const std::string& stem) const;

  // Top-K stems by cosine (descending).
  std::vector<std::pair<std::string, float>> top_k(const std::vector<float>& query_embedding,
                                                   std::size_t k) const;

  // Embed query via backend or test hook.
  bool embed_query_vec(const std::string& query, EmbeddingBackend* backend, const EmbedFn& test_embed,
                       std::vector<float>* out, std::string* error) const;

 private:
  void set_rows_unlocked(std::vector<CodingStemEmbedRow> rows, const std::string& content_hash,
                         StemPassageProfileId profile);

  bool ready_ = false;
  StemPassageProfileId profile_ = StemPassageProfileId::Baseline;
  std::string content_hash_;
  std::vector<CodingStemEmbedRow> rows_;
  std::unordered_map<std::string, std::size_t> by_stem_;
  mutable std::mutex mu_;
};

// Legacy/simple builder (baseline shape: stem + paths + bare names, cap 480).
std::string build_coding_stem_index_passage(const std::string& stem,
                                            const std::vector<std::string>& paths,
                                            const std::vector<std::string>& names);

// Profile-aware builder (classes, sigs, twin-header docs, module blurb, …).
std::string build_coding_stem_index_passage(const StemPassageBuildInput& in,
                                            StemPassageProfileId profile);

}  // namespace tuide
