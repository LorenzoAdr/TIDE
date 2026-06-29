#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "lsp/lsp_client.hpp"
#include "symbols/regex_symbol_provider.hpp"
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
  bool supports_navigation() const override;
  SourceLocation goto_definition(const NavigationParams& params) override;
  SourceLocation goto_declaration(const NavigationParams& params) override;

  bool supports_semantic_highlight() const override;
  bool ensure_semantic_tokens(const std::string& path) override;
  SemanticTokenDocument semantic_tokens_for_file(const std::string& path) override;

  bool supports_hover() const override;
  HoverInfo hover_at(const HoverParams& params) override;

  bool supports_diagnostics() const override;
  uint64_t diagnostics_revision() const override;
  DocumentDiagnostics diagnostics_for_file(const std::string& path) override;
  std::vector<DocumentDiagnostics> workspace_diagnostics() override;

  bool supports_formatting() const override;
  std::optional<std::string> format_document(const FormatParams& params) override;

  bool supports_rename() const override;
  std::vector<LspFileEdits> rename_symbol(const RenameParams& params) override;

  bool supports_call_hierarchy() const override;
  std::vector<CallHierarchyItem> prepare_call_hierarchy(const CallHierarchyParams& params) override;
  std::vector<CallHierarchyItem> incoming_calls(const CallHierarchyItem& item) override;
  std::vector<CallHierarchyItem> outgoing_calls(const CallHierarchyItem& item) override;

  void on_workspace_opened(const std::string& root) override;
  void on_workspace_closed() override;
  void on_document_opened(const std::string& path, const std::string& text) override;
  void on_document_changed(const std::string& path, const std::string& text) override;
  void on_document_closed(const std::string& path) override;

  bool lsp_active() const { return use_lsp_; }

  void set_lsp_enabled(bool enabled);
  bool lsp_enabled() const;

 private:
  enum class AsyncJobKind { DocumentSymbols, SemanticTokens };

  struct AsyncJob {
    AsyncJobKind kind;
    std::string path;
  };

  struct AsyncResult {
    AsyncJobKind kind;
    std::string path;
  };

  std::string buffer_text_for_path(const std::string& path) const;
  void refresh_diagnostics_cache_locked() const;
  void start_lsp_locked();
  void stop_lsp_locked();
  void start_async_worker_locked();
  void stop_async_worker_locked();
  void async_worker_main();
  void enqueue_document_symbols_locked(const std::string& path, bool force = false);
  void enqueue_semantic_tokens_locked(const std::string& path);
  bool symbols_lsp_pending_locked(const std::string& path) const;
  void tick_content_refresh_locked();
  static int64_t steady_now_ms();

  mutable std::mutex mutex_;
  LspClient client_;
  RegexSymbolProvider fallback_;
  bool lsp_enabled_ = true;
  bool use_lsp_ = false;
  std::string workspace_root_;
  std::unordered_map<std::string, std::string> open_buffers_;
  mutable uint64_t cached_diag_revision_ = 0;
  mutable std::vector<DocumentDiagnostics> cached_diagnostics_;

  ThreadSafeQueue<AsyncJob> async_jobs_;
  ThreadSafeQueue<AsyncResult> async_results_;
  std::thread async_worker_;
  std::atomic<bool> async_stop_{false};
  mutable std::mutex inflight_mutex_;
  std::unordered_set<std::string> inflight_symbols_;
  std::unordered_set<std::string> inflight_semantic_;
  std::unordered_map<std::string, int64_t> pending_content_refresh_;
  std::atomic<uint64_t> semantic_highlight_revision_{0};
  std::atomic<uint64_t> document_symbols_revision_{0};
};

}  // namespace tgdb
