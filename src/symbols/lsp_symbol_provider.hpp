#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "lsp/lsp_client.hpp"
#include "parser/tree_sitter_service.hpp"
#include "symbols/tree_sitter_symbol_provider.hpp"
#include "symbols/symbol_provider.hpp"
#include "util/thread_safe_queue.hpp"

namespace tgdb {

class LspSymbolProvider : public ISymbolProvider {
 public:
  LspSymbolProvider();
  ~LspSymbolProvider() override;

  std::vector<SymbolInfo> symbols_for_file(const std::string& path) override;
  bool symbols_lsp_pending(const std::string& path) const override;
  bool drain_async_results() override;
  uint64_t semantic_highlight_revision() const override;
  uint64_t document_symbols_revision() const override;
  bool indexes_workspace_bulk() const override;
  std::vector<SymbolInfo> workspace_symbols(const std::string& workspace_root,
                                              const std::string& query) override;
  bool supports_semantic_completion() const override;
  std::vector<CompletionItem> completions_at(const CompletionParams& params) override;
  bool completion_uses_async_fetch() const override;
  void request_completion(const CompletionParams& params, const std::string& cache_key) override;
  std::optional<std::vector<CompletionItem>> poll_completion(
      const std::string& cache_key) override;
  bool supports_navigation() const override;
  SourceLocation goto_definition(const NavigationParams& params) override;
  SourceLocation goto_declaration(const NavigationParams& params) override;
  SourceLocation goto_implementation(const NavigationParams& params) override;

  bool supports_semantic_highlight() const override;
  bool ensure_semantic_tokens(const std::string& path) override;
  SemanticTokenDocument semantic_tokens_for_file(const std::string& path) override;
  bool semantic_tokens_current_for_file(const std::string& path) override;

  bool supports_hover() const override;
  bool hover_uses_async_fetch() const override;
  void request_hover(const HoverParams& params, const std::string& cache_key) override;
  std::optional<HoverInfo> poll_hover(const std::string& cache_key) override;
  HoverInfo hover_at(const HoverParams& params) override;

  bool supports_diagnostics() const override;
  uint64_t diagnostics_revision() const override;
  bool document_sync_pending(const std::string& path) const override;
  bool diagnostics_display_ready(const std::string& path) const override;
  DocumentDiagnostics diagnostics_for_file(const std::string& path) override;
  std::vector<DocumentDiagnostics> workspace_diagnostics() override;

  bool supports_formatting() const override;
  std::optional<std::string> format_document(const FormatParams& params) override;

  bool supports_rename() const override;
  std::vector<LspFileEdits> rename_symbol(const RenameParams& params) override;

  bool supports_code_actions() const override;
  std::vector<CodeActionItem> code_actions_for_diagnostic(const CodeActionParams& params) override;

  bool supports_call_hierarchy() const override;
  std::vector<CallHierarchyItem> prepare_call_hierarchy(const CallHierarchyParams& params) override;
  std::vector<CallHierarchyItem> incoming_calls(const CallHierarchyItem& item) override;
  std::vector<CallHierarchyItem> outgoing_calls(const CallHierarchyItem& item) override;

  void on_workspace_opened(const std::string& root,
                           const std::string& compile_commands_dir = {}) override;
  void on_workspace_closed() override;
  void on_document_opened(const std::string& path, const std::string& text) override;
  void on_document_changed(const std::string& path, const std::string& text) override;
  void on_document_saved(const std::string& path) override;
  void on_document_closed(const std::string& path) override;
  void tick_debounced_updates() override;
  void flush_document_sync(const std::string& path) override;

  bool lsp_active() const { return use_lsp_; }
  bool lsp_loading() const override;

  void set_lsp_enabled(bool enabled);
  bool lsp_enabled() const;
  void set_workspace_clangd_options(bool use_gcc_query_driver, bool background_index);
  void set_ui_inhibited(bool inhibited);

 private:
  enum class AsyncJobKind { DocumentSymbols, SemanticTokens, Hover, Completion };

  struct AsyncJob {
    AsyncJobKind kind;
    std::string path;
    std::string hover_key;
    HoverParams hover_params;
    std::string completion_key;
    CompletionParams completion_params;
  };

  struct AsyncResult {
    AsyncJobKind kind;
    std::string path;
  };

  std::string buffer_text_for_path(const std::string& path) const;
  void refresh_diagnostics_cache_locked() const;
  void start_lsp_async(const std::string& compile_commands_dir);
  void finish_lsp_start_locked(bool ok);
  void join_startup_thread();
  void stop_lsp();
  void stop_lsp_locked();
  void stop_lsp_locked_finalize();
  void restart_lsp_after_transport_failure();
  void process_pending_transport_restart();
  void start_async_worker_locked();
  void stop_async_worker_locked();
  void async_worker_main();
  void enqueue_document_symbols_locked(const std::string& path, bool force = false);
  void enqueue_semantic_tokens_locked(const std::string& path, bool force = false);
  bool symbols_lsp_pending_locked(const std::string& path) const;
  void tick_content_refresh_locked();
  void tick_pending_did_change_locked();
  void flush_pending_did_change_for_key_locked(const std::string& key);
  void flush_pending_did_change_for_key(const std::string& key);
  void flush_all_pending_did_change_locked();
  bool sync_document_for_completion(const std::string& path, const std::string& text);
  void open_companion_sources_for_clangd_locked(const std::string& header_path);
  void clear_shadow_companion_locked(const std::string& companion_path);
  bool buffer_open_locked(const std::string& path) const;
  static int64_t steady_now_ms();

  mutable std::mutex mutex_;
  LspClient client_;
  TreeSitterSymbolProvider fallback_;
  bool lsp_enabled_ = true;
  bool use_gcc_query_driver_ = true;
  bool use_background_index_ = false;
  bool use_lsp_ = false;
  bool ui_inhibited_ = false;
  std::string workspace_root_;
  std::string compile_commands_dir_;
  std::unordered_map<std::string, std::string> open_buffers_;
  std::unordered_map<std::string, std::unordered_set<std::string>> shadow_companions_;
  mutable uint64_t cached_diag_revision_ = 0;
  mutable std::vector<DocumentDiagnostics> cached_diagnostics_;

  ThreadSafeQueue<AsyncJob> async_jobs_;
  ThreadSafeQueue<AsyncResult> async_results_;
  std::thread async_worker_;
  std::thread lsp_startup_thread_;
  std::atomic<bool> lsp_starting_{false};
  std::atomic<bool> async_stop_{false};
  mutable std::mutex inflight_mutex_;
  std::unordered_set<std::string> inflight_symbols_;
  std::unordered_set<std::string> inflight_semantic_;
  std::unordered_set<std::string> inflight_hover_;
  std::unordered_set<std::string> inflight_completion_;
  std::unordered_map<std::string, HoverInfo> hover_cache_;
  std::unordered_map<std::string, std::vector<CompletionItem>> completion_cache_;
  std::unordered_map<std::string, std::string> latest_completion_key_by_path_;
  std::unordered_map<std::string, int64_t> last_completion_document_sync_ms_;
  std::unordered_map<std::string, int64_t> pending_semantic_refresh_;
  std::unordered_map<std::string, int64_t> pending_content_refresh_;
  std::unordered_map<std::string, int64_t> pending_did_change_;
  std::atomic<uint64_t> semantic_highlight_revision_{0};
  std::atomic<uint64_t> document_symbols_revision_{0};
  std::atomic<bool> pending_transport_restart_{false};
  std::atomic<bool> lsp_restart_in_progress_{false};
  int64_t last_lsp_failure_restart_ms_ = 0;
  int64_t lsp_ready_since_ms_ = 0;
};

}  // namespace tgdb
