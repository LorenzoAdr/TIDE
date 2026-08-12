#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <sys/types.h>

#include "ai/ai_types.hpp"
#include "ai/model_store.hpp"

namespace tuide {

// Local llama-server (--embedding) for L0/L1 embeddings (127.0.0.1 only).
// Uses a keep-alive HTTP/1.1 socket (no per-call curl) and supports batched input.
class EmbeddingBackend {
 public:
  using ProgressFn = std::function<void(const std::string& line)>;

  EmbeddingBackend();
  ~EmbeddingBackend();

  EmbeddingBackend(const EmbeddingBackend&) = delete;
  EmbeddingBackend& operator=(const EmbeddingBackend&) = delete;

  bool ready() const;
  std::string status_text() const;
  int port() const { return port_; }
  const std::string& model_path() const { return model_path_; }

  bool ensure_ready(const AiSettings& settings, const ProgressFn& on_progress, std::string* error);
  void stop();

  // E5 / nomic task prefixes applied here.
  bool embed_query(const std::string& text, std::vector<float>* out, std::string* error) const;
  bool embed_passage(const std::string& text, std::vector<float>* out, std::string* error) const;
  bool embed_raw(const std::string& text, std::vector<float>* out, std::string* error) const;

  // Batched passages (search_document: prefix). Chunks automatically (~32).
  bool embed_passages(const std::vector<std::string>& texts,
                      std::vector<std::vector<float>>* out, std::string* error) const;
  // Batched raw strings (caller supplies any prefixes).
  bool embed_many_raw(const std::vector<std::string>& texts,
                      std::vector<std::vector<float>>* out, std::string* error) const;

 private:
  bool embed_prefixed(const std::string& prefix, const std::string& text, std::vector<float>* out,
                      std::string* error) const;
  bool health_ok() const;
  bool wait_until_healthy(int timeout_ms, std::string* error) const;
  bool start_server(const std::string& server_bin, const std::string& lib_dir,
                    const AiLevel0EmbedSettings& emb, const ProgressFn& on_progress,
                    std::string* error);
  // Kill whatever listens on port_ (stale embed server with old flags).
  void stop_listener_on_port();
  // Requires mu_ held. Closes HTTP + kills server_pid_ we own (not foreign listeners).
  void stop_owned_unlocked();

  void http_close_unlocked() const;
  bool http_ensure_connected_unlocked(std::string* error) const;
  bool http_exchange_unlocked(const std::string& method, const std::string& path,
                              const std::string& body, int timeout_ms, std::string* response,
                              std::string* error) const;
  bool post_embeddings_json(const std::string& payload, std::string* response,
                            std::string* error) const;
  // Own short-lived socket (safe for concurrent callers; does not use http_fd_).
  bool post_embeddings_json_ephemeral(const std::string& payload, std::string* response,
                                      std::string* error) const;

  mutable std::mutex mu_;       // server lifecycle / ready_
  mutable std::mutex http_mu_;  // keep-alive socket (separate to avoid deadlock with mu_)
  ModelStore store_;
  std::string model_path_;
  std::string server_bin_;
  std::string lib_dir_;
  std::string server_stamp_;
  int port_ = 18765;
  int http_batch_ = 64;
  int n_parallel_ = 8;
  pid_t server_pid_ = -1;
  std::atomic<bool> ready_{false};
  mutable int http_fd_ = -1;
};

}  // namespace tuide
