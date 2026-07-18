#include "symbols/lsp_symbol_provider.hpp"

#include "indexer/index_rules.hpp"
#include "lsp/lsp_sync.hpp"
#include "lsp/lsp_uri.hpp"
#include "lsp/language_server_spec.hpp"
#include <memory>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "util/bundled_tools.hpp"
#include "util/thread_name.hpp"
#include "util/monitor_log.hpp"
#include "lsp/gfortran_diagnostics.hpp"
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

bool language_allows_debounce_while_lsp_starting(const std::string& language_id) {
  return language_id_is_python(language_id) || language_id_is_shellscript(language_id) ||
         language_id_is_latex(language_id) || language_id_is_rust(language_id) ||
         language_id_is_go(language_id) || language_id_is_zig(language_id) ||
         language_id_is_fortran(language_id) || language_id_is_lua(language_id) ||
         language_id_is_js_ts(language_id) || language_id_is_cmake(language_id) ||
         language_id_is_make(language_id);
}

bool lazy_client_ready(const std::unique_ptr<LspClient>& client) {
  return client && client->ready();
}

bool any_lazy_lsp_client_ready(const std::unique_ptr<LspClient>& python_client,
                               const std::unique_ptr<LspClient>& bash_client,
                               const std::unique_ptr<LspClient>& tex_client,
                               const std::unique_ptr<LspClient>& rust_client,
                               const std::unique_ptr<LspClient>& go_client,
                               const std::unique_ptr<LspClient>& zig_client,
                               const std::unique_ptr<LspClient>& fortran_client,
                               const std::unique_ptr<LspClient>& lua_client,
                               const std::unique_ptr<LspClient>& typescript_client,
                               const std::unique_ptr<LspClient>& cmake_client,
                               const std::unique_ptr<LspClient>& make_client) {
  return lazy_client_ready(python_client) || lazy_client_ready(bash_client) ||
         lazy_client_ready(tex_client) || lazy_client_ready(rust_client) ||
         lazy_client_ready(go_client) || lazy_client_ready(zig_client) ||
         lazy_client_ready(fortran_client) || lazy_client_ready(lua_client) ||
         lazy_client_ready(typescript_client) || lazy_client_ready(cmake_client) ||
         lazy_client_ready(make_client);
}

bool is_lazy_lsp_language(const std::string& language_id) {
  return language_id_is_python(language_id) || language_id_is_shellscript(language_id) ||
         language_id_is_latex(language_id) || language_id_is_rust(language_id) ||
         language_id_is_go(language_id) || language_id_is_zig(language_id) ||
         language_id_is_fortran(language_id) || language_id_is_lua(language_id) ||
         language_id_is_js_ts(language_id) || language_id_is_cmake(language_id) ||
         language_id_is_make(language_id);
}

void append_client_diagnostics(std::vector<DocumentDiagnostics>& out,
                               const std::unique_ptr<LspClient>& client) {
  if (!client) {
    return;
  }
  auto docs = client->all_diagnostics();
  out.insert(out.end(), docs.begin(), docs.end());
}

void merge_document_diagnostics(std::vector<DocumentDiagnostics>& out,
                                const DocumentDiagnostics& extra) {
  if (extra.path.empty()) {
    return;
  }
  for (auto& doc : out) {
    if (doc.path == extra.path) {
      doc.items.insert(doc.items.end(), extra.items.begin(), extra.items.end());
      return;
    }
  }
  if (!extra.items.empty()) {
    out.push_back(extra);
  }
}

uint64_t client_diagnostics_revision(const std::unique_ptr<LspClient>& client, int shift) {
  if (!client) {
    return 0;
  }
  return client->diagnostics_revision() << shift;
}

void append_workspace_symbols(std::vector<SymbolInfo>& out,
                              const std::unique_ptr<LspClient>& client,
                              const std::string& workspace_root, const std::string& query) {
  if (client && client->ready()) {
    auto symbols = client->workspace_symbols(workspace_root, query);
    out.insert(out.end(), symbols.begin(), symbols.end());
  }
}

void cancel_client_completion(const std::unique_ptr<LspClient>& client) {
  if (client) {
    client->cancel_inflight_completion();
  }
}

int client_latest_completion_id(const std::unique_ptr<LspClient>& client) {
  return client ? client->latest_completion_request_id() : 0;
}

void open_lazy_lsp_buffers(LspClient* client, const std::string& language_id,
                           const std::unordered_map<std::string, std::string>& open_buffers,
                           const std::function<std::string(const std::string&)>& buffer_text_for_path) {
  if (client == nullptr) {
    return;
  }
  for (const auto& entry : open_buffers) {
    const std::string lang = language_id_for_path(entry.first);
    const bool lang_ok =
        lang == language_id ||
        (language_id == "typescript" && (lang == "javascript" || lang == "typescript"));
    if (!lang_ok) {
      continue;
    }
    std::string text = entry.second;
    if (text.empty()) {
      text = buffer_text_for_path(entry.first);
    }
    if (!is_lsp_trackable_path(entry.first, text)) {
      continue;
    }
    client->did_open(entry.first, text);
  }
}

void configure_bash_language_server(LspClient* client) {
  if (client == nullptr || !client->ready()) {
    return;
  }
  nlohmann::json bash_settings = {{"enableSourceErrorDiagnostics", true}};
  if (const auto shellcheck = resolve_shellcheck(); shellcheck.has_value()) {
    bash_settings["shellcheckPath"] = *shellcheck;
  }
  client->did_change_workspace_configuration({{"bashIde", std::move(bash_settings)}});
}

void configure_rust_analyzer(LspClient* client) {
  if (client == nullptr || !client->ready()) {
    return;
  }
  // Prefer check-on-save (default) and keep native diagnostics enabled so buffer edits
  // update quickly; unresolved-name / rustc findings still arrive after save.
  nlohmann::json ra = nlohmann::json::object();
  ra["checkOnSave"] = true;
  ra["diagnostics"] = {{"enable", true}, {"experimental", {{"enable", true}}}};
  client->did_change_workspace_configuration({{"rust-analyzer", std::move(ra)}});
}

void configure_texlab_language_server(LspClient* client) {
  if (client == nullptr || !client->ready()) {
    return;
  }
  // TexLab defaults leave chktex off; enable lint on open/save/edit (needs `chktex` on PATH).
  nlohmann::json texlab_settings = {
      {"chktex", {{"onOpenAndSave", true}, {"onEdit", true}}},
  };
  client->did_change_workspace_configuration({{"texlab", std::move(texlab_settings)}});
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
  if (language_id_is_shellscript(lang)) {
    if (bash_client_ && bash_client_->ready()) {
      return bash_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_latex(lang)) {
    if (tex_client_ && tex_client_->ready()) {
      return tex_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_rust(lang)) {
    if (rust_client_ && rust_client_->ready()) {
      return rust_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_go(lang)) {
    if (go_client_ && go_client_->ready()) {
      return go_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_zig(lang)) {
    if (zig_client_ && zig_client_->ready()) {
      return zig_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_fortran(lang)) {
    if (fortran_client_ && fortran_client_->ready()) {
      return fortran_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_lua(lang)) {
    if (lua_client_ && lua_client_->ready()) {
      return lua_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_js_ts(lang)) {
    if (typescript_client_ && typescript_client_->ready()) {
      return typescript_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_cmake(lang)) {
    if (cmake_client_ && cmake_client_->ready()) {
      return cmake_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_make(lang)) {
    if (make_client_ && make_client_->ready()) {
      return make_client_.get();
    }
    return nullptr;
  }
  if (language_id_is_cpp_family(lang) || lang == "plaintext") {
    return client_.ready() ? &client_ : nullptr;
  }
  return nullptr;
}

const LspClient* LspSymbolProvider::client_for_path(const std::string& path) const {
  return const_cast<LspSymbolProvider*>(this)->client_for_path(path);
}

bool LspSymbolProvider::any_lsp_ready() const {
  if (client_.ready()) {
    return true;
  }
  return any_lazy_lsp_client_ready(python_client_, bash_client_, tex_client_, rust_client_,
                                   go_client_, zig_client_, fortran_client_, lua_client_,
                                   typescript_client_, cmake_client_, make_client_);
}

void LspSymbolProvider::notify_lsp_status(const char* i18n_key) {
  if (i18n_key == nullptr || i18n_key[0] == '\0') {
    return;
  }
  std::function<void(const std::string&)> callback;
  {
    std::lock_guard<std::mutex> lock(lsp_status_callback_mutex_);
    callback = lsp_status_callback_;
  }
  if (callback) {
    callback(i18n_key);
  }
}

void LspSymbolProvider::set_lsp_status_callback(
    std::function<void(const std::string& i18n_key)> callback) {
  std::lock_guard<std::mutex> lock(lsp_status_callback_mutex_);
  lsp_status_callback_ = std::move(callback);
}

void LspSymbolProvider::join_python_startup_thread() {
  if (python_lsp_startup_thread_.joinable()) {
    python_lsp_startup_thread_.join();
  }
  python_lsp_starting_.store(false, std::memory_order_release);
}

void LspSymbolProvider::join_bash_startup_thread() {
  if (bash_lsp_startup_thread_.joinable()) {
    bash_lsp_startup_thread_.join();
  }
  bash_lsp_starting_.store(false, std::memory_order_release);
}

void LspSymbolProvider::join_tex_startup_thread() {
  if (tex_lsp_startup_thread_.joinable()) {
    tex_lsp_startup_thread_.join();
  }
  tex_lsp_starting_.store(false, std::memory_order_release);
}

void LspSymbolProvider::finish_python_lsp_start_locked(bool ok, bool binary_missing) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    // stop_lsp() owns process/thread teardown; never stop() while holding mutex_.
    return;
  }
  if (!ok) {
    python_client_.reset();
    notify_lsp_status(binary_missing ? "status.basedpyright_missing" : "status.basedpyright_failed");
    monitor_log::event("lsp", "basedpyright start failed or binary missing");
    return;
  }
  use_lsp_ = true;
  lsp_ready_since_ms_ = steady_now_ms();
  notify_lsp_status("status.basedpyright_started");
  if (diagnostics_notify_callback_) {
    python_client_->set_diagnostics_notify_callback(diagnostics_notify_callback_);
  }
  open_lazy_lsp_buffers(python_client_.get(), "python", open_buffers_,
                        [this](const std::string& path) { return buffer_text_for_path(path); });
  for (const auto& entry : open_buffers_) {
    if (!language_id_is_python(language_id_for_path(entry.first))) {
      continue;
    }
    if (!is_lsp_trackable_path(entry.first, entry.second.empty() ? buffer_text_for_path(entry.first)
                                                                 : entry.second)) {
      continue;
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
    const bool binary_missing = !spec.has_value();
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
    bool want_worker = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (ok) {
        python_client_ = std::move(client);
      }
      python_lsp_starting_.store(false, std::memory_order_release);
      finish_python_lsp_start_locked(ok, binary_missing);
      want_worker = ok && use_lsp_;
    }
    if (want_worker) {
      ensure_async_worker_running();
    }
  });
}

void LspSymbolProvider::finish_bash_lsp_start_locked(bool ok, bool binary_missing) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  if (!ok) {
    bash_client_.reset();
    notify_lsp_status(binary_missing ? "status.bash_ls_missing" : "status.bash_ls_failed");
    monitor_log::event("lsp", "bash-language-server start failed or binary missing");
    return;
  }
  use_lsp_ = true;
  lsp_ready_since_ms_ = steady_now_ms();
  notify_lsp_status("status.bash_ls_started");
  configure_bash_language_server(bash_client_.get());
  if (diagnostics_notify_callback_) {
    bash_client_->set_diagnostics_notify_callback(diagnostics_notify_callback_);
  }
  open_lazy_lsp_buffers(bash_client_.get(), "shellscript", open_buffers_,
                        [this](const std::string& path) { return buffer_text_for_path(path); });
  for (const auto& entry : open_buffers_) {
    if (!language_id_is_shellscript(language_id_for_path(entry.first))) {
      continue;
    }
    if (!is_lsp_trackable_path(entry.first, entry.second.empty() ? buffer_text_for_path(entry.first)
                                                                 : entry.second)) {
      continue;
    }
    enqueue_semantic_tokens_locked(normalize_lsp_path(entry.first));
  }
  semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
}

void LspSymbolProvider::ensure_bash_lsp_async() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (bash_client_ && bash_client_->ready()) {
      return;
    }
    if (bash_lsp_starting_.load(std::memory_order_acquire)) {
      return;
    }
  }
  join_bash_startup_thread();
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (bash_client_ && bash_client_->ready()) {
      return;
    }
    root = workspace_root_;
  }
  bash_lsp_starting_.store(true, std::memory_order_release);
  bash_lsp_startup_thread_ = std::thread([this, root] {
    set_current_thread_name("lsp-bash-start");
    auto client = std::make_unique<LspClient>();
    const auto spec = make_bash_ls_spec(root);
    bool ok = false;
    const bool binary_missing = !spec.has_value();
    if (spec.has_value()) {
      ok = client->start(*spec);
    }
    if (shutting_down_.load(std::memory_order_acquire)) {
      if (ok) {
        client->stop();
      }
      bash_lsp_starting_.store(false, std::memory_order_release);
      return;
    }
    bool want_worker = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (ok) {
        bash_client_ = std::move(client);
      }
      bash_lsp_starting_.store(false, std::memory_order_release);
      finish_bash_lsp_start_locked(ok, binary_missing);
      want_worker = ok && use_lsp_;
    }
    if (want_worker) {
      ensure_async_worker_running();
    }
  });
}

void LspSymbolProvider::finish_tex_lsp_start_locked(bool ok, bool binary_missing) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  if (!ok) {
    tex_client_.reset();
    notify_lsp_status(binary_missing ? "status.texlab_missing" : "status.texlab_failed");
    monitor_log::event("lsp", "texlab start failed or binary missing");
    return;
  }
  use_lsp_ = true;
  lsp_ready_since_ms_ = steady_now_ms();
  notify_lsp_status("status.texlab_started");
  configure_texlab_language_server(tex_client_.get());
  if (diagnostics_notify_callback_) {
    tex_client_->set_diagnostics_notify_callback(diagnostics_notify_callback_);
  }
  open_lazy_lsp_buffers(tex_client_.get(), "latex", open_buffers_,
                        [this](const std::string& path) { return buffer_text_for_path(path); });
  for (const auto& entry : open_buffers_) {
    if (!language_id_is_latex(language_id_for_path(entry.first))) {
      continue;
    }
    if (!is_lsp_trackable_path(entry.first, entry.second.empty() ? buffer_text_for_path(entry.first)
                                                                 : entry.second)) {
      continue;
    }
    enqueue_semantic_tokens_locked(normalize_lsp_path(entry.first));
  }
  semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
}

void LspSymbolProvider::ensure_tex_lsp_async() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (tex_client_ && tex_client_->ready()) {
      return;
    }
    if (tex_lsp_starting_.load(std::memory_order_acquire)) {
      return;
    }
  }
  join_tex_startup_thread();
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (tex_client_ && tex_client_->ready()) {
      return;
    }
    root = workspace_root_;
  }
  tex_lsp_starting_.store(true, std::memory_order_release);
  tex_lsp_startup_thread_ = std::thread([this, root] {
    set_current_thread_name("lsp-tex-start");
    auto client = std::make_unique<LspClient>();
    const auto spec = make_texlab_spec(root);
    bool ok = false;
    const bool binary_missing = !spec.has_value();
    if (spec.has_value()) {
      ok = client->start(*spec);
    }
    if (shutting_down_.load(std::memory_order_acquire)) {
      if (ok) {
        client->stop();
      }
      tex_lsp_starting_.store(false, std::memory_order_release);
      return;
    }
    bool want_worker = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (ok) {
        tex_client_ = std::move(client);
      }
      tex_lsp_starting_.store(false, std::memory_order_release);
      finish_tex_lsp_start_locked(ok, binary_missing);
      want_worker = ok && use_lsp_;
    }
    if (want_worker) {
      ensure_async_worker_running();
    }
  });
}

namespace {

void join_lazy_startup_thread(std::thread& startup_thread, std::atomic<bool>& starting) {
  if (startup_thread.joinable()) {
    startup_thread.join();
  }
  starting.store(false, std::memory_order_release);
}

}  // namespace

void LspSymbolProvider::finish_simple_lazy_lsp_start_locked(std::unique_ptr<LspClient>& client,
                                                            const SimpleLazyLspConfig& cfg, bool ok,
                                                            bool binary_missing) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  if (!ok) {
    client.reset();
    notify_lsp_status(binary_missing ? cfg.missing_i18n : cfg.failed_i18n);
    monitor_log::event("lsp", cfg.monitor_message);
    return;
  }
  use_lsp_ = true;
  lsp_ready_since_ms_ = steady_now_ms();
  notify_lsp_status(cfg.started_i18n);
  if (diagnostics_notify_callback_) {
    client->set_diagnostics_notify_callback(diagnostics_notify_callback_);
  }
  if (std::string(cfg.language_id) == "rust") {
    configure_rust_analyzer(client.get());
  }
  open_lazy_lsp_buffers(client.get(), cfg.language_id, open_buffers_,
                        [this](const std::string& path) { return buffer_text_for_path(path); });
  for (const auto& entry : open_buffers_) {
    const std::string lang = language_id_for_path(entry.first);
    const bool lang_ok =
        lang == cfg.language_id ||
        (std::string(cfg.language_id) == "typescript" &&
         (lang == "javascript" || lang == "typescript"));
    if (!lang_ok) {
      continue;
    }
    if (!is_lsp_trackable_path(entry.first, entry.second.empty() ? buffer_text_for_path(entry.first)
                                                                 : entry.second)) {
      continue;
    }
    enqueue_semantic_tokens_locked(normalize_lsp_path(entry.first));
  }
  semantic_highlight_revision_.fetch_add(1, std::memory_order_relaxed);
  // Wake the debounce timer so edits queued while this server was starting are flushed
  // (flush keeps pending until client_for_path succeeds).
  schedule_did_change_debounce_wake();
}

void LspSymbolProvider::ensure_simple_lazy_lsp_async(
    std::unique_ptr<LspClient>& client, std::atomic<bool>& starting, std::thread& startup_thread,
    const SimpleLazyLspConfig& cfg, void (LspSymbolProvider::*finish_fn)(bool, bool)) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (client && client->ready()) {
      return;
    }
    if (starting.load(std::memory_order_acquire)) {
      return;
    }
  }
  join_lazy_startup_thread(startup_thread, starting);
  std::string root;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    if (client && client->ready()) {
      return;
    }
    root = workspace_root_;
  }
  starting.store(true, std::memory_order_release);
  startup_thread = std::thread([this, root, &client, &starting, cfg, finish_fn] {
    set_current_thread_name(cfg.thread_name);
    auto new_client = std::make_unique<LspClient>();
    const auto spec = cfg.make_spec(root);
    bool ok = false;
    const bool binary_missing = !spec.has_value();
    if (spec.has_value()) {
      ok = new_client->start(*spec);
    }
    if (shutting_down_.load(std::memory_order_acquire)) {
      if (ok) {
        new_client->stop();
      }
      starting.store(false, std::memory_order_release);
      return;
    }
    bool want_worker = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // Assign the client *before* clearing `starting` so waiters never observe
      // starting=false with a still-null client (that made completions give up).
      if (ok) {
        client = std::move(new_client);
      }
      starting.store(false, std::memory_order_release);
      (this->*finish_fn)(ok, binary_missing);
      want_worker = ok && use_lsp_;
    }
    if (want_worker) {
      ensure_async_worker_running();
    }
  });
}

void LspSymbolProvider::join_rust_startup_thread() {
  join_lazy_startup_thread(rust_lsp_startup_thread_, rust_lsp_starting_);
}

void LspSymbolProvider::join_go_startup_thread() {
  join_lazy_startup_thread(go_lsp_startup_thread_, go_lsp_starting_);
}

void LspSymbolProvider::join_zig_startup_thread() {
  join_lazy_startup_thread(zig_lsp_startup_thread_, zig_lsp_starting_);
}

void LspSymbolProvider::join_fortran_startup_thread() {
  join_lazy_startup_thread(fortran_lsp_startup_thread_, fortran_lsp_starting_);
}

void LspSymbolProvider::join_lua_startup_thread() {
  join_lazy_startup_thread(lua_lsp_startup_thread_, lua_lsp_starting_);
}

void LspSymbolProvider::join_typescript_startup_thread() {
  join_lazy_startup_thread(typescript_lsp_startup_thread_, typescript_lsp_starting_);
}

void LspSymbolProvider::join_cmake_startup_thread() {
  join_lazy_startup_thread(cmake_lsp_startup_thread_, cmake_lsp_starting_);
}

void LspSymbolProvider::join_make_startup_thread() {
  join_lazy_startup_thread(make_lsp_startup_thread_, make_lsp_starting_);
}

void LspSymbolProvider::finish_rust_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      rust_client_,
      {"lsp-rust-start", "rust", "status.rust_analyzer_missing", "status.rust_analyzer_failed",
       "status.rust_analyzer_started", "rust-analyzer start failed or binary missing"},
      ok, binary_missing);
}

void LspSymbolProvider::finish_go_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      go_client_,
      {"lsp-go-start", "go", "status.gopls_missing", "status.gopls_failed", "status.gopls_started",
       "gopls start failed or binary missing"},
      ok, binary_missing);
}

void LspSymbolProvider::finish_zig_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      zig_client_,
      {"lsp-zig-start", "zig", "status.zls_missing", "status.zls_failed", "status.zls_started",
       "zls start failed or binary missing"},
      ok, binary_missing);
}

void LspSymbolProvider::finish_fortran_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      fortran_client_,
      {"lsp-fortran-start", "fortran", "status.fortls_missing", "status.fortls_failed",
       "status.fortls_started", "fortls start failed or binary missing"},
      ok, binary_missing);
}

void LspSymbolProvider::refresh_fortran_compiler_diagnostics(const std::string& path,
                                                             const std::string& text) {
  if (path.empty() || !language_id_is_fortran(language_id_for_path(path))) {
    return;
  }
  const auto gfortran = resolve_gfortran();
  if (!gfortran.has_value()) {
    return;
  }
  const std::string key = normalize_lsp_path(path);
  if (key.empty()) {
    return;
  }
  auto doc = run_gfortran_diagnostics(path, text, *gfortran);
  std::function<void(const std::string&)> notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutting_down_.load(std::memory_order_acquire)) {
      return;
    }
    if (doc.has_value()) {
      doc->path = key;
      fortran_compiler_diagnostics_[key] = std::move(*doc);
    } else {
      fortran_compiler_diagnostics_.erase(key);
    }
    fortran_compiler_diag_revision_.fetch_add(1, std::memory_order_acq_rel);
    cached_diag_revision_ = 0;
    notify = diagnostics_notify_callback_;
  }
  if (notify) {
    notify(path);
  }
}

void LspSymbolProvider::finish_lua_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      lua_client_,
      {"lsp-lua-start", "lua", "status.lua_ls_missing", "status.lua_ls_failed",
       "status.lua_ls_started", "lua-language-server start failed or binary missing"},
      ok, binary_missing);
}

void LspSymbolProvider::finish_typescript_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      typescript_client_,
      {"lsp-ts-start", "typescript", "status.typescript_ls_missing", "status.typescript_ls_failed",
       "status.typescript_ls_started", "typescript-language-server start failed or binary missing"},
      ok, binary_missing);
  if (!ok || !typescript_client_) {
    return;
  }
  for (const auto& entry : open_buffers_) {
    if (!language_id_is_javascript(language_id_for_path(entry.first))) {
      continue;
    }
    if (!is_lsp_trackable_path(entry.first, entry.second.empty() ? buffer_text_for_path(entry.first)
                                                                 : entry.second)) {
      continue;
    }
    std::string text = entry.second;
    if (text.empty()) {
      text = buffer_text_for_path(entry.first);
    }
    typescript_client_->did_open(entry.first, text);
    enqueue_semantic_tokens_locked(normalize_lsp_path(entry.first));
  }
}

void LspSymbolProvider::finish_cmake_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      cmake_client_,
      {"lsp-cmake-start", "cmake", "status.neocmakelsp_missing", "status.neocmakelsp_failed",
       "status.neocmakelsp_started", "neocmakelsp start failed or binary missing"},
      ok, binary_missing);
}

void LspSymbolProvider::finish_make_lsp_start_locked(bool ok, bool binary_missing) {
  finish_simple_lazy_lsp_start_locked(
      make_client_,
      {"lsp-make-start", "make", "status.make_ls_missing", "status.make_ls_failed",
       "status.make_ls_started", "make-ls start failed or binary missing"},
      ok, binary_missing);
}

void LspSymbolProvider::ensure_rust_lsp_async(const std::string& hint_path) {
  std::string root;
  std::string discovered_hint = hint_path;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_ || workspace_root_.empty()) {
      return;
    }
    root = workspace_root_;
    if (discovered_hint.empty()) {
      for (const auto& entry : open_buffers_) {
        if (language_id_is_rust(language_id_for_path(entry.first))) {
          discovered_hint = entry.first;
          break;
        }
      }
    }
  }
  std::string cargo_root =
      discovered_hint.empty() ? std::string{}
                              : discover_project_root_with_marker(discovered_hint, "Cargo.toml");
  if (cargo_root.empty()) {
    cargo_root = discover_project_root_with_marker(root, "Cargo.toml");
  }
  if (cargo_root.empty()) {
    cargo_root = root;
  }

  // If rust-analyzer is already running against the wrong project root (e.g. a CMake
  // workspace with no Cargo.toml), restart it with the discovered cargo root.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (rust_client_ && rust_client_->ready()) {
      const std::string current = normalize_lsp_path(rust_client_->workspace_root());
      const std::string wanted = normalize_lsp_path(cargo_root);
      if (!current.empty() && !wanted.empty() && current == wanted) {
        return;
      }
      if (!wanted.empty() && current != wanted) {
        // Fall through after releasing the lock to stop+restart.
      } else {
        return;
      }
    }
  }
  if (rust_client_) {
    join_rust_startup_thread();
    if (rust_client_) {
      rust_client_->stop();
      std::lock_guard<std::mutex> lock(mutex_);
      rust_client_.reset();
    }
  }

  ensure_simple_lazy_lsp_async(
      rust_client_, rust_lsp_starting_, rust_lsp_startup_thread_,
      {"lsp-rust-start", "rust", "status.rust_analyzer_missing", "status.rust_analyzer_failed",
       "status.rust_analyzer_started", "rust-analyzer start failed or binary missing",
       [cargo_root](const std::string&) { return make_rust_analyzer_spec(cargo_root); }},
      &LspSymbolProvider::finish_rust_lsp_start_locked);
}

void LspSymbolProvider::ensure_go_lsp_async() {
  ensure_simple_lazy_lsp_async(
      go_client_, go_lsp_starting_, go_lsp_startup_thread_,
      {"lsp-go-start", "go", "status.gopls_missing", "status.gopls_failed", "status.gopls_started",
       "gopls start failed or binary missing", make_gopls_spec},
      &LspSymbolProvider::finish_go_lsp_start_locked);
}

void LspSymbolProvider::ensure_zig_lsp_async() {
  ensure_simple_lazy_lsp_async(
      zig_client_, zig_lsp_starting_, zig_lsp_startup_thread_,
      {"lsp-zig-start", "zig", "status.zls_missing", "status.zls_failed", "status.zls_started",
       "zls start failed or binary missing", make_zls_spec},
      &LspSymbolProvider::finish_zig_lsp_start_locked);
}

void LspSymbolProvider::ensure_fortran_lsp_async() {
  ensure_simple_lazy_lsp_async(
      fortran_client_, fortran_lsp_starting_, fortran_lsp_startup_thread_,
      {"lsp-fortran-start", "fortran", "status.fortls_missing", "status.fortls_failed",
       "status.fortls_started", "fortls start failed or binary missing", make_fortls_spec},
      &LspSymbolProvider::finish_fortran_lsp_start_locked);
}

void LspSymbolProvider::ensure_lua_lsp_async() {
  ensure_simple_lazy_lsp_async(
      lua_client_, lua_lsp_starting_, lua_lsp_startup_thread_,
      {"lsp-lua-start", "lua", "status.lua_ls_missing", "status.lua_ls_failed",
       "status.lua_ls_started", "lua-language-server start failed or binary missing",
       make_lua_ls_spec},
      &LspSymbolProvider::finish_lua_lsp_start_locked);
}

void LspSymbolProvider::ensure_typescript_lsp_async() {
  ensure_simple_lazy_lsp_async(
      typescript_client_, typescript_lsp_starting_, typescript_lsp_startup_thread_,
      {"lsp-ts-start", "typescript", "status.typescript_ls_missing", "status.typescript_ls_failed",
       "status.typescript_ls_started", "typescript-language-server start failed or binary missing",
       make_typescript_ls_spec},
      &LspSymbolProvider::finish_typescript_lsp_start_locked);
}

void LspSymbolProvider::ensure_cmake_lsp_async() {
  ensure_simple_lazy_lsp_async(
      cmake_client_, cmake_lsp_starting_, cmake_lsp_startup_thread_,
      {"lsp-cmake-start", "cmake", "status.neocmakelsp_missing", "status.neocmakelsp_failed",
       "status.neocmakelsp_started", "neocmakelsp start failed or binary missing",
       make_neocmakelsp_spec},
      &LspSymbolProvider::finish_cmake_lsp_start_locked);
}

void LspSymbolProvider::ensure_make_lsp_async() {
  ensure_simple_lazy_lsp_async(
      make_client_, make_lsp_starting_, make_lsp_startup_thread_,
      {"lsp-make-start", "make", "status.make_ls_missing", "status.make_ls_failed",
       "status.make_ls_started", "make-ls start failed or binary missing", make_make_ls_spec},
      &LspSymbolProvider::finish_make_lsp_start_locked);
}

void LspSymbolProvider::refresh_diagnostics_cache_locked() const {
  uint64_t revision = client_.diagnostics_revision();
  revision ^= client_diagnostics_revision(python_client_, 1);
  revision ^= client_diagnostics_revision(bash_client_, 2);
  revision ^= client_diagnostics_revision(tex_client_, 3);
  revision ^= client_diagnostics_revision(rust_client_, 4);
  revision ^= client_diagnostics_revision(go_client_, 5);
  revision ^= client_diagnostics_revision(zig_client_, 6);
  revision ^= client_diagnostics_revision(fortran_client_, 7);
  revision ^= client_diagnostics_revision(lua_client_, 8);
  revision ^= client_diagnostics_revision(typescript_client_, 9);
  revision ^= client_diagnostics_revision(cmake_client_, 10);
  revision ^= client_diagnostics_revision(make_client_, 11);
  revision ^= fortran_compiler_diag_revision_.load(std::memory_order_acquire) << 12;
  if (revision == cached_diag_revision_) {
    return;
  }
  cached_diagnostics_ = client_.all_diagnostics();
  append_client_diagnostics(cached_diagnostics_, python_client_);
  append_client_diagnostics(cached_diagnostics_, bash_client_);
  append_client_diagnostics(cached_diagnostics_, tex_client_);
  append_client_diagnostics(cached_diagnostics_, rust_client_);
  append_client_diagnostics(cached_diagnostics_, go_client_);
  append_client_diagnostics(cached_diagnostics_, zig_client_);
  append_client_diagnostics(cached_diagnostics_, fortran_client_);
  append_client_diagnostics(cached_diagnostics_, lua_client_);
  append_client_diagnostics(cached_diagnostics_, typescript_client_);
  append_client_diagnostics(cached_diagnostics_, cmake_client_);
  append_client_diagnostics(cached_diagnostics_, make_client_);
  for (const auto& entry : fortran_compiler_diagnostics_) {
    merge_document_diagnostics(cached_diagnostics_, entry.second);
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
  // Do not clear use_lsp_ if another lazy language server is already serving files.
  if (ok) {
    use_lsp_ = true;
  } else if (!any_lazy_lsp_client_ready(python_client_, bash_client_, tex_client_, rust_client_,
                                        go_client_, zig_client_, fortran_client_, lua_client_,
                                        typescript_client_, cmake_client_, make_client_)) {
    use_lsp_ = false;
  }
  if (!ok) {
    return;
  }
  lsp_ready_since_ms_ = steady_now_ms();
  if (ui_inhibited_ && use_background_index_) {
    client_.set_background_paused(true);
  }
  for (const auto& entry : open_buffers_) {
    const std::string lang = language_id_for_path(entry.first);
    if (is_lazy_lsp_language(lang)) {
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

  // Join outside mutex_: the async worker takes mutex_ while running jobs.
  stop_async_worker();
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
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
    bool want_worker = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      lsp_starting_.store(false, std::memory_order_release);
      finish_lsp_start_locked(ok);
      want_worker = ok && use_lsp_;
    }
    if (want_worker) {
      ensure_async_worker_running();
    }
  });
}

bool LspSymbolProvider::lsp_loading() const {
  return lsp_starting_.load(std::memory_order_acquire) ||
         python_lsp_starting_.load(std::memory_order_acquire) ||
         bash_lsp_starting_.load(std::memory_order_acquire) ||
         tex_lsp_starting_.load(std::memory_order_acquire) ||
         rust_lsp_starting_.load(std::memory_order_acquire) ||
         go_lsp_starting_.load(std::memory_order_acquire) ||
         zig_lsp_starting_.load(std::memory_order_acquire) ||
         fortran_lsp_starting_.load(std::memory_order_acquire) ||
         lua_lsp_starting_.load(std::memory_order_acquire) ||
         typescript_lsp_starting_.load(std::memory_order_acquire) ||
         cmake_lsp_starting_.load(std::memory_order_acquire) ||
         make_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::clangd_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return client_.ready();
}

bool LspSymbolProvider::python_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return python_client_ && python_client_->ready();
}

bool LspSymbolProvider::bash_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return bash_client_ && bash_client_->ready();
}

bool LspSymbolProvider::tex_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tex_client_ && tex_client_->ready();
}

bool LspSymbolProvider::clangd_starting() const {
  return lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::python_lsp_starting() const {
  return python_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::bash_lsp_starting() const {
  return bash_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::tex_lsp_starting() const {
  return tex_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::rust_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rust_client_ && rust_client_->ready();
}

bool LspSymbolProvider::go_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return go_client_ && go_client_->ready();
}

bool LspSymbolProvider::zig_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return zig_client_ && zig_client_->ready();
}

bool LspSymbolProvider::fortran_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return fortran_client_ && fortran_client_->ready();
}

bool LspSymbolProvider::lua_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lua_client_ && lua_client_->ready();
}

bool LspSymbolProvider::typescript_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return typescript_client_ && typescript_client_->ready();
}

bool LspSymbolProvider::cmake_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cmake_client_ && cmake_client_->ready();
}

bool LspSymbolProvider::make_lsp_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return make_client_ && make_client_->ready();
}

bool LspSymbolProvider::rust_lsp_starting() const {
  return rust_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::go_lsp_starting() const {
  return go_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::zig_lsp_starting() const {
  return zig_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::fortran_lsp_starting() const {
  return fortran_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::lua_lsp_starting() const {
  return lua_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::typescript_lsp_starting() const {
  return typescript_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::cmake_lsp_starting() const {
  return cmake_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::make_lsp_starting() const {
  return make_lsp_starting_.load(std::memory_order_acquire);
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
  // Join any restart thread *before* claiming stop_lsp_in_progress_. Otherwise the
  // restart body can block in stop_lsp() waiting for that flag while we wait on join.
  pending_transport_restart_.store(false, std::memory_order_release);
  std::thread restart_to_join;
  {
    std::lock_guard<std::mutex> life(lifecycle_mutex_);
    restart_to_join = std::move(lsp_restart_thread_);
  }
  join_thread_if_joinable(restart_to_join);
  lsp_restart_in_progress_.store(false, std::memory_order_release);

  {
    std::unique_lock<std::mutex> life(lifecycle_mutex_);
    if (stop_lsp_in_progress_.load(std::memory_order_acquire)) {
      lifecycle_cv_.wait(life, [this] {
        return !stop_lsp_in_progress_.load(std::memory_order_acquire);
      });
      return;
    }
    stop_lsp_in_progress_.store(true, std::memory_order_release);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_async_worker_stop_locked();
  }
  // Snapshot lazy clients under the provider lock, then stop outside it. Stopping a
  // language server joins its reader thread and must never run while holding mutex_.
  LspClient* python = nullptr;
  LspClient* bash = nullptr;
  LspClient* tex = nullptr;
  LspClient* rust = nullptr;
  LspClient* go = nullptr;
  LspClient* zig = nullptr;
  LspClient* fortran = nullptr;
  LspClient* lua = nullptr;
  LspClient* typescript = nullptr;
  LspClient* cmake = nullptr;
  LspClient* make = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    python = python_client_.get();
    bash = bash_client_.get();
    tex = tex_client_.get();
    rust = rust_client_.get();
    go = go_client_.get();
    zig = zig_client_.get();
    fortran = fortran_client_.get();
    lua = lua_client_.get();
    typescript = typescript_client_.get();
    cmake = cmake_client_.get();
    make = make_client_.get();
  }
  client_.stop();
  if (python != nullptr) {
    python->stop();
  }
  if (bash != nullptr) {
    bash->stop();
  }
  if (tex != nullptr) {
    tex->stop();
  }
  if (rust != nullptr) {
    rust->stop();
  }
  if (go != nullptr) {
    go->stop();
  }
  if (zig != nullptr) {
    zig->stop();
  }
  if (fortran != nullptr) {
    fortran->stop();
  }
  if (lua != nullptr) {
    lua->stop();
  }
  if (typescript != nullptr) {
    typescript->stop();
  }
  if (cmake != nullptr) {
    cmake->stop();
  }
  if (make != nullptr) {
    make->stop();
  }
  join_thread_if_joinable(lsp_startup_thread_);
  lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(python_lsp_startup_thread_);
  python_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(bash_lsp_startup_thread_);
  bash_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(tex_lsp_startup_thread_);
  tex_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(rust_lsp_startup_thread_);
  rust_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(go_lsp_startup_thread_);
  go_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(zig_lsp_startup_thread_);
  zig_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(fortran_lsp_startup_thread_);
  fortran_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(lua_lsp_startup_thread_);
  lua_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(typescript_lsp_startup_thread_);
  typescript_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(cmake_lsp_startup_thread_);
  cmake_lsp_starting_.store(false, std::memory_order_release);
  join_thread_if_joinable(make_lsp_startup_thread_);
  make_lsp_starting_.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> life(lifecycle_mutex_);
    join_thread_if_joinable(async_worker_);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    python_client_.reset();
    bash_client_.reset();
    tex_client_.reset();
    rust_client_.reset();
    go_client_.reset();
    zig_client_.reset();
    fortran_client_.reset();
    fortran_compiler_diagnostics_.clear();
    fortran_compiler_diag_revision_.store(0, std::memory_order_release);
    lua_client_.reset();
    typescript_client_.reset();
    cmake_client_.reset();
    make_client_.reset();
    reset_async_queues_locked();
    stop_lsp_locked_finalize();
  }

  {
    std::lock_guard<std::mutex> life(lifecycle_mutex_);
    stop_lsp_in_progress_.store(false, std::memory_order_release);
  }
  lifecycle_cv_.notify_all();
}

void LspSymbolProvider::restart_lsp_after_transport_failure() {
  if (shutting_down_.load(std::memory_order_acquire) ||
      stop_lsp_in_progress_.load(std::memory_order_acquire)) {
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
  if (shutting_down_.load(std::memory_order_acquire) ||
      stop_lsp_in_progress_.load(std::memory_order_acquire)) {
    return;
  }
  start_lsp_async(compile_dir);
}

void LspSymbolProvider::process_pending_transport_restart() {
  if (shutting_down_.load(std::memory_order_acquire) ||
      stop_lsp_in_progress_.load(std::memory_order_acquire) ||
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
  std::thread previous;
  {
    std::lock_guard<std::mutex> life(lifecycle_mutex_);
    if (shutting_down_.load(std::memory_order_acquire) ||
        stop_lsp_in_progress_.load(std::memory_order_acquire)) {
      lsp_restart_in_progress_.store(false, std::memory_order_release);
      return;
    }
    previous = std::move(lsp_restart_thread_);
  }
  join_thread_if_joinable(previous);
  {
    std::lock_guard<std::mutex> life(lifecycle_mutex_);
    if (shutting_down_.load(std::memory_order_acquire) ||
        stop_lsp_in_progress_.load(std::memory_order_acquire)) {
      lsp_restart_in_progress_.store(false, std::memory_order_release);
      return;
    }
    lsp_restart_thread_ = std::thread([this]() {
      set_current_thread_name("lsp-restart");
      restart_lsp_after_transport_failure();
      lsp_restart_in_progress_.store(false, std::memory_order_release);
    });
  }
}

void LspSymbolProvider::start_async_worker() {
  stop_async_worker();
  if (shutting_down_.load(std::memory_order_acquire) ||
      stop_lsp_in_progress_.load(std::memory_order_acquire)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    async_stop_ = false;
    async_jobs_.reset();
    async_results_.reset();
    {
      std::lock_guard<std::mutex> inflight(inflight_mutex_);
      inflight_symbols_.clear();
      inflight_semantic_.clear();
      inflight_hover_.clear();
      inflight_completion_.clear();
    }
  }
  {
    std::lock_guard<std::mutex> life(lifecycle_mutex_);
    if (shutting_down_.load(std::memory_order_acquire) ||
        stop_lsp_in_progress_.load(std::memory_order_acquire) || async_worker_.joinable()) {
      return;
    }
    async_worker_ = std::thread([this] {
      set_current_thread_name("lsp-async");
      async_worker_main();
    });
  }
}

void LspSymbolProvider::ensure_async_worker_running() {
  if (shutting_down_.load(std::memory_order_acquire) ||
      stop_lsp_in_progress_.load(std::memory_order_acquire)) {
    return;
  }
  if (async_worker_.joinable()) {
    return;
  }
  start_async_worker();
}

void LspSymbolProvider::signal_async_worker_stop_locked() {
  async_stop_ = true;
  async_jobs_.close();
}

void LspSymbolProvider::reset_async_queues_locked() {
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
}

void LspSymbolProvider::stop_async_worker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_async_worker_stop_locked();
  }
  // Serialize joins so stop_lsp / restart never join the same thread concurrently.
  {
    std::lock_guard<std::mutex> life(lifecycle_mutex_);
    join_thread_if_joinable(async_worker_);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    reset_async_queues_locked();
  }
}

void LspSymbolProvider::enqueue_document_symbols_locked(const std::string& path, bool force) {
  const std::string lang = language_id_for_path(path);
  if (language_id_is_shellscript(lang) || language_id_is_latex(lang)) {
    return;
  }
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

    // Lazy servers (cmake, rust-analyzer, …) may still be starting when the UI
    // queues a completion. Wait here so we do not drop the first keystrokes.
    if (job->kind == AsyncJobKind::Completion || job->kind == AsyncJobKind::Hover) {
      const std::string wait_path =
          job->kind == AsyncJobKind::Completion ? job->completion_params.path : job->hover_params.path;
      if (!wait_path.empty()) {
        wait_for_client_for_path(wait_path, 8000);
      }
    }

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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_) {
      pending_did_change_.erase(key);
      return;
    }
    if (pending_did_change_.find(key) == pending_did_change_.end()) {
      return;
    }
    for (const auto& entry : open_buffers_) {
      if (normalize_lsp_path(entry.first) == key) {
        path_to_send = entry.first;
        text_to_send = entry.second;
        break;
      }
    }
    if (path_to_send.empty() || !is_lsp_trackable_path(path_to_send, text_to_send)) {
      pending_did_change_.erase(key);
      return;
    }
  }

  LspClient* lsp = client_for_path(path_to_send);
  if (lsp == nullptr) {
    // Server still starting (common for lazy rust-analyzer). Keep pending and retry.
    schedule_did_change_debounce_wake();
    return;
  }
  lsp->did_change(path_to_send, text_to_send);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_did_change_.erase(key);
    pending_content_refresh_[key] = steady_now_ms();
  }
  if (language_id_is_fortran(language_id_for_path(path_to_send))) {
    refresh_fortran_compiler_diagnostics(path_to_send, text_to_send);
  }
}

void LspSymbolProvider::flush_pending_did_change_for_key_locked(const std::string& key) {
  // Never call the locking flush while mutex_ is held (deadlock). Collect under the
  // caller's lock, then send after release — callers that hold the lock should use
  // flush_all_pending_did_change_locked's key snapshot pattern, or call the unlocked
  // flush_pending_did_change_for_key after releasing.
  (void)key;
}

void LspSymbolProvider::flush_all_pending_did_change_locked() {
  // Assumes mutex_ is NOT held. Name kept for callers; gathers keys then flushes unlocked.
  std::vector<std::string> keys;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_ || pending_did_change_.empty()) {
      return;
    }
    keys.reserve(pending_did_change_.size());
    for (const auto& entry : pending_did_change_) {
      keys.push_back(entry.first);
    }
  }
  for (const std::string& key : keys) {
    flush_pending_did_change_for_key(key);
  }
}

void LspSymbolProvider::tick_pending_did_change_locked() {
  // Legacy helper: do not hold mutex_ across didChange I/O.
  tick_debounced_updates();
}

void LspSymbolProvider::tick_debounced_updates() {
  process_pending_transport_restart();
  std::vector<std::string> due;
  bool run_content_refresh = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Always flush document sync — even while UI is inhibited. Blocking didChange here
    // left language servers (especially rust-analyzer) stuck on the open-time buffer.
    if (use_lsp_ && !pending_did_change_.empty()) {
      const int64_t now = steady_now_ms();
      constexpr int64_t kDebounceMs = kLspDidChangeDebounceMs;
      for (const auto& entry : pending_did_change_) {
        if (now - entry.second >= kDebounceMs) {
          due.push_back(entry.first);
        }
      }
    }
    run_content_refresh = !ui_inhibited_;
  }
  for (const std::string& key : due) {
    flush_pending_did_change_for_key(key);
  }
  if (run_content_refresh) {
    std::lock_guard<std::mutex> lock(mutex_);
    tick_content_refresh_locked();
  }
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!use_lsp_) {
      return;
    }
    for (const auto& entry : open_buffers_) {
      if (normalize_lsp_path(entry.first) == key) {
        path_to_send = entry.first;
        text_to_send = entry.second;
        break;
      }
    }
    if (path_to_send.empty() || !is_lsp_trackable_path(path_to_send, text_to_send)) {
      pending_did_change_.erase(key);
      return;
    }
  }
  if (LspClient* lsp = client_for_path(path_to_send)) {
    lsp->did_change(path_to_send, text_to_send);
    std::lock_guard<std::mutex> lock(mutex_);
    pending_did_change_.erase(key);
    pending_content_refresh_[key] = steady_now_ms();
  } else {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_did_change_[key] = steady_now_ms();
    schedule_did_change_debounce_wake();
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
  if (bash_client_) {
    bash_client_->set_diagnostics_notify_callback(callback);
  }
  if (tex_client_) {
    tex_client_->set_diagnostics_notify_callback(callback);
  }
  if (rust_client_) {
    rust_client_->set_diagnostics_notify_callback(callback);
  }
  if (go_client_) {
    go_client_->set_diagnostics_notify_callback(callback);
  }
  if (zig_client_) {
    zig_client_->set_diagnostics_notify_callback(callback);
  }
  if (fortran_client_) {
    fortran_client_->set_diagnostics_notify_callback(callback);
  }
  if (lua_client_) {
    lua_client_->set_diagnostics_notify_callback(callback);
  }
  if (typescript_client_) {
    typescript_client_->set_diagnostics_notify_callback(callback);
  }
  if (cmake_client_) {
    cmake_client_->set_diagnostics_notify_callback(callback);
  }
  if (make_client_) {
    make_client_->set_diagnostics_notify_callback(callback);
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
  if (bash_client_) {
    bash_client_->set_request_counter(counter);
  }
  if (tex_client_) {
    tex_client_->set_request_counter(counter);
  }
  if (rust_client_) {
    rust_client_->set_request_counter(counter);
  }
  if (go_client_) {
    go_client_->set_request_counter(counter);
  }
  if (zig_client_) {
    zig_client_->set_request_counter(counter);
  }
  if (fortran_client_) {
    fortran_client_->set_request_counter(counter);
  }
  if (lua_client_) {
    lua_client_->set_request_counter(counter);
  }
  if (typescript_client_) {
    typescript_client_->set_request_counter(counter);
  }
  if (cmake_client_) {
    cmake_client_->set_request_counter(counter);
  }
  if (make_client_) {
    make_client_->set_request_counter(counter);
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
    if (bash_client_) {
      bash_client_->set_background_paused(true);
    }
    if (tex_client_) {
      tex_client_->set_background_paused(true);
    }
    if (rust_client_) {
      rust_client_->set_background_paused(true);
    }
    if (go_client_) {
      go_client_->set_background_paused(true);
    }
    if (zig_client_) {
      zig_client_->set_background_paused(true);
    }
    if (fortran_client_) {
      fortran_client_->set_background_paused(true);
    }
    if (lua_client_) {
      lua_client_->set_background_paused(true);
    }
    if (typescript_client_) {
      typescript_client_->set_background_paused(true);
    }
    if (cmake_client_) {
      cmake_client_->set_background_paused(true);
    }
    if (make_client_) {
      make_client_->set_background_paused(true);
    }
    return;
  }
  client_.set_background_paused(false);
  if (python_client_) {
    python_client_->set_background_paused(false);
  }
  if (bash_client_) {
    bash_client_->set_background_paused(false);
  }
  if (tex_client_) {
    tex_client_->set_background_paused(false);
  }
  if (rust_client_) {
    rust_client_->set_background_paused(false);
  }
  if (go_client_) {
    go_client_->set_background_paused(false);
  }
  if (zig_client_) {
    zig_client_->set_background_paused(false);
  }
  if (fortran_client_) {
    fortran_client_->set_background_paused(false);
  }
  if (lua_client_) {
    lua_client_->set_background_paused(false);
  }
  if (typescript_client_) {
    typescript_client_->set_background_paused(false);
  }
  if (cmake_client_) {
    cmake_client_->set_background_paused(false);
  }
  if (make_client_) {
    make_client_->set_background_paused(false);
  }
  if (should_flush) {
    flush_all_pending_did_change_locked();
    std::lock_guard<std::mutex> lock(mutex_);
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
  // Register the buffer before kicking lazy LSP start so finish_* can did_open it.
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
      open_header = notify_open && is_cpp_header_path(path);
      if (open_header) {
        notify_open = false;
      }
    }
  }
  const std::string lang = language_id_for_path(path);
  if (is_lazy_lsp_language(lang)) {
    if (language_id_is_python(lang)) {
      ensure_python_lsp_async();
    } else if (language_id_is_shellscript(lang)) {
      ensure_bash_lsp_async();
    } else if (language_id_is_latex(lang)) {
      ensure_tex_lsp_async();
    } else if (language_id_is_rust(lang)) {
      ensure_rust_lsp_async(path);
    } else if (language_id_is_go(lang)) {
      ensure_go_lsp_async();
    } else if (language_id_is_zig(lang)) {
      ensure_zig_lsp_async();
    } else if (language_id_is_fortran(lang)) {
      ensure_fortran_lsp_async();
    } else if (language_id_is_lua(lang)) {
      ensure_lua_lsp_async();
    } else if (language_id_is_js_ts(lang)) {
      ensure_typescript_lsp_async();
    } else if (language_id_is_cmake(lang)) {
      ensure_cmake_lsp_async();
    } else if (language_id_is_make(lang)) {
      ensure_make_lsp_async();
    }
  }
  if (ensure_open) {
    if (LspClient* lsp = client_for_path(path)) {
      lsp->did_open(path, text);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (use_lsp_ && is_cpp_header_path(path)) {
      open_companion_sources_for_clangd_locked(path);
    }
  } else if (open_header) {
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
    } else if (is_lazy_lsp_language(lang)) {
      if (language_id_is_python(lang)) {
        ensure_python_lsp_async();
      } else if (language_id_is_shellscript(lang)) {
        ensure_bash_lsp_async();
      } else if (language_id_is_latex(lang)) {
        ensure_tex_lsp_async();
      } else   if (language_id_is_rust(lang)) {
    ensure_rust_lsp_async(path);
  } else if (language_id_is_go(lang)) {
        ensure_go_lsp_async();
      } else if (language_id_is_zig(lang)) {
        ensure_zig_lsp_async();
      } else if (language_id_is_fortran(lang)) {
        ensure_fortran_lsp_async();
      } else if (language_id_is_lua(lang)) {
        ensure_lua_lsp_async();
      } else if (language_id_is_js_ts(lang)) {
        ensure_typescript_lsp_async();
      } else if (language_id_is_cmake(lang)) {
        ensure_cmake_lsp_async();
      } else if (language_id_is_make(lang)) {
        ensure_make_lsp_async();
      }
    }
  }
  if (language_id_is_fortran(lang) && is_lsp_trackable_path(path, text)) {
    refresh_fortran_compiler_diagnostics(path, text);
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
  if (!any_lsp_ready() && !language_allows_debounce_while_lsp_starting(language_id_for_path(path))) {
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
    lsp->did_save(path, saved_text);
    lsp->invalidate_semantic_tokens_for_file(path);
  }
  if (language_id_is_fortran(language_id_for_path(path))) {
    refresh_fortran_compiler_diagnostics(path, saved_text);
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
  append_workspace_symbols(out, python_client_, workspace_root_, query);
  append_workspace_symbols(out, bash_client_, workspace_root_, query);
  append_workspace_symbols(out, tex_client_, workspace_root_, query);
  append_workspace_symbols(out, rust_client_, workspace_root_, query);
  append_workspace_symbols(out, go_client_, workspace_root_, query);
  append_workspace_symbols(out, zig_client_, workspace_root_, query);
  append_workspace_symbols(out, fortran_client_, workspace_root_, query);
  append_workspace_symbols(out, lua_client_, workspace_root_, query);
  append_workspace_symbols(out, typescript_client_, workspace_root_, query);
  append_workspace_symbols(out, cmake_client_, workspace_root_, query);
  append_workspace_symbols(out, make_client_, workspace_root_, query);
  return out;
}

bool LspSymbolProvider::supports_semantic_completion() const {
  return completion_uses_async_fetch();
}

std::vector<CompletionItem> LspSymbolProvider::completions_at(
    const CompletionParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }

  std::string text = params.text;
  LspClient* lsp = prepare_lsp_client(params.path, text);
  if (lsp == nullptr) {
    return fallback_.completions_at(params);
  }
  const std::string key = normalize_lsp_path(params.path);
  if (key.empty()) {
    return {};
  }
  return lsp->completions_at(key, text, params.line, params.character);
}

bool LspSymbolProvider::completion_uses_async_fetch() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!lsp_enabled_) {
    return false;
  }
  if (any_lsp_ready()) {
    return true;
  }
  return python_lsp_starting_.load(std::memory_order_acquire) ||
         bash_lsp_starting_.load(std::memory_order_acquire) ||
         tex_lsp_starting_.load(std::memory_order_acquire) ||
         rust_lsp_starting_.load(std::memory_order_acquire) ||
         go_lsp_starting_.load(std::memory_order_acquire) ||
         zig_lsp_starting_.load(std::memory_order_acquire) ||
         fortran_lsp_starting_.load(std::memory_order_acquire) ||
         lua_lsp_starting_.load(std::memory_order_acquire) ||
         typescript_lsp_starting_.load(std::memory_order_acquire) ||
         cmake_lsp_starting_.load(std::memory_order_acquire) ||
         make_lsp_starting_.load(std::memory_order_acquire);
}

bool LspSymbolProvider::lazy_lsp_starting_for_path(const std::string& path) const {
  const std::string lang = language_id_for_path(path);
  if (language_id_is_python(lang)) {
    return python_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_shellscript(lang)) {
    return bash_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_latex(lang)) {
    return tex_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_rust(lang)) {
    return rust_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_go(lang)) {
    return go_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_zig(lang)) {
    return zig_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_fortran(lang)) {
    return fortran_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_lua(lang)) {
    return lua_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_js_ts(lang)) {
    return typescript_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_cmake(lang)) {
    return cmake_lsp_starting_.load(std::memory_order_acquire);
  }
  if (language_id_is_make(lang)) {
    return make_lsp_starting_.load(std::memory_order_acquire);
  }
  return false;
}

bool LspSymbolProvider::request_completion(const CompletionParams& params,
                                           const std::string& cache_key) {
  if (params.path.empty() || cache_key.empty() ||
      !is_lsp_trackable_path(params.path, params.text)) {
    return false;
  }

  const std::string path_key = normalize_lsp_path(params.path);
  if (path_key.empty()) {
    return false;
  }

  std::string text = params.text;
  ensure_lazy_lsp_for_path(params.path);
  LspClient* ready_client = prepare_lsp_client(params.path, text);
  const bool starting = lazy_lsp_starting_for_path(params.path);
  if (ready_client == nullptr && !starting) {
    // Not ready and not starting (missing binary / disabled) — do not pretend we fetched.
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lsp_enabled_) {
      return false;
    }
    if (ready_client == nullptr && client_for_path(params.path) == nullptr && !starting) {
      return false;
    }
    if (!params.text.empty()) {
      open_buffers_[params.path] = params.text;
    }
    latest_completion_key_by_path_[path_key] = cache_key;
    if (completion_cache_.find(cache_key) != completion_cache_.end()) {
      return true;
    }
  }

  client_.cancel_inflight_completion();
  cancel_client_completion(python_client_);
  cancel_client_completion(bash_client_);
  cancel_client_completion(tex_client_);
  cancel_client_completion(rust_client_);
  cancel_client_completion(go_client_);
  cancel_client_completion(zig_client_);
  cancel_client_completion(fortran_client_);
  cancel_client_completion(lua_client_);
  cancel_client_completion(typescript_client_);
  cancel_client_completion(cmake_client_);
  cancel_client_completion(make_client_);

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
      return true;
    }
    inflight_completion_.insert(cache_key);
  }

  ensure_async_worker_running();

  AsyncJob job;
  job.kind = AsyncJobKind::Completion;
  job.path = path_key;
  job.completion_key = cache_key;
  job.completion_params = params;
  if (!text.empty() && job.completion_params.text.empty()) {
    job.completion_params.text = text;
  }
  async_jobs_.remove_if(
      [](const AsyncJob& queued) { return queued.kind == AsyncJobKind::Completion; });
  async_jobs_.push_front(std::move(job));
  return true;
}

void LspSymbolProvider::cancel_completion_fetch() {
  client_.cancel_inflight_completion();
  cancel_client_completion(python_client_);
  cancel_client_completion(bash_client_);
  cancel_client_completion(tex_client_);
  cancel_client_completion(rust_client_);
  cancel_client_completion(go_client_);
  cancel_client_completion(zig_client_);
  cancel_client_completion(fortran_client_);
  cancel_client_completion(lua_client_);
  cancel_client_completion(typescript_client_);
  cancel_client_completion(cmake_client_);
  cancel_client_completion(make_client_);
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
  const int py_latest = client_latest_completion_id(python_client_);
  const int bash_latest = client_latest_completion_id(bash_client_);
  const int tex_latest = client_latest_completion_id(tex_client_);
  const int rust_latest = client_latest_completion_id(rust_client_);
  const int go_latest = client_latest_completion_id(go_client_);
  const int zig_latest = client_latest_completion_id(zig_client_);
  const int fortran_latest = client_latest_completion_id(fortran_client_);
  const int lua_latest = client_latest_completion_id(lua_client_);
  const int typescript_latest = client_latest_completion_id(typescript_client_);
  const int cmake_latest = client_latest_completion_id(cmake_client_);
  const int make_latest = client_latest_completion_id(make_client_);
  if (it->second.request_id <= 0 ||
      (it->second.request_id != clangd_latest && it->second.request_id != py_latest &&
       it->second.request_id != bash_latest && it->second.request_id != tex_latest &&
       it->second.request_id != rust_latest && it->second.request_id != go_latest &&
       it->second.request_id != zig_latest && it->second.request_id != fortran_latest &&
       it->second.request_id != lua_latest && it->second.request_id != typescript_latest &&
       it->second.request_id != cmake_latest && it->second.request_id != make_latest)) {
    completion_cache_.erase(it);
    return std::nullopt;
  }
  std::vector<CompletionItem> items = std::move(it->second.items);
  completion_cache_.erase(it);
  return items;
}

void LspSymbolProvider::ensure_lazy_lsp_for_path(const std::string& path) {
  const std::string lang = language_id_for_path(path);
  if (language_id_is_python(lang)) {
    ensure_python_lsp_async();
  } else if (language_id_is_shellscript(lang)) {
    ensure_bash_lsp_async();
  } else if (language_id_is_latex(lang)) {
    ensure_tex_lsp_async();
  } else   if (language_id_is_rust(lang)) {
    ensure_rust_lsp_async(path);
  } else if (language_id_is_go(lang)) {
    ensure_go_lsp_async();
  } else if (language_id_is_zig(lang)) {
    ensure_zig_lsp_async();
  } else if (language_id_is_fortran(lang)) {
    ensure_fortran_lsp_async();
  } else if (language_id_is_lua(lang)) {
    ensure_lua_lsp_async();
  } else if (language_id_is_js_ts(lang)) {
    ensure_typescript_lsp_async();
  } else if (language_id_is_cmake(lang)) {
    ensure_cmake_lsp_async();
  } else if (language_id_is_make(lang)) {
    ensure_make_lsp_async();
  }
}

bool LspSymbolProvider::wait_for_client_for_path(const std::string& path, int timeout_ms) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (LspClient* lsp = client_for_path(path); lsp != nullptr && lsp->ready()) {
        return true;
      }
      const std::string lang = language_id_for_path(path);
      if (!is_lazy_lsp_language(lang)) {
        return client_.ready();
      }
      // Idle and still no client ⇒ missing/failed start. Keep waiting only while starting.
      if (!lazy_lsp_starting_for_path(path)) {
        return false;
      }
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

LspClient* LspSymbolProvider::prepare_lsp_client(const std::string& path, std::string& text) {
  if (!lsp_enabled_ || path.empty() || !is_lsp_trackable_path(path, text)) {
    return nullptr;
  }
  ensure_lazy_lsp_for_path(path);
  // Never block the UI for seconds waiting for a lazy server. A short poll covers the
  // case where startup just finished; otherwise callers fail fast.
  wait_for_client_for_path(path, 150);

  LspClient* lsp = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_cpp_header_path(path)) {
      open_companion_sources_for_clangd_locked(path);
    }
    if (text.empty()) {
      text = buffer_text_for_path(path);
    }
    lsp = client_for_path(path);
    if (lsp == nullptr) {
      return nullptr;
    }
    if (!text.empty() && !lsp->document_is_open(path)) {
      lsp->did_open(path, text);
    }
  }
  return lsp;
}

bool LspSymbolProvider::supports_navigation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  // Allow navigation attempts whenever LSP is enabled: lazy servers may still be
  // starting, and tree-sitter can resolve local definitions (e.g. bash functions).
  return lsp_enabled_;
}

SourceLocation LspSymbolProvider::goto_definition(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::string text = params.text;
  SourceLocation loc;
  if (LspClient* lsp = prepare_lsp_client(params.path, text)) {
    loc = lsp->goto_definition(params.path, text, params.line, params.character);
  }
  if (loc.valid && !navigation_at_same_spot(loc, params)) {
    return loc;
  }
  NavigationParams ts_params = params;
  ts_params.text = text;
  SourceLocation ts = tree_sitter_service().definition_at(ts_params);
  if (ts.valid && !navigation_at_same_spot(ts, params)) {
    return ts;
  }
  if (ts.valid) {
    return ts;
  }
  return loc;
}

SourceLocation LspSymbolProvider::goto_declaration(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::string text = params.text;
  SourceLocation loc;
  if (LspClient* lsp = prepare_lsp_client(params.path, text)) {
    loc = lsp->goto_declaration(params.path, text, params.line, params.character);
    if (!loc.valid) {
      loc = lsp->goto_definition(params.path, text, params.line, params.character);
    }
  }
  if (loc.valid && !navigation_at_same_spot(loc, params)) {
    return loc;
  }
  NavigationParams ts_params = params;
  ts_params.text = text;
  SourceLocation ts = tree_sitter_service().definition_at(ts_params);
  if (ts.valid) {
    return ts;
  }
  return loc;
}

SourceLocation LspSymbolProvider::goto_implementation(const NavigationParams& params) {
  if (params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }
  std::string text = params.text;
  SourceLocation loc;
  if (LspClient* lsp = prepare_lsp_client(params.path, text)) {
    loc = lsp->goto_implementation(params.path, text, params.line, params.character);
    if (!loc.valid) {
      loc = lsp->goto_definition(params.path, text, params.line, params.character);
    }
  }
  if (loc.valid && !navigation_at_same_spot(loc, params)) {
    return loc;
  }
  NavigationParams ts_params = params;
  ts_params.text = text;
  SourceLocation ts = tree_sitter_service().definition_at(ts_params);
  if (ts.valid) {
    return ts;
  }
  return loc;
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
  return completion_uses_async_fetch();
}

void LspSymbolProvider::request_hover(const HoverParams& params, const std::string& cache_key) {
  if (params.path.empty() || cache_key.empty() ||
      !is_lsp_trackable_path(params.path, params.text)) {
    return;
  }

  std::string text = params.text;
  if (prepare_lsp_client(params.path, text) == nullptr) {
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
  revision ^= client_diagnostics_revision(python_client_, 1);
  revision ^= client_diagnostics_revision(bash_client_, 2);
  revision ^= client_diagnostics_revision(tex_client_, 3);
  revision ^= client_diagnostics_revision(rust_client_, 4);
  revision ^= client_diagnostics_revision(go_client_, 5);
  revision ^= client_diagnostics_revision(zig_client_, 6);
  revision ^= client_diagnostics_revision(fortran_client_, 7);
  revision ^= client_diagnostics_revision(lua_client_, 8);
  revision ^= client_diagnostics_revision(typescript_client_, 9);
  revision ^= client_diagnostics_revision(cmake_client_, 10);
  revision ^= client_diagnostics_revision(make_client_, 11);
  revision ^= fortran_compiler_diag_revision_.load(std::memory_order_acquire) << 12;
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
  const std::string key = normalize_lsp_path(path);
  if (!key.empty() && pending_did_change_.find(key) != pending_did_change_.end()) {
    return false;
  }
  // gfortran overlay is authoritative for compiler diagnostics; don't wait for fortls
  // (which often omits a versioned publishDiagnostics after didChange).
  if (!key.empty() && fortran_compiler_diagnostics_.find(key) != fortran_compiler_diagnostics_.end()) {
    return true;
  }
  if (const LspClient* lsp = client_for_path(path)) {
    const std::string text = buffer_text_for_path(path);
    if (!text.empty() && !lsp->document_has_text(path, text)) {
      return false;
    }
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
