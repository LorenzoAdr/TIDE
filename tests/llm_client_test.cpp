// Test del transporte LLM sin red: parseo de URL, construcción del body,
// acumulador SSE (incluido el caso de línea partida entre dos trozos, que es lo
// normal con sockets) y clasificación/reintento de errores.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ai/ai_types.hpp"
#include "ai/llm_client.hpp"

using tuide::LlmDelta;
using tuide::LlmEndpoint;
using tuide::LlmErrorKind;
using tuide::LlmMessage;
using tuide::LlmRequest;
using tuide::LlmResponse;
using tuide::LlmStreamParser;
using tuide::LlmToolDef;
using tuide::LlmUrl;
using tuide::LlmUsage;

static int failures = 0;

static void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

// Servidor de un solo hilo sobre un puerto efímero de loopback. Hace falta red
// de verdad (no simulada) para probar los relojes del transporte: lo que se
// mide aquí es cuánto tarda en rendirse, y eso no se puede falsear.
class FakeServer {
 public:
  using Handler = std::function<void(int fd, int conn_no, const std::atomic<bool>&)>;

  explicit FakeServer(Handler h, int conns = 1) : handler_(std::move(h)) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // que el kernel elija
    ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listen_fd_, 4);
    sockaddr_in got{};
    socklen_t len = sizeof(got);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&got), &len);
    port_ = ::ntohs(got.sin_port);
    thread_ = std::thread([this, conns] {
      for (int i = 0; i < conns && !stop_.load(); ++i) {
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
          return;
        }
        handler_(fd, i, stop_);
        ::close(fd);
      }
    });
  }

  ~FakeServer() {
    stop_.store(true);
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::string api_base() const { return "http://127.0.0.1:" + std::to_string(port_) + "/v1"; }

 private:
  Handler handler_;
  int listen_fd_ = -1;
  int port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

// Lee la petición hasta el final de cabeceras y descarta el cuerpo: sin esto el
// cliente se bloquearía escribiendo en un socket que nadie vacía.
static void drain_request(int fd) {
  char buf[4096];
  for (int i = 0; i < 64; ++i) {
    const ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
    if (r <= 0 || static_cast<std::size_t>(r) < sizeof(buf)) {
      return;
    }
  }
}

// Se queda mudo pero mirando el flag, para que cerrar el servidor no cueste
// el sueño entero.
static void hang_until_stop(const std::atomic<bool>& stop) {
  for (int i = 0; i < 300 && !stop.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

static void send_all(int fd, const std::string& s) {
  std::size_t sent = 0;
  while (sent < s.size()) {
    const ssize_t w = ::send(fd, s.data() + sent, s.size() - sent, MSG_NOSIGNAL);
    if (w <= 0) {
      return;
    }
    sent += static_cast<std::size_t>(w);
  }
}

static long ms_since(const std::chrono::steady_clock::time_point& t0) {
  return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count());
}

static LlmEndpoint full_caps_endpoint() {
  LlmEndpoint ep;
  ep.api_base = "https://api.example.com/v1";
  ep.model = "m-1";
  ep.caps.tool_calling = true;
  ep.caps.json_schema = true;
  ep.caps.grammar = true;
  ep.caps.streaming = true;
  return ep;
}

int main() {
  // ------------------------------------------------------------ URL
  {
    LlmUrl u;
    std::string err;
    expect(tuide::llm_parse_url("https://api.deepseek.com/v1", &u, &err), "url https ok");
    expect(u.https && u.host == "api.deepseek.com" && u.port == 443 && u.path == "/v1",
           "url https campos");

    expect(tuide::llm_parse_url("http://127.0.0.1:8080/v1/", &u, &err), "url http puerto");
    expect(!u.https && u.host == "127.0.0.1" && u.port == 8080 && u.path == "/v1",
           "url http campos (barra final recortada)");

    expect(tuide::llm_parse_url("http://localhost", &u, &err), "url sin path");
    expect(u.port == 80 && u.path.empty(), "url sin path → puerto 80");

    expect(tuide::llm_parse_url("http://[::1]:18766/v1", &u, &err), "url ipv6");
    expect(u.host == "::1" && u.port == 18766, "url ipv6 campos");

    expect(!tuide::llm_parse_url("api.example.com/v1", &u, &err), "url sin esquema rechazada");
    expect(!tuide::llm_parse_url("", &u, &err), "url vacía rechazada");
  }

  // ------------------------------------------------------------ body
  {
    const LlmEndpoint ep = full_caps_endpoint();
    LlmRequest req;
    req.messages.push_back(LlmMessage::system_msg("sys"));
    req.messages.push_back(LlmMessage::user_msg("hola"));
    {
      LlmMessage asst = LlmMessage::assistant_msg("");
      tuide::LlmToolCall call;
      call.id = "c1";
      call.name = "grep";
      call.arguments = R"({"pattern":"X"})";
      asst.tool_calls.push_back(call);
      req.messages.push_back(asst);
    }
    req.messages.push_back(LlmMessage::tool_msg("c1", "grep", "3 hits"));
    LlmToolDef tool;
    tool.name = "grep";
    tool.description = "busca";
    tool.parameters = nlohmann::json{{"type", "object"}};
    req.tools.push_back(tool);
    req.tool_choice = "auto";
    req.json_schema = nlohmann::json{{"type", "object"}};
    req.grammar = "root ::= \"{\"";
    req.enable_thinking = false;
    req.reasoning_budget = 128;

    const auto body = tuide::llm_build_chat_body(ep, req, /*stream=*/true);
    expect(body["model"] == "m-1", "body model del endpoint");
    expect(body["messages"].size() == 4, "body nº mensajes");
    expect(body["messages"][2]["content"].is_null(), "assistant con tool_calls → content null");
    expect(body["messages"][2]["tool_calls"][0]["function"]["name"] == "grep", "tool_call name");
    expect(body["messages"][3]["tool_call_id"] == "c1", "tool msg con tool_call_id");
    expect(body.contains("tools") && body["tools"].size() == 1, "tools enviadas con caps");
    expect(body["tool_choice"] == "auto", "tool_choice");
    expect(body["response_format"]["type"] == "json_schema", "json_schema con caps");
    expect(!body.contains("grammar"), "json_schema gana a grammar (no se pisan)");
    expect(body["stream"] == true, "stream");
    expect(body["stream_options"]["include_usage"] == true, "include_usage para contabilidad");
    expect(body["chat_template_kwargs"]["enable_thinking"] == false, "enable_thinking");
    expect(body["thinking_budget_tokens"] == 128, "thinking_budget_tokens");
    expect(!body.contains("parallel_tool_calls"), "parallel_tool_calls solo si caps");
  }
  {
    // Sin caps: ni tools, ni json_schema, ni grammar, ni stream_options.
    LlmEndpoint ep;
    ep.api_base = "http://127.0.0.1:8080/v1";
    ep.model = "local";
    ep.caps = {};  // todo false salvo streaming (default true) — lo apagamos
    ep.caps.streaming = false;
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    LlmToolDef tool;
    tool.name = "grep";
    req.tools.push_back(tool);
    req.json_schema = nlohmann::json{{"type", "object"}};
    req.grammar = "root ::= \"{\"";
    const auto body = tuide::llm_build_chat_body(ep, req, /*stream=*/false);
    expect(!body.contains("tools"), "sin caps.tool_calling no se envían tools");
    expect(!body.contains("response_format"), "sin caps.json_schema no hay response_format");
    expect(!body.contains("grammar"), "sin caps.grammar no hay grammar");
    expect(!body.contains("stream"), "sin stream no hay campo stream");
  }
  {
    // GBNF cuando el proveedor solo soporta grammar (llama-server).
    LlmEndpoint ep;
    ep.api_base = "http://127.0.0.1:8080/v1";
    ep.model = "local";
    ep.caps.grammar = true;
    ep.caps.prompt_cache = true;
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    req.grammar = "root ::= \"{\"";
    req.json_schema = nlohmann::json{{"type", "object"}};
    const auto body = tuide::llm_build_chat_body(ep, req, /*stream=*/false);
    expect(body.contains("grammar"), "grammar cuando no hay json_schema");
    expect(body["cache_prompt"] == true, "cache_prompt con caps.prompt_cache");
  }
  {
    // json_object: el equivalente remoto de l2_json.gbnf ("solo JSON válido").
    LlmEndpoint ep;
    ep.api_base = "https://api.deepseek.com/v1";
    ep.model = "deepseek-chat";
    ep.caps.json_schema = true;
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    req.json_object = true;
    const auto body = tuide::llm_build_chat_body(ep, req, /*stream=*/false);
    expect(body["response_format"]["type"] == "json_object", "json_object con caps.json_schema");

    // Con grammar disponible gana el GBNF (más preciso que json_object).
    LlmEndpoint local;
    local.api_base = "http://127.0.0.1:8080/v1";
    local.model = "l";
    local.caps.grammar = true;
    local.caps.json_schema = true;
    LlmRequest req2 = req;
    req2.grammar = "root ::= object";
    const auto body2 = tuide::llm_build_chat_body(local, req2, /*stream=*/false);
    expect(body2.contains("grammar") && !body2.contains("response_format"),
           "grammar gana a json_object");

    // Sin caps no se manda ninguna de las dos.
    LlmEndpoint dumb;
    dumb.api_base = "https://unknown.example/v1";
    dumb.model = "m";
    const auto body3 = tuide::llm_build_chat_body(dumb, req, /*stream=*/false);
    expect(!body3.contains("response_format"), "json_object requiere caps");
  }
  {
    // Caps inferidas por api_base (ai_types.hpp): local vs DeepSeek vs desconocido.
    const auto local = tuide::ai_infer_provider_caps("http://127.0.0.1:18766/v1", "");
    expect(local.grammar && local.json_schema && local.prompt_cache && !local.tool_calling,
           "caps local: GBNF + json_schema + cache, sin tools");
    const auto ds = tuide::ai_infer_provider_caps("https://api.deepseek.com/v1", "deepseek-chat");
    expect(ds.tool_calling && ds.json_schema && ds.prompt_cache, "caps DeepSeek");
    const auto unknown = tuide::ai_infer_provider_caps("https://algo.raro.example/v1", "x");
    expect(unknown.streaming && !unknown.tool_calling && !unknown.json_schema,
           "proveedor desconocido: conservador");

    tuide::AiSettings s;
    s.level2.api_base = "https://api.deepseek.com/v1";
    s.level2.api_model = "deepseek-chat";
    s.level2.max_tokens = 2048;
    s.models.explorer.api_base = "http://127.0.0.1:18766/v1";
    s.models.explorer.api_model = "qwen-1.5b";
    s.models.explorer.max_tokens = 700;
    const auto pilot = tuide::ai_resolve_role_backend(s, "pilot");
    expect(pilot.api_model == "deepseek-chat" && pilot.caps.tool_calling,
           "rol pilot hereda de level2");
    const auto explorer = tuide::ai_resolve_role_backend(s, "explorer");
    expect(explorer.api_base == "http://127.0.0.1:18766/v1" && explorer.api_model == "qwen-1.5b",
           "rol explorer con override");
    expect(explorer.caps.grammar && !explorer.caps.tool_calling,
           "caps del rol se recalculan para SU api_base");
    expect(explorer.max_tokens == 700 && explorer.max_tokens_explicit,
           "max_tokens del rol, marcado como explícito");
    expect(pilot.max_tokens == 2048 && !pilot.max_tokens_explicit,
           "max_tokens heredado no es explícito");
    expect(tuide::ai_resolve_role_backend(s, "verifier").api_model == "deepseek-chat",
           "verifier hereda");
  }

  // ------------------------------------------------------------ SSE
  {
    std::vector<std::string> seen;
    LlmStreamParser p([&seen](const LlmDelta& d) {
      if (!d.text.empty()) {
        seen.push_back(d.text);
      }
    });
    // Línea partida a propósito en mitad del JSON y del prefijo "data:".
    const std::string s1 = "data: {\"choices\":[{\"delta\":{\"content\":\"Hol";
    const std::string s2 = "a \"}}]}\n\nda";
    const std::string s3 = "ta: {\"choices\":[{\"delta\":{\"content\":\"mundo\"}}]}\n\n";
    const std::string s4 =
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}],"
        "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":4,"
        "\"prompt_tokens_details\":{\"cached_tokens\":8}}}\n\ndata: [DONE]\n\n";
    p.feed(s1);
    p.feed(s2);
    p.feed(s3);
    p.feed(s4);
    p.finish();
    expect(p.saw_done(), "SSE vio [DONE]");
    expect(p.emitted(), "SSE emitió deltas");
    expect(seen.size() == 2 && seen[0] == "Hola " && seen[1] == "mundo",
           "SSE deltas en orden con línea partida");
    const LlmResponse r = p.take();
    expect(r.content == "Hola mundo", "SSE content acumulado: '" + r.content + "'");
    expect(r.finish_reason == "stop", "SSE finish_reason");
    expect(r.usage.prompt_tokens == 10 && r.usage.completion_tokens == 4, "SSE usage");
    expect(r.usage.cached_prompt_tokens == 8, "SSE cached_tokens");
    expect(r.usage.calls == 1, "SSE usage.calls");
  }
  {
    // tool_calls troceadas: nombre en un chunk, argumentos en varios, por índice.
    LlmStreamParser p;
    p.feed(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c1\","
        "\"function\":{\"name\":\"grep\",\"arguments\":\"{\\\"pat\"}}]}}]}\n\n");
    p.feed(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"tern\\\":\\\"X\\\"}\"}}]}}]}\n\n");
    p.feed(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,\"id\":\"c2\","
        "\"function\":{\"name\":\"read\",\"arguments\":\"{}\"}}]}}]}\n\n");
    p.feed("data: {\"choices\":[{\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n");
    p.finish();
    const LlmResponse r = p.take();
    expect(r.tool_calls.size() == 2, "SSE 2 tool calls, got " + std::to_string(r.tool_calls.size()));
    if (r.tool_calls.size() == 2) {
      expect(r.tool_calls[0].id == "c1" && r.tool_calls[0].name == "grep", "tool call 0 ident");
      expect(r.tool_calls[0].arguments == R"({"pattern":"X"})",
             "tool call 0 args reensamblados: " + r.tool_calls[0].arguments);
      expect(r.tool_calls[1].name == "read", "tool call 1 por índice distinto");
    }
    expect(r.finish_reason == "tool_calls", "SSE finish_reason tool_calls");
  }
  {
    // reasoning_content separado del content.
    LlmStreamParser p;
    p.feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"pienso\"}}]}\n\n");
    p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"digo\"}}]}\n\n");
    p.feed("data: [DONE]\n\n");
    p.finish();
    const LlmResponse r = p.take();
    expect(r.reasoning == "pienso" && r.content == "digo", "reasoning separado de content");
  }
  {
    // Cuerpo que no es SSE (el proveedor ignoró stream): se detecta.
    LlmStreamParser p;
    const std::string whole =
        R"({"choices":[{"message":{"content":"entero"},"finish_reason":"stop"}]})";
    p.feed(whole);
    p.finish();
    expect(!p.looks_like_sse(), "cuerpo no-SSE detectado");
    expect(p.raw_text() == whole, "raw_text conserva el cuerpo");
    LlmResponse parsed;
    std::string err;
    expect(tuide::llm_parse_chat_body(p.raw_text(), &parsed, &err), "fallback a parseo entero");
    expect(parsed.content == "entero", "fallback content");
  }
  {
    // Solo reasoning, content vacío: es una respuesta válida (el modelo gastó
    // los tokens pensando, o metió el JSON en el bloque de razonamiento).
    LlmStreamParser p;
    p.feed("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"{\\\"do\\\":\\\"cerrar\\\"}\"}}]}\n\n");
    p.feed("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
    p.finish();
    const LlmResponse r = p.take();
    expect(r.content.empty() && !r.reasoning.empty(), "solo reasoning: content vacío");
    expect(r.finish_reason == "stop", "solo reasoning: finish_reason llega");
  }
  {
    // Stream cortado sin [DONE] ni finish_reason.
    LlmStreamParser p;
    p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"a medi\"}}]}\n\n");
    p.finish();
    expect(!p.saw_done(), "stream cortado no vio DONE");
    const LlmResponse r = p.take();
    expect(r.content == "a medi" && r.finish_reason.empty(), "parcial conservado");
  }

  // ------------------------------------------------- respuesta no-streaming
  {
    LlmResponse r;
    std::string err;
    const std::string body = R"({
      "choices":[{"message":{"content":[{"type":"text","text":"por "},
                                        {"type":"text","text":"partes"}],
                             "tool_calls":[{"id":"c9","function":{"name":"edit","arguments":"{}"}}]},
                  "finish_reason":"tool_calls"}],
      "usage":{"prompt_tokens":100,"completion_tokens":20,"prompt_cache_hit_tokens":64,
               "completion_tokens_details":{"reasoning_tokens":7}}})";
    expect(tuide::llm_parse_chat_body(body, &r, &err), "parseo no-stream ok: " + err);
    expect(r.content == "por partes", "content como array de partes");
    expect(r.tool_calls.size() == 1 && r.tool_calls[0].name == "edit", "tool_calls no-stream");
    expect(r.usage.cached_prompt_tokens == 64, "cached DeepSeek (prompt_cache_hit_tokens)");
    expect(r.usage.reasoning_tokens == 7, "reasoning_tokens");
  }
  {
    LlmResponse r;
    std::string err;
    expect(!tuide::llm_parse_chat_body(R"({"error":{"message":"contexto excedido"}})", &r, &err),
           "cuerpo de error no es ok");
    expect(err == "contexto excedido", "mensaje de error extraído: " + err);
    expect(tuide::llm_extract_error_message(R"({"error":{"message":"boom"}})") == "boom",
           "llm_extract_error_message objeto");
    expect(tuide::llm_extract_error_message("<html>502</html>").find("502") != std::string::npos,
           "llm_extract_error_message cuerpo no-JSON");
  }
  {
    LlmResponse r;
    std::string err;
    expect(!tuide::llm_parse_chat_body("no soy json", &r, &err), "cuerpo ilegible");
    expect(!tuide::llm_parse_chat_body(R"({"choices":[]})", &r, &err), "choices vacío");
  }

  // ------------------------------------------------- clasificación de error
  {
    expect(tuide::llm_error_is_retryable(LlmErrorKind::Timeout), "timeout reintentable");
    expect(tuide::llm_error_is_retryable(LlmErrorKind::RateLimit), "429 reintentable");
    expect(tuide::llm_error_is_retryable(LlmErrorKind::ServerError), "5xx reintentable");
    expect(tuide::llm_error_is_retryable(LlmErrorKind::Network), "red reintentable");
    expect(tuide::llm_error_is_retryable(LlmErrorKind::BadResponse), "cuerpo roto reintentable");
    expect(!tuide::llm_error_is_retryable(LlmErrorKind::Auth), "auth NO reintentable");
    expect(!tuide::llm_error_is_retryable(LlmErrorKind::ClientError), "4xx NO reintentable");
    expect(!tuide::llm_error_is_retryable(LlmErrorKind::Cancelled), "cancelado NO reintentable");
    expect(!tuide::llm_error_is_retryable(LlmErrorKind::Config), "config NO reintentable");
    expect(std::string(tuide::llm_error_kind_name(LlmErrorKind::RateLimit)) == "rate_limit",
           "nombre de kind");
  }

  // ------------------------------------------------- usage acumulado
  {
    LlmUsage total;
    LlmUsage a;
    a.prompt_tokens = 10;
    a.completion_tokens = 2;
    a.calls = 1;
    LlmUsage b;
    b.prompt_tokens = 5;
    b.completion_tokens = 3;
    b.cached_prompt_tokens = 4;
    b.calls = 1;
    total.add(a);
    total.add(b);
    expect(total.prompt_tokens == 15 && total.completion_tokens == 5 && total.calls == 2,
           "usage acumulado");
    expect(total.total() == 20, "usage total()");
    expect(total.cached_prompt_tokens == 4, "usage cached acumulado");
  }

  // ------------------------------------------- errores de config sin red
  {
    LlmEndpoint ep;  // api_base vacío
    tuide::LlmClient client(ep);
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    const LlmResponse r = client.chat(req);
    expect(!r.ok && r.kind == LlmErrorKind::Config, "endpoint sin api_base → Config");
  }
  {
    LlmEndpoint ep;
    ep.api_base = "ftp://nope/v1";
    ep.model = "m";
    tuide::LlmClient client(ep);
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    const LlmResponse r = client.chat(req);
    expect(!r.ok && r.kind == LlmErrorKind::Config, "esquema no soportado → Config");
  }
  {
    LlmEndpoint ep;
    ep.api_base = "http://127.0.0.1:1/v1";  // puerto cerrado
    ep.model = "m";
    ep.max_retries = 0;
    ep.connect_timeout_ms = 300;
    ep.timeout_ms = 1000;
    tuide::LlmClient client(ep);
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    const LlmResponse r = client.chat(req);
    expect(!r.ok && r.kind == LlmErrorKind::Network, "connect rechazado → Network");
    expect(r.attempts == 1, "max_retries=0 → un solo intento");
    expect(!client.last_attempts_text().empty(), "traza de intentos no vacía");
  }
  {
    // Cancelación previa: no se intenta nada.
    LlmEndpoint ep;
    ep.api_base = "http://127.0.0.1:1/v1";
    ep.model = "m";
    ep.max_retries = 3;
    ep.connect_timeout_ms = 300;
    std::atomic<bool> cancel{true};
    tuide::LlmClient client(ep);
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    const LlmResponse r = client.chat(req, &cancel);
    expect(!r.ok && r.kind == LlmErrorKind::Cancelled, "cancel previo → Cancelled");
  }

  // --- relojes del transporte -------------------------------------------
  // El presupuesto es de la llamada, no del intento: un servidor que acepta y
  // calla no puede costar `max_retries+1` veces el timeout. Es el fallo que se
  // comió 1500 s de un caso de la suite de trampas en una sola llamada.
  {
    FakeServer srv([](int fd, int, const std::atomic<bool>& stop) {
      drain_request(fd);
      hang_until_stop(stop);  // mudo a propósito
    });
    LlmEndpoint ep;
    ep.api_base = srv.api_base();
    ep.model = "m";
    ep.timeout_ms = 1200;
    ep.max_retries = 3;
    ep.connect_timeout_ms = 400;
    tuide::LlmClient client(ep);
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    req.stream = false;
    const auto t0 = std::chrono::steady_clock::now();
    const LlmResponse r = client.chat(req);
    const long took = ms_since(t0);
    expect(!r.ok && r.kind == LlmErrorKind::Timeout, "servidor mudo → Timeout");
    expect(took < 2400, "el presupuesto cubre los reintentos: " + std::to_string(took) +
                            "ms debería quedarse cerca de 1200, no multiplicarlo");
    expect(r.error.find("sin presupuesto para reintentar") != std::string::npos ||
               r.attempts == 1,
           "o no reintentó, o dijo por qué dejó de hacerlo: " + r.error);
  }

  // Un stream que arranca y se queda mudo se corta por el reloj de silencio, no
  // por el deadline: esperar los 5 min completos no lo arregla.
  {
    FakeServer srv([](int fd, int, const std::atomic<bool>& stop) {
      drain_request(fd);
      send_all(fd,
               "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
               "Connection: close\r\n\r\n"
               "data: {\"choices\":[{\"delta\":{\"content\":\"hola\"}}]}\n\n");
      hang_until_stop(stop);  // y aquí se atasca
    });
    LlmEndpoint ep;
    ep.api_base = srv.api_base();
    ep.model = "m";
    ep.caps.streaming = true;
    ep.timeout_ms = 20000;
    ep.stall_timeout_ms = 700;
    ep.max_retries = 2;
    tuide::LlmClient client(ep);
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    req.stream = true;
    std::string got;
    const auto t0 = std::chrono::steady_clock::now();
    const LlmResponse r =
        client.chat(req, nullptr, [&got](const LlmDelta& d) { got += d.text; });
    const long took = ms_since(t0);
    expect(got == "hola", "el trozo que sí llegó se entregó: '" + got + "'");
    expect(!r.ok && r.kind == LlmErrorKind::Timeout, "stream atascado → Timeout");
    expect(r.error.find("sin datos durante") != std::string::npos,
           "el error dice que fue silencio, no deadline: " + r.error);
    expect(took < 3000, "cortó por silencio (" + std::to_string(took) +
                            "ms), no esperó los 20000 del deadline");
  }

  // Lo contrario: un fallo rápido sigue mereciendo reintento, y le queda
  // presupuesto de sobra para que funcione.
  {
    const std::string body =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"ok\"}}]}";
    FakeServer srv(
        [&body](int fd, int conn_no, const std::atomic<bool>&) {
          drain_request(fd);
          if (conn_no == 0) {
            return;  // cuelga el teléfono sin contestar
          }
          send_all(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                       "Content-Length: " + std::to_string(body.size()) +
                       "\r\nConnection: close\r\n\r\n" + body);
        },
        2);
    LlmEndpoint ep;
    ep.api_base = srv.api_base();
    ep.model = "m";
    ep.timeout_ms = 8000;
    ep.max_retries = 2;
    tuide::LlmClient client(ep);
    LlmRequest req;
    req.messages.push_back(LlmMessage::user_msg("x"));
    req.stream = false;
    const LlmResponse r = client.chat(req);
    expect(r.ok, "fallo rápido reintentado con éxito: " + r.error);
    expect(r.content == "ok", "contenido del segundo intento: '" + r.content + "'");
    expect(r.attempts == 2, "hicieron falta 2 intentos, hubo " + std::to_string(r.attempts));
  }

  if (failures) {
    std::cerr << failures << " failures\n";
    return 1;
  }
  std::cout << "llm_client_test OK\n";
  return 0;
}
