#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tuide {

enum class AiAuthor : uint8_t {
  Human = 0,
  Level1_AI = 1,
  Level2_AI = 2,
  Lsp = 3,
  System = 4,
};

inline bool ai_author_is_ai(AiAuthor author) {
  return author == AiAuthor::Level1_AI || author == AiAuthor::Level2_AI;
}

struct AiTextEdit {
  AiAuthor author = AiAuthor::Human;
  uint64_t op_id = 0;
  int start_line = 1;
  int start_col = 1;
  int end_line = 1;
  int end_col = 1;
  uint32_t new_len = 0;
  uint64_t timestamp_ms = 0;
};

struct AiToolCall {
  std::string name;
  std::string args_json;
};

enum class AiRouteKind {
  ResolveTool,
  ResolveTask,
  EscalateLevel1,
  ForceLevel1,
  CancelAgent,
  ModelStatus,
  ModelDownload,
  Trace,
  Help,
  Error,
};

struct AiRouteResult {
  AiRouteKind kind = AiRouteKind::EscalateLevel1;
  std::string tool_name;
  std::string task_name;
  std::string arg;
  std::string message;
  std::vector<std::string> seeds;
};

struct AiLevel0EmbedSettings {
  std::string model_id = "nomic-embed-text-v1.5-q4_k_m";
  std::string model_path;
  bool auto_download = true;
  int server_port = 18765;
  // Per-slot context. Short signature passages (~100 chars) fit in 128–256 tokens.
  // Total KV ≈ n_ctx with non-unified slots: each slot gets n_ctx / n_parallel.
  int n_ctx = 1024;
  // -1 = auto (99 if libggml-vulkan/CUDA/HIP next to llama-server, else 0).
  int n_gpu_layers = -1;
  // 0 = auto (hardware_concurrency).
  int n_threads = 0;
  int batch_size = 2048;   // llama -b
  int ubatch_size = 512;   // llama -ub
  // Server slots; >1 lets llama-server batch array inputs in parallel.
  int n_parallel = 8;
  // Texts per HTTP /v1/embeddings request (llama-server batches these in one forward pass).
  int http_batch = 64;
};

struct AiLevel0Settings {
  AiLevel0EmbedSettings embeddings;
  // Tuned for nomic-embed-text-v1.5 on short ES/EN IDE intents.
  float min_score = 0.65f;
  float min_margin = 0.02f;
};

struct AiLevel1Settings {
  // Model id key used by ModelStore (default Qwen2.5-1.5B Instruct Q4).
  std::string model_id = "qwen2.5-1.5b-instruct-q4_k_m";
  // Absolute path override; empty → resolve via ModelStore.
  std::string model_path;
  // Absolute path to llama-cli; empty → PATH / cache runtime.
  std::string cli_path;
  int max_steps = 8;
  int max_tokens = 512;
  int n_ctx = 4096;
  float temperature = 0.2f;
  bool auto_download = true;
};

struct AiSettings {
  bool enabled = true;
  std::vector<std::string> command_whitelist = {"compile", "launch"};
  // Named tasks: name -> argv string (shell-tokenized).
  std::vector<std::pair<std::string, std::string>> tasks;
  std::string level2_mode = "dry_run";
  std::string models_cache_dir;
  // Official AI level trace → <workspace>/.tuide/ai/trace.ndjson
  bool trace_enabled = true;
  std::string trace_path;  // empty → default under .tuide/ai/
  // Linux: download llama.cpp Vulkan release (libggml-vulkan.so). Off by default — CPU
  // bundle is faster for batched embeddings on many GPUs/drivers; enable for L1 GPU or to test.
  bool llama_vulkan_bundle = false;
  AiLevel0Settings level0;
  AiLevel1Settings level1;
};

}  // namespace tuide
