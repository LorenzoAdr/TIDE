#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "lsp/lsp_client.hpp"
#include "lsp/language_server_spec.hpp"
#include "parser/tree_sitter_service.hpp"
#include "symbols/tree_sitter_symbol_provider.hpp"
#include "symbols/symbol_provider.hpp"
#include "util/thread_safe_queue.hpp"

namespace tuide {

enum class LspAsyncJobKind { DocumentSymbols, SemanticTokens, Hover, Completion };

class LspSymbolProvider : public ISymbolProvider {
 public:
  LspSymbolProvider();
  ~LspSymbolProvider() override;

  std::vector<SymbolInfo> symbols_for_file(const std::string& path) override;
  bool symbols_lsp_pending(const std::string& path) const override;
  bool drain_async_results() override;
  bool async_drain_invalidates_view() const override;
  uint64_t semantic_highlight_revision() const override;
  uint64_t document_symbols_revision() const override;
  bool indexes_workspace_bulk() const override;
  std::vector<SymbolInfo> workspace_symbols(const std::string& workspace_root,
                                              const std::string& query) override;
  bool supports_semantic_completion() const override;
  std::vector<CompletionItem> completions_at(const CompletionParams& params) override;
  bool completion_uses_async_fetch() const override;
  bool request_completion(const CompletionParams& params, const std::string& cache_key) override;
  void cancel_completion_fetch() override;
  void cancel_hover_fetch() override;
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
  void invalidate_semantic_tokens_for_file(const std::string& path) override;
  uint64_t document_generation_for_file(const std::string& path) const override;

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
  bool supports_call_hierarchy(const std::string& path) const override;
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

  bool clangd_ready() const;
  bool python_lsp_ready() const;
  bool bash_lsp_ready() const;
  bool tex_lsp_ready() const;
  bool rust_lsp_ready() const;
  bool go_lsp_ready() const;
  bool zig_lsp_ready() const;
  bool fortran_lsp_ready() const;
  bool lua_lsp_ready() const;
  bool typescript_lsp_ready() const;
  bool cmake_lsp_ready() const;
  bool make_lsp_ready() const;
  bool clangd_starting() const;
  bool python_lsp_starting() const;
  bool bash_lsp_starting() const;
  bool tex_lsp_starting() const;
  bool rust_lsp_starting() const;
  bool go_lsp_starting() const;
  bool zig_lsp_starting() const;
  bool fortran_lsp_starting() const;
  bool lua_lsp_starting() const;
  bool typescript_lsp_starting() const;
  bool cmake_lsp_starting() const;
  bool make_lsp_starting() const;

  void set_lsp_enabled(bool enabled);
  bool lsp_enabled() const;
  void set_workspace_clangd_options(bool use_gcc_query_driver, bool background_index);
  void set_ui_inhibited(bool inhibited);
  void set_lsp_request_counter(std::atomic<uint64_t>* counter);
  void set_async_job_ready_callback(std::function<void(LspAsyncJobKind)> callback);
  void set_diagnostics_notify_callback(std::function<void(const std::string& path)> callback);
  void set_did_change_debounce_callback(std::function<void()> callback);
  void set_lsp_status_callback(std::function<void(const std::string& i18n_key)> callback);

 private:
  enum class AsyncJobKind { DocumentSymbols, SemanticTokens, Hover, Completion };
  static LspAsyncJobKind to_public_job_kind(AsyncJobKind kind);
  void notify_async_job_ready(AsyncJobKind kind);

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
  void ensure_python_lsp_async();
  void ensure_bash_lsp_async();
  void ensure_tex_lsp_async();
  void ensure_rust_lsp_async(const std::string& hint_path = {});
  void ensure_go_lsp_async();
  void ensure_zig_lsp_async();
  void ensure_fortran_lsp_async();
  void refresh_fortran_compiler_diagnostics(const std::string& path, const std::string& text);
  void ensure_lua_lsp_async();
  void ensure_typescript_lsp_async();
  void ensure_cmake_lsp_async();
  void ensure_make_lsp_async();
  void finish_lsp_start_locked(bool ok);
  void finish_python_lsp_start_locked(bool ok, bool binary_missing);
  void finish_bash_lsp_start_locked(bool ok, bool binary_missing);
  void finish_tex_lsp_start_locked(bool ok, bool binary_missing);
  void finish_rust_lsp_start_locked(bool ok, bool binary_missing);
  void finish_go_lsp_start_locked(bool ok, bool binary_missing);
  void finish_zig_lsp_start_locked(bool ok, bool binary_missing);
  void finish_fortran_lsp_start_locked(bool ok, bool binary_missing);
  void finish_lua_lsp_start_locked(bool ok, bool binary_missing);
  void finish_typescript_lsp_start_locked(bool ok, bool binary_missing);
  void finish_cmake_lsp_start_locked(bool ok, bool binary_missing);
  void finish_make_lsp_start_locked(bool ok, bool binary_missing);
  void notify_lsp_status(const char* i18n_key);
  void join_startup_thread();
  void join_python_startup_thread();
  void join_bash_startup_thread();
  void join_tex_startup_thread();
  void join_rust_startup_thread();
  void join_go_startup_thread();
  void join_zig_startup_thread();
  void join_fortran_startup_thread();
  void join_lua_startup_thread();
  void join_typescript_startup_thread();
  void join_cmake_startup_thread();
  void join_make_startup_thread();
  struct SimpleLazyLspConfig {
    const char* thread_name;
    const char* language_id;
    const char* missing_i18n;
    const char* failed_i18n;
    const char* started_i18n;
    const char* monitor_message;
    std::function<std::optional<LanguageServerSpec>(const std::string&)> make_spec;
  };
  void finish_simple_lazy_lsp_start_locked(std::unique_ptr<LspClient>& client,
                                           const SimpleLazyLspConfig& cfg, bool ok,
                                           bool binary_missing);
  void ensure_simple_lazy_lsp_async(std::unique_ptr<LspClient>& client,
                                    std::atomic<bool>& starting, std::thread& startup_thread,
                                    const SimpleLazyLspConfig& cfg,
                                    void (LspSymbolProvider::*finish_fn)(bool, bool));
  void stop_lsp();
  void stop_lsp_locked_finalize();
  void restart_lsp_after_transport_failure();
  void process_pending_transport_restart();
  void start_async_worker();
  void ensure_async_worker_running();
  void stop_async_worker();
  void signal_async_worker_stop_locked();
  void reset_async_queues_locked();
  void async_worker_main();
  void enqueue_document_symbols_locked(const std::string& path, bool force = false);
  void enqueue_semantic_tokens_locked(const std::string& path, bool force = false);
  bool symbols_lsp_pending_locked(const std::string& path) const;
  void tick_content_refresh_locked();
  void tick_pending_did_change_locked();
  void flush_pending_did_change_for_key_locked(const std::string& key);
  void flush_pending_did_change_for_key(const std::string& key);
  void flush_all_pending_did_change_locked();
  void schedule_did_change_debounce_wake();
  void request_did_change_wake_after(int64_t delay_ms);
  void did_change_timer_main();
  bool sync_document_for_completion(const std::string& path, const std::string& text);
  void open_companion_sources_for_clangd_locked(const std::string& header_path);
  void clear_shadow_companion_locked(const std::string& companion_path);
  bool buffer_open_locked(const std::string& path) const;
  LspClient* client_for_path(const std::string& path);
  const LspClient* client_for_path(const std::string& path) const;
  bool any_lsp_ready() const;
  void ensure_lazy_lsp_for_path(const std::string& path);
  bool wait_for_client_for_path(const std::string& path, int timeout_ms);
  bool lazy_lsp_starting_for_path(const std::string& path) const;
  LspClient* prepare_lsp_client(const std::string& path, std::string& text);
  static int64_t steady_now_ms();

  mutable std::mutex mutex_;
  LspClient client_;  // clangd (C/C++)
  std::unique_ptr<LspClient> python_client_;  // basedpyright (lazy)
  std::unique_ptr<LspClient> bash_client_;    // bash-language-server (lazy)
  std::unique_ptr<LspClient> tex_client_;     // texlab (lazy)
  std::unique_ptr<LspClient> rust_client_;
  std::unique_ptr<LspClient> go_client_;
  std::unique_ptr<LspClient> zig_client_;
  std::unique_ptr<LspClient> fortran_client_;
  // fortls is not a compiler; overlay gfortran -fsyntax-only diagnostics (VS Code parity).
  std::unordered_map<std::string, DocumentDiagnostics> fortran_compiler_diagnostics_;
  std::atomic<uint64_t> fortran_compiler_diag_revision_{0};
  std::unique_ptr<LspClient> lua_client_;
  std::unique_ptr<LspClient> typescript_client_;
  std::unique_ptr<LspClient> cmake_client_;
  std::unique_ptr<LspClient> make_client_;
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
  std::thread python_lsp_startup_thread_;
  std::thread bash_lsp_startup_thread_;
  std::thread tex_lsp_startup_thread_;
  std::thread rust_lsp_startup_thread_;
  std::thread go_lsp_startup_thread_;
  std::thread zig_lsp_startup_thread_;
  std::thread fortran_lsp_startup_thread_;
  std::thread lua_lsp_startup_thread_;
  std::thread typescript_lsp_startup_thread_;
  std::thread cmake_lsp_startup_thread_;
  std::thread make_lsp_startup_thread_;
  std::atomic<bool> lsp_starting_{false};
  std::atomic<bool> python_lsp_starting_{false};
  std::atomic<bool> bash_lsp_starting_{false};
  std::atomic<bool> tex_lsp_starting_{false};
  std::atomic<bool> rust_lsp_starting_{false};
  std::atomic<bool> go_lsp_starting_{false};
  std::atomic<bool> zig_lsp_starting_{false};
  std::atomic<bool> fortran_lsp_starting_{false};
  std::atomic<bool> lua_lsp_starting_{false};
  std::atomic<bool> typescript_lsp_starting_{false};
  std::atomic<bool> cmake_lsp_starting_{false};
  std::atomic<bool> make_lsp_starting_{false};
  std::atomic<bool> async_stop_{false};
  mutable std::mutex inflight_mutex_;
  std::unordered_set<std::string> inflight_symbols_;
  std::unordered_set<std::string> inflight_semantic_;
  std::unordered_set<std::string> inflight_hover_;
  std::unordered_set<std::string> inflight_completion_;
  std::unordered_map<std::string, HoverInfo> hover_cache_;
  struct CachedCompletion {
    std::vector<CompletionItem> items;
    int request_id = 0;
  };
  std::unordered_map<std::string, CachedCompletion> completion_cache_;
  std::unordered_map<std::string, std::string> latest_completion_key_by_path_;
  std::unordered_map<std::string, int64_t> last_completion_document_sync_ms_;
  std::unordered_map<std::string, int64_t> pending_semantic_refresh_;
  std::unordered_map<std::string, int64_t> pending_content_refresh_;
  std::unordered_map<std::string, int64_t> pending_did_change_;
  std::atomic<uint64_t> semantic_highlight_revision_{0};
  std::atomic<uint64_t> document_symbols_revision_{0};
  std::atomic<bool> pending_transport_restart_{false};
  std::atomic<bool> lsp_restart_in_progress_{false};
  std::atomic<bool> shutting_down_{false};
  std::mutex lifecycle_mutex_;
  std::condition_variable lifecycle_cv_;
  std::atomic<bool> stop_lsp_in_progress_{false};
  std::thread lsp_restart_thread_;
  int64_t last_lsp_failure_restart_ms_ = 0;
  int64_t lsp_ready_since_ms_ = 0;
  bool async_drain_invalidates_view_ = true;
  std::function<void(LspAsyncJobKind)> async_job_ready_callback_;
  std::mutex async_job_ready_callback_mutex_;
  std::function<void(const std::string& path)> diagnostics_notify_callback_;
  std::function<void()> did_change_debounce_callback_;
  std::function<void(const std::string& i18n_key)> lsp_status_callback_;
  std::mutex lsp_status_callback_mutex_;
  std::mutex did_change_debounce_callback_mutex_;
  std::thread did_change_timer_;
  std::atomic<bool> did_change_timer_stop_{false};
  std::mutex did_change_timer_mutex_;
  std::condition_variable did_change_timer_cv_;
  int64_t did_change_timer_fire_at_ms_ = 0;
};

}  // namespace tuide
