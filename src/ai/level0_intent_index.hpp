#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ai/ai_types.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/level0_router.hpp"

namespace tuide {

struct Level0IntentExample {
  std::string intent_id;
  bool is_task = false;
  std::string name;
  std::string arg_policy;
  std::string example;
  std::vector<float> embedding;
};

// Semantic intent index for L0 (human NL → tool/task). Uses EmbeddingBackend.
class Level0IntentIndex {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;

  Level0IntentIndex() = default;

  bool ready() const { return ready_; }
  const std::string& catalog_path() const { return catalog_path_; }

  // Load catalog JSON from path (or embedded default if path empty/missing).
  bool load_catalog(const std::string& path, std::string* error);

  // Embed all examples via backend; cache under cache_dir/embed/intent/.
  bool build(EmbeddingBackend* backend, const std::string& cache_dir, const std::string& model_id,
             const ProgressFn& on_progress, std::string* error);

  Level0IntentMatch match(const std::string& query, EmbeddingBackend* backend, float min_score,
                          float min_margin, std::string* error) const;

  // Test helper: install precomputed rows (no backend needed for match_precomputed).
  void set_examples_for_test(std::vector<Level0IntentExample> examples);
  Level0IntentMatch match_precomputed(const std::vector<float>& query_embedding, float min_score,
                                      float min_margin) const;

  static std::string default_catalog_json();
  static std::string resolve_default_catalog_path();

 private:
  bool ready_ = false;
  std::string catalog_path_;
  std::string catalog_hash_;
  std::vector<Level0IntentExample> examples_;
};

}  // namespace tuide
