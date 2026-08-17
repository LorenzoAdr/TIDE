#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tuide {

// L2 interaction workflow (orthogonal to level2_mode backend: dry_run|local|remote).
enum class AiWorkflowKind : uint8_t {
  Agent = 0,  // explore → pack → edit ↔ compile → done
  Ask = 1,    // explore → pack → synthesize → done
  Plan = 2,   // explore → pack → plan_doc (synthesize) → done
  Git = 3,    // git context (+ optional explore) → synthesize → done
};

inline const char* ai_workflow_kind_name(AiWorkflowKind kind) {
  switch (kind) {
    case AiWorkflowKind::Agent:
      return "agent";
    case AiWorkflowKind::Ask:
      return "ask";
    case AiWorkflowKind::Plan:
      return "plan";
    case AiWorkflowKind::Git:
      return "git";
  }
  return "agent";
}

inline AiWorkflowKind parse_ai_workflow_kind(std::string_view s) {
  if (s == "ask") {
    return AiWorkflowKind::Ask;
  }
  if (s == "plan") {
    return AiWorkflowKind::Plan;
  }
  if (s == "git") {
    return AiWorkflowKind::Git;
  }
  return AiWorkflowKind::Agent;
}

inline AiWorkflowKind cycle_ai_workflow_kind(AiWorkflowKind kind) {
  switch (kind) {
    case AiWorkflowKind::Agent:
      return AiWorkflowKind::Ask;
    case AiWorkflowKind::Ask:
      return AiWorkflowKind::Plan;
    case AiWorkflowKind::Plan:
      return AiWorkflowKind::Git;
    case AiWorkflowKind::Git:
      return AiWorkflowKind::Agent;
  }
  return AiWorkflowKind::Agent;
}

inline bool ai_workflow_allows_edit(AiWorkflowKind kind) {
  return kind == AiWorkflowKind::Agent;
}

inline bool ai_workflow_is_readonly(AiWorkflowKind kind) {
  return kind != AiWorkflowKind::Agent;
}

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
  Level2Harness,
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
  // Downloads only via Toolpacks / AI missing toast (never automatic).
  bool auto_download = false;
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
  // Downloads only via Toolpacks / AI missing toast (never automatic).
  bool auto_download = false;
};

// Fase E — coder L2 (local GGUF via llama-cli, or OpenAI-compatible remote).
struct AiLevel2Settings {
  // dry_run | harness | local | remote  (mirrors AiSettings::level2_mode; kept in sync on load)
  std::string model_id = "qwen2.5-coder-7b-instruct-q4_k_m";
  std::string model_path;  // empty → ModelStore cache/l2/
  std::string cli_path;    // empty → same runtime as L1
  // Remote (mode=remote): OpenAI-compatible chat completions.
  std::string api_base = "http://127.0.0.1:8080/v1";
  std::string api_key;     // or env TUIDE_L2_API_KEY
  std::string api_model = "qwen2.5-coder-7b-instruct";
  int max_steps = 32;
  int max_tokens = 2048;
  int n_ctx = 8192;
  // Effective context for remote prompt/pack scaling (local still uses n_ctx for llama -c).
  int n_ctx_remote = 32768;
  float temperature = 0.1f;
  bool auto_download = false;
  // Reject premature clarify this many times (force more get_code_of/tools) before accepting.
  int clarify_pushback_max = 3;
};

struct AiSettings {
  bool enabled = true;
  std::vector<std::string> command_whitelist = {"compile", "launch"};
  // Named tasks: name -> argv string (shell-tokenized).
  std::vector<std::pair<std::string, std::string>> tasks;
  std::string level2_mode = "dry_run";  // dry_run | harness | local | remote
  // agent | ask | plan | git — explicit L2 state machine (see AiWorkflowKind).
  std::string level2_workflow = "agent";
  // Relative (preferred) or absolute directory prefixes. Empty = unrestricted AI access.
  std::vector<std::string> path_scope;
  // Git workflow: how many recent commits to seed into the L2 session.
  int level2_git_log_n = 20;
  AiLevel2Settings level2;
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
