#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "ai/ai_types.hpp"

namespace tuide {

struct AiModelInfo {
  std::string id;
  std::string filename;
  std::string url;
  std::string license_note;
  std::size_t approx_bytes = 0;
};

// Default L1 (D9): Qwen2.5-1.5B-Instruct Q4_K_M (~1 GB, Apache-2.0).
AiModelInfo default_l1_model();

// L0 intent embeddings: multilingual-e5-small Q8_0 (~126 MB, MIT).
AiModelInfo default_intent_embed_model();

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
