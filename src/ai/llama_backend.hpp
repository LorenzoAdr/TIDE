#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <sys/types.h>

#include "ai/ai_types.hpp"
#include "ai/model_store.hpp"

namespace tuide {

struct LlamaCompletionRequest {
  std::string system_prompt;
  std::string user_prompt;
  // Prior turns as plain text (folded into the user message for --single-turn).
  std::string history_text;
  int max_tokens = 512;
  int n_ctx = 2048;
  float temperature = 0.2f;
  // Shown in context-overflow errors ("L1" / "L2").
  std::string context_role = "L1";
  // Config key hint, e.g. ai.level1.n_ctx / ai.level2.n_ctx.
  std::string n_ctx_setting_hint = "ai.level1.n_ctx";
  // Optional GBNF grammar file for llama-cli --grammar-file / llama-server `grammar`.
  std::string grammar_file;
  // Absent = omit from the JSON body. reasoning_budget -1 = omit.
  std::optional<bool> enable_thinking;
  int reasoning_budget = -1;
};

struct LlamaCompletionResult {
  bool ok = false;
  std::string text;
  std::string error;
};

// Extract assistant content from llama-server /v1/chat/completions JSON.
bool parse_llama_chat_completion(const std::string& body, std::string* content, std::string* error);

void attach_thinking_json(nlohmann::json& body, const std::optional<bool>& enable_thinking,
                          int reasoning_budget);

nlohmann::json build_chat_completions_body(const LlamaCompletionRequest& req,
                                           const std::string& model, const std::string& user_text,
                                           bool cache_prompt);

class LlamaBackend {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;

  LlamaBackend();
  ~LlamaBackend();

  LlamaBackend(const LlamaBackend&) = delete;
  LlamaBackend& operator=(const LlamaBackend&) = delete;

  void set_model_path(std::string path);
  void set_cli_path(std::string path);
  const std::string& model_path() const { return model_path_; }
  const std::string& cli_path() const { return cli_path_; }
  int completion_server_port() const { return server_port_; }
  bool completion_server_ready() const { return server_ready_.load(); }

  bool ready() const;
  std::string status_text() const;

  bool ensure_ready(const AiSettings& settings, const ProgressFn& on_progress, std::string* error);

  // Start or reuse llama-server for this model (L2). L1 keeps one-shot llama-cli.
  bool ensure_completion_server(const AiSettings& settings, const ProgressFn& on_progress,
                                std::string* error);
  void stop_completion_server();

  LlamaCompletionResult complete(const LlamaCompletionRequest& req,
                                 std::atomic<bool>* cancel = nullptr) const;

 private:
  LlamaCompletionResult complete_cli(const LlamaCompletionRequest& req,
                                     std::atomic<bool>* cancel) const;
  LlamaCompletionResult complete_server(const LlamaCompletionRequest& req,
                                        std::atomic<bool>* cancel) const;

  bool health_ok() const;
  bool wait_until_healthy(int timeout_ms, std::string* error) const;
  bool start_completion_server(const std::string& server_bin, const std::string& lib_dir,
                               const AiLevel2Settings& l2, const ProgressFn& on_progress,
                               std::string* error);
  void stop_owned_unlocked();
  void stop_listener_on_port();

  void http_close_unlocked() const;
  bool http_ensure_connected_unlocked(std::string* error) const;
  bool http_exchange_unlocked(const std::string& method, const std::string& path,
                              const std::string& body, int timeout_ms, std::atomic<bool>* cancel,
                              std::string* response, std::string* error) const;

  ModelStore store_;
  std::string model_path_;
  std::string cli_path_;
  std::string lib_dir_;
  std::string server_bin_;
  std::string server_stamp_;
  int server_port_ = 18766;
  int server_n_ctx_ = 8192;
  pid_t server_pid_ = -1;
  std::atomic<bool> server_ready_{false};
  mutable std::mutex mu_;
  mutable std::mutex http_mu_;
  mutable int http_fd_ = -1;
};

}  // namespace tuide
