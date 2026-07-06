#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/lsp_transport.hpp"
#include "lsp/lsp_text_edits.hpp"
#include "lsp/semantic_tokens.hpp"
#include "lsp/diagnostics.hpp"
#include "symbols/call_hierarchy.hpp"
#include "symbols/code_action.hpp"
#include "symbols/hover_info.hpp"
#include "symbols/symbol_provider.hpp"

namespace tgdb {

class LspClient {
 public:
  LspClient();
  ~LspClient();

  bool start(const std::string& workspace_root,
             const std::string& compile_commands_dir = {},
             bool use_gcc_query_driver = true, bool background_index = false);
  void stop();
  bool ready() const { return ready_.load(); }
  bool transport_running() const;
  bool clangd_process_alive() const;
  bool semantic_tokens_supported() const { return semantic_tokens_supported_; }

  void did_open(const std::string& absolute_path, const std::string& text);
  void did_change(const std::string& absolute_path, const std::string& text);
  void did_close(const std::string& absolute_path);

  std::vector<SymbolInfo> document_symbols(const std::string& absolute_path);
  bool has_cached_document_symbols(const std::string& absolute_path) const;
  std::optional<std::vector<SymbolInfo>> cached_document_symbols(
      const std::string& absolute_path) const;
  std::vector<SymbolInfo> workspace_symbols(const std::string& workspace_root,
                                            const std::string& query);
  std::vector<CompletionItem> completions_at(const std::string& absolute_path,
                                               const std::string& text, int line,
                                               int character);
  SourceLocation goto_definition(const std::string& absolute_path, const std::string& text,
                                 int line, int character);
  SourceLocation goto_declaration(const std::string& absolute_path, const std::string& text,
                                  int line, int character);
  SourceLocation goto_implementation(const std::string& absolute_path, const std::string& text,
                                   int line, int character);

  SemanticTokenDocument semantic_tokens_for_file(const std::string& absolute_path);
  bool has_ready_semantic_tokens(const std::string& absolute_path) const;
  bool ensure_semantic_tokens(const std::string& absolute_path);
  void invalidate_semantic_tokens_for_file(const std::string& absolute_path);

  HoverInfo hover(const std::string& absolute_path, const std::string& text, int line,
                  int character);

  std::optional<std::string> format_document(const std::string& absolute_path,
                                             const std::string& text);

  std::vector<LspFileEdits> rename_symbol(const std::string& absolute_path,
                                          const std::string& text, int line, int character,
                                          const std::string& new_name);

  std::vector<CodeActionItem> code_actions(const CodeActionParams& params);
  std::vector<LspFileEdits> resolve_code_action_edits(const nlohmann::json& action);

  std::vector<CallHierarchyItem> prepare_call_hierarchy(const std::string& absolute_path,
                                                        const std::string& text, int line,
                                                        int character);
  std::vector<CallHierarchyItem> incoming_calls(const CallHierarchyItem& item);
  std::vector<CallHierarchyItem> outgoing_calls(const CallHierarchyItem& item);

  DocumentDiagnostics diagnostics_for_file(const std::string& absolute_path);
  bool document_diagnostics_current(const std::string& absolute_path) const;
  std::vector<DocumentDiagnostics> all_diagnostics() const;
  uint64_t diagnostics_revision() const { return diagnostics_revision_.load(); }

 private:
  struct DocumentState {
    std::string uri;
    std::string text;
    int version = 0;
    uint64_t generation = 0;
    uint64_t diagnostics_generation = 0;
    uint64_t idle_generation = 0;
  };

  uint64_t document_generation(const std::string& absolute_path) const;
  void sync_document_and_wait(const std::string& absolute_path, const std::string& text);
  void wait_for_completion_ready(const std::string& absolute_path, const std::string& text,
                                 int line, int timeout_ms);
  bool wait_for_document_ready(const std::string& key, uint64_t generation, int timeout_ms);
  bool document_semantic_tokens_cover_line(const std::string& key, uint64_t generation,
                                           int line) const;
  static int parse_wait_timeout_ms(const std::string& text);
  static int completion_wait_timeout_ms(const std::string& text, int line);

  struct SemanticTokenAttempt {
    int count = 0;
    int64_t last_ms = 0;
  };

  bool spawn_clangd(const std::string& workspace_root, const std::string& compile_commands_dir,
                    bool use_gcc_query_driver, bool background_index);
  bool initialize(const std::string& workspace_root);
  void invalidate_cache(const std::string& absolute_path);
  void invalidate_semantic_tokens(const std::string& absolute_path);
  bool refresh_semantic_tokens(const std::string& absolute_path);
  static int64_t steady_now_ms();
  static SemanticTokenDocument decode_semantic_tokens(const nlohmann::json& result,
                                                      const std::vector<std::string>& token_types);
  static std::vector<std::string> default_semantic_token_types();
  void load_semantic_legend(const nlohmann::json& initialize_result);

  static SymbolKind map_lsp_kind(int kind);
  static std::string kind_prefix(SymbolKind kind);
  static void flatten_symbols(const nlohmann::json& nodes, int depth,
                              const std::string& relative_file,
                              std::vector<SymbolInfo>* out);
  static CompletionItem parse_completion_item(const nlohmann::json& item);
  static std::string completion_label(const nlohmann::json& item);
  static SymbolKind map_completion_kind(int kind);
  static SourceLocation parse_location_result(const nlohmann::json& result);
  static bool parse_single_location(const nlohmann::json& loc, SourceLocation* out);
  static HoverInfo parse_hover_result(const nlohmann::json& result);
  static void append_hover_content(const nlohmann::json& content, HoverInfo* out);
  static std::string strip_markdown(const std::string& text);
  void on_lsp_notification(const std::string& method, const nlohmann::json& params);
  static Diagnostic parse_diagnostic(const nlohmann::json& item);
  static DocumentDiagnostics parse_publish_diagnostics(const nlohmann::json& params);
  SourceLocation request_location(const std::string& method, const std::string& absolute_path,
                                  const std::string& text, int line, int character);

  bool send_lsp_request(const std::string& method, nlohmann::json params, int timeout_ms,
                        nlohmann::json* out);
  void send_lsp_notification(const std::string& method, nlohmann::json params);
  void on_transport_reader_eof();

  LspTransport transport_;
  std::mutex transport_io_mutex_;
  std::atomic<bool> ready_{false};
  std::atomic<bool> intentionally_stopping_{false};
  pid_t child_pid_ = -1;
  int stdin_write_fd_ = -1;
  int stdout_read_fd_ = -1;
  std::string workspace_root_;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, DocumentState> documents_;
  std::unordered_map<std::string, std::vector<SymbolInfo>> symbol_cache_;
  std::unordered_map<std::string, SemanticTokenDocument> semantic_token_cache_;
  std::unordered_map<std::string, SemanticTokenAttempt> semantic_token_attempts_;
  std::unordered_map<std::string, DocumentDiagnostics> diagnostics_;
  std::atomic<uint64_t> diagnostics_revision_{0};
  std::vector<std::string> semantic_token_types_;
  bool semantic_tokens_supported_ = false;
  int next_request_id_ = 1;
};

}  // namespace tgdb
