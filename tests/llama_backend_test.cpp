#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "ai/ai_types.hpp"
#include "ai/llama_backend.hpp"

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
  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "llama_backend_test ok\n";
  return 0;
}
