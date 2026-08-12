#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ai/repo_map.hpp"
#include "symbols/symbol_kind.hpp"

namespace tuide {

class EmbeddingBackend;
struct SymbolIndexSnapshot;

struct CodingSymbolEmbedRow {
  std::string file;
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  std::string signature;
  std::string passage;
  std::vector<float> embedding;
};

// Full-workspace symbol signature embeddings (cached). Built in background after
// the symbol snapshot is ready so L1 can rank without lexical map caps.
class CodingSymbolEmbedIndex {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;
  using PercentFn = std::function<void(std::size_t done, std::size_t total)>;

  bool ready() const { return ready_; }
  std::size_t size() const { return rows_.size(); }
  const std::string& content_hash() const { return content_hash_; }

  // Build/refresh from snapshot. Caches under cache_dir/embed/coding_symbols/.
  // on_percent: done/total over passages (total=0 → indeterminate / preparing).
  bool ensure(const SymbolIndexSnapshot* snapshot, EmbeddingBackend* backend,
              const std::string& cache_dir, const std::string& model_id,
              const ProgressFn& on_progress, std::string* error,
              const PercentFn& on_percent = {});

  void set_rows_for_test(std::vector<CodingSymbolEmbedRow> rows);

  // Drop cached rows (e.g. workspace root changed).
  void invalidate();

  bool embed_query_vec(const std::string& query, EmbeddingBackend* backend,
                       std::vector<float>* out, std::string* error) const;

  // Top-K symbols by cosine → RepoMapEntry (score = lround(cos * 1e6)).
  std::vector<RepoMapEntry> top_entries(const std::vector<float>& query_embedding,
                                        std::size_t k) const;

 private:
  bool ready_ = false;
  std::string content_hash_;
  std::vector<CodingSymbolEmbedRow> rows_;
};

std::string coding_symbol_index_passage(const std::string& file, const std::string& name,
                                        const std::string& signature);

}  // namespace tuide
