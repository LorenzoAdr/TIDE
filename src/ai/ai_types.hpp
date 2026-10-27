#pragma once

#include <cctype>
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
  // `/init`: genera el borrador de AGENTS.md que el piloto lee en cada consulta.
  ProjectInit,
  // `/memory` | `/remember <x>` | `/forget <n>` — memoria durable del agente.
  Memory,
  // `/sessions` | `/resume [id]` — historial de investigaciones.
  Sessions,
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
  // 127.0.0.1 → tuide forks llama-server. Any other host → attach-only (GPU on another machine).
  std::string server_host = "127.0.0.1";
  int server_port = 18765;
  // Per-slot context. Short signature passages (~100 chars) fit in 128–256 tokens.
  // Total KV ≈ n_ctx with non-unified slots: each slot gets n_ctx / n_parallel.
  int n_ctx = 2048;
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

// Fase E — coder L2 (local GGUF via llama-server, or OpenAI-compatible remote).
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
  // Local llama-server (127.0.0.1). Distinct from L0 embeddings (18765).
  int server_port = 18766;
  // -1 = auto (99 if CUDA/Vulkan/HIP next to llama-server, else 0).
  int n_gpu_layers = -1;
  // 0 = auto (hardware_concurrency).
  int n_threads = 0;
};

// Qué sabe hacer el backend de chat. Determina qué se le manda (tools nativas,
// response_format json_schema, GBNF, stream_options) — mandar algo que el
// proveedor no entiende suele devolver 400, y no mandarlo cuando sí lo entiende
// es lo que hoy nos cuesta los reintentos por "JSON inválido".
struct AiProviderCaps {
  // true (default) = se infieren de api_base/api_model en cada resolución.
  // Poner cualquier campo en el config implica auto_detect=false.
  bool auto_detect = true;
  bool tool_calling = false;
  bool json_schema = false;
  bool grammar = false;  // GBNF en el body (familia llama.cpp)
  bool streaming = true;
  bool prompt_cache = false;
  bool parallel_tool_calls = false;
  // 0 = desconocido; el presupuesto real sale de level2.n_ctx / n_ctx_remote.
  int context_window = 0;
};

// Backend por rol del harness: lo que quede vacío/0 hereda de ai.level2.
// Permite el reparto que importa: explorador en un modelo barato (o local) y
// piloto/verificador en el fuerte, sin dos configuraciones paralelas.
struct AiRoleBackend {
  std::string api_base;
  std::string api_key;
  std::string api_model;
  int max_tokens = 0;
  int timeout_ms = 0;
  // Silencio tolerado a media respuesta. 0 = el del transporte (60 s). Se puede
  // subir para un proveedor que calle durante la fase de razonamiento.
  int stall_timeout_ms = 0;
};

struct AiModelRoles {
  AiRoleBackend pilot;
  AiRoleBackend explorer;
  AiRoleBackend verifier;
  AiRoleBackend edit;
};

// Tunables for the single-admin harness (src/ai/l2_admin.*). Defaults mirror the
// compile-time kAdminMax* constants in l2_admin.hpp — see that file for the
// safety rationale (fail-closed on verify, budgets renew per consulta).
struct AiHarnessSettings {
  int max_proposes = 12;         // piloto: turnos LLM por consulta (kAdminMaxProposes)
  int max_spawns = 8;            // piloto: spawns totales por consulta (kAdminMaxSpawns)
  int max_explores = 4;          // explorador: spawns por consulta (kAdminMaxExplores)
  int explore_max_steps = 6;     // explorador: pasos internos por spawn (kAdminExploreMaxSteps)
  bool verifier_enabled = true;  // false = editar/cerrar sin pase adversarial (riesgo)
  int max_verify_passes = 2;     // kAdminMaxVerifyPasses
  int verify_max_steps = 6;      // kAdminVerifyMaxSteps
  bool refuter_enabled = true;   // segundo pase adversarial dentro de la verificación
  bool allow_shell = true;       // permitir spawns AdminSpawnTipo::Shell
  bool allow_web = true;         // permitir spawns AdminSpawnTipo::Web / WebFetch
  bool allow_test = true;        // permitir spawns AdminSpawnTipo::Test
  // false (default) = un explore por fenómeno/polo, cada uno independiente (comportamiento
  // actual). true = un solo explorador puede cubrir una cadena de sub-preguntas CONECTADAS
  // en un mismo brief. Probado como experimento P16 (docs/plans/l2-admin-verify-round-
  // reduction.md sección 9): gana en cadenas secuenciales, pero diluir varios ángulos
  // independientes en un job puede disparar el atajo P2bis y saltarse el verificador —
  // por eso, cuando está activo, ese atajo concreto se desactiva (ver admin_run_verify).
  bool explorer_cumulative_mode = false;
  // Herramienta "trail" del explorador: reconstruye la cadena de llamadas (call-stack)
  // hacia un símbolo vía grep + heurísticas (sin LLM extra — reusa src/ai/l2_explore_a_trail.cpp,
  // el mismo motor determinista del explorador legacy de Level2Session). Default true: es
  // solo lectura, determinista, y estrictamente añade evidencia estructurada al grep/read
  // habituales, sin el riesgo de shell/web.
  bool allow_causal_trail = true;
  // Herramienta "dataflow" del explorador: dónde se declara/escribe/lee una variable o campo,
  // vía grep + heurísticas (sin LSP, sin LLM extra — reusa src/ai/l2_explore_a_dataflow.cpp,
  // el hermano de "trail" para estado en vez de llamadas). Default true, mismo motivo que trail.
  bool allow_dataflow_trace = true;
  // Herramienta "headers_of": includes/headers de un archivo (FS puro, ya registrada en el
  // ToolRegistry compartido — src/ai/tool_registry.cpp). Default true.
  bool allow_headers_of = true;
  // Herramienta "repo_map": vista del repo rankeada por PageRank vía SymbolWorkspaceIndexer
  // (ya registrada en el ToolRegistry compartido). Default true.
  bool allow_repo_map = true;
  // Cuántos patterns de grep / paths de read puede pedir el explorador EN UNA
  // SOLA ola (un turno LLM) — kAdminExploreMaxGrepPerWave/MaxReadPerWave.
  int max_grep_per_wave = 3;
  int max_read_per_wave = 2;
  // Tope ACUMULADO de greps/reads en todo el explore (varias olas) —
  // kAdminExploreMaxGrep/MaxRead. Si este total es bajo, subir el límite por
  // ola no sirve de mucho: sube ambos a la vez si quieres más margen real.
  int max_grep_total = 4;
  int max_read_total = 3;
};

struct AiSettings {
  bool enabled = true;
  // Single-admin chat path (replaces L0→L1→L2 hot path). Legacy code stays compiled.
  bool admin_enabled = true;
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
  AiHarnessSettings harness;
  AiProviderCaps caps;
  AiModelRoles models;
};

// ---------------------------------------------------------------- caps

// Tabla deliberadamente corta y documentada: solo lo que sabemos cierto del
// endpoint OpenAI-compatible. Un proveedor desconocido se trata como "solo
// streaming" — conservador, nunca manda campos que puedan provocar un 400.
// Para cualquier otro caso, el usuario fija las caps a mano en el config.
inline AiProviderCaps ai_infer_provider_caps(const std::string& api_base,
                                             const std::string& api_model) {
  (void)api_model;
  AiProviderCaps caps;
  caps.auto_detect = true;
  std::string host = api_base;
  for (char& c : host) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  const bool plain_http = host.rfind("http://", 0) == 0;
  auto has = [&host](std::string_view needle) {
    return host.find(needle) != std::string::npos;
  };
  if (plain_http) {
    // Sin TLS = servidor propio (llama-server, LM Studio, vLLM en LAN).
    // GBNF y cache_prompt son de la familia llama.cpp; tool-calling con modelos
    // pequeños es poco fiable, así que se deja apagado hasta que se active a mano.
    caps.grammar = true;
    caps.json_schema = true;
    caps.prompt_cache = true;
    caps.streaming = true;
    return caps;
  }
  if (has("api.deepseek.com")) {
    caps.tool_calling = true;
    caps.json_schema = true;
    caps.prompt_cache = true;  // caché de prefijo automática
    caps.streaming = true;
    return caps;
  }
  if (has("api.openai.com")) {
    caps.tool_calling = true;
    caps.json_schema = true;
    caps.prompt_cache = true;
    caps.parallel_tool_calls = true;
    caps.streaming = true;
    return caps;
  }
  if (has("openrouter.ai") || has("api.mistral.ai") || has("api.groq.com")) {
    caps.tool_calling = true;
    caps.json_schema = true;
    caps.streaming = true;
    return caps;
  }
  if (has("api.anthropic.com") || has("dashscope")) {
    // Capa de compatibilidad OpenAI: tools sí, json_schema no garantizado.
    caps.tool_calling = true;
    caps.streaming = true;
    return caps;
  }
  caps.streaming = true;
  return caps;
}

inline AiProviderCaps ai_resolve_provider_caps(const AiProviderCaps& configured,
                                               const std::string& api_base,
                                               const std::string& api_model) {
  if (!configured.auto_detect) {
    return configured;
  }
  AiProviderCaps caps = ai_infer_provider_caps(api_base, api_model);
  // El tamaño de contexto es dato del usuario, no inferible: si lo puso, gana.
  if (configured.context_window > 0) {
    caps.context_window = configured.context_window;
  }
  return caps;
}

// ---------------------------------------------------------------- roles

struct AiResolvedBackend {
  std::string api_base;
  std::string api_key;
  std::string api_model;
  AiProviderCaps caps;
  int max_tokens = 0;
  int timeout_ms = 0;
  int stall_timeout_ms = 0;
  std::string role;
  // true = el valor viene de ai.roles.<rol>.max_tokens, no heredado. Los sitios
  // de llamada fijan su propio max_tokens por fase, así que sin esta marca un
  // override por rol nunca ganaría.
  bool max_tokens_explicit = false;
};

inline const AiRoleBackend* ai_role_backend(const AiSettings& settings, std::string_view role) {
  if (role == "pilot") {
    return &settings.models.pilot;
  }
  if (role == "explorer") {
    return &settings.models.explorer;
  }
  if (role == "verifier" || role == "refuter") {
    return &settings.models.verifier;
  }
  if (role == "edit") {
    return &settings.models.edit;
  }
  return nullptr;
}

// Backend efectivo de un rol: override del rol si existe, con herencia de
// ai.level2 campo a campo, y caps recalculadas para el api_base que acabe
// usándose (un rol que apunta a un llama-server local no tiene las caps del
// remoto).
inline AiResolvedBackend ai_resolve_role_backend(const AiSettings& settings,
                                                 std::string_view role) {
  AiResolvedBackend out;
  out.role = std::string(role);
  out.api_base = settings.level2.api_base;
  out.api_key = settings.level2.api_key;
  out.api_model = settings.level2.api_model;
  out.max_tokens = settings.level2.max_tokens;
  out.timeout_ms = 0;
  const AiRoleBackend* override_role = ai_role_backend(settings, role);
  bool same_endpoint = true;
  if (override_role != nullptr) {
    if (!override_role->api_base.empty()) {
      same_endpoint = override_role->api_base == settings.level2.api_base;
      out.api_base = override_role->api_base;
    }
    if (!override_role->api_key.empty()) {
      out.api_key = override_role->api_key;
    }
    if (!override_role->api_model.empty()) {
      out.api_model = override_role->api_model;
    }
    if (override_role->max_tokens > 0) {
      out.max_tokens = override_role->max_tokens;
      out.max_tokens_explicit = true;
    }
    if (override_role->timeout_ms > 0) {
      out.timeout_ms = override_role->timeout_ms;
    }
    if (override_role->stall_timeout_ms > 0) {
      out.stall_timeout_ms = override_role->stall_timeout_ms;
    }
  }
  // Caps explícitas del config solo valen para el endpoint de ai.level2; si el
  // rol apunta a otro sitio, se vuelven a inferir.
  if (!settings.caps.auto_detect && same_endpoint) {
    out.caps = settings.caps;
  } else {
    out.caps = ai_resolve_provider_caps(settings.caps, out.api_base, out.api_model);
  }
  return out;
}

inline std::string ai_provider_caps_text(const AiProviderCaps& caps) {
  std::string out;
  auto add = [&out](const char* name, bool on) {
    if (!on) {
      return;
    }
    if (!out.empty()) {
      out += " ";
    }
    out += name;
  };
  add("tool_calling", caps.tool_calling);
  add("json_schema", caps.json_schema);
  add("grammar", caps.grammar);
  add("streaming", caps.streaming);
  add("prompt_cache", caps.prompt_cache);
  add("parallel_tools", caps.parallel_tool_calls);
  if (out.empty()) {
    out = "(ninguna)";
  }
  if (caps.context_window > 0) {
    out += " ctx=" + std::to_string(caps.context_window);
  }
  out += caps.auto_detect ? " [auto]" : " [config]";
  return out;
}

}  // namespace tuide
