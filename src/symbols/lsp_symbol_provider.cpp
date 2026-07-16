#include "symbols/lsp_symbol_provider.hpp"

#include "indexer/index_rules.hpp"
#include "lsp/lsp_sync.hpp"
#include "lsp/lsp_uri.hpp"
#include "lsp/language_server_spec.hpp"
#include <memory>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "util/thread_name.hpp"
#include "util/monitor_log.hpp"
namespace fs = std::filesystem;

namespace {

void join_thread_if_joinable(std::thread& worker) {
  if (!worker.joinable()) {
    return;
  }
  if (worker.get_id() == std::this_thread::get_id()) {
    return;
  }
  worker.join();
}

}  // namespace

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


LspClient* LspSymbolProvider::client_for_path(const std::string& path) {
  const std::string lang = language_id_for_path(path);
  if (language_id_is_python(lang)) {
    if (python_client_ && python_client_->ready()) {
      return python_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_cpp_family(lang) || lang == "plaintext") {
    return client_.ready() ? &client_ : nullptr;
  }
  return client_.ready() ? &client_ : nullptr;
}

const LspClient* LspSymbolProvider::client_for_path(const std::string& path) const {
  return const_cast<LspSymbolProvider*>(this)->client_for_path(path);
}

bool LspSymbolProvider::any_lsp_ready() const {
  if (client_.ready()) {
    return true;
  }
  return python_client_ && python_client_->ready();
}

void LspSymbolProvider::join_python_startup_thread() {
  if (python_lsp_startup_thread_.joinable()) {
    python_lsp_startup_thread_.join();
  }
  python_lsp_starting_.store(false, std::memory_order_release);
}

void LspSymbolProvider::finish_python_lsp_start_locked(bool ok) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    if (python_client_) {
      python_client_->stop();
      python_client_.reset();
    }
    return;
  }
  if (!ok) {
    python_client_.reset();
    monitor_log::event("lsp", "basedpyright start failed or binary missing");
    return;
  }
  use_lsp_ = true;
  lsp_ready_since_ms_ = steady_now_ms();
  if (diagnostics_notify_callback_) {
    python_client_->set_diagnostics_notify_callback(diagnostics_notify_callback_);
  }
  if (!async_worker_.joinable()) {
    start_async_worker_locked();
  }
  for (const auto& entry : open_buffers_) {
    if (!language_id_is_python(language_id_for_path(entry.first))) {
      continue;
    }
    std::string text = entry.second;
    if (text.empty()) {
      text = buffer_text_for_path(entry.first);
    }
    if (!is_lsp_trackable_path(entry.first, text)) {
      continue;
    }
    if (python_client_) {
      python_client_->did_open(entry.first, text);
    }
    enqueue_semantic_tokens_locked(normalize_lsp_path(entry.first));
  }
  semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
}

void LspSymbolProvider::ensure_python_lsp_async() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (python_client_ && python_client_->ready()) {
      return;
    }
    if (python_lsp_starting_.load(std::memory_order_acquire)) {
      return;
    }
  }
  join_python_startup_thread();
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (python_client_ && python_client_->ready()) {
      return;
    }
    root = workspace_root_;
  }
  python_lsp_starting_.store(true, std::memory_order_release);
  python_lsp_startup_thread_ = std::thread([this, root] {
    set_current_thread_name("lsp-py-start");
    auto client = std::make_unique<LspClient>();
    const auto spec = make_basedpyright_spec(root);
    bool ok = false;
    if (spec.has_value()) {
      ok = client->start(*spec);
    }
    if (shutting_down_.load(std::memory_order_acquire)) {
      if (ok) {
        client->stop();
      }
      python_lsp_starting_.store(false, std::memory_order_release);
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    python_lsp_starting_.store(false, std::memory_order_release);
    if (ok) {
      python_client_ = std::move(client);
    }
    finish_python_lsp_start_locked(ok);
  });
}

void LspSymbolProvider::refresh_diagnostics_cache_locked() const {
  uint64_t revision = client_.diagnostics_revision();
  if (python_client_) {
    revision ^= (python_client_->diagnostics_revision() << 1);
  }
  if (revision == cached_diag_revision_) {
    return;
  }
  cached_diagnostics_ = client_.all_diagnostics();
  if (python_client_) {
    auto py = python_client_->all_diagnostics();
    cached_diagnostics_.insert(cached_diagnostics_.end(), py.begin(), py.end());
  }
  cached_diag_revision_ = revision;
}

LspSymbolProvider::LspSymbolProvider() {
  did_change_timer_ = std::thread([this] {
    set_current_thread_name("lsp-didchange");
    did_change_timer_main();
  });
}

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
  if (shutting_down_.load(std::memory_order_acquire)) {
    use_lsp_ = false;
    return;
  }
  // Do not clear use_lsp_ if basedpyright is already serving Python files.
  if (ok) {
    use_lsp_ = true;
  } else if (!(python_client_ && python_client_->ready())) {
    use_lsp_ = false;
  }
  if (!ok) {
    return;
  }
  lsp_ready_since_ms_ = steady_now_ms();
  start_async_worker_locked();
  if (ui_inhibited_ && use_background_index_) {
    client_.set_background_paused(true);
  }
  for (const auto& entry : open_buffers_) {
    if (language_id_is_python(language_id_for_path(entry.first))) {
      continue;
    }
    std::string text = entry.second;
    if (text.empty()) {
      text = buffer_text_for_path(entry.first);
    }
    if (!is_lsp_trackable_path(entry.first, text)) {
      continue;
    }
    client_.did_open(entry.first, text);
    if (is_cpp_header_path(entry.first)) {
      open_companion_sources_for_clangd_locked(entry.first);
    }
    enqueue_semantic_tokens_locked(normalize_lsp_path(entry.first));
  }
  semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
}

void LspSymbolProvider::start_lsp_async(const std::string& compile_commands_dir) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
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

  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_async_worker_locked();
  }

  lsp_starting_.store(true, std::memory_order_release);
  lsp_startup_thread_ = std::thread([this, root, compile_dir, gcc_query, background_index] {
    set_current_thread_name("lsp-start");
    const bool ok = client_.start(root, compile_dir, gcc_query, background_index);
    if (shutting_down_.load(std::memory_order_acquire)) {
      client_.stop();
      lsp_starting_.store(false, std::memory_order_release);
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    lsp_starting_.store(false, std::memory_order_release);
    finish_lsp_start_locked(ok);
  });
}

bool LspSymbolProvider::lsp_loading() const {
  return lsp_starting_.load(std::memory_order_acquire) ||
         python_lsp_starting_.load(std::memory_order_acquire);
}

void LspSymbolProvider::stop_lsp_locked() {
  async_stop_ = true;
  async_jobs_.close();
}

void LspSymbolProvider::stop_lsp_locked_finalize() {
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
  use_lsp_ = false;
  cached_diag_revision_ = 0;
  cached_diagnostics_.clear();
  hover_cache_.clear();
  completion_cache_.clear();
}

void LspSymbolProvider::stop_lsp() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_lsp_locked();
  }
  client_.stop();
  if (python_client_) {
    python_client_->stop();
  }
  join_thread_if_joinable(lsp_startup_thread_);
  lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(python_lsp_startup_thread_);
  python_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(async_worker_);
  std::lock_guard<std::mutex> lock(mutex_);
  python_client_.reset();
  stop_lsp_locked_finalize();
}

void LspSymbolProvider::restart_lsp_after_transport_failure() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  if (async_worker_.joinable() && std::this_thread::get_id() == async_worker_.get_id()) {
    pending_transport_restart_.store(true, std::memory_order_release);
    return;
  }

  std::string compile_dir;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty() || lsp_starting_.load()) {
      return;
    }
    const int64_t now = steady_now_ms();
    constexpr int64_t kRestartCooldownMs = 3000;
    if (last_lsp_failure_restart_ms_ > 0 &&
        now - last_lsp_failure_restart_ms_ < kRestartCooldownMs) {
      return;
    }
    last_lsp_failure_restart_ms_ = now;
    compile_dir = compile_commands_dir_;
  }
  stop_lsp();
  start_lsp_async(compile_dir);
}

void LspSymbolProvider::process_pending_transport_restart() {
  if (shutting_down_.load(std::memory_order_acquire) ||
      !pending_transport_restart_.load(std::memory_order_acquire)) {
    return;
  }
  const int64_t now = steady_now_ms();
  constexpr int64_t kRestartCooldownMs = 3000;
  if (last_lsp_failure_restart_ms_ > 0 &&
      now - last_lsp_failure_restart_ms_ < kRestartCooldownMs) {
    return;
  }
  if (lsp_restart_in_progress_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  pending_transport_restart_.store(false, std::memory_order_release);
  std::thread([this]() {
    set_current_thread_name("lsp-restart");
    restart_lsp_after_transport_failure();
    lsp_restart_in_progress_.store(false, std::memory_order_release);
  }).detach();
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
    if (async_worker_.get_id() == std::this_thread::get_id()) {
      return;
    }
    async_worker_.join();
  }
  async_stop_ = false;
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
  LspClient* lsp = client_for_path(path);
  if (path.empty() || !use_lsp_ || lsp == nullptr) {
    return;
  }
  if (!force && lsp->has_cached_document_symbols(path)) {
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

void LspSymbolProvider::enqueue_semantic_tokens_locked(const std::string& path, bool force) {
  if (kLspSemanticTokensStandby) {
    return;
  }
  LspClient* lsp = client_for_path(path);
  if (path.empty() || !use_lsp_ || lsp == nullptr) {
    return;
  }
  if (!force && lsp->has_ready_semantic_tokens(path)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    if (inflight_semantic_.count(path) > 0) {
      return;
    }
    inflight_semantic_.insert(path);
  }
  async_jobs_.push_front({AsyncJobKind::SemanticTokens, path});
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
    LspClient* job_lsp = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      job_lsp = job ? client_for_path(job->path) : nullptr;
      run_job = use_lsp_ && job_lsp != nullptr;
    }

    if (async_stop_ || shutting_down_.load(std::memory_order_acquire)) {
      run_job = false;
    }

    if (run_job && job_lsp != nullptr) {
      if (job->kind == AsyncJobKind::DocumentSymbols) {
        job_lsp->document_symbols(job->path);
        async_results_.push({job->kind, job->path});
        notify_async_job_ready(job->kind);
      } else if (job->kind == AsyncJobKind::SemanticTokens) {
        std::string text;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          const auto it = open_buffers_.find(job->path);
          if (it != open_buffers_.end()) {
            text = it->second;
          }
          if (text.empty()) {
            text = buffer_text_for_path(job->path);
          }
        }
        if (is_lsp_trackable_path(job->path, text)) {
          flush_document_sync(job->path);
        }
        const bool fetched = job_lsp->ensure_semantic_tokens(job->path);
        const bool ready = job_lsp->has_ready_semantic_tokens(job->path);
        const auto doc = job_lsp->semantic_tokens_for_file(job->path);
        if (fetched) {
          async_results_.push({job->kind, job->path});
          notify_async_job_ready(job->kind);
        } else if (!job_lsp->transport_running()) {
          pending_transport_restart_.store(true, std::memory_order_release);
        }
      } else if (job->kind == AsyncJobKind::Completion) {
        const CompletionParams& params = job->completion_params;
        const std::string key = normalize_lsp_path(params.path);
        std::vector<CompletionItem> items;
        int request_id = 0;
        bool stale = false;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          const auto latest = latest_completion_key_by_path_.find(key);
          if (latest == latest_completion_key_by_path_.end() ||
              latest->second != job->completion_key) {
            stale = true;
          }
        }
        if (!stale && !key.empty()) {
          const std::string text =
              params.text.empty() ? buffer_text_for_path(params.path) : params.text;
          if (!text.empty()) {
            flush_document_sync(params.path);
          }
          items = job_lsp->completions_at(key, text, params.line, params.character, false,
                                       &request_id);
          if (request_id <= 0) {
            stale = true;
          }
        }
        if (!stale) {
          std::lock_guard<std::mutex> lock(mutex_);
          const auto latest = latest_completion_key_by_path_.find(key);
          if (latest != latest_completion_key_by_path_.end() &&
              latest->second == job->completion_key) {
            CachedCompletion cached;
            cached.items = std::move(items);
            cached.request_id = request_id;
            completion_cache_[job->completion_key] = std::move(cached);
            async_results_.push({job->kind, job->path});
            notify_async_job_ready(job->kind);
          } else {
            async_results_.push({job->kind, job->path});
            notify_async_job_ready(job->kind);
          }
        } else {
          async_results_.push({job->kind, job->path});
          notify_async_job_ready(job->kind);
        }
      } else {
        const HoverInfo info = job_lsp->hover(job->hover_params.path, job->hover_params.text,
                                           job->hover_params.line, job->hover_params.character);
        std::lock_guard<std::mutex> lock(mutex_);
        hover_cache_[job->hover_key] = info;
        async_results_.push({job->kind, job->path});
        notify_async_job_ready(job->kind);
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
  }
}

bool LspSymbolProvider::symbols_lsp_pending_locked(const std::string& path) const {
  if (path.empty() || !use_lsp_ || !is_indexed_source_path(path)) {
    return false;
  }
  if (const LspClient* lsp = client_for_path(path);
      lsp != nullptr && lsp->has_cached_document_symbols(path)) {
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
  process_pending_transport_restart();
  bool updated = false;
  bool invalidates_view = false;
  int drained = 0;
  while (auto result = async_results_.try_pop()) {
    updated = true;
    ++drained;
    if (result->kind != AsyncJobKind::Completion) {
      invalidates_view = true;
    }
    if (result->kind == AsyncJobKind::SemanticTokens) {
      semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
    } else if (result->kind == AsyncJobKind::DocumentSymbols) {
      document_symbols_revision_.fetch_add(1, std::memory_order_relaxed);
    }
  }
  async_drain_invalidates_view_ = invalidates_view;
  if (drained > 0) {
    TGDB_MON("lsp", "drain_async_results count=" + std::to_string(drained));
  }
  return updated;
}

bool LspSymbolProvider::async_drain_invalidates_view() const {
  return async_drain_invalidates_view_;
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
  constexpr int64_t kSemanticDebounceMs = 300;
  for (auto it = pending_semantic_refresh_.begin(); it != pending_semantic_refresh_.end();) {
    if (now - it->second >= kSemanticDebounceMs) {
      enqueue_semantic_tokens_locked(it->first, true);
      it = pending_semantic_refresh_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = pending_content_refresh_.begin(); it != pending_content_refresh_.end();) {
    if (now - it->second >= kDebounceMs) {
      enqueue_semantic_tokens_locked(it->first);
      it = pending_content_refresh_.erase(it);
    } else {
      ++it;
    }
  }
}

void LspSymbolProvider::flush_pending_did_change_for_key(const std::string& key) {
  if (key.empty()) {
    return;
  }
  std::string path_to_send;
  std::string text_to_send;
  bool send = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_) {
      return;
    }
    pending_did_change_.erase(key);
    for (const auto& entry : open_buffers_) {
      if (normalize_lsp_path(entry.first) == key) {
        path_to_send = entry.first;
        text_to_send = entry.second;
        break;
      }
    }
    if (!path_to_send.empty() && is_lsp_trackable_path(path_to_send, text_to_send)) {
      send = true;
    }
  }
  if (!send) {
    return;
  }
  if (LspClient* lsp = client_for_path(path_to_send)) {
    lsp->did_change(path_to_send, text_to_send);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_content_refresh_[key] = steady_now_ms();
  }
}

void LspSymbolProvider::flush_pending_did_change_for_key_locked(const std::string& key) {
  flush_pending_did_change_for_key(key);
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
    flush_pending_did_change_for_key(key);
  }
}

void LspSymbolProvider::tick_pending_did_change_locked() {
  if (!use_lsp_ || pending_did_change_.empty()) {
    return;
  }
  const int64_t now = steady_now_ms();
  constexpr int64_t kDebounceMs = kLspDidChangeDebounceMs;
  std::vector<std::string> due;
  for (const auto& entry : pending_did_change_) {
    if (now - entry.second >= kDebounceMs) {
      due.push_back(entry.first);
    }
  }
  for (const std::string& key : due) {
    flush_pending_did_change_for_key(key);
  }
}

void LspSymbolProvider::tick_debounced_updates() {
  process_pending_transport_restart();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ui_inhibited_) {
      return;
    }
  }
  std::vector<std::string> due;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_ || pending_did_change_.empty()) {
      tick_content_refresh_locked();
      return;
    }
    const int64_t now = steady_now_ms();
    constexpr int64_t kDebounceMs = kLspDidChangeDebounceMs;
    for (const auto& entry : pending_did_change_) {
      if (now - entry.second >= kDebounceMs) {
        due.push_back(entry.first);
      }
    }
  }
  for (const std::string& key : due) {
    flush_pending_did_change_for_key(key);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  tick_content_refresh_locked();
}

bool LspSymbolProvider::sync_document_for_completion(const std::string& path,
                                                     const std::string& text) {
  if (path.empty() || text.empty()) {
    return false;
  }
  const std::string key = normalize_lsp_path(path);
  if (key.empty() || !is_lsp_trackable_path(path, text)) {
    return false;
  }
  bool send_change = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_) {
      return false;
    }
    open_buffers_[path] = text;
    if (LspClient* lsp = client_for_path(path); lsp && lsp->document_has_text(path, text)) {
      return true;
    }
    const int64_t now = steady_now_ms();
    constexpr int64_t kMinSyncIntervalMs = 250;
    const auto it = last_completion_document_sync_ms_.find(key);
    if (it != last_completion_document_sync_ms_.end() && now - it->second < kMinSyncIntervalMs) {
      return true;
    }
    last_completion_document_sync_ms_[key] = now;
    send_change = true;
  }
  if (!send_change) {
    return false;
  }
  if (LspClient* lsp = client_for_path(path)) {
    lsp->did_change(path, text);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_semantic_refresh_[key] = steady_now_ms();
  }
  return true;
}

void LspSymbolProvider::flush_document_sync(const std::string& path) {
  if (path.empty()) {
    return;
  }
  const std::string key = normalize_lsp_path(path);
  if (key.empty()) {
    return;
  }
  std::string path_to_send;
  std::string text_to_send;
  bool send = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_) {
      return;
    }
    pending_did_change_.erase(key);
    for (const auto& entry : open_buffers_) {
      if (normalize_lsp_path(entry.first) == key) {
        path_to_send = entry.first;
        text_to_send = entry.second;
        break;
      }
    }
    if (!path_to_send.empty() && is_lsp_trackable_path(path_to_send, text_to_send)) {
      send = true;
    }
  }
  if (!send) {
    return;
  }
  if (LspClient* lsp = client_for_path(path_to_send)) {
    lsp->did_change(path_to_send, text_to_send);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_content_refresh_[key] = steady_now_ms();
  }
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
  stop_lsp();
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

LspAsyncJobKind LspSymbolProvider::to_public_job_kind(const AsyncJobKind kind) {
  switch (kind) {
    case AsyncJobKind::DocumentSymbols:
      return LspAsyncJobKind::DocumentSymbols;
    case AsyncJobKind::SemanticTokens:
      return LspAsyncJobKind::SemanticTokens;
    case AsyncJobKind::Hover:
      return LspAsyncJobKind::Hover;
    case AsyncJobKind::Completion:
      return LspAsyncJobKind::Completion;
  }
  return LspAsyncJobKind::Completion;
}

void LspSymbolProvider::notify_async_job_ready(const AsyncJobKind kind) {
  std::function<void(LspAsyncJobKind)> callback;
  {
    std::lock_guard<std::mutex> lock(async_job_ready_callback_mutex_);
    callback = async_job_ready_callback_;
  }
  if (callback) {
    callback(to_public_job_kind(kind));
  }
}

void LspSymbolProvider::set_async_job_ready_callback(std::function<void(LspAsyncJobKind)> callback) {
  std::lock_guard<std::mutex> lock(async_job_ready_callback_mutex_);
  async_job_ready_callback_ = std::move(callback);
}

void LspSymbolProvider::set_diagnostics_notify_callback(
    std::function<void(const std::string& path)> callback) {
  diagnostics_notify_callback_ = callback;
  client_.set_diagnostics_notify_callback(callback);
  if (python_client_) {
    python_client_->set_diagnostics_notify_callback(callback);
  }
}

void LspSymbolProvider::set_did_change_debounce_callback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(did_change_debounce_callback_mutex_);
  did_change_debounce_callback_ = std::move(callback);
}

void LspSymbolProvider::request_did_change_wake_after(int64_t delay_ms) {
  if (delay_ms < 0) {
    delay_ms = 0;
  }
  const int64_t fire_at = steady_now_ms() + delay_ms;
  {
    std::lock_guard<std::mutex> lock(did_change_timer_mutex_);
    if (did_change_timer_fire_at_ms_ == 0 || fire_at > did_change_timer_fire_at_ms_) {
      did_change_timer_fire_at_ms_ = fire_at;
    }
  }
  did_change_timer_cv_.notify_one();
}

void LspSymbolProvider::schedule_did_change_debounce_wake() {
  request_did_change_wake_after(kLspDidChangeDebounceMs);
}

void LspSymbolProvider::did_change_timer_main() {
  while (!did_change_timer_stop_.load(std::memory_order_acquire)) {
    int64_t fire_at = 0;
    {
      std::unique_lock<std::mutex> lock(did_change_timer_mutex_);
      did_change_timer_cv_.wait(lock, [this] {
        return did_change_timer_stop_.load(std::memory_order_acquire) ||
               did_change_timer_fire_at_ms_ != 0;
      });
      if (did_change_timer_stop_.load(std::memory_order_acquire)) {
        break;
      }
      fire_at = did_change_timer_fire_at_ms_;
      const int64_t now = steady_now_ms();
      if (fire_at > now) {
        did_change_timer_cv_.wait_for(lock, std::chrono::milliseconds(fire_at - now), [this, fire_at] {
          return did_change_timer_stop_.load(std::memory_order_acquire) ||
                 did_change_timer_fire_at_ms_ != fire_at;
        });
      }
      if (did_change_timer_fire_at_ms_ == fire_at) {
        did_change_timer_fire_at_ms_ = 0;
      } else {
        continue;
      }
    }
    std::function<void()> callback;
    {
      std::lock_guard<std::mutex> lock(did_change_debounce_callback_mutex_);
      callback = did_change_debounce_callback_;
    }
    if (callback) {
      callback();
    }
  }
}

void LspSymbolProvider::set_lsp_request_counter(std::atomic<uint64_t>* counter) {
  client_.set_request_counter(counter);
  if (python_client_) {
    python_client_->set_request_counter(counter);
  }
}

void LspSymbolProvider::set_ui_inhibited(const bool inhibited) {
  bool should_flush = false;
  bool should_pause = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ui_inhibited_ == inhibited) {
      return;
    }
    ui_inhibited_ = inhibited;
    should_flush = !inhibited && use_lsp_;
    should_pause = inhibited && use_lsp_ && use_background_index_;
  }
  if (should_pause) {
    client_.set_background_paused(true);
    if (python_client_) {
      python_client_->set_background_paused(true);
    }
    return;
  }
  client_.set_background_paused(false);
  if (python_client_) {
    python_client_->set_background_paused(false);
  }
  if (should_flush) {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_all_pending_did_change_locked();
    tick_content_refresh_locked();
  }
}

void LspSymbolProvider::on_workspace_opened(const std::string& root,
                                            const std::string& compile_commands_dir) {
  shutting_down_.store(false, std::memory_order_release);
  stop_lsp();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workspace_root_ = root;
    compile_commands_dir_ = compile_commands_dir;
  }
  start_lsp_async(compile_commands_dir);
}

void LspSymbolProvider::on_workspace_closed() {
  if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  did_change_timer_stop_.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(did_change_timer_mutex_);
    did_change_timer_fire_at_ms_ = 0;
  }
  did_change_timer_cv_.notify_all();
  if (did_change_timer_.joinable() &&
      did_change_timer_.get_id() != std::this_thread::get_id()) {
    did_change_timer_.join();
  }
  pending_transport_restart_.store(false, std::memory_order_release);
  stop_lsp();
  std::lock_guard<std::mutex> lock(mutex_);
  workspace_root_.clear();
  open_buffers_.clear();
  shadow_companions_.clear();
}

bool LspSymbolProvider::buffer_open_locked(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  const std::string key = normalize_lsp_path(path);
  for (const auto& entry : open_buffers_) {
    if (normalize_lsp_path(entry.first) == key) {
      return true;
    }
  }
  return false;
}

void LspSymbolProvider::clear_shadow_companion_locked(const std::string& companion_path) {
  const std::string key = normalize_lsp_path(companion_path);
  for (auto it = shadow_companions_.begin(); it != shadow_companions_.end();) {
    auto& companions = it->second;
    companions.erase(key);
    if (companions.empty()) {
      it = shadow_companions_.erase(it);
    } else {
      ++it;
    }
  }
}

void LspSymbolProvider::open_companion_sources_for_clangd_locked(const std::string& header_path) {
  if (!use_lsp_ || !is_cpp_header_path(header_path)) {
    return;
  }

  const std::string header_key = normalize_lsp_path(header_path);
  for (const std::string& companion : companion_source_paths_for_header(header_path)) {
    const std::string companion_key = normalize_lsp_path(companion);
    if (companion_key.empty() || buffer_open_locked(companion_key)) {
      continue;
    }
    const std::string companion_text = read_file_text(companion_key);
    if (!is_lsp_trackable_path(companion_key, companion_text)) {
      continue;
    }
    client_.did_open(companion_key, companion_text);
    shadow_companions_[header_key].insert(companion_key);
  }
}

void LspSymbolProvider::on_document_opened(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  if (language_id_is_python(language_id_for_path(path))) {
    ensure_python_lsp_async();
  }
  bool notify_open = false;
  bool open_header = false;
  bool ensure_open = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto [it, inserted] = open_buffers_.try_emplace(path, text);
    if (!inserted) {
      if (it->second != text) {
        it->second = text;
        const std::string key = normalize_lsp_path(path);
        if (!key.empty() && use_lsp_ && is_lsp_trackable_path(path, text)) {
          pending_did_change_[key] = steady_now_ms();
          schedule_did_change_debounce_wake();
        }
      }
      {
        LspClient* lsp = client_for_path(path);
        ensure_open = use_lsp_ && is_lsp_trackable_path(path, text) && lsp != nullptr &&
                      !lsp->document_is_open(path);
      }
      if (!ensure_open) {
        return;
      }
    } else {
      clear_shadow_companion_locked(path);
      const std::string key = normalize_lsp_path(path);
      if (!key.empty()) {
        pending_did_change_.erase(key);
      }
      notify_open = lsp_enabled_ && is_lsp_trackable_path(path, text);
      if (notify_open && language_id_is_python(language_id_for_path(path))) {
        // Ensure basedpyright is up; did_open happens below or after start.
      }
      open_header = notify_open && is_cpp_header_path(path);
      if (open_header) {
        notify_open = false;
      }
    }
  }
  if (language_id_is_python(language_id_for_path(path))) {
    ensure_python_lsp_async();
  }
  if (ensure_open) {
    if (LspClient* lsp = client_for_path(path)) {
      lsp->did_open(path, text);
    } else if (language_id_is_python(language_id_for_path(path))) {
      // Server still starting; finish_python_lsp_start_locked will open buffers.
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (use_lsp_ && is_cpp_header_path(path)) {
      open_companion_sources_for_clangd_locked(path);
    }
    return;
  }
  if (open_header) {
    if (LspClient* lsp = client_for_path(path)) {
      lsp->did_open(path, text);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (use_lsp_) {
      open_companion_sources_for_clangd_locked(path);
    }
  } else if (notify_open) {
    if (LspClient* lsp = client_for_path(path)) {
      lsp->did_open(path, text);
    } else if (language_id_is_python(language_id_for_path(path))) {
      ensure_python_lsp_async();
    }
  }
}

void LspSymbolProvider::on_document_changed(const std::string& path, const std::string& text) {
  if (path.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  open_buffers_[path] = text;
  if (!lsp_enabled_ || !is_lsp_trackable_path(path, text)) {
    return;
  }
  // Allow debounce even while the language server is still starting.
  if (!any_lsp_ready() && !language_id_is_python(language_id_for_path(path))) {
    return;
  }
  const std::string key = normalize_lsp_path(path);
  if (!key.empty()) {
    pending_did_change_[key] = steady_now_ms();
    schedule_did_change_debounce_wake();
  }
}

void LspSymbolProvider::on_document_saved(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::string key;
  const std::string saved_text = buffer_text_for_path(path);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_ || !is_lsp_trackable_path(path, saved_text)) {
      return;
    }
    key = normalize_lsp_path(path);
    if (key.empty()) {
      return;
    }
    open_buffers_[path] = saved_text;
    pending_content_refresh_.erase(key);
    semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
  }
  flush_document_sync(path);
  if (LspClient* lsp = client_for_path(path)) {
    lsp->invalidate_semantic_tokens_for_file(path);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (use_lsp_) {
      enqueue_semantic_tokens_locked(key);
    }
  }
}

void LspSymbolProvider::on_document_closed(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  open_buffers_.erase(path);
  if (!use_lsp_) {
    return;
  }

  if (LspClient* lsp = client_for_path(path)) {
    lsp->did_close(path);
  }

  const std::string key = normalize_lsp_path(path);
  const auto shadow_it = shadow_companions_.find(key);
  if (shadow_it != shadow_companions_.end()) {
    for (const std::string& companion : shadow_it->second) {
      if (!buffer_open_locked(companion)) {
        client_.did_close(companion);
      }
    }
    shadow_companions_.erase(shadow_it);
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
  return fallback_.symbols_for_file(path);
}

std::vector<SymbolInfo> LspSymbolProvider::workspace_symbols(const std::string& workspace_root,
                                                               const std::string& query) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  (void)workspace_root;
  std::vector<SymbolInfo> out = client_.workspace_symbols(workspace_root_, query);
  if (python_client_ && python_client_->ready()) {
    auto py = python_client_->workspace_symbols(workspace_root_, query);
    out.insert(out.end(), py.begin(), py.end());
  }
  return out;
}

bool LspSymbolProvider::supports_semantic_completion() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lsp_enabled_ && any_lsp_ready();
}

std::vector<CompletionItem> LspSymbolProvider::completions_at(
    const CompletionParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }

  std::string key;
  std::string text;
  bool active = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active = use_lsp_;
    key = normalize_lsp_path(params.path);
    if (key.empty()) {
      return {};
    }
    text = params.text.empty() ? buffer_text_for_path(key) : params.text;
  }
  if (!active) {
    return fallback_.completions_at(params);
  }
  LspClient* lsp = client_for_path(params.path);
  if (lsp == nullptr) {
    return fallback_.completions_at(params);
  }
  const auto items = lsp->completions_at(key, text, params.line, params.character);
  return items;
}

bool LspSymbolProvider::completion_uses_async_fetch() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lsp_enabled_ && any_lsp_ready();
}

void LspSymbolProvider::request_completion(const CompletionParams& params,
                                           const std::string& cache_key) {
  if (params.path.empty() || cache_key.empty() ||
      !is_lsp_trackable_path(params.path, params.text)) {
    return;
  }

  const std::string path_key = normalize_lsp_path(params.path);
  if (path_key.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || client_for_path(params.path) == nullptr) {
      return;
    }
    if (!params.text.empty()) {
      open_buffers_[params.path] = params.text;
    }
    latest_completion_key_by_path_[path_key] = cache_key;
    if (completion_cache_.find(cache_key) != completion_cache_.end()) {
      return;
    }
  }

  client_.cancel_inflight_completion();
  if (python_client_) {
    python_client_->cancel_inflight_completion();
  }

  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    for (auto it = inflight_completion_.begin(); it != inflight_completion_.end();) {
      if (*it != cache_key) {
        it = inflight_completion_.erase(it);
      } else {
        ++it;
      }
    }
    if (inflight_completion_.count(cache_key) > 0) {
      return;
    }
    inflight_completion_.insert(cache_key);
  }

  AsyncJob job;
  job.kind = AsyncJobKind::Completion;
  job.path = path_key;
  job.completion_key = cache_key;
  job.completion_params = params;
  async_jobs_.remove_if(
      [](const AsyncJob& queued) { return queued.kind == AsyncJobKind::Completion; });
  async_jobs_.push_front(std::move(job));
}

void LspSymbolProvider::cancel_completion_fetch() {
  client_.cancel_inflight_completion();
  if (python_client_) {
    python_client_->cancel_inflight_completion();
  }
  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    inflight_completion_.clear();
  }
  async_jobs_.remove_if(
      [](const AsyncJob& queued) { return queued.kind == AsyncJobKind::Completion; });
}

void LspSymbolProvider::cancel_hover_fetch() {
  {
    std::lock_guard<std::mutex> lock(inflight_mutex_);
    inflight_hover_.clear();
  }
  async_jobs_.remove_if([](const AsyncJob& queued) { return queued.kind == AsyncJobKind::Hover; });
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
  const int clangd_latest = client_.latest_completion_request_id();
  const int py_latest =
      python_client_ ? python_client_->latest_completion_request_id() : 0;
  if (it->second.request_id <= 0 ||
      (it->second.request_id != clangd_latest && it->second.request_id != py_latest)) {
    completion_cache_.erase(it);
    return std::nullopt;
  }
  std::vector<CompletionItem> items = std::move(it->second.items);
  completion_cache_.erase(it);
  return items;
}

bool LspSymbolProvider::supports_navigation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lsp_enabled_ && any_lsp_ready();
}

SourceLocation LspSymbolProvider::goto_definition(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  if (is_cpp_header_path(params.path)) {
    open_companion_sources_for_clangd_locked(params.path);
  }
  const std::string text =
      params.text.empty() ? buffer_text_for_path(params.path) : params.text;
  LspClient* lsp = client_for_path(params.path);
  if (lsp == nullptr) {
    return {};
  }
  return lsp->goto_definition(params.path, text, params.line, params.character);
}

SourceLocation LspSymbolProvider::goto_declaration(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  if (is_cpp_header_path(params.path)) {
    open_companion_sources_for_clangd_locked(params.path);
  }
  const std::string text =
      params.text.empty() ? buffer_text_for_path(params.path) : params.text;
  LspClient* lsp = client_for_path(params.path);
  if (lsp == nullptr) {
    return {};
  }
  return lsp->goto_declaration(params.path, text, params.line, params.character);
}

SourceLocation LspSymbolProvider::goto_implementation(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return {};
  }
  if (is_cpp_header_path(params.path)) {
    open_companion_sources_for_clangd_locked(params.path);
  }
  const std::string text =
      params.text.empty() ? buffer_text_for_path(params.path) : params.text;
  LspClient* lsp = client_for_path(params.path);
  if (lsp == nullptr) {
    return {};
  }
  return lsp->goto_implementation(params.path, text, params.line, params.character);
}

bool LspSymbolProvider::supports_semantic_highlight() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_;
}

bool LspSymbolProvider::ensure_semantic_tokens(const std::string& path) {
  if (kLspSemanticTokensStandby) {
    return false;
  }
  if (path.empty() || !is_lsp_trackable_path(path)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return false;
  }
  if (LspClient* lsp = client_for_path(path); lsp && lsp->has_ready_semantic_tokens(path)) {
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
  if (LspClient* lsp = client_for_path(path)) {
    return lsp->semantic_tokens_for_file(path);
  }
  return {};
}

bool LspSymbolProvider::semantic_tokens_current_for_file(const std::string& path) {
  if (path.empty() || !is_lsp_trackable_path(path)) {
    return true;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return true;
  }
  if (const LspClient* lsp = client_for_path(path)) {
    return lsp->has_ready_semantic_tokens(path);
  }
  return true;
}

void LspSymbolProvider::invalidate_semantic_tokens_for_file(const std::string& path) {
  if (path.empty()) {
    return;
  }
  const std::string key = normalize_lsp_path(path);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return;
  }
  if (LspClient* lsp = client_for_path(path)) {
    lsp->invalidate_semantic_tokens_for_file(path);
  }
  semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> inflight_lock(inflight_mutex_);
    inflight_semantic_.erase(key);
  }
}

uint64_t LspSymbolProvider::document_generation_for_file(const std::string& path) const {
  if (path.empty() || !is_lsp_trackable_path(path)) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return 0;
  }
  if (const LspClient* lsp = client_for_path(path)) {
    return lsp->document_generation(path);
  }
  return 0;
}

bool LspSymbolProvider::supports_hover() const {
  return true;
}

bool LspSymbolProvider::hover_uses_async_fetch() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lsp_enabled_ && any_lsp_ready();
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
  uint64_t revision = client_.diagnostics_revision();
  if (python_client_) {
    revision ^= (python_client_->diagnostics_revision() << 1);
  }
  return revision;
}

bool LspSymbolProvider::document_sync_pending(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return false;
  }
  const std::string key = normalize_lsp_path(path);
  return pending_did_change_.find(key) != pending_did_change_.end();
}

bool LspSymbolProvider::diagnostics_display_ready(const std::string& path) const {
  if (path.empty()) {
    return true;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_) {
    return true;
  }
  if (const LspClient* lsp = client_for_path(path)) {
    return lsp->document_diagnostics_current(path);
  }
  return true;
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
  DocumentDiagnostics empty;
  empty.path = key;
  return empty;
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
  return use_lsp_ && any_lsp_ready();
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
  if (LspClient* lsp = client_for_path(params.path)) {
    return lsp->format_document(key, text);
  }
  return std::nullopt;
}

bool LspSymbolProvider::supports_rename() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_ && any_lsp_ready();
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
  if (LspClient* lsp = client_for_path(params.path)) {
    return lsp->rename_symbol(key, text, params.line, params.character, params.new_name);
  }
  return {};
}

bool LspSymbolProvider::supports_code_actions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_ && any_lsp_ready();
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
  if (LspClient* lsp = client_for_path(params.path)) {
    return lsp->code_actions(params);
  }
  return {};
}

bool LspSymbolProvider::supports_call_hierarchy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return use_lsp_ && any_lsp_ready();
}

bool LspSymbolProvider::supports_call_hierarchy(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_ || path.empty() || !is_lsp_trackable_path(path)) {
    return false;
  }
  return client_for_path(path) != nullptr;
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
  if (LspClient* lsp = client_for_path(params.path)) {
    return lsp->prepare_call_hierarchy(key, text, params.line, params.character);
  }
  return {};
}

std::vector<CallHierarchyItem> LspSymbolProvider::incoming_calls(
    const CallHierarchyItem& item) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_ || item.path.empty()) {
    return {};
  }
  if (LspClient* lsp = client_for_path(item.path)) {
    return lsp->incoming_calls(item);
  }
  return {};
}

std::vector<CallHierarchyItem> LspSymbolProvider::outgoing_calls(
    const CallHierarchyItem& item) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!use_lsp_ || item.path.empty()) {
    return {};
  }
  if (LspClient* lsp = client_for_path(item.path)) {
    return lsp->outgoing_calls(item);
  }
  return {};
}

}  // namespace tgdb
