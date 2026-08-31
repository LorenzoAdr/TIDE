#include <cstdlib>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "ai/ai_types.hpp"
#include "ai/llama_backend.hpp"
#include "ai/llama_net.hpp"

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    tuide::AiLevel2Settings l2;
    expect(l2.server_port == 18766, "default L2 server_port");
    expect(l2.n_gpu_layers == -1, "default n_gpu_layers auto");
    expect(l2.n_threads == 0, "default n_threads auto");
  }
  {
    std::string content;
    std::string err;
    const std::string body =
        R"({"choices":[{"message":{"role":"assistant","content":"{\"action\":\"done\"}"}}]})";
    expect(tuide::parse_llama_chat_completion(body, &content, &err), "parse ok");
    expect(content == "{\"action\":\"done\"}", "content");
  }
  {
    std::string content;
    std::string err;
    expect(!tuide::parse_llama_chat_completion("{\"error\":\"boom\"}", &content, &err), "error");
    expect(err.find("boom") != std::string::npos, "error text");
  }
  {
    std::string content;
    std::string err;
    expect(!tuide::parse_llama_chat_completion("not-json", &content, &err), "bad json");
  }
  {
    std::string content;
    std::string err;
    const std::string body = R"({"choices":[{"text":"hello"}]})";
    expect(tuide::parse_llama_chat_completion(body, &content, &err), "text field");
    expect(content == "hello", "text content");
  }
  {
    expect(tuide::llama_normalize_host("http://192.168.64.1:18765/v1") == "192.168.64.1",
           "normalize strips scheme/port/path");
    int port = 0;
    expect(tuide::llama_normalize_host("host.orb.internal:18765", &port) == "host.orb.internal",
           "normalize hostname");
    expect(port == 18765, "port from host:port");
    expect(tuide::llama_host_is_local("127.0.0.1"), "loopback is local");
    expect(tuide::llama_host_is_local("localhost:9999"), "localhost is local");
    expect(!tuide::llama_host_is_local("192.168.64.1"), "UTM gateway is remote");
    expect(!tuide::llama_host_is_local("host.orb.internal"), "OrbStack host is remote");
  }
  {
    tuide::AiSettings s;
    ::setenv("TUIDE_L2_API_BASE", "http://192.168.64.1:8080/v1", 1);
    ::setenv("TUIDE_L2_API_MODEL", "qwen-metal", 1);
    ::setenv("TUIDE_EMBED_HOST", "192.168.64.1", 1);
    ::setenv("TUIDE_EMBED_PORT", "18765", 1);
    tuide::apply_ai_runtime_env(&s);
    expect(s.level2.api_base == "http://192.168.64.1:8080/v1", "env L2 api_base");
    expect(s.level2.api_model == "qwen-metal", "env L2 api_model");
    expect(s.level0.embeddings.server_host == "192.168.64.1", "env embed host");
    expect(s.level0.embeddings.server_port == 18765, "env embed port");
    ::unsetenv("TUIDE_L2_API_BASE");
    ::unsetenv("TUIDE_L2_API_MODEL");
    ::unsetenv("TUIDE_EMBED_HOST");
    ::unsetenv("TUIDE_EMBED_PORT");
  }
  {
    std::string bad = "ok";
    bad.push_back(static_cast<char>('\xC3'));
    bad.push_back('\n');
    nlohmann::json j = {{"content", bad}};
    std::string dumped;
    try {
      dumped = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& ex) {
      expect(false, std::string("replace dump threw: ") + ex.what());
    }
    expect(!dumped.empty(), "replace dump ok");
  }
  {
    tuide::LlamaCompletionRequest req;
    req.system_prompt = "sys";
    req.user_prompt = "user";
    req.max_tokens = 420;
    req.temperature = 0.1f;
    req.enable_thinking = true;
    req.reasoning_budget = 1536;
    const auto body = tuide::build_chat_completions_body(req, "l2", "user", true);
    expect(body["chat_template_kwargs"]["enable_thinking"] == true, "kwargs thinking on");
    expect(body["thinking_budget_tokens"] == 1536, "thinking_budget_tokens");
    expect(body["reasoning_budget_tokens"] == 1536, "reasoning_budget_tokens");
    expect(body["max_tokens"] == 420, "max_tokens unchanged in builder");
    expect(body.contains("cache_prompt") && body["cache_prompt"] == true, "cache_prompt");
  }
  {
    tuide::LlamaCompletionRequest req;
    req.enable_thinking = false;
    req.reasoning_budget = 0;
    req.max_tokens = 512;
    const auto body = tuide::build_chat_completions_body(req, "l2", "edit", false);
    expect(body["chat_template_kwargs"]["enable_thinking"] == false, "edit thinking off");
    expect(body["thinking_budget_tokens"] == 0, "edit budget 0");
    expect(!body.contains("cache_prompt"), "no cache_prompt when false");
  }
  {
    tuide::LlamaCompletionRequest req;
    req.max_tokens = 256;
    const auto body = tuide::build_chat_completions_body(req, "l2", "x", false);
    expect(!body.contains("chat_template_kwargs"), "omit kwargs when unset");
    expect(!body.contains("thinking_budget_tokens"), "omit thinking_budget when -1");
    expect(!body.contains("reasoning_budget_tokens"), "omit reasoning_budget when -1");
  }
  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "llama_backend_test ok\n";
  return 0;
}
