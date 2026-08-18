#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ai/ai_types.hpp"

namespace tuide {

struct AiModelShard {
  std::string filename;
  std::string url;
  std::size_t approx_bytes = 0;
};

struct AiModelInfo {
  std::string id;
  std::string filename;
  std::string url;
  std::string license_note;
  std::size_t approx_bytes = 0;
  // Short UI label (e.g. "1.5B", "14B").
  std::string label;
  // Extra GGUF parts (llama.cpp multi-shard); primary is filename/url.
  std::vector<AiModelShard> extra_shards;
};

// Default L1: Qwen2.5-1.5B-Instruct Q4_K_M (~1 GB, Apache-2.0).
AiModelInfo default_l1_model();
// Catalog: 1.5B (default), 3B, 7B Instruct.
const std::vector<AiModelInfo>& l1_model_catalog();
std::optional<AiModelInfo> find_l1_model(const std::string& id);
AiModelInfo resolve_l1_model(const AiLevel1Settings& settings);

// Default L2 coder: Qwen2.5-Coder-7B-Instruct Q4_K_M (~4.7 GB, Apache-2.0).
AiModelInfo default_l2_model();
// Legacy small alternate (~1 GB) — still resolvable, not offered in download UI.
AiModelInfo default_l2_model_small();
// Catalog offered in UI: 7B (default), 14B, 32B Coder.
const std::vector<AiModelInfo>& l2_model_catalog();
std::optional<AiModelInfo> find_l2_model(const std::string& id);
AiModelInfo resolve_l2_model(const AiLevel2Settings& settings);

// L0 intent embeddings: nomic-embed-text-v1.5 Q4_K_M (~84 MB, Apache-2.0).
AiModelInfo default_intent_embed_model();

// Toolpack package id for a catalog model (e.g. ai-l1-3b / ai-l2-14b).
std::string ai_package_id_for_l1_model(const std::string& model_id);
std::string ai_package_id_for_l2_model(const std::string& model_id);

class ModelStore {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;

  explicit ModelStore(std::string cache_dir = {});

  const std::string& cache_dir() const { return cache_dir_; }
  static std::string default_cache_dir();

  std::string model_path(const AiModelInfo& info) const;
  std::string model_path_for_id(const std::string& id) const;
  bool has_model(const AiModelInfo& info) const;

  // Downloads GGUF if missing. Returns absolute path or empty + error.
  std::string ensure_model(const AiModelInfo& info, bool auto_download, const ProgressFn& on_progress,
                           std::string* error) const;

  // L2 coder GGUF under cache/l2/.
  std::string l2_model_path(const AiModelInfo& info) const;
  std::string l2_model_path_for_id(const std::string& id) const;
  bool has_l2_model(const AiModelInfo& info) const;
  std::string ensure_l2_model(const AiModelInfo& info, bool auto_download,
                              const ProgressFn& on_progress, std::string* error) const;

  // Intent embed GGUF under cache/embed/intent/.
  std::string intent_embed_model_path(const AiModelInfo& info) const;
  bool has_intent_embed_model(const AiModelInfo& info) const;
  std::string ensure_intent_embed_model(const AiModelInfo& info, bool auto_download,
                                        const ProgressFn& on_progress, std::string* error) const;

  // llama-cli runtime under cache/runtime/<bundle>/ (must keep .so next to the binary).
  std::string runtime_dir() const;
  std::string llama_bundle_dir() const;
  std::string llama_cli_path() const;
  std::string llama_server_path() const;
  bool has_llama_cli() const;
  bool has_llama_server() const;
  // True when cli is executable and (if under our cache) sibling libs exist.
  bool cli_runnable(const std::string& cli_path) const;
  bool server_runnable(const std::string& server_path) const;
  // Directory to prepend to LD_LIBRARY_PATH (empty if not needed).
  std::string library_dir_for_cli(const std::string& cli_path) const;
  std::string ensure_llama_cli(bool auto_download, const ProgressFn& on_progress,
                               std::string* error) const;

  // Resolve CLI: TUIDE_LLAMA_CLI → PATH → cache bundle.
  std::string resolve_llama_cli() const;
  // Resolve server: TUIDE_LLAMA_SERVER → PATH → cache bundle (same extract as CLI).
  std::string resolve_llama_server() const;

 private:
  bool prefer_vulkan_llama_bundle() const;
  std::string llama_bundle_dir_for(bool vulkan) const;
  std::string llama_cli_path_for(bool vulkan) const;
  std::string llama_server_path_for(bool vulkan) const;
  bool has_llama_bundle(bool vulkan) const;
  std::string ensure_llama_bundle(bool vulkan, bool auto_download, const ProgressFn& on_progress,
                                  std::string* error) const;

  std::string cache_dir_;
};

// expected_size>0 → progreso por tamaño del .partial; si no, mejor esfuerzo (curl bar / Content-Length).
// on_progress may receive "__pct__:N" (0..100) for UI busy-strip updates.
bool download_url_to_file(const std::string& url, const std::string& dest,
                          const ModelStore::ProgressFn& on_progress, std::string* error,
                          std::uint64_t expected_size = 0);

// Sync TUIDE_LLAMA_VULKAN env from workspace ai.llama_vulkan_bundle (Linux Vulkan bundle).
void apply_llama_bundle_preference(const AiSettings& settings);

}  // namespace tuide
