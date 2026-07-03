#include "symbols/lsp_symbol_provider.hpp"

#include "indexer/index_rules.hpp"
#include "lsp/lsp_uri.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "util/thread_name.hpp"
#include "util/monitor_log.hpp"

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

void LspSymbolProvider::join_startup_thread() {
  if (lsp_startup_thread_.joinable()) {
    lsp_startup_thread_.join();
  }
  lsp_starting_.store(false, std::memory_order_release);
}

void LspSymbolProvider::finish_lsp_start_locked(bool ok) {
  use_lsp_ = ok;
  if (!ok) {
    return;
  }
  start_async_worker_locked();
  for (const auto& entry : open_buffers_) {
    if (is_lsp_trackable_path(entry.first, entry.second)) {
      client_.did_open(entry.first, entry.second);
    }
  }
}

void LspSymbolProvider::start_lsp_async(const std::string& compile_commands_dir) {
  join_startup_thread();

  std::string root;
  std::string compile_dir;
  bool gcc_query = true;
  bool background_index = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!compile_commands_dir.empty()) {
      compile_commands_dir_ = compile_commands_dir;
    }
    if (!lsp_enabled_ || workspace_root_.empty()) {
      use_lsp_ = false;
      return;
    }
    root = workspace_root_;
    compile_dir = compile_commands_dir_.empty() ? compile_commands_dir : compile_commands_dir_;
    gcc_query = use_gcc_query_driver_;
    background_index = use_background_index_;
  }

  lsp_starting_.store(true, std::memory_order_release);
  lsp_startup_thread_ = std::thread([this, root, compile_dir, gcc_query, background_index] {
    set_current_thread_name("lsp-start");
    const bool ok = client_.start(root, compile_dir, gcc_query, background_index);
    std::lock_guard<std::mutex> lock(mutex_);
    lsp_starting_.store(false, std::memory_order_release);
    finish_lsp_start_locked(ok);
  });
}

bool LspSymbolProvider::lsp_loading() const {
  return lsp_starting_.load(std::memory_order_acquire);
}

void LspSymbolProvider::stop_lsp_locked() {
  stop_async_worker_locked();
  client_.stop();
  use_lsp_ = false;
  cached_diag_revision_ = 0;
  cached_diagnostics_.clear();
  hover_cache_.clear();
  completion_cache_.clear();
}

void LspSymbolProvider::stop_lsp() {
  join_startup_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  stop_lsp_locked();
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
    inflight_hover_.clear();
    inflight_completion_.clear();
  }
  async_worker_ = std::thread([this] {
    set_current_thread_name("lsp-async");
    async_worker_main();
  });
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
    inflight_hover_.clear();
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

    std::ostringstream job_name;
    if (job->kind == AsyncJobKind::DocumentSymbols) {
      job_name << "async.document_symbols path=" << job->path;
    } else if (job->kind == AsyncJobKind::SemanticTokens) {
      job_name << "async.semantic_tokens path=" << job->path;
    } else if (job->kind == AsyncJobKind::Completion) {
      job_name << "async.completion path=" << job->completion_params.path;
    } else {
      job_name << "async.hover path=" << job->hover_params.path;
    }
    monitor_log::MonitorScope job_scope("lsp", job_name.str());

    bool run_job = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      run_job = use_lsp_ && client_.ready();
    }

    if (run_job) {
      if (job->kind == AsyncJobKind::DocumentSymbols) {
        client_.document_symbols(job->path);
      } else if (job->kind == AsyncJobKind::SemanticTokens) {
        client_.ensure_semantic_tokens(job->path);
      } else if (job->kind == AsyncJobKind::Completion) {
        const CompletionParams& params = job->completion_params;
        const std::string key = normalize_lsp_path(params.path);
        std::vector<CompletionItem> items;
        if (!key.empty()) {
          const std::string text =
              params.text.empty() ? buffer_text_for_path(key) : params.text;
          items = client_.completions_at(key, text, params.line, params.character);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        completion_cache_[job->completion_key] = std::move(items);
      } else {
        const HoverInfo info = client_.hover(job->hover_params.path, job->hover_params.text,
                                           job->hover_params.line, job->hover_params.character);
        std::lock_guard<std::mutex> lock(mutex_);
        hover_cache_[job->hover_key] = info;
      }
    }

    if (job->kind == AsyncJobKind::DocumentSymbols) {
      {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        inflight_symbols_.erase(job->path);
      }
    } else if (job->kind == AsyncJobKind::SemanticTokens) {
      {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        inflight_semantic_.erase(job->path);
      }
    } else if (job->kind == AsyncJobKind::Completion) {
      {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        inflight_completion_.erase(job->completion_key);
      }
    } else {
      {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        inflight_hover_.erase(job->hover_key);
      }
    }

    if (run_job) {
      async_results_.push({job->kind, job->path});
    }
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
  int drained = 0;
  while (auto result = async_results_.try_pop()) {
    updated = true;
    ++drained;
    if (result->kind == AsyncJobKind::SemanticTokens) {
      semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
    } else {
      document_symbols_revision_.fetch_add(1, std::memory_order_relaxed);
    }
  }
  if (drained > 0) {
    TGDB_MON("lsp", "drain_async_results count=" + std::to_string(drained));
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

void LspSymbolProvider::flush_pending_did_change_for_key_locked(const std::string& key) {
  if (!use_lsp_ || key.empty()) {
    return;
  }
  pending_did_change_.erase(key);
  std::string path_to_send;
  std::string text_to_send;
  for (const auto& entry : open_buffers_) {
    if (normalize_lsp_path(entry.first) == key) {
      path_to_send = entry.first;
      text_to_send = entry.second;
      break;
    }
  }
  if (path_to_send.empty() || !is_lsp_trackable_path(path_to_send, text_to_send)) {
    return;
  }
  client_.did_change(path_to_send, text_to_send);
  pending_content_refresh_[key] = steady_now_ms();
}

void LspSymbolProvider::flush_all_pending_did_change_locked() {
  if (!use_lsp_ || pending_did_change_.empty()) {
    return;
  }
  std::vector<std::string> keys;
  keys.reserve(pending_did_change_.size());
  for (const auto& entry : pending_did_change_) {
    keys.push_back(entry.first);
  }
  for (const std::string& key : keys) {
    flush_pending_did_change_for_key_locked(key);
  }
}

void LspSymbolProvider::tick_pending_did_change_locked() {
  if (!use_lsp_ || pending_did_change_.empty()) {
    return;
  }
  const int64_t now = steady_now_ms();
  constexpr int64_t kDebounceMs = 1000;
  std::vector<std::string> due;
  for (const auto& entry : pending_did_change_) {
    if (now - entry.second >= kDebounceMs) {
      due.push_back(entry.first);
    }
  }
  for (const std::string& key : due) {
    flush_pending_did_change_for_key_locked(key);
  }
}

void LspSymbolProvider::tick_debounced_updates() {
  std::lock_guard<std::mutex> lock(mutex_);
  tick_pending_did_change_locked();
  tick_content_refresh_locked();
}

void LspSymbolProvider::flush_document_sync(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string key = normalize_lsp_path(path);
  if (key.empty()) {
    return;
  }
  flush_pending_did_change_for_key_locked(key);
}

void LspSymbolProvider::set_lsp_enabled(bool enabled) {
  bool start_after = false;
  std::string compile_dir;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lsp_enabled_ == enabled) {
      return;
    }
    lsp_enabled_ = enabled;
    compile_dir = compile_commands_dir_;
    start_after = enabled;
  }
  if (!enabled) {
    stop_lsp();
    return;
  }
  start_lsp_async(compile_dir);
}

bool LspSymbolProvider::lsp_enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lsp_enabled_;
}

void LspSymbolProvider::set_workspace_clangd_options(const bool use_gcc_query_driver,
                                                     const bool background_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  use_gcc_query_driver_ = use_gcc_query_driver;
  use_background_index_ = background_index;
}

void LspSymbolProvider::on_workspace_opened(const std::string& root,
                                            const std::string& compile_commands_dir) {
  stop_lsp();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    open_buffers_.clear();
    workspace_root_ = root;
    compile_commands_dir_ = compile_commands_dir;
  }
  start_lsp_async(compile_commands_dir);
}

void LspSymbolProvider::on_workspace_closed() {
  stop_lsp();
  std::lock_guard<std::mutex> lock(mutex_);
  workspace_root_.clear();
  open_buffers_.clear();
}

void LspSymbolProvider::on_document_opened(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  bool notify_open = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_all_pending_did_change_locked();
    open_buffers_[path] = text;
    notify_open = use_lsp_ && is_lsp_trackable_path(path, text);
  }
  if (notify_open) {
    client_.did_open(path, text);
  }
}

void LspSymbolProvider::on_document_changed(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  open_buffers_[path] = text;
  if (!use_lsp_ || !is_lsp_trackable_path(path, text)) {
    return;
  }
  const std::string key = normalize_lsp_path(path);
  if (!key.empty()) {
    pending_did_change_[key] = steady_now_ms();
  }
}

void LspSymbolProvider::on_document_saved(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_ || !is_lsp_trackable_path(path)) {
    return;
  }
  const std::string key = normalize_lsp_path(path);
  if (key.empty()) {
    return;
  }
  flush_pending_did_change_for_key_locked(key);
  client_.invalidate_semantic_tokens_for_file(path);
  pending_content_refresh_.erase(key);
  semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
  enqueue_semantic_tokens_locked(key);
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

bool LspSymbolProvider::completion_uses_async_fetch() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_;
}

void LspSymbolProvider::request_completion(const CompletionParams& params,
                                           const std::string& cache_key) {
  if (params.path.empty() || cache_key.empty() ||
      !is_lsp_trackable_path(params.path, params.text)) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_) {
      return;
    }
    if (completion_cache_.find(cache_key) != completion_cache_.end()) {
      return;
    }
  }

  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    if (inflight_completion_.count(cache_key) > 0) {
      return;
    }
    inflight_completion_.insert(cache_key);
  }

  AsyncJob job;
  job.kind = AsyncJobKind::Completion;
  job.path = normalize_lsp_path(params.path);
  job.completion_key = cache_key;
  job.completion_params = params;
  async_jobs_.push(std::move(job));
}

std::optional<std::vector<CompletionItem>> LspSymbolProvider::poll_completion(
    const std::string& cache_key) {
  if (cache_key.empty()) {
    return std::nullopt;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = completion_cache_.find(cache_key);
  if (it == completion_cache_.end()) {
    return std::nullopt;
  }
  std::vector<CompletionItem> items = it->second;
  completion_cache_.erase(it);
  return items;
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
  if (client_.has_ready_semantic_tokens(path)) {
    return true;
  }
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

bool LspSymbolProvider::hover_uses_async_fetch() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_;
}

void LspSymbolProvider::request_hover(const HoverParams& params, const std::string& cache_key) {
  if (params.path.empty() || cache_key.empty() ||
      !is_lsp_trackable_path(params.path, params.text)) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_) {
      return;
    }
    if (hover_cache_.find(cache_key) != hover_cache_.end()) {
      return;
    }
  }

  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    if (inflight_hover_.count(cache_key) > 0) {
      return;
    }
    inflight_hover_.insert(cache_key);
  }

  AsyncJob job;
  job.kind = AsyncJobKind::Hover;
  job.path = normalize_lsp_path(params.path);
  job.hover_key = cache_key;
  job.hover_params = params;
  async_jobs_.push(std::move(job));
}

std::optional<HoverInfo> LspSymbolProvider::poll_hover(const std::string& cache_key) {
  if (cache_key.empty()) {
    return std::nullopt;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = hover_cache_.find(cache_key);
  if (it == hover_cache_.end()) {
    return std::nullopt;
  }
  HoverInfo info = it->second;
  hover_cache_.erase(it);
  return info;
}

HoverInfo LspSymbolProvider::hover_at(const HoverParams& params) {
  if (params.path.empty()) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (use_lsp_ && is_lsp_trackable_path(params.path, params.text)) {
    return {};
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

bool LspSymbolProvider::supports_formatting() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_ && client_.ready();
}

std::optional<std::string> LspSymbolProvider::format_document(const FormatParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return std::nullopt;
  }

  const std::string key = normalize_lsp_path(params.path);
  if (key.empty()) {
    return std::nullopt;
  }

  const std::string text =
      params.text.empty() ? buffer_text_for_path(key) : params.text;
  return client_.format_document(key, text);
}

bool LspSymbolProvider::supports_rename() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_ && client_.ready();
}

std::vector<LspFileEdits> LspSymbolProvider::rename_symbol(const RenameParams& params) {
  if (params.path.empty() || params.new_name.empty() ||
      !is_lsp_trackable_path(params.path, params.text)) {
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
  return client_.rename_symbol(key, text, params.line, params.character, params.new_name);
}

bool LspSymbolProvider::supports_code_actions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_ && client_.ready();
}

std::vector<CodeActionItem> LspSymbolProvider::code_actions_for_diagnostic(
    const CodeActionParams& params) {
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
  return client_.code_actions(params);
}

bool LspSymbolProvider::supports_call_hierarchy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_ && client_.ready();
}

std::vector<CallHierarchyItem> LspSymbolProvider::prepare_call_hierarchy(
    const CallHierarchyParams& params) {
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
  return client_.prepare_call_hierarchy(key, text, params.line, params.character);
}

std::vector<CallHierarchyItem> LspSymbolProvider::incoming_calls(
    const CallHierarchyItem& item) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  return client_.incoming_calls(item);
}

std::vector<CallHierarchyItem> LspSymbolProvider::outgoing_calls(
    const CallHierarchyItem& item) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  return client_.outgoing_calls(item);
}

}  // namespace tgdb
