#include "ai/l2_brain.hpp"
#include "ai/action_json.hpp"
#include "ai/llama_backend.hpp"
#include "ai/llama_net.hpp"
#include "ai/llm_client.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace tuide {
namespace {

std::string trim_trailing_slash(std::string s) {
  while (!s.empty() && (s.back() == '/' || s.back() == ' ')) {
    s.pop_back();
  }
  return s;
}

std::string resolve_api_key(const std::string& configured) {
  if (!configured.empty()) {
    return configured;
  }
  for (const char* var : {"TUIDE_L2_API_KEY", "DEEPSEEK_API_KEY", "OPENAI_API_KEY"}) {
    if (const char* env = std::getenv(var); env != nullptr && env[0] != '\0') {
      return env;
    }
  }
  return {};
}

std::string read_text_file(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

RemoteL2Brain::RemoteL2Brain() = default;
RemoteL2Brain::~RemoteL2Brain() = default;

bool RemoteL2Brain::ensure_ready(const AiSettings& settings,
                                 const std::function<void(const std::string&)>& on_progress,
                                 std::string* error) {
  all_ = settings;
  apply_ai_runtime_env(&all_);
  all_.level2.api_base = trim_trailing_slash(all_.level2.api_base);
  cfg_ = all_.level2;
  if (cfg_.api_base.empty()) {
    if (error) {
      *error = "L2 remote: ai.level2.api_base vacío";
    }
    return false;
  }
  if (cfg_.api_model.empty()) {
    if (error) {
      *error = "L2 remote: ai.level2.api_model vacío";
    }
    return false;
  }
  {
    // Falla aquí y no en la primera llamada: un api_base sin esquema es el
    // error de config más común y el mensaje del transporte llega mucho después.
    LlmUrl probe;
    std::string perr;
    if (!llm_parse_url(cfg_.api_base, &probe, &perr)) {
      if (error) {
        *error = "L2 remote: " + perr;
      }
      return false;
    }
  }
  // Los servidores OpenAI-compatibles locales suelen no pedir clave; la nube sí.
  (void)resolve_api_key(cfg_.api_key);
  if (on_progress) {
    const AiProviderCaps caps =
        ai_resolve_provider_caps(all_.caps, cfg_.api_base, cfg_.api_model);
    on_progress("L2 remote ready: " + cfg_.api_base + " model=" + cfg_.api_model + " caps=" +
                ai_provider_caps_text(caps));
  }
  return true;
}

L2BrainResult RemoteL2Brain::propose(const L2BrainRequest& req, std::atomic<bool>* cancel) {
  L2BrainResult out;
  out.backend = "remote";
  if (cancel != nullptr && cancel->load()) {
    out.error = "cancelado";
    out.error_kind = "cancelled";
    return out;
  }

  const AiResolvedBackend backend =
      ai_resolve_role_backend(all_, req.role.empty() ? std::string("agent") : req.role);

  LlmEndpoint endpoint;
  endpoint.api_base = trim_trailing_slash(backend.api_base);
  endpoint.api_key = resolve_api_key(backend.api_key);
  endpoint.model = backend.api_model;
  endpoint.caps = LlmProviderCaps{};
  endpoint.caps.tool_calling = backend.caps.tool_calling;
  endpoint.caps.json_schema = backend.caps.json_schema;
  endpoint.caps.grammar = backend.caps.grammar;
  endpoint.caps.streaming = backend.caps.streaming;
  endpoint.caps.prompt_cache = backend.caps.prompt_cache;
  endpoint.caps.parallel_tool_calls = backend.caps.parallel_tool_calls;
  endpoint.caps.context_window = backend.caps.context_window;
  endpoint.timeout_ms = backend.timeout_ms > 0 ? backend.timeout_ms : 300000;

  // Un cliente por endpoint: reutilizar el objeto conserva el socket entre
  // llamadas del mismo rol (y entre roles si comparten api_base).
  const std::string key = endpoint.api_base + "|" + endpoint.model;
  if (!client_ || client_key_ != key) {
    client_ = std::make_unique<LlmClient>(endpoint);
    client_key_ = key;
  } else {
    client_->set_endpoint(endpoint);
  }

  LlmRequest lreq;
  lreq.messages.push_back(LlmMessage::system_msg(req.system_prompt));
  lreq.messages.push_back(LlmMessage::user_msg(req.user_prompt));
  // Un max_tokens declarado para el rol manda sobre el de la fase: si el
  // usuario le pone techo al explorador, no lo pisa el call site.
  lreq.max_tokens = backend.max_tokens_explicit
                        ? backend.max_tokens
                        : (req.max_tokens > 0 ? req.max_tokens : backend.max_tokens);
  lreq.temperature = req.temperature;
  lreq.enable_thinking = req.enable_thinking;
  lreq.reasoning_budget = req.reasoning_budget;
  lreq.role = req.role.empty() ? "agent" : req.role;
  lreq.stream = static_cast<bool>(req.on_text) || static_cast<bool>(req.on_reasoning);
  // GBNF (si el backend es de la familia llama.cpp) y, en cualquier caso, la
  // exigencia de JSON parseable — que es para lo que servía el grammar en las
  // fases admin_*: eliminar los reintentos "JSON inválido, reemite".
  lreq.grammar = read_text_file(req.grammar_file);
  lreq.json_object = req.require_json || !req.grammar_file.empty();

  LlmDeltaFn on_delta;
  if (lreq.stream) {
    on_delta = [&req](const LlmDelta& d) {
      if (!d.text.empty() && req.on_text) {
        req.on_text(d.text);
      }
      if (!d.reasoning.empty() && req.on_reasoning) {
        req.on_reasoning(d.reasoning);
      }
    };
  }

  const LlmResponse resp = client_->chat(lreq, cancel, on_delta);

  out.attempts = resp.attempts;
  out.ms = resp.ms;
  out.streamed = resp.emitted;
  out.usage.prompt_tokens = resp.usage.prompt_tokens;
  out.usage.completion_tokens = resp.usage.completion_tokens;
  out.usage.cached_prompt_tokens = resp.usage.cached_prompt_tokens;
  out.usage.reasoning_tokens = resp.usage.reasoning_tokens;
  out.usage.calls = resp.usage.calls > 0 ? resp.usage.calls : 1;
  out.error_kind = llm_error_kind_name(resp.kind);

  if (!resp.ok) {
    out.error = resp.error.empty() ? "respuesta remote inválida" : resp.error;
    if (resp.attempts > 1) {
      out.error += " (" + std::to_string(resp.attempts) + " intentos)";
    }
    // Aunque haya fallado, el texto parcial puede servir al llamante (y ya se
    // mostró por streaming): se devuelve en `raw`, nunca como `text` válido.
    out.raw = resp.reasoning.empty() ? resp.content
                                     : (resp.reasoning + "\n---- content ----\n" + resp.content);
    return out;
  }
  if (resp.content.empty() && resp.reasoning.empty()) {
    out.error = "respuesta remote sin content";
    out.error_kind = "bad_response";
    return out;
  }

  out.ok = true;
  out.raw = resp.reasoning.empty() ? resp.content
                                   : (resp.reasoning + "\n---- content ----\n" + resp.content);
  const std::string source = resp.content.empty() ? resp.reasoning : resp.content;
  const std::string extracted = extract_action_json(source);
  out.text = extracted.empty() ? source : extracted;
  return out;
}

std::unique_ptr<L2Brain> make_l2_brain(const std::string& mode, LlamaBackend* shared_backend) {
  if (mode == "local") {
    return std::make_unique<LocalL2Brain>(shared_backend);
  }
  if (mode == "remote") {
    return std::make_unique<RemoteL2Brain>();
  }
  return nullptr;
}

}  // namespace tuide
