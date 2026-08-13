#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tuide {

class EmbeddingBackend;
struct SymbolIndexSnapshot;

struct CodingStemEmbedRow {
  std::string stem;
  std::string passage;
  std::vector<float> embedding;
};

// Workspace stem/file passages embedded once and cached for coding-pack recall.
class CodingStemEmbedIndex {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;
  // Optional test embedder (same contract as CodingEmbedFn).
  using EmbedFn = std::function<bool(bool is_query, const std::string& text, std::vector<float>* out)>;

  bool ready() const { return ready_; }
  std::size_t size() const { return rows_.size(); }

  // Build/refresh from snapshot symbols. Caches under cache_dir/embed/coding_stems/.
  bool ensure(const SymbolIndexSnapshot* snapshot, EmbeddingBackend* backend,
              const std::string& cache_dir, const std::string& model_id,
              const ProgressFn& on_progress, std::string* error);

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
  void set_rows_unlocked(std::vector<CodingStemEmbedRow> rows, const std::string& content_hash);

  bool ready_ = false;
  std::string content_hash_;
  std::vector<CodingStemEmbedRow> rows_;
  std::unordered_map<std::string, std::size_t> by_stem_;
  mutable std::mutex mu_;
};

// Build a stable passage for a stem from sample paths/names (used by index + tests).
std::string build_coding_stem_index_passage(const std::string& stem,
                                            const std::vector<std::string>& paths,
                                            const std::vector<std::string>& names);

}  // namespace tuide
