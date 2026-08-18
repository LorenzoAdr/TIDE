#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "ai/ai_types.hpp"
#include "ai/llama_backend.hpp"

namespace tuide {

struct L2BrainRequest {
  std::string system_prompt;
  std::string user_prompt;
  std::string phase;
  int max_tokens = 2048;
  int n_ctx = 8192;
  float temperature = 0.1f;
  // Absolute path to GBNF grammar; empty → unconstrained decode.
  std::string grammar_file;
};

struct L2BrainResult {
  bool ok = false;
  std::string text;
  std::string error;
  std::string backend;  // "local" | "remote" | "scripted"
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

class RemoteL2Brain : public L2Brain {
 public:
  std::string name() const override { return "remote"; }
  bool ensure_ready(const AiSettings& settings,
                    const std::function<void(const std::string&)>& on_progress,
                    std::string* error) override;
  L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>* cancel) override;

 private:
  AiLevel2Settings cfg_;
};

// Factory: mode local → LocalL2Brain; remote → RemoteL2Brain; else nullptr.
std::unique_ptr<L2Brain> make_l2_brain(const std::string& mode, LlamaBackend* shared_backend);

}  // namespace tuide
