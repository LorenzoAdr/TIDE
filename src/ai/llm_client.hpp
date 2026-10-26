#pragma once

// Transporte único hacia modelos OpenAI-compatibles (remoto o llama-server local).
//
// Sustituye al camino viejo de `RemoteL2Brain` (un `curl` por `popen` con fichero
// temporal, sin streaming, sin reintentos, sin `usage`). Lo que aporta:
//
//   · streaming SSE incremental (el panel AI ve tokens mientras el modelo piensa);
//   · reintentos con backoff + jitter solo en fallos recuperables, respetando
//     `Retry-After`, y NUNCA si ya se emitieron deltas (duplicaría texto en la UI);
//   · clasificación de error utilizable por el lazo (timeout ≠ 429 ≠ 400);
//   · `usage` real (incluye tokens de caché de prefijo) para contabilidad y traza;
//   · salida estructurada: `response_format: json_schema` o GBNF, según caps;
//   · tool-calling nativo (lo consume el loop v2; el contrato ya vive aquí).
//
// Dos transportes bajo la misma interfaz, elegidos por esquema de la URL:
//   http://  → socket crudo HTTP/1.1 con keep-alive y decodificación chunked
//              (llama-server en localhost o un host de LAN con GPU);
//   https:// → subproceso `curl` (fork/exec, mismo patrón que admin_run_shell_safe)
//              porque el binario no enlaza TLS y la historia portable depende de
//              no añadir OpenSSL como dependencia.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tuide {

// ---------------------------------------------------------------- mensajes

enum class LlmRole : std::uint8_t { System, User, Assistant, Tool };
const char* llm_role_name(LlmRole role);

struct LlmToolCall {
  std::string id;
  std::string name;
  std::string arguments;  // JSON crudo tal cual lo emitió el modelo (puede ser inválido)
};

struct LlmMessage {
  LlmRole role = LlmRole::User;
  std::string content;
  std::vector<LlmToolCall> tool_calls;  // solo Assistant
  std::string tool_call_id;             // solo Tool: a qué llamada responde
  std::string name;                     // solo Tool: nombre de la tool

  static LlmMessage system_msg(std::string text);
  static LlmMessage user_msg(std::string text);
  static LlmMessage assistant_msg(std::string text);
  static LlmMessage tool_msg(std::string call_id, std::string name, std::string text);
};

struct LlmToolDef {
  std::string name;
  std::string description;
  nlohmann::json parameters = nlohmann::json::object();  // JSON Schema de los args
};

// ------------------------------------------------------------ uso y caps

struct LlmUsage {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  // Tokens de prompt servidos desde caché de prefijo del proveedor
  // (`prompt_tokens_details.cached_tokens` en OpenAI, `prompt_cache_hit_tokens`
  // en DeepSeek). Es la métrica que dice si el reordenado del prompt sirvió.
  int cached_prompt_tokens = 0;
  int reasoning_tokens = 0;
  int calls = 0;

  int total() const { return prompt_tokens + completion_tokens; }
  bool empty() const { return prompt_tokens == 0 && completion_tokens == 0 && calls == 0; }
  void add(const LlmUsage& other);
};

struct LlmProviderCaps {
  bool tool_calling = false;
  bool json_schema = false;
  bool grammar = false;  // GBNF en el body (llama.cpp / llama-server)
  bool streaming = true;
  bool prompt_cache = false;
  bool parallel_tool_calls = false;
  int context_window = 0;  // 0 = desconocido
};

// ------------------------------------------------------------- errores

enum class LlmErrorKind : std::uint8_t {
  None = 0,
  Config,       // falta api_base/model: no se intentó nada
  Cancelled,    // el usuario canceló
  Timeout,      // deadline del rol agotado
  Network,      // DNS/connect/socket roto/curl no disponible
  RateLimit,    // 429
  ServerError,  // 5xx
  ClientError,  // 4xx no-auth (prompt mal formado, contexto excedido…)
  Auth,         // 401/403
  BadResponse   // 2xx pero cuerpo ilegible o stream cortado
};
const char* llm_error_kind_name(LlmErrorKind kind);
bool llm_error_is_retryable(LlmErrorKind kind);

// ------------------------------------------------------------- endpoint

struct LlmEndpoint {
  std::string api_base;  // http(s)://host[:port]/v1
  std::string api_key;
  std::string model;
  LlmProviderCaps caps;
  int timeout_ms = 300000;
  int connect_timeout_ms = 15000;
  int max_retries = 2;
};

struct LlmUrl {
  bool https = false;
  std::string host;
  int port = 80;
  std::string path;  // prefijo sin barra final ("/v1"); vacío si la URL no traía path
};
bool llm_parse_url(const std::string& url, LlmUrl* out, std::string* err);
// ¿Hay algo escuchando en ese api_base? Solo abre y cierra un socket (ni TLS ni
// HTTP), para validar al arrancar un endpoint por rol sin esperar a la primera
// llamada del agente.
bool llm_probe_endpoint(const std::string& api_base, int timeout_ms, std::string* err);

// ------------------------------------------------------------- streaming

struct LlmDelta {
  std::string text;       // trozo de content
  std::string reasoning;  // trozo de razonamiento (reasoning_content / reasoning)
  // >=0 cuando el trozo pertenece a una tool call en construcción.
  int tool_call_index = -1;
  std::string tool_call_name;
  std::string tool_call_arguments;
};
using LlmDeltaFn = std::function<void(const LlmDelta&)>;

// ------------------------------------------------------------- petición

struct LlmRequest {
  std::vector<LlmMessage> messages;
  std::vector<LlmToolDef> tools;
  std::string tool_choice;  // "" | "auto" | "none" | "required"
  std::string model;        // vacío = el del endpoint
  int max_tokens = 2048;
  float temperature = 0.1f;
  // Salida estructurada. `json_schema` solo se envía si caps.json_schema;
  // `grammar` solo si caps.grammar. Si el proveedor no soporta ninguna, el
  // llamante sigue siendo responsable de validar el texto.
  std::string json_schema_name = "response";
  nlohmann::json json_schema;  // vacío/null = sin response_format
  std::string grammar;         // GBNF literal
  // "solo exijo JSON bien formado, sin schema": equivalente remoto de
  // tools/l2_battery/grammars/l2_json.gbnf. Se traduce a
  // response_format:{type:json_object} cuando el proveedor lo soporta.
  bool json_object = false;
  std::optional<bool> enable_thinking;
  int reasoning_budget = -1;
  bool stream = true;
  std::string role = "agent";  // telemetría: pilot|explorer|verifier|edit
  int timeout_ms = 0;          // 0 = el del endpoint
  int max_retries = -1;        // <0 = el del endpoint
};

struct LlmResponse {
  bool ok = false;
  std::string content;
  std::string reasoning;
  std::vector<LlmToolCall> tool_calls;
  std::string finish_reason;  // stop|length|tool_calls|truncated|…
  LlmUsage usage;
  LlmErrorKind kind = LlmErrorKind::None;
  std::string error;
  int http_status = 0;
  int attempts = 0;
  long ms = 0;
  bool streamed = false;
  // true si se llamó al callback de deltas: el llamante ya mostró texto, así
  // que un reintento duplicaría. `LlmClient` lo usa para cortar el backoff.
  bool emitted = false;
};

// ------------------------------------------------------------- helpers puros

nlohmann::json llm_build_chat_body(const LlmEndpoint& endpoint, const LlmRequest& req, bool stream);
void llm_parse_usage(const nlohmann::json& usage, LlmUsage* out);
bool llm_parse_chat_json(const nlohmann::json& doc, LlmResponse* out, std::string* err);
bool llm_parse_chat_body(const std::string& body, LlmResponse* out, std::string* err);
// Extrae el mensaje de error de un cuerpo de error de proveedor ({"error":{...}}).
std::string llm_extract_error_message(const std::string& body);

// Acumulador de un stream SSE de chat.completions. Tolera que una línea llegue
// partida entre dos `feed()` (es lo normal con sockets y con `curl --no-buffer`).
class LlmStreamParser {
 public:
  explicit LlmStreamParser(LlmDeltaFn on_delta = {});

  void feed(const char* data, std::size_t n);
  void feed(const std::string& chunk) { feed(chunk.data(), chunk.size()); }
  // Cierra el stream: vuelca la línea pendiente si quedaba alguna.
  void finish();

  bool saw_done() const { return saw_done_; }
  bool emitted() const { return emitted_; }
  // Cuerpo acumulado cuando lo recibido NO parece SSE (p.ej. JSON de error de
  // un 4xx que llegó por el mismo canal).
  const std::string& raw_text() const { return raw_; }
  bool looks_like_sse() const { return saw_sse_field_; }

  LlmResponse take();

 private:
  void handle_line(std::string line);
  void handle_payload(const std::string& payload);
  LlmToolCall* tool_call_slot(int index);

  LlmDeltaFn on_delta_;
  std::string pending_;  // línea incompleta
  std::string raw_;
  LlmResponse out_;
  std::vector<int> tool_call_order_;  // índice del proveedor → posición en out_.tool_calls
  bool saw_done_ = false;
  bool emitted_ = false;
  bool saw_sse_field_ = false;
};

// ------------------------------------------------------------- cliente

class LlmClient {
 public:
  explicit LlmClient(LlmEndpoint endpoint);
  ~LlmClient();
  LlmClient(const LlmClient&) = delete;
  LlmClient& operator=(const LlmClient&) = delete;

  const LlmEndpoint& endpoint() const { return endpoint_; }
  void set_endpoint(LlmEndpoint endpoint);

  // Una llamada completa, con reintentos. `on_delta` vacío = sin streaming
  // aunque el endpoint lo soporte (ahorra el coste de trocear para llamadas
  // internas que nadie mira).
  LlmResponse chat(const LlmRequest& req, std::atomic<bool>* cancel = nullptr,
                   const LlmDeltaFn& on_delta = {});

  // Resumen legible de los intentos de la última llamada (para traza/UI).
  std::string last_attempts_text() const;

 private:
  LlmResponse attempt(const LlmRequest& req, const std::string& payload, bool stream,
                      std::atomic<bool>* cancel, const LlmDeltaFn& on_delta, int* retry_after_ms);

  LlmEndpoint endpoint_;
  mutable std::mutex mu_;
  int keep_fd_ = -1;  // socket reutilizado entre llamadas (solo transporte http://)
  // Mutex aparte: `chat()` retiene mu_ durante toda la petición (hasta 300 s
  // más backoff). Si el getter de diagnóstico compartiera ese mutex, leerlo
  // desde la UI congelaría el IDE mientras el modelo responde.
  mutable std::mutex attempts_mu_;
  std::string attempts_text_;
};

}  // namespace tuide
