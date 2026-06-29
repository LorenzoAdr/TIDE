#include "symbols/lsp_symbol_provider.hpp"

#include "indexer/index_rules.hpp"
#include "lsp/lsp_uri.hpp"

#include <chrono>
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

int64_t LspSymbolProvider::steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

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

void LspSymbolProvider::start_lsp_locked() {
  if (!lsp_enabled_ || workspace_root_.empty()) {
    use_lsp_ = false;
    return;
  }
  use_lsp_ = client_.start(workspace_root_);
  if (use_lsp_) {
    start_async_worker_locked();
  }
}

void LspSymbolProvider::stop_lsp_locked() {
  stop_async_worker_locked();
  client_.stop();
  use_lsp_ = false;
  cached_diag_revision_ = 0;
  cached_diagnostics_.clear();
}

void LspSymbolProvider::start_async_worker_locked() {
  stop_async_worker_locked();
  async_stop_ = false;
  async_jobs_.reset();
  async_results_.reset();
  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    inflight_symbols_.clear();
    inflight_semantic_.clear();
  }
  async_worker_ = std::thread([this] { async_worker_main(); });
}

void LspSymbolProvider::stop_async_worker_locked() {
  async_stop_ = true;
  async_jobs_.close();
  if (async_worker_.joinable()) {
    async_worker_.join();
  }
  async_jobs_.reset();
  async_results_.reset();
  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    inflight_symbols_.clear();
    inflight_semantic_.clear();
  }
}

void LspSymbolProvider::enqueue_document_symbols_locked(const std::string& path, bool force) {
  if (path.empty() || !use_lsp_ || !client_.ready()) {
    return;
  }
  if (!force && client_.has_cached_document_symbols(path)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    if (inflight_symbols_.count(path) > 0) {
      return;
    }
    inflight_symbols_.insert(path);
  }
  async_jobs_.push_front({AsyncJobKind::DocumentSymbols, path});
}

void LspSymbolProvider::enqueue_semantic_tokens_locked(const std::string& path) {
  if (path.empty() || !use_lsp_ || !client_.ready()) {
    return;
  }
  if (client_.has_ready_semantic_tokens(path)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    if (inflight_semantic_.count(path) > 0) {
      return;
    }
    inflight_semantic_.insert(path);
  }
  async_jobs_.push({AsyncJobKind::SemanticTokens, path});
}

void LspSymbolProvider::async_worker_main() {
  while (true) {
    auto job = async_jobs_.wait_pop();
    if (!job) {
      break;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (job->kind == AsyncJobKind::DocumentSymbols) {
        client_.document_symbols(job->path);
      } else {
        client_.ensure_semantic_tokens(job->path);
      }
    }

    if (job->kind == AsyncJobKind::DocumentSymbols) {
      {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        inflight_symbols_.erase(job->path);
      }
    } else {
      {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        inflight_semantic_.erase(job->path);
      }
    }

    async_results_.push({job->kind, job->path});
  }
}

bool LspSymbolProvider::symbols_lsp_pending_locked(const std::string& path) const {
  if (path.empty() || !use_lsp_ || !is_indexed_source_path(path)) {
    return false;
  }
  if (client_.has_cached_document_symbols(path)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(inflight_mutex_);
  return inflight_symbols_.count(path) > 0;
}

bool LspSymbolProvider::symbols_lsp_pending(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return symbols_lsp_pending_locked(path);
}

bool LspSymbolProvider::drain_async_results() {
  bool updated = false;
  while (auto result = async_results_.try_pop()) {
    updated = true;
    if (result->kind == AsyncJobKind::SemanticTokens) {
      semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
    } else {
      document_symbols_revision_.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return updated;
}

uint64_t LspSymbolProvider::semantic_highlight_revision() const {
  return semantic_highlight_revision_.load(std::memory_order_relaxed);
}

uint64_t LspSymbolProvider::document_symbols_revision() const {
  return document_symbols_revision_.load(std::memory_order_relaxed);
}

void LspSymbolProvider::tick_content_refresh_locked() {
  if (!use_lsp_) {
    return;
  }
  const int64_t now = steady_now_ms();
  constexpr int64_t kDebounceMs = 400;
  for (auto it = pending_content_refresh_.begin(); it != pending_content_refresh_.end();) {
    if (now - it->second >= kDebounceMs) {
      enqueue_document_symbols_locked(it->first, true);
      enqueue_semantic_tokens_locked(it->first);
      it = pending_content_refresh_.erase(it);
    } else {
      ++it;
    }
  }
}

void LspSymbolProvider::set_lsp_enabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (lsp_enabled_ == enabled) {
    return;
  }
  lsp_enabled_ = enabled;
  if (!enabled) {
    stop_lsp_locked();
    return;
  }
  start_lsp_locked();
  if (!use_lsp_) {
    return;
  }
  for (const auto& entry : open_buffers_) {
    if (is_lsp_trackable_path(entry.first, entry.second)) {
      client_.did_open(entry.first, entry.second);
    }
  }
}

bool LspSymbolProvider::lsp_enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lsp_enabled_;
}

void LspSymbolProvider::on_workspace_opened(const std::string& root) {
  std::lock_guard<std::mutex> lock(mutex_);
  stop_lsp_locked();
  open_buffers_.clear();
  workspace_root_ = root;
  start_lsp_locked();
}

void LspSymbolProvider::on_workspace_closed() {
  std::lock_guard<std::mutex> lock(mutex_);
  stop_lsp_locked();
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
  if (use_lsp_ && is_lsp_trackable_path(path, text)) {
    client_.did_open(path, text);
  }
}

void LspSymbolProvider::on_document_changed(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  open_buffers_[path] = text;
  if (use_lsp_ && is_lsp_trackable_path(path, text)) {
    client_.did_change(path, text);
    const std::string key = normalize_lsp_path(path);
    if (!key.empty()) {
      pending_content_refresh_[key] = steady_now_ms();
    }
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
    if (auto cached = client_.cached_document_symbols(path)) {
      return *cached;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      enqueue_document_symbols_locked(normalize_lsp_path(path));
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
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return false;
  }
  tick_content_refresh_locked();
  enqueue_semantic_tokens_locked(normalize_lsp_path(path));
  return false;
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
