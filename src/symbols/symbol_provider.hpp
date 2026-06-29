#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lsp/lsp_text_edits.hpp"
#include "lsp/semantic_tokens.hpp"
#include "lsp/diagnostics.hpp"
#include "symbols/call_hierarchy.hpp"
#include "symbols/hover_info.hpp"

namespace tgdb {

enum class SymbolKind { kNamespace, kClass, kStruct, kFunction, kMethod, kVariable };

struct SymbolInfo {
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  int depth = 0;
  std::string file;  // ruta relativa al workspace (opcional)
};

enum class InsertTextFormat { kPlain = 1, kSnippet = 2 };

struct CompletionItem {
  std::string label;
  std::string insert_text;
  std::string detail;
  SymbolKind kind = SymbolKind::kVariable;
  std::string file;
  bool has_replace_range = false;
  int replace_line = 0;
  int replace_start = 0;
  int replace_end = 0;
  InsertTextFormat insert_format = InsertTextFormat::kPlain;
};

struct CompletionParams {
  std::string path;
  std::string text;
  int line = 0;
  int character = 0;
};

struct FormatParams {
  std::string path;
  std::string text;
};

struct RenameParams {
  std::string path;
  std::string text;
  int line = 0;
  int character = 0;
  std::string new_name;
};

struct SourceLocation {
  std::string path;
  int line = 0;
  int character = 0;
  bool valid = false;
};

struct NavigationParams {
  std::string path;
  std::string text;
  int line = 0;
  int character = 0;
};

class ISymbolProvider {
 public:
  virtual ~ISymbolProvider() = default;
  virtual std::vector<SymbolInfo> symbols_for_file(const std::string& path) = 0;

  virtual bool symbols_lsp_pending(const std::string& path) const {
    (void)path;
    return false;
  }
  virtual bool drain_async_results() { return false; }
  virtual uint64_t semantic_highlight_revision() const { return 0; }
  virtual uint64_t document_symbols_revision() const { return 0; }

  virtual bool indexes_workspace_bulk() const { return true; }
  virtual std::vector<SymbolInfo> workspace_symbols(const std::string& workspace_root,
                                                      const std::string& query) {
    (void)workspace_root;
    (void)query;
    return {};
  }

  virtual bool supports_semantic_completion() const { return false; }
  virtual std::vector<CompletionItem> completions_at(const CompletionParams& params) {
    (void)params;
    return {};
  }

  virtual bool supports_navigation() const { return false; }
  virtual SourceLocation goto_definition(const NavigationParams& params) {
    (void)params;
    return {};
  }
  virtual SourceLocation goto_declaration(const NavigationParams& params) {
    (void)params;
    return {};
  }

  virtual void on_workspace_opened(const std::string& root) { (void)root; }
  virtual void on_workspace_closed() {}
  virtual void on_document_opened(const std::string& path, const std::string& text) {
    (void)path;
    (void)text;
  }
  virtual void on_document_changed(const std::string& path, const std::string& text) {
    (void)path;
    (void)text;
  }
  virtual void on_document_closed(const std::string& path) { (void)path; }

  virtual bool supports_semantic_highlight() const { return false; }
  virtual bool ensure_semantic_tokens(const std::string& path) {
    (void)path;
    return false;
  }
  virtual SemanticTokenDocument semantic_tokens_for_file(const std::string& path) {
    (void)path;
    return {};
  }

  virtual bool supports_hover() const { return false; }
  virtual HoverInfo hover_at(const HoverParams& params) {
    (void)params;
    return {};
  }

  virtual bool supports_diagnostics() const { return false; }
  virtual uint64_t diagnostics_revision() const { return 0; }
  virtual DocumentDiagnostics diagnostics_for_file(const std::string& path) {
    (void)path;
    return {};
  }
  virtual std::vector<DocumentDiagnostics> workspace_diagnostics() { return {}; }

  virtual bool supports_formatting() const { return false; }
  virtual std::optional<std::string> format_document(const FormatParams& params) {
    (void)params;
    return std::nullopt;
  }

  virtual bool supports_rename() const { return false; }
  virtual std::vector<LspFileEdits> rename_symbol(const RenameParams& params) {
    (void)params;
    return {};
  }

  virtual bool supports_call_hierarchy() const { return false; }
  virtual std::vector<CallHierarchyItem> prepare_call_hierarchy(const CallHierarchyParams& params) {
    (void)params;
    return {};
  }
  virtual std::vector<CallHierarchyItem> incoming_calls(const CallHierarchyItem& item) {
    (void)item;
    return {};
  }
  virtual std::vector<CallHierarchyItem> outgoing_calls(const CallHierarchyItem& item) {
    (void)item;
    return {};
  }
};

}  // namespace tgdb
