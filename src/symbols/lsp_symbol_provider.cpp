#include "symbols/lsp_symbol_provider.hpp"

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
}

void LspSymbolProvider::on_document_opened(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    open_buffers_[path] = text;
  }
  if (use_lsp_) {
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
  if (use_lsp_) {
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

  std::lock_guard<std::mutex> lock(mutex_);
  if (use_lsp_) {
    const std::string text = buffer_text_for_path(path);
    if (!text.empty()) {
      client_.did_open(path, text);
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
  if (params.path.empty()) {
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
  if (params.path.empty()) {
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
  if (params.path.empty()) {
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
  if (path.empty()) {
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
  if (path.empty()) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  return client_.semantic_tokens_for_file(path);
}

}  // namespace tgdb
