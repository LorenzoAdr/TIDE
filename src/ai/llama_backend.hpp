#pragma once

#include <atomic>
#include <functional>
#include <string>

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
};

struct LlamaCompletionResult {
  bool ok = false;
  std::string text;
  std::string error;
};

class LlamaBackend {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;

  LlamaBackend();

  void set_model_path(std::string path);
  void set_cli_path(std::string path);
  const std::string& model_path() const { return model_path_; }
  const std::string& cli_path() const { return cli_path_; }

  bool ready() const;
  std::string status_text() const;

  bool ensure_ready(const AiSettings& settings, const ProgressFn& on_progress, std::string* error);

  LlamaCompletionResult complete(const LlamaCompletionRequest& req,
                                 std::atomic<bool>* cancel = nullptr) const;

 private:
  ModelStore store_;
  std::string model_path_;
  std::string cli_path_;
  std::string lib_dir_;
};

}  // namespace tuide
