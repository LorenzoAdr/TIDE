#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lsp/lsp_text_edits.hpp"
#include "lsp/semantic_tokens.hpp"
#include "lsp/diagnostics.hpp"
#include "symbols/call_hierarchy.hpp"
#include "symbols/code_action.hpp"
#include "symbols/hover_info.hpp"
#include "symbols/symbol_kind.hpp"

#include "util/path_normalize.hpp"

namespace tgdb {

struct SymbolInfo {
  std::string name;
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  int end_line = 0;  // 1-based inclusive; 0 = inferir del siguiente símbolo
  int depth = 0;
  std::string file;  // ruta relativa al workspace (opcional)
};

enum class InsertTextFormat { kPlain = 1, kSnippet = 2 };

struct CompletionItem {
  std::string label;
  std::string insert_text;
  std::string detail;
  std::string sort_text;
  std::string filter_text;
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
  virtual bool lsp_loading() const { return false; }
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
  virtual bool completion_uses_async_fetch() const { return false; }
  virtual void request_completion(const CompletionParams& params, const std::string& cache_key) {
    (void)params;
    (void)cache_key;
  }
  virtual std::optional<std::vector<CompletionItem>> poll_completion(
      const std::string& cache_key) {
    (void)cache_key;
    return std::nullopt;
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
  virtual SourceLocation goto_implementation(const NavigationParams& params) {
    (void)params;
    return {};
  }

  virtual void on_workspace_opened(const std::string& root,
                                   const std::string& compile_commands_dir = {}) {
    (void)root;
    (void)compile_commands_dir;
  }
  virtual void on_workspace_closed() {}
  virtual void on_document_opened(const std::string& path, const std::string& text) {
    (void)path;
    (void)text;
  }
  virtual void on_document_changed(const std::string& path, const std::string& text) {
    (void)path;
    (void)text;
  }
  virtual void on_document_saved(const std::string& path) { (void)path; }
  virtual void on_document_closed(const std::string& path) { (void)path; }
  virtual void tick_debounced_updates() {}
  virtual void flush_document_sync(const std::string& path) { (void)path; }

  virtual bool supports_semantic_highlight() const { return false; }
  virtual bool ensure_semantic_tokens(const std::string& path) {
    (void)path;
    return false;
  }
  virtual SemanticTokenDocument semantic_tokens_for_file(const std::string& path) {
    (void)path;
    return {};
  }
  virtual bool semantic_tokens_current_for_file(const std::string& path) {
    (void)path;
    return true;
  }
  virtual void invalidate_semantic_tokens_for_file(const std::string& path) { (void)path; }
  virtual uint64_t document_generation_for_file(const std::string& path) const {
    (void)path;
    return 0;
  }

  virtual bool supports_hover() const { return false; }
  virtual bool hover_uses_async_fetch() const { return false; }
  virtual void request_hover(const HoverParams& params, const std::string& cache_key) {
    (void)params;
    (void)cache_key;
  }
  virtual std::optional<HoverInfo> poll_hover(const std::string& cache_key) {
    (void)cache_key;
    return std::nullopt;
  }
  virtual HoverInfo hover_at(const HoverParams& params) {
    (void)params;
    return {};
  }

  virtual bool supports_diagnostics() const { return false; }
  virtual uint64_t diagnostics_revision() const { return 0; }
  virtual bool document_sync_pending(const std::string& path) const {
    (void)path;
    return false;
  }
  virtual bool diagnostics_display_ready(const std::string& path) const {
    (void)path;
    return true;
  }
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

  virtual bool supports_code_actions() const { return false; }
  virtual std::vector<CodeActionItem> code_actions_for_diagnostic(const CodeActionParams& params) {
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

inline bool navigation_at_same_spot(const SourceLocation& loc, const NavigationParams& params) {
  if (!loc.valid || params.path.empty()) {
    return false;
  }
  return normalize_path(loc.path) == normalize_path(params.path) && loc.line == params.line &&
         loc.character == params.character;
}

inline SourceLocation resolve_symbol_navigation(ISymbolProvider& symbols,
                                                const NavigationParams& params,
                                                bool declaration) {
  SourceLocation primary =
      declaration ? symbols.goto_declaration(params) : symbols.goto_definition(params);
  if (!primary.valid) {
    SourceLocation alternate =
        declaration ? symbols.goto_definition(params) : symbols.goto_declaration(params);
    if (alternate.valid && !navigation_at_same_spot(alternate, params)) {
      return alternate;
    }
    if (!declaration) {
      SourceLocation impl = symbols.goto_implementation(params);
      if (impl.valid && !navigation_at_same_spot(impl, params)) {
        return impl;
      }
    }
    return alternate;
  }
  if (navigation_at_same_spot(primary, params)) {
    SourceLocation alternate =
        declaration ? symbols.goto_definition(params) : symbols.goto_declaration(params);
    if (alternate.valid && !navigation_at_same_spot(alternate, params)) {
      return alternate;
    }
    if (!declaration) {
      SourceLocation impl = symbols.goto_implementation(params);
      if (impl.valid && !navigation_at_same_spot(impl, params)) {
        return impl;
      }
    }
  }
  return primary;
}

inline SourceLocation resolve_implementation_navigation(ISymbolProvider& symbols,
                                                        const NavigationParams& params) {
  SourceLocation impl = symbols.goto_implementation(params);
  if (impl.valid && !navigation_at_same_spot(impl, params)) {
    return impl;
  }
  SourceLocation def = symbols.goto_definition(params);
  if (def.valid && !navigation_at_same_spot(def, params)) {
    return def;
  }
  return symbols.goto_declaration(params);
}

}  // namespace tgdb
