#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "lsp/lsp_transport.hpp"
#include "lsp/semantic_tokens.hpp"
#include "symbols/symbol_provider.hpp"

namespace tgdb {

class LspClient {
 public:
  LspClient();
  ~LspClient();

  bool start(const std::string& workspace_root);
  void stop();
  bool ready() const { return ready_.load(); }

  void did_open(const std::string& absolute_path, const std::string& text);
  void did_change(const std::string& absolute_path, const std::string& text);
  void did_close(const std::string& absolute_path);

  std::vector<SymbolInfo> document_symbols(const std::string& absolute_path);
  std::vector<SymbolInfo> workspace_symbols(const std::string& workspace_root,
                                            const std::string& query);
  std::vector<CompletionItem> completions_at(const std::string& absolute_path,
                                               const std::string& text, int line,
                                               int character);
  SourceLocation goto_definition(const std::string& absolute_path, const std::string& text,
                                 int line, int character);
  SourceLocation goto_declaration(const std::string& absolute_path, const std::string& text,
                                  int line, int character);

  SemanticTokenDocument semantic_tokens_for_file(const std::string& absolute_path);
  bool ensure_semantic_tokens(const std::string& absolute_path);

 private:
  struct DocumentState {
    std::string uri;
    std::string text;
    int version = 0;
  };

  struct SemanticTokenAttempt {
    int count = 0;
    int64_t last_ms = 0;
  };

  bool spawn_clangd(const std::string& workspace_root);
  bool initialize(const std::string& workspace_root);
  std::string find_compile_commands_dir(const std::string& workspace_root) const;
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
  SourceLocation request_location(const std::string& method, const std::string& absolute_path,
                                  const std::string& text, int line, int character);

  LspTransport transport_;
  std::atomic<bool> ready_{false};
  pid_t child_pid_ = -1;
  int stdin_write_fd_ = -1;
  int stdout_read_fd_ = -1;
  std::string workspace_root_;

  std::mutex mutex_;
  std::unordered_map<std::string, DocumentState> documents_;
  std::unordered_map<std::string, std::vector<SymbolInfo>> symbol_cache_;
  std::unordered_map<std::string, SemanticTokenDocument> semantic_token_cache_;
  std::unordered_map<std::string, SemanticTokenAttempt> semantic_token_attempts_;
  std::vector<std::string> semantic_token_types_;
  bool semantic_tokens_supported_ = false;
  int next_request_id_ = 1;
};

}  // namespace tgdb
