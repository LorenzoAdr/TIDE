#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
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

struct AiInsertAnchor {
  std::string path;       // absolute preferred
  int line = 0;           // 0-based editor line
  int col = 0;
  std::string symbol_hint;
};

struct AiControllerDeps {
  WorkspaceModel* workspace = nullptr;
  std::shared_ptr<ISymbolProvider> symbols;
  WorkspaceIndexer* indexer = nullptr;
  SymbolWorkspaceIndexer* symbol_indexer = nullptr;
  GitService* git = nullptr;
  MainLayoutState* layout = nullptr;
  WorkspaceConfig* config = nullptr;
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
  // Reset chat context: wipe L2 session artifacts and optionally clear transcript.
  void clear_ai_session(bool clear_transcript = true);
  bool has_continuable_session() const;

  // Entry point for the AI tab input (always L0 first — D14).
  void handle_user_input(const std::string& line);

  // Context-menu insert: stash locus, focus AI tab; next Enter runs Agent with seeded pack.
  void begin_insert_at(const AiInsertAnchor& anchor);
  bool has_pending_insert() const { return pending_insert_; }
  void clear_pending_insert();

  // After the workspace symbol map is ready (and AI indexes were requested): warm stem embeds.
  void on_symbol_map_ready();

  // Cancels the current L1 agent and/or background task.
  void cancel_current();

  // Shown when a user action needs an AI pack that is not installed (Toolpacks / toast).
  using MissingPackageFn = std::function<void(const std::string& pack_id)>;
  void set_missing_package_handler(MissingPackageFn fn);
  void clear_missing_package_notice(const std::string& pack_id = {});

  bool enabled() const { return settings_.enabled; }
  bool agent_busy() const { return agent_busy_.load(); }
  bool task_busy() const { return task_busy_.load(); }
  bool download_busy() const { return download_busy_.load(); }
  bool busy() const { return agent_busy() || task_busy() || download_busy(); }

  // Explicit L2 workflow (agent|ask|plan|git). Persisted in workspace config when possible.
  std::string level2_workflow() const;
  void cycle_level2_workflow();
  void set_level2_workflow(const std::string& workflow);

  // Session override for L2 backend (local|remote). Empty → settings.level2_mode.
  // Does not rewrite workspace defaults; dry_run/harness stay in Settings.
  std::string level2_mode_override() const;
  std::string effective_level2_mode() const;
  void set_level2_mode_override(const std::string& mode);
  void cycle_level2_mode_override();

  // Directory prefixes the AI may explore (empty = unrestricted). Persisted in workspace config.
  const std::vector<std::string>& path_scope() const;
  void set_path_scope(std::vector<std::string> paths);

 private:
  void ensure_tools();
  void sync_task_runner();
  void handle_route(const AiRouteResult& route, const std::string& original);
  void run_tool(const std::string& name, const std::string& arg);
  void run_task(const std::string& name);
  void dump_context_pack(const std::vector<std::string>& seeds);
  void run_level1_async(const std::string& message);
  void run_insert_async(const std::string& user_message, AiInsertAnchor anchor);
  void cancel_level1();
  void cancel_all();
  void handle_level2_harness(const std::string& arg);
  void bootstrap_level2_session(const std::string& query, const std::string& instruction,
                                const std::vector<std::string>& seeds,
                                const std::string& workflow = {},
                                const std::string& seed_pack_markdown = {});
  // After bootstrap when mode=local|remote: run autonomous loop (same agent thread).
  void run_level2_autonomous_inline(const std::string& reason);
  void run_level2_followup_async(const std::string& message);
  bool level2_mode_is_autonomous() const;
  std::string build_git_context_seed(const std::string& query) const;
  void show_model_status();
  void download_models(const std::string& what);
  bool ensure_backend_ready();
  // prompt_if_missing: only for user-driven AI input (never background warm).
  bool ensure_intent_embeddings_ready(bool prompt_if_missing = false);
  bool ensure_coding_stem_index_ready();
  void maybe_start_coding_stem_warm_async();
  void join_symbol_embed_thread();
  void begin_thinking();
  void end_thinking();
  void begin_download(std::string_view label = {});
  void update_download_percent(int percent);
  void end_download();
  ModelStore::ProgressFn make_store_progress();
  void request_missing_package(const std::string& pack_id);
  static bool is_cancel_input(const std::string& line);
  void wake(bool force = false);
  void join_agent_thread();
  void join_task_thread();

  AiControllerDeps deps_;
  AiSettings settings_;
  ToolRegistry tools_;
  TaskRunner tasks_;
  LlamaBackend backend_;
  LlamaBackend l2_backend_;
  EmbeddingBackend embed_backend_;
  Level0IntentIndex intent_index_;
  CodingStemEmbedIndex coding_stem_index_;
  CodingSymbolEmbedIndex coding_symbol_index_;
  bool tools_ready_ = false;
  bool intent_embed_attempted_ = false;
  MissingPackageFn on_missing_package_;
  std::unordered_set<std::string> missing_notified_;

  mutable std::mutex lines_mu_;
  std::vector<std::string> lines_;
  static constexpr std::size_t kMaxLines = 4000;
  // Coalesce streaming wakes (compile logs) so we don't paint every line.
  std::mutex wake_mu_;
  std::chrono::steady_clock::time_point last_stream_wake_{};
  bool stream_wake_pending_ = false;

  std::atomic<bool> agent_busy_{false};
  std::atomic<bool> agent_cancel_{false};
  std::atomic<bool> download_busy_{false};
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

  // Chat-session L2 backend override: "" | "local" | "remote".
  std::string level2_mode_override_;

  bool pending_insert_ = false;
  AiInsertAnchor insert_anchor_;
};

}  // namespace tuide
