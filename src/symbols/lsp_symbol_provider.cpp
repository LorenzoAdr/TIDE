#include "symbols/lsp_symbol_provider.hpp"

#include "indexer/index_rules.hpp"
#include "lsp/lsp_uri.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string read_file_text(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

}  // namespace

void LspSymbolProvider::refresh_diagnostics_cache_locked() const {
  const uint64_t revision = client_.diagnostics_revision();
  if (revision == cached_diag_revision_) {
    return;
  }
  cached_diagnostics_ = client_.all_diagnostics();
  cached_diag_revision_ = revision;
}

LspSymbolProvider::LspSymbolProvider() = default;

LspSymbolProvider::~LspSymbolProvider() {
  on_workspace_closed();
}

void LspSymbolProvider::on_workspace_opened(const std::string& root) {
  std::lock_guard<std::mutex> lock(mutex_);
  client_.stop();
  open_buffers_.clear();
  workspace_root_ = root;
  use_lsp_ = client_.start(root);
}

void LspSymbolProvider::on_workspace_closed() {
  std::lock_guard<std::mutex> lock(mutex_);
  client_.stop();
  use_lsp_ = false;
  workspace_root_.clear();
  open_buffers_.clear();
  cached_diag_revision_ = 0;
  cached_diagnostics_.clear();
}

void LspSymbolProvider::on_document_opened(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    open_buffers_[path] = text;
  }
  if (use_lsp_ && is_lsp_trackable_path(path, text)) {
    client_.did_open(path, text);
  }
}

void LspSymbolProvider::on_document_changed(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    open_buffers_[path] = text;
  }
  if (use_lsp_ && is_lsp_trackable_path(path, text)) {
    client_.did_change(path, text);
  }
}

void LspSymbolProvider::on_document_closed(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  open_buffers_.erase(path);
  if (use_lsp_) {
    client_.did_close(path);
  }
}

std::string LspSymbolProvider::buffer_text_for_path(const std::string& path) const {
  const auto it = open_buffers_.find(path);
  if (it != open_buffers_.end()) {
    return it->second;
  }
  return read_file_text(path);
}

bool LspSymbolProvider::indexes_workspace_bulk() const {
  return !use_lsp_;
}

std::vector<SymbolInfo> LspSymbolProvider::symbols_for_file(const std::string& path) {
  if (path.empty()) {
    return {};
  }

  bool use_lsp = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    use_lsp = use_lsp_;
  }

  if (use_lsp && is_indexed_source_path(path)) {
    const std::string text = buffer_text_for_path(path);
    if (!text.empty() && is_lsp_trackable_path(path, text)) {
      on_document_opened(path, text);
    }
    auto symbols = client_.document_symbols(path);
    if (!symbols.empty()) {
      return symbols;
    }
  }
  return fallback_.symbols_for_file(path);
}

std::vector<SymbolInfo> LspSymbolProvider::workspace_symbols(const std::string& workspace_root,
                                                               const std::string& query) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  (void)workspace_root;
  return client_.workspace_symbols(workspace_root_, query);
}

bool LspSymbolProvider::supports_semantic_completion() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_;
}

std::vector<CompletionItem> LspSymbolProvider::completions_at(
    const CompletionParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }

  const std::string key = normalize_lsp_path(params.path);
  if (key.empty()) {
    return {};
  }

  const std::string text =
      params.text.empty() ? buffer_text_for_path(key) : params.text;
  return client_.completions_at(key, text, params.line, params.character);
}

bool LspSymbolProvider::supports_navigation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_;
}

SourceLocation LspSymbolProvider::goto_definition(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  const std::string text =
      params.text.empty() ? buffer_text_for_path(params.path) : params.text;
  return client_.goto_definition(params.path, text, params.line, params.character);
}

SourceLocation LspSymbolProvider::goto_declaration(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  const std::string text =
      params.text.empty() ? buffer_text_for_path(params.path) : params.text;
  return client_.goto_declaration(params.path, text, params.line, params.character);
}

bool LspSymbolProvider::supports_semantic_highlight() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_;
}

bool LspSymbolProvider::ensure_semantic_tokens(const std::string& path) {
  if (path.empty() || !is_lsp_trackable_path(path)) {
    return false;
  }
  bool active = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active = use_lsp_;
  }
  if (!active) {
    return false;
  }
  return client_.ensure_semantic_tokens(path);
}

SemanticTokenDocument LspSymbolProvider::semantic_tokens_for_file(const std::string& path) {
  if (path.empty() || !is_lsp_trackable_path(path)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  return client_.semantic_tokens_for_file(path);
}

bool LspSymbolProvider::supports_hover() const {
  return true;
}

HoverInfo LspSymbolProvider::hover_at(const HoverParams& params) {
  if (params.path.empty()) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (use_lsp_ && is_lsp_trackable_path(params.path, params.text)) {
    const std::string text =
        params.text.empty() ? buffer_text_for_path(params.path) : params.text;
    HoverInfo info =
        client_.hover(params.path, text, params.line, params.character);
    if (info.valid) {
      return info;
    }
  }
  return fallback_.hover_at(params);
}

bool LspSymbolProvider::supports_diagnostics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_;
}

uint64_t LspSymbolProvider::diagnostics_revision() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return 0;
  }
  return client_.diagnostics_revision();
}

DocumentDiagnostics LspSymbolProvider::diagnostics_for_file(const std::string& path) {
  if (path.empty() || !is_lsp_trackable_path(path)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  refresh_diagnostics_cache_locked();
  const std::string key = normalize_lsp_path(path);
  for (const auto& doc : cached_diagnostics_) {
    if (doc.path == key) {
      return doc;
    }
  }
  return {};
}

std::vector<DocumentDiagnostics> LspSymbolProvider::workspace_diagnostics() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  refresh_diagnostics_cache_locked();
  return cached_diagnostics_;
}

}  // namespace tgdb
