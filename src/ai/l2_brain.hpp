#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ai/ai_types.hpp"
#include "ai/llama_backend.hpp"

namespace tuide {

// Contabilidad de tokens de una llamada. Estructura plana a propósito: así
// l2_brain.hpp no arrastra llm_client.hpp a todos los targets de test que
// compilan l2_admin.cpp.
struct L2TokenUsage {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int cached_prompt_tokens = 0;  // servidos por caché de prefijo del proveedor
  int reasoning_tokens = 0;
  int calls = 0;
};

struct L2BrainRequest {
  std::string system_prompt;
  std::string user_prompt;
  std::string phase;
  int max_tokens = 2048;
  int n_ctx = 8192;
  float temperature = 0.1f;
  // Absolute path to GBNF grammar; empty → unconstrained decode.
  std::string grammar_file;
  std::optional<bool> enable_thinking;
  int reasoning_budget = -1;
  // Rol del harness: selecciona backend/modelo (ai.roles.*) y etiqueta la traza.
  // pilot | explorer | verifier | edit; vacío = el backend por defecto.
  std::string role;
  // Exige JSON bien formado aunque no haya GBNF: se traduce a
  // response_format:{type:json_object} si el proveedor lo soporta.
  bool require_json = false;
  // Streaming del texto del asistente. Vacío = sin streaming (el transporte
  // tampoco trocea). Se llama desde el hilo del agente, no desde el de UI.
  std::function<void(const std::string&)> on_text;
  std::function<void(const std::string&)> on_reasoning;
};

struct L2BrainResult {
  bool ok = false;
  std::string text;
  std::string raw;  // assistant message before JSON extract (includes thinking)
  std::string error;
  std::string backend;  // "local" | "remote" | "scripted"
  L2TokenUsage usage;
  int attempts = 0;
  long ms = 0;
  // Clasificación del fallo (llm_error_kind_name): timeout|rate_limit|auth|…
  std::string error_kind;
  // true si se emitió algún trozo por on_text (la UI ya mostró texto).
  bool streamed = false;
};

class L2Brain {
 public:
  virtual ~L2Brain() = default;
  virtual std::string name() const = 0;
  virtual bool ensure_ready(const AiSettings& settings,
                            const std::function<void(const std::string&)>& on_progress,
                            std::string* error) = 0;
  virtual L2BrainResult propose(const L2BrainRequest& req,
                                std::atomic<bool>* cancel = nullptr) = 0;
};

class LocalL2Brain : public L2Brain {
 public:
  explicit LocalL2Brain(LlamaBackend* backend = nullptr);
  std::string name() const override { return "local"; }
  bool ensure_ready(const AiSettings& settings,
                    const std::function<void(const std::string&)>& on_progress,
                    std::string* error) override;
  L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>* cancel) override;

 private:
  LlamaBackend owned_;
  LlamaBackend* backend_ = nullptr;
};

class LlmClient;  // ai/llm_client.hpp

class RemoteL2Brain : public L2Brain {
 public:
  RemoteL2Brain();
  ~RemoteL2Brain() override;
  std::string name() const override { return "remote"; }
  bool ensure_ready(const AiSettings& settings,
                    const std::function<void(const std::string&)>& on_progress,
                    std::string* error) override;
  L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>* cancel) override;

 private:
  AiLevel2Settings cfg_;
  // Settings completos: los overrides por rol viven en ai.roles.* (ver
  // ai_resolve_role_backend), no en level2.
  AiSettings all_;
  // Cliente cacheado por endpoint: mantiene el socket vivo entre las ~12
  // llamadas de una consulta cuando el backend es http:// (keep-alive).
  std::unique_ptr<LlmClient> client_;
  std::string client_key_;
};

// Factory: mode local → LocalL2Brain; remote → RemoteL2Brain; else nullptr.
std::unique_ptr<L2Brain> make_l2_brain(const std::string& mode, LlamaBackend* shared_backend);

}  // namespace tuide
