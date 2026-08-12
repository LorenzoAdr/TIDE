#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ai/ai_types.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/level0_intent_index.hpp"
#include "ai/llama_backend.hpp"
#include "ai/coding_stem_embed_index.hpp"
#include "ai/coding_symbol_embed_index.hpp"
#include "ai/task_runner.hpp"
#include "ai/tool_registry.hpp"
#include "app/workspace_config.hpp"
#include "app/workspace_model.hpp"
#include "git/git_service.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"

namespace tuide {

struct MainLayoutState;

struct AiControllerDeps {
  WorkspaceModel* workspace = nullptr;
  std::shared_ptr<ISymbolProvider> symbols;
  WorkspaceIndexer* indexer = nullptr;
  SymbolWorkspaceIndexer* symbol_indexer = nullptr;
  GitService* git = nullptr;
  MainLayoutState* layout = nullptr;
  const WorkspaceConfig* config = nullptr;
};

// Owns AI transcript and dispatches every user line through Nivel 0 (then L1).
class AiController {
 public:
  explicit AiController(AiControllerDeps deps);
  ~AiController();

  void set_deps(AiControllerDeps deps);
  void refresh_settings();

  std::vector<std::string> snapshot_lines() const;
  // Deprecated alias for UI that held a const ref; prefer snapshot_lines().
  const std::vector<std::string>& lines() const { return lines_; }

  void append(const std::string& line);
  void clear();

  // Entry point for the AI tab input (always L0 first — D14).
  void handle_user_input(const std::string& line);

  // Kick coding-symbol (map) embeddings after the workspace symbol map is ready.
  // Safe to call from the indexer worker; no-ops if AI disabled / already running.
  void on_symbol_map_ready();

  // Cancels the current L1 agent and/or background task.
  void cancel_current();

  bool enabled() const { return settings_.enabled; }
  bool agent_busy() const { return agent_busy_.load(); }
  bool task_busy() const { return task_busy_.load(); }
  bool busy() const { return agent_busy() || task_busy(); }

 private:
  void ensure_tools();
  void sync_task_runner();
  void handle_route(const AiRouteResult& route, const std::string& original);
  void run_tool(const std::string& name, const std::string& arg);
  void run_task(const std::string& name);
  void dump_context_pack(const std::vector<std::string>& seeds);
  void run_level1_async(const std::string& message);
  void cancel_level1();
  void cancel_all();
  void show_model_status();
  void download_models(const std::string& what);
  bool ensure_backend_ready();
  bool ensure_intent_embeddings_ready();
  bool ensure_coding_stem_index_ready();
  void maybe_start_coding_symbol_index_async();
  void join_symbol_embed_thread();
  void begin_thinking();
  void end_thinking();
  void wake(bool force = false);
  void join_agent_thread();
  void join_task_thread();

  AiControllerDeps deps_;
  AiSettings settings_;
  ToolRegistry tools_;
  TaskRunner tasks_;
  LlamaBackend backend_;
  EmbeddingBackend embed_backend_;
  Level0IntentIndex intent_index_;
  CodingStemEmbedIndex coding_stem_index_;
  CodingSymbolEmbedIndex coding_symbol_index_;
  bool tools_ready_ = false;
  bool intent_embed_attempted_ = false;

  mutable std::mutex lines_mu_;
  std::vector<std::string> lines_;
  static constexpr std::size_t kMaxLines = 4000;
  // Coalesce streaming wakes (compile logs) so we don't paint every line.
  std::mutex wake_mu_;
  std::chrono::steady_clock::time_point last_stream_wake_{};
  bool stream_wake_pending_ = false;

  std::atomic<bool> agent_busy_{false};
  std::atomic<bool> agent_cancel_{false};
  std::thread agent_thread_;
  std::mutex agent_mu_;

  std::atomic<bool> task_busy_{false};
  std::thread task_thread_;
  std::mutex task_mu_;

  std::atomic<bool> symbol_embed_started_{false};
  std::atomic<bool> symbol_embed_running_{false};
  std::atomic<bool> symbol_embed_restart_pending_{false};
  std::atomic<bool> symbol_embed_busy_active_{false};
  std::atomic<std::size_t> symbol_embed_done_{0};
  std::atomic<std::size_t> symbol_embed_total_{0};
  std::thread symbol_embed_thread_;
  std::mutex symbol_embed_mu_;
  std::string symbol_embed_workspace_root_;

  // Último tool L0 para follow-ups cortos ("y dentro de src?").
  std::string last_l0_tool_;
  std::string last_l0_arg_;
};

}  // namespace tuide
