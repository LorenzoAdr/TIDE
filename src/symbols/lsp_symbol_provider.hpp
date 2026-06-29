#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "lsp/lsp_client.hpp"
#include "symbols/regex_symbol_provider.hpp"
#include "symbols/symbol_provider.hpp"

namespace tgdb {

class LspSymbolProvider : public ISymbolProvider {
 public:
  LspSymbolProvider();
  ~LspSymbolProvider() override;

  std::vector<SymbolInfo> symbols_for_file(const std::string& path) override;
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

  void on_workspace_opened(const std::string& root) override;
  void on_workspace_closed() override;
  void on_document_opened(const std::string& path, const std::string& text) override;
  void on_document_changed(const std::string& path, const std::string& text) override;
  void on_document_closed(const std::string& path) override;

  bool lsp_active() const { return use_lsp_; }

  void set_lsp_enabled(bool enabled);
  bool lsp_enabled() const;

 private:
  std::string buffer_text_for_path(const std::string& path) const;
  void refresh_diagnostics_cache_locked() const;
  void start_lsp_locked();
  void stop_lsp_locked();

  mutable std::mutex mutex_;
  LspClient client_;
  RegexSymbolProvider fallback_;
  bool lsp_enabled_ = true;
  bool use_lsp_ = false;
  std::string workspace_root_;
  std::unordered_map<std::string, std::string> open_buffers_;
  mutable uint64_t cached_diag_revision_ = 0;
  mutable std::vector<DocumentDiagnostics> cached_diagnostics_;
};

}  // namespace tgdb
