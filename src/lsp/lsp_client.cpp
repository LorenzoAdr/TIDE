#include "lsp/lsp_client.hpp"

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lsp/lsp_uri.hpp"
#include "lsp/lsp_position.hpp"
#include "lsp/language_server_spec.hpp"
#include "indexer/index_rules.hpp"
#include "util/bundled_tools.hpp"

#include "app/workspace_config.hpp"
#include "util/clang_format_config.hpp"
#include "util/compile_commands_remap.hpp"
#include <chrono>

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

std::string relative_to_workspace(const std::string& workspace_root,
                                  const std::string& absolute_path) {
  if (workspace_root.empty() || absolute_path.empty()) {
    return {};
  }
  std::error_code ec;
  const auto rel = fs::relative(fs::path(absolute_path), fs::path(workspace_root), ec);
  if (ec) {
    return {};
  }
  return rel.generic_string();
}

bool diagnostics_items_equal(const std::vector<Diagnostic>& a, const std::vector<Diagnostic>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].line != b[i].line || a[i].start_col != b[i].start_col ||
        a[i].end_col != b[i].end_col || a[i].severity != b[i].severity ||
        a[i].message != b[i].message || a[i].source != b[i].source) {
      return false;
    }
  }
  return true;
}

}  // namespace

LspClient::LspClient() = default;

LspClient::~LspClient() {
  stop();
}

bool LspClient::spawn_language_server(const LanguageServerSpec& spec) {
  if (spec.command.empty()) {
    return false;
  }

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    return false;
  }

  if (pid == 0) {
    // Do not use child_die_with_parent() here: fork from the lsp-start thread plus
    // PR_SET_PDEATHSIG inherited by the server after exec causes immediate SIGTERM.
    if (!spec.workspace_root.empty()) {
      if (::chdir(spec.workspace_root.c_str()) != 0) {
        _exit(127);
      }
    }
    for (const std::string& entry : spec.env) {
      ::putenv(const_cast<char*>(entry.c_str()));
    }
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);

    const int devnull = ::open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
      ::close(devnull);
    }

    std::vector<std::string> args_strings;
    args_strings.push_back(spec.command);
    for (const std::string& arg : spec.args) {
      args_strings.push_back(arg);
    }

    std::vector<char*> argv;
    argv.reserve(args_strings.size() + 1);
    for (auto& arg : args_strings) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    execv(spec.command.c_str(), argv.data());
    _exit(127);
  }

  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);
  child_pid_ = pid;
  stdin_write_fd_ = stdin_pipe[1];
  stdout_read_fd_ = stdout_pipe[0];
  return transport_.start(stdin_write_fd_, stdout_read_fd_);
}

bool LspClient::spawn_clangd(const std::string& workspace_root,
                             const std::string& compile_commands_dir,
                             const bool use_gcc_query_driver,
                             const bool background_index) {
  const auto spec =
      make_clangd_spec(workspace_root, compile_commands_dir, use_gcc_query_driver,
                       background_index);
  if (!spec.has_value()) {
    return false;
  }
  return spawn_language_server(*spec);
}

bool LspClient::initialize(const std::string& workspace_root,
                           const std::string& initialization_options_json) {
  nlohmann::json params;
  params["processId"] = static_cast<int>(getpid());
  const std::string root_uri = path_to_uri(workspace_root);
  params["rootUri"] = root_uri;
  // rust-analyzer / gopls prefer workspaceFolders; rootUri alone often yields an empty project.
  if (!root_uri.empty()) {
    const std::string folder_name = fs::path(workspace_root).filename().string();
    params["workspaceFolders"] = nlohmann::json::array(
        {{{"uri", root_uri},
          {"name", folder_name.empty() ? std::string("workspace") : folder_name}}});
  }
  params["capabilities"]["workspace"]["workspaceFolders"] = true;
  // neocmakelsp dynamically registers watched-files / configuration; advertise support.
  params["capabilities"]["workspace"]["didChangeWatchedFiles"] = {
      {"dynamicRegistration", true}};
  params["capabilities"]["workspace"]["configuration"] = true;
  params["capabilities"]["textDocument"]["documentSymbol"]["hierarchicalDocumentSymbolSupport"] =
      true;
  params["capabilities"]["textDocument"]["publishDiagnostics"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["synchronization"] = {
      {"didSave", true}, {"willSave", false}, {"willSaveWaitUntil", false}};
  params["capabilities"]["textDocument"]["completion"]["completionItem"]["snippetSupport"] =
      true;
  params["capabilities"]["textDocument"]["definition"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["declaration"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["hover"] = {{"contentFormat", {"plaintext", "markdown"}}};
  params["capabilities"]["textDocument"]["formatting"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["rename"] = {{"prepareSupport", true}};
  params["capabilities"]["textDocument"]["codeAction"] = {
      {"codeActionLiteralSupport",
       {{"codeActionKind",
         {{"valueSet", nlohmann::json::array({"quickfix", "refactor", "source"})}}}}}};
  params["capabilities"]["callHierarchy"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["semanticTokens"] = {
      {"requests", {{"range", false}, {"full", {{"delta", true}}}}},
      {"tokenTypes",
       {"namespace", "type",      "class",   "enum",       "interface", "struct",
        "typeParameter", "parameter", "variable", "property", "enumMember", "event",
        "function",      "method",    "macro",  "keyword",    "modifier",   "comment",
        "string",        "number",    "regexp", "operator",   "decorator"}},
      // TexLab deserializes SemanticTokensClientCapabilities strictly (serde); omit
      // tokenModifiers and the initialize handshake fails with "missing field".
      {"tokenModifiers", nlohmann::json::array()},
      {"formats", nlohmann::json::array({"relative"})}};
  // Same strictness: ResolveSupportClientCapabilities.properties is required when the
  // resolveSupport object is present (empty {} crashes TexLab).
  params["capabilities"]["workspace"]["symbol"]["resolveSupport"] = {
      {"properties", nlohmann::json::array({"location"})}};
  params["clientInfo"]["name"] = "tgdb";
  params["clientInfo"]["version"] = "0.1.0";
  if (!initialization_options_json.empty()) {
    try {
      params["initializationOptions"] = nlohmann::json::parse(initialization_options_json);
    } catch (...) {
      // Keep initialize usable even if a spec ships malformed options.
    }
  }

  nlohmann::json result;
  if (!send_lsp_request("initialize", std::move(params), 15000, &result)) {
    return false;
  }

  load_semantic_legend(result);
  document_sync_kind_ = 2;
  if (result.contains("capabilities") && result["capabilities"].is_object()) {
    const auto& caps = result["capabilities"];
    semantic_tokens_supported_ =
        caps.contains("semanticTokensProvider") && caps["semanticTokensProvider"].is_object();
    definition_supported_ = !caps.contains("definitionProvider") ||
                            !(caps["definitionProvider"].is_boolean() &&
                              caps["definitionProvider"].get<bool>() == false);
    declaration_supported_ =
        caps.contains("declarationProvider") &&
        !(caps["declarationProvider"].is_boolean() &&
          caps["declarationProvider"].get<bool>() == false);
    implementation_supported_ =
        caps.contains("implementationProvider") &&
        !(caps["implementationProvider"].is_boolean() &&
          caps["implementationProvider"].get<bool>() == false);
    if (caps.contains("textDocumentSync")) {
      const auto& sync = caps["textDocumentSync"];
      if (sync.is_number_integer()) {
        document_sync_kind_ = sync.get<int>();
      } else if (sync.is_object() && sync.contains("change") && sync["change"].is_number_integer()) {
        document_sync_kind_ = sync["change"].get<int>();
      }
    }
  }
  send_lsp_notification("initialized", nlohmann::json::object());
  return true;
}

nlohmann::json LspClient::make_did_change_content(const std::string& previous_text,
                                                  const std::string& text) const {
  // TextDocumentSyncKind.Incremental == 2. Full (1) / None (0) require whole-document updates.
  // Sending ranged edits to Full-only servers (neocmakelsp, make-ls) desyncs the buffer and
  // yields null completion results at the edit site.
  if (document_sync_kind_ == 2) {
    if (const std::optional<LspTextEdit> edit = single_lsp_edit_between(previous_text, text)) {
      return nlohmann::json::array({lsp_content_change_json(*edit)});
    }
  }
  return nlohmann::json::array({{{"text", text}}});
}

bool LspClient::start(const LanguageServerSpec& spec) {
  stop();
  if (spec.workspace_root.empty() || spec.command.empty()) {
    return false;
  }
  // Handlers must be installed *after* transport_.start(): start() calls stop() which
  // clears notification/eof handlers to make reader join safe during teardown.
  if (!spawn_language_server(spec)) {
    stop();
    return false;
  }
  transport_.set_notification_handler([this](const std::string& method,
                                             const nlohmann::json& params) {
    on_lsp_notification(method, params);
  });
  // Only drop superseded *completion* responses. Dropping any older request id while a
  // completion is in flight used to discard definition/hover replies issued earlier,
  // which made Ctrl+click wait the full timeout and then fail.
  transport_.set_response_acceptance_filter([this](int response_id) {
    const int latest = latest_completion_request_id_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(completion_ids_mutex_);
    const auto it = completion_request_ids_.find(response_id);
    if (it == completion_request_ids_.end()) {
      return true;
    }
    completion_request_ids_.erase(it);
    return response_id >= latest;
  });
  transport_.set_reader_eof_handler([this] { on_transport_reader_eof(); });
  workspace_root_ = spec.workspace_root;
  server_id_ = spec.id;
  intentionally_stopping_.store(false, std::memory_order_release);
  if (!initialize(spec.workspace_root, spec.initialization_options_json)) {
    stop();
    return false;
  }
  ready_ = true;
  intentionally_stopping_.store(false, std::memory_order_release);
  return true;
}

bool LspClient::start(const std::string& workspace_root,
                      const std::string& compile_commands_dir,
                      const bool use_gcc_query_driver, const bool background_index) {
  std::string compile_dir = compile_commands_dir;
  if (compile_dir.empty()) {
    WorkspaceConfig default_config;
    const auto setup = ensure_compile_commands_for_clangd(workspace_root, default_config);
    compile_dir = setup.compile_dir;
  }
  const auto spec =
      make_clangd_spec(workspace_root, compile_dir, use_gcc_query_driver, background_index);
  if (!spec.has_value()) {
    return false;
  }
  return start(*spec);
}

void LspClient::stop() {
  std::lock_guard<std::mutex> stop_lock(stop_mutex_);
  intentionally_stopping_.store(true, std::memory_order_release);
  ready_ = false;

  // Kill the language server *before* joining the reader. basedpyright/node and
  // similar servers can keep producing huge diagnostics; closing pipes alone has
  // raced with blocking reads, and callbacks during join can re-enter the app.
  // transport_.stop() clears notification handlers so the reader cannot call back
  // into the app while we join — keep diagnostics_notify_callback_ so a later
  // start() still notifies the UI without the provider having to rebind it.
  if (child_pid_ > 0) {
    int status = 0;
    kill(child_pid_, SIGCONT);
    kill(child_pid_, SIGTERM);
    for (int i = 0; i < 20; ++i) {
      const pid_t result = waitpid(child_pid_, &status, WNOHANG);
      if (result == child_pid_ || result < 0) {
        child_pid_ = -1;
        break;
      }
      usleep(50000);
    }
    if (child_pid_ > 0) {
      kill(child_pid_, SIGKILL);
      for (int i = 0; i < 40; ++i) {
        const pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == child_pid_ || result < 0) {
          child_pid_ = -1;
          break;
        }
        usleep(50000);
      }
      if (child_pid_ > 0) {
        child_pid_ = -1;
      }
    }
  }

  transport_.stop();

  stdin_write_fd_ = -1;
  stdout_read_fd_ = -1;

  next_request_id_ = 1;
  inflight_completion_request_id_.store(0, std::memory_order_release);
  latest_completion_request_id_.store(0, std::memory_order_release);
  {
    std::lock_guard<std::mutex> ids_lock(completion_ids_mutex_);
    completion_request_ids_.clear();
  }
  server_id_.clear();

  std::lock_guard<std::mutex> lock(mutex_);
  documents_.clear();
  symbol_cache_.clear();
  semantic_token_cache_.clear();
  semantic_token_attempts_.clear();
  diagnostics_.clear();
  diagnostics_revision_.store(0, std::memory_order_release);
  semantic_token_types_.clear();
  semantic_tokens_supported_ = false;
  definition_supported_ = true;
  declaration_supported_ = true;
  implementation_supported_ = true;
  document_sync_kind_ = 2;
  workspace_root_.clear();
}

void LspClient::set_background_paused(bool paused) {
  const pid_t pid = child_pid_;
  if (pid <= 0) {
    return;
  }
  kill(pid, paused ? SIGSTOP : SIGCONT);
}

int64_t LspClient::steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void LspClient::set_request_counter(std::atomic<uint64_t>* counter) {
  request_counter_ = counter;
}

bool LspClient::send_lsp_request(const std::string& method, nlohmann::json params, int timeout_ms,
                                   nlohmann::json* out) {
  if (out == nullptr || intentionally_stopping_.load(std::memory_order_acquire)) {
    return false;
  }
  int id = 0;
  {
    std::lock_guard<std::mutex> lock(transport_io_mutex_);
    if (!transport_.is_running() || intentionally_stopping_.load(std::memory_order_acquire)) {
      return false;
    }
    id = next_request_id_++;
    if (!transport_.write_request(id, method, std::move(params))) {
      return false;
    }
    if (request_counter_ != nullptr) {
      request_counter_->fetch_add(1, std::memory_order_relaxed);
    }
  }
  const bool ok = transport_.wait_response(id, timeout_ms, out);
  return ok;
}

void LspClient::cancel_inflight_completion() {
  const int id = inflight_completion_request_id_.exchange(0, std::memory_order_acq_rel);
  if (id > 0) {
    transport_.send_cancel(id);
  }
}

bool LspClient::send_completion_request(nlohmann::json params, int timeout_ms,
                                          nlohmann::json* out) {
  if (out == nullptr || intentionally_stopping_.load(std::memory_order_acquire)) {
    return false;
  }
  cancel_inflight_completion();

  int id = 0;
  {
    std::lock_guard<std::mutex> lock(transport_io_mutex_);
    if (!transport_.is_running() || intentionally_stopping_.load(std::memory_order_acquire)) {
      return false;
    }
    id = next_request_id_++;
    latest_completion_request_id_.store(id, std::memory_order_release);
    inflight_completion_request_id_.store(id, std::memory_order_release);
    {
      std::lock_guard<std::mutex> ids_lock(completion_ids_mutex_);
      completion_request_ids_.insert(id);
    }
    if (!transport_.write_request(id, "textDocument/completion", std::move(params))) {
      inflight_completion_request_id_.store(0, std::memory_order_release);
      std::lock_guard<std::mutex> ids_lock(completion_ids_mutex_);
      completion_request_ids_.erase(id);
      return false;
    }
    if (request_counter_ != nullptr) {
      request_counter_->fetch_add(1, std::memory_order_relaxed);
    }
  }

  const bool ok = transport_.wait_response(id, timeout_ms, out);
  if (inflight_completion_request_id_.load(std::memory_order_acquire) == id) {
    inflight_completion_request_id_.store(0, std::memory_order_release);
  }
  if (!ok) {
    return false;
  }
  return latest_completion_request_id_.load(std::memory_order_acquire) == id;
}

void LspClient::send_lsp_notification(const std::string& method, nlohmann::json params) {
  if (intentionally_stopping_.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard<std::mutex> lock(transport_io_mutex_);
  if (!transport_.is_running()) {
    return;
  }
  transport_.send_notification(method, std::move(params));
}

bool LspClient::transport_running() const {
  return transport_.is_running();
}

bool LspClient::process_alive() const {
  return child_pid_ > 0 && kill(child_pid_, 0) == 0;
}

void LspClient::on_transport_reader_eof() {
  ready_ = false;
  if (child_pid_ > 0) {
    int status = 0;
    const pid_t result = waitpid(child_pid_, &status, WNOHANG);
    if (result == child_pid_) {
      child_pid_ = -1;
    }
  }
}

void LspClient::invalidate_cache(const std::string& absolute_path) {
  const std::string key = normalize_lsp_path(absolute_path);
  symbol_cache_.erase(key);
}

void LspClient::invalidate_semantic_tokens(const std::string& absolute_path) {
  const std::string key = normalize_lsp_path(absolute_path);
  semantic_token_cache_.erase(key);
  semantic_token_attempts_.erase(key);
}

// Used when the document's text changes: resets the retry backoff so the next
// ensure_semantic_tokens() call fetches promptly, but -- unlike invalidate_semantic_tokens()
// -- deliberately keeps the cached SemanticTokenDocument (with its resultId/raw token data)
// around. has_ready_semantic_tokens() already stops treating it as current the moment
// `generation` is bumped (see the callers below), so nothing gets shown that doesn't match the
// new text; preserving the old document here just lets the next fetch ask clangd for the
// /full/delta edits instead of re-sending the whole file's tokens.
void LspClient::mark_semantic_tokens_stale(const std::string& absolute_path) {
  const std::string key = normalize_lsp_path(absolute_path);
  semantic_token_attempts_.erase(key);
}

void LspClient::invalidate_semantic_tokens_for_file(const std::string& absolute_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  invalidate_semantic_tokens(absolute_path);
}

std::vector<std::string> LspClient::default_semantic_token_types() {
  return {"namespace", "type",      "class",   "enum",       "interface", "struct",
          "typeParameter", "parameter", "variable", "property", "enumMember", "event",
          "function",      "method",    "macro",  "keyword",    "modifier",   "comment",
          "string",        "number",    "regexp", "operator",   "decorator"};
}

void LspClient::load_semantic_legend(const nlohmann::json& initialize_result) {
  semantic_token_types_ = default_semantic_token_types();
  if (!initialize_result.contains("capabilities")) {
    return;
  }
  const auto& caps = initialize_result["capabilities"];
  if (!caps.contains("semanticTokensProvider")) {
    return;
  }
  const auto& provider = caps["semanticTokensProvider"];
  if (!provider.contains("legend") || !provider["legend"].contains("tokenTypes") ||
      !provider["legend"]["tokenTypes"].is_array()) {
    return;
  }
  semantic_token_types_.clear();
  for (const auto& token_type : provider["legend"]["tokenTypes"]) {
    if (token_type.is_string()) {
      semantic_token_types_.push_back(token_type.get<std::string>());
    }
  }
  if (semantic_token_types_.empty()) {
    semantic_token_types_ = default_semantic_token_types();
  }
}

void LspClient::did_open(const std::string& absolute_path, const std::string& text) {
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return;
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return;
  }

  DocumentState doc;
  bool notify_open = false;
  bool notify_change = false;
  std::string previous_text;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it != documents_.end()) {
      if (it->second.text == text) {
        return;
      }
      previous_text = it->second.text;
      it->second.text = text;
      it->second.version += 1;
      it->second.generation += 1;
      mark_semantic_tokens_stale(key);
      doc = it->second;
      notify_change = true;
    } else {
      doc.uri = path_to_uri(key);
      doc.text = text;
      doc.version = 1;
      doc.generation = 1;
      documents_[key] = doc;
      invalidate_cache(key);
      invalidate_semantic_tokens(key);
      notify_open = true;
    }
  }

  if (notify_open) {
    nlohmann::json params = {
        {"textDocument",
         {{"uri", doc.uri},
          {"languageId", language_id_for_path(key)},
          {"version", doc.version},
          {"text", doc.text}}}};
    send_lsp_notification("textDocument/didOpen", std::move(params));
  } else if (notify_change) {
    nlohmann::json params = {
        {"textDocument", {{"uri", doc.uri}, {"version", doc.version}}},
        {"contentChanges", make_did_change_content(previous_text, doc.text)}};
    send_lsp_notification("textDocument/didChange", std::move(params));
  }
}

void LspClient::did_change(const std::string& absolute_path, const std::string& text) {
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return;
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return;
  }

  DocumentState doc;
  bool open_new = false;
  std::string previous_text;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it == documents_.end()) {
      open_new = true;
    } else if (it->second.text == text) {
      return;
    } else {
      previous_text = it->second.text;
      it->second.text = text;
      it->second.version += 1;
      it->second.generation += 1;
      mark_semantic_tokens_stale(key);
      doc = it->second;
    }
  }
  if (open_new) {
    did_open(absolute_path, text);
    return;
  }

  nlohmann::json params = {{"textDocument", {{"uri", doc.uri}, {"version", doc.version}}},
                           {"contentChanges", make_did_change_content(previous_text, doc.text)}};
  send_lsp_notification("textDocument/didChange", std::move(params));
}

void LspClient::did_save(const std::string& absolute_path, const std::string& text) {
  if (!ready_.load() || absolute_path.empty() || !is_lsp_trackable_path(absolute_path, text)) {
    return;
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return;
  }

  std::string uri;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it == documents_.end()) {
      return;
    }
    uri = it->second.uri;
  }

  nlohmann::json params = {{"textDocument", {{"uri", uri}}}};
  if (!text.empty()) {
    params["text"] = text;
  }
  send_lsp_notification("textDocument/didSave", std::move(params));
}

void LspClient::did_change_workspace_configuration(const nlohmann::json& settings) {
  if (!ready_.load() || settings.is_null()) {
    return;
  }
  nlohmann::json params = {{"settings", settings}};
  send_lsp_notification("workspace/didChangeConfiguration", std::move(params));
}

void LspClient::did_close(const std::string& absolute_path) {
  if (!ready_.load() || absolute_path.empty()) {
    return;
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return;
  }

  std::string uri;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it == documents_.end()) {
      return;
    }
    uri = it->second.uri;
    documents_.erase(it);
    invalidate_cache(key);
    invalidate_semantic_tokens(key);
    diagnostics_.erase(key);
  }

  nlohmann::json params = {{"textDocument", {{"uri", uri}}}};
  send_lsp_notification("textDocument/didClose", std::move(params));
}

SymbolKind LspClient::map_lsp_kind(int kind) {
  switch (kind) {
    case 3:
      return SymbolKind::kNamespace;
    case 5:
      return SymbolKind::kClass;
    case 23:
      return SymbolKind::kStruct;
    case 6:
    case 9:
      return SymbolKind::kMethod;
    case 12:
      return SymbolKind::kFunction;
    case 8:
    case 13:
    case 22:
    default:
      return SymbolKind::kVariable;
  }
}

std::string LspClient::kind_prefix(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::kNamespace:
      return "ns ";
    case SymbolKind::kClass:
      return "C ";
    case SymbolKind::kStruct:
      return "S ";
    case SymbolKind::kMethod:
      return "M ";
    case SymbolKind::kVariable:
      return "v ";
    case SymbolKind::kFunction:
    default:
      return "f ";
  }
}

void LspClient::flatten_symbols(const nlohmann::json& nodes, int depth,
                                  const std::string& relative_file,
                                  std::vector<SymbolInfo>* out) {
  if (!nodes.is_array()) {
    return;
  }
  for (const auto& node : nodes) {
    if (!node.is_object() || !node.contains("name")) {
      continue;
    }
    SymbolInfo info;
    info.name = node["name"].get<std::string>();
    if (node.contains("kind")) {
      info.kind = map_lsp_kind(node["kind"].get<int>());
    }
    info.name = kind_prefix(info.kind) + info.name;
    info.depth = depth;
    info.file = relative_file;
    const nlohmann::json* range = nullptr;
    if (node.contains("location") && node["location"].contains("range")) {
      range = &node["location"]["range"];
    } else if (node.contains("range")) {
      range = &node["range"];
    }
    if (range != nullptr && range->contains("start") && (*range)["start"].contains("line")) {
      info.line = (*range)["start"]["line"].get<int>() + 1;
    }
    if (range != nullptr && range->contains("end") && (*range)["end"].contains("line")) {
      info.end_line = (*range)["end"]["line"].get<int>() + 1;
    }
    out->push_back(std::move(info));

    if (node.contains("children")) {
      flatten_symbols(node["children"], depth + 1, relative_file, out);
    }
  }
}

bool LspClient::has_cached_document_symbols(const std::string& absolute_path) const {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return symbol_cache_.find(key) != symbol_cache_.end();
}

std::optional<std::vector<SymbolInfo>> LspClient::cached_document_symbols(
    const std::string& absolute_path) const {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return std::nullopt;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = symbol_cache_.find(key);
  if (it == symbol_cache_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<SymbolInfo> LspClient::document_symbols(const std::string& absolute_path) {
  if (!ready_.load() || absolute_path.empty() || !is_indexed_source_path(absolute_path)) {
    return {};
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (auto cached = cached_document_symbols(key)) {
    return *cached;
  }

  std::string text;
  bool need_open = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it != documents_.end()) {
      text = it->second.text;
    } else {
      need_open = true;
    }
  }
  if (text.empty()) {
    text = read_file_text(key);
  }
  if (need_open && !text.empty()) {
    did_open(key, text);
  }

  const std::string uri = path_to_uri(key);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}}};
  nlohmann::json result;
  if (!send_lsp_request("textDocument/documentSymbol", std::move(params), 10000, &result)) {
    return {};
  }

  const std::string relative_file = relative_to_workspace(workspace_root_, key);
  std::vector<SymbolInfo> symbols;
  flatten_symbols(result, 0, relative_file, &symbols);

  std::lock_guard<std::mutex> lock(mutex_);
  symbol_cache_[key] = symbols;
  return symbols;
}

std::vector<SymbolInfo> LspClient::workspace_symbols(const std::string& workspace_root,
                                                     const std::string& query) {
  if (!ready_.load()) {
    return {};
  }
  (void)workspace_root;

  nlohmann::json params = {{"query", query.empty() ? "a" : query}};
  nlohmann::json result;
  if (!send_lsp_request("workspace/symbol", std::move(params), 10000, &result)) {
    return {};
  }

  if (!result.is_array()) {
    return {};
  }

  std::vector<SymbolInfo> symbols;
  for (const auto& item : result) {
    if (!item.is_object() || !item.contains("name")) {
      continue;
    }
    SymbolInfo info;
    if (item.contains("kind")) {
      info.kind = map_lsp_kind(item["kind"].get<int>());
    }
    info.name = kind_prefix(info.kind) + item["name"].get<std::string>();
    if (item.contains("location") && item["location"].contains("uri")) {
      const std::string abs = uri_to_path(item["location"]["uri"].get<std::string>());
      info.file = relative_to_workspace(workspace_root_, abs);
      if (item["location"].contains("range") &&
          item["location"]["range"].contains("start") &&
          item["location"]["range"]["start"].contains("line")) {
        info.line = item["location"]["range"]["start"]["line"].get<int>() + 1;
      }
    }
    symbols.push_back(std::move(info));
  }
  return symbols;
}

SymbolKind LspClient::map_completion_kind(int kind) {
  switch (kind) {
    case 3:
      return SymbolKind::kNamespace;
    case 5:
    case 7:
      return SymbolKind::kClass;
    case 22:
    case 23:
      return SymbolKind::kStruct;
    case 2:
    case 6:
      return SymbolKind::kMethod;
    case 4:
    case 12:
      return SymbolKind::kFunction;
    default:
      return SymbolKind::kVariable;
  }
}

std::string LspClient::completion_label(const nlohmann::json& item) {
  if (!item.contains("label")) {
    return {};
  }
  if (item["label"].is_string()) {
    return item["label"].get<std::string>();
  }
  if (item["label"].is_object() && item["label"].contains("label")) {
    return item["label"]["label"].get<std::string>();
  }
  return {};
}

CompletionItem LspClient::parse_completion_item(const nlohmann::json& item) {
  CompletionItem out;
  out.label = completion_label(item);
  if (out.label.empty()) {
    return out;
  }

  if (item.contains("kind")) {
    out.kind = map_completion_kind(item["kind"].get<int>());
  }
  if (item.contains("detail") && item["detail"].is_string()) {
    out.detail = item["detail"].get<std::string>();
  }
  if (item.contains("sortText") && item["sortText"].is_string()) {
    out.sort_text = item["sortText"].get<std::string>();
  }
  if (item.contains("filterText") && item["filterText"].is_string()) {
    out.filter_text = item["filterText"].get<std::string>();
  }

  if (item.contains("insertTextFormat") && item["insertTextFormat"].is_number_integer()) {
    const int format = item["insertTextFormat"].get<int>();
    out.insert_format =
        format == 2 ? InsertTextFormat::kSnippet : InsertTextFormat::kPlain;
  }

  if (item.contains("insertText") && item["insertText"].is_string()) {
    out.insert_text = item["insertText"].get<std::string>();
  }
  if (item.contains("textEdit") && item["textEdit"].is_object()) {
    const auto& edit = item["textEdit"];
    if (out.insert_text.empty() && edit.contains("newText") && edit["newText"].is_string()) {
      out.insert_text = edit["newText"].get<std::string>();
    }
    if (out.insert_format == InsertTextFormat::kPlain && edit.contains("insertTextFormat") &&
        edit["insertTextFormat"].is_number_integer()) {
      const int format = edit["insertTextFormat"].get<int>();
      out.insert_format =
          format == 2 ? InsertTextFormat::kSnippet : InsertTextFormat::kPlain;
    }
    // TextEdit uses "range"; InsertReplaceEdit uses "replace" (prefer replace on accept).
    const nlohmann::json* range = nullptr;
    if (edit.contains("range") && edit["range"].is_object()) {
      range = &edit["range"];
    } else if (edit.contains("replace") && edit["replace"].is_object()) {
      range = &edit["replace"];
    }
    if (range != nullptr && range->contains("start") && range->contains("end")) {
      const auto& start = (*range)["start"];
      const auto& end = (*range)["end"];
      if (start.contains("line") && start.contains("character") && end.contains("character")) {
        out.has_replace_range = true;
        out.replace_line = start["line"].get<int>();
        out.replace_start = start["character"].get<int>();
        out.replace_end = end["character"].get<int>();
      }
    }
  }

  if (out.insert_text.empty()) {
    out.insert_text = out.label;
  }

  if (out.insert_format == InsertTextFormat::kPlain &&
      out.insert_text.find('$') != std::string::npos) {
    out.insert_format = InsertTextFormat::kSnippet;
  }

  return out;
}

std::vector<CompletionItem> LspClient::completions_at(const std::string& absolute_path,
                                                        const std::string& text, int line,
                                                        int character, bool document_synced,
                                                        int* out_request_id) {
  if (out_request_id != nullptr) {
    *out_request_id = 0;
  }
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return {};
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return {};
  }

  const int wait_ms = !text.empty() ? completion_wait_timeout_ms(text, line) : 5000;

  if (!document_synced) {
    constexpr int kMaxReadyPollMs = 400;
    const int ready_poll_ms = std::min(wait_ms, kMaxReadyPollMs);
    if (!text.empty()) {
      wait_for_completion_ready(absolute_path, text, line, ready_poll_ms);
    } else {
      uint64_t generation = 0;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = documents_.find(key);
        if (it != documents_.end()) {
          generation = it->second.generation;
        }
      }
      if (generation > 0) {
        wait_for_completion_ready(absolute_path, std::string{}, line,
                                  std::min(5000, kMaxReadyPollMs));
      }
    }
  }

  const std::string uri = path_to_uri(key);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"position", make_lsp_position(text, line, character)},
                           {"context", {{"triggerKind", 1}}}};

  nlohmann::json result;
  const bool rpc_ok = send_completion_request(std::move(params), 10000, &result);
  if (!rpc_ok) {
    return {};
  }
  if (out_request_id != nullptr) {
    *out_request_id = latest_completion_request_id();
  }

  auto parse_items = [](const nlohmann::json& completion_result) -> nlohmann::json {
    if (completion_result.is_array()) {
      return completion_result;
    }
    if (completion_result.is_object() && completion_result.contains("items") &&
        completion_result["items"].is_array()) {
      return completion_result["items"];
    }
    return nlohmann::json::array();
  };

  nlohmann::json items = parse_items(result);

  std::vector<CompletionItem> completions;
  for (const auto& item : items) {
    if (!item.is_object()) {
      continue;
    }
    CompletionItem parsed = parse_completion_item(item);
    if (parsed.label.empty()) {
      continue;
    }
    completions.push_back(std::move(parsed));
    if (completions.size() >= 200) {
      break;
    }
  }

  if (completions.empty() && line > 0 && !text.empty() &&
      !intentionally_stopping_.load(std::memory_order_acquire) && !document_synced) {
    wait_for_completion_ready(absolute_path, text, line, 400);
    nlohmann::json retry_params = {{"textDocument", {{"uri", uri}}},
                                   {"position", make_lsp_position(text, line, character)},
                                   {"context", {{"triggerKind", 1}}}};
    nlohmann::json retry_result;
    if (send_completion_request(std::move(retry_params), 5000, &retry_result)) {
      if (out_request_id != nullptr) {
        *out_request_id = latest_completion_request_id();
      }
      items = parse_items(retry_result);
      for (const auto& item : items) {
        if (!item.is_object()) {
          continue;
        }
        CompletionItem parsed = parse_completion_item(item);
        if (parsed.label.empty()) {
          continue;
        }
        completions.push_back(std::move(parsed));
        if (completions.size() >= 200) {
          break;
        }
      }
    }
  }

  return completions;
}

bool LspClient::parse_single_location(const nlohmann::json& loc, SourceLocation* out) {
  if (out == nullptr || !loc.is_object()) {
    return false;
  }

  std::string uri;
  const nlohmann::json* range = nullptr;
  if (loc.contains("targetUri")) {
    if (loc["targetUri"].is_string()) {
      uri = loc["targetUri"].get<std::string>();
    }
    if (loc.contains("targetRange") && loc["targetRange"].is_object()) {
      range = &loc["targetRange"];
    }
  } else {
    if (loc.contains("uri") && loc["uri"].is_string()) {
      uri = loc["uri"].get<std::string>();
    }
    if (loc.contains("range") && loc["range"].is_object()) {
      range = &loc["range"];
    }
  }

  if (uri.empty() || range == nullptr || !range->contains("start")) {
    return false;
  }

  const auto& start = (*range)["start"];
  if (!start.contains("line") || !start.contains("character")) {
    return false;
  }

  out->path = uri_to_path(uri);
  out->line = start["line"].get<int>();
  out->character = start["character"].get<int>();
  out->valid = !out->path.empty();
  return out->valid;
}

SourceLocation LspClient::parse_location_result(const nlohmann::json& result) {
  SourceLocation loc;
  if (result.is_null()) {
    return loc;
  }
  if (result.is_array()) {
    for (const auto& entry : result) {
      if (parse_single_location(entry, &loc)) {
        return loc;
      }
    }
    return loc;
  }
  parse_single_location(result, &loc);
  return loc;
}

SourceLocation LspClient::request_location(const std::string& method,
                                           const std::string& absolute_path,
                                           const std::string& text, int line,
                                           int character) {
  SourceLocation loc;
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return loc;
  }

  if (!text.empty()) {
    did_change(absolute_path, text);
  }

  const std::string uri = path_to_uri(absolute_path);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"position", make_lsp_position(text, line, character)}};

  nlohmann::json result;
  if (!send_lsp_request(method, std::move(params), 10000, &result)) {
    return loc;
  }
  return parse_location_result(result);
}

SourceLocation LspClient::goto_definition(const std::string& absolute_path,
                                          const std::string& text, int line,
                                          int character) {
  if (!definition_supported_) {
    return {};
  }
  cancel_inflight_completion();
  return request_location("textDocument/definition", absolute_path, text, line, character);
}

SourceLocation LspClient::goto_declaration(const std::string& absolute_path,
                                           const std::string& text, int line,
                                           int character) {
  if (!declaration_supported_) {
    return {};
  }
  cancel_inflight_completion();
  return request_location("textDocument/declaration", absolute_path, text, line, character);
}

SourceLocation LspClient::goto_implementation(const std::string& absolute_path,
                                              const std::string& text, int line,
                                              int character) {
  if (!implementation_supported_) {
    return {};
  }
  cancel_inflight_completion();
  return request_location("textDocument/implementation", absolute_path, text, line, character);
}

std::string LspClient::strip_markdown(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  bool in_code = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '`') {
      in_code = !in_code;
      continue;
    }
    if (!in_code && c == '*' && i + 1 < text.size() && text[i + 1] == '*') {
      i += 1;
      continue;
    }
    if (!in_code && c == '*' ) {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

void LspClient::append_hover_content(const nlohmann::json& content, HoverInfo* out) {
  if (out == nullptr) {
    return;
  }
  if (content.is_string()) {
    const std::string raw = content.get<std::string>();
    if (out->title.empty()) {
      out->title = strip_markdown(raw);
    } else {
      out->body_lines.push_back(strip_markdown(raw));
    }
    return;
  }
  if (content.is_object()) {
    std::string value;
    if (content.contains("value") && content["value"].is_string()) {
      value = content["value"].get<std::string>();
    }
    if (value.empty()) {
      return;
    }
    const std::string plain = strip_markdown(value);
    if (out->title.empty()) {
      out->title = plain;
    } else {
      out->body_lines.push_back(plain);
    }
    return;
  }
  if (content.is_array()) {
    for (const auto& item : content) {
      append_hover_content(item, out);
    }
  }
}

HoverInfo LspClient::parse_hover_result(const nlohmann::json& result) {
  HoverInfo info;
  if (result.is_null()) {
    return info;
  }
  if (!result.is_object() || !result.contains("contents")) {
    return info;
  }
  append_hover_content(result["contents"], &info);
  info.valid = !info.title.empty() || !info.body_lines.empty();
  return info;
}

HoverInfo LspClient::hover(const std::string& absolute_path, const std::string& text,
                           int line, int character) {
  HoverInfo info;
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return info;
  }

  if (!text.empty()) {
    sync_document_and_wait(absolute_path, text);
  }

  const std::string uri = path_to_uri(absolute_path);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"position", make_lsp_position(text, line, character)}};

  nlohmann::json result;
  if (!send_lsp_request("textDocument/hover", std::move(params), 5000, &result)) {
    return info;
  }
  return parse_hover_result(result);
}

std::optional<std::string> LspClient::format_document(const std::string& absolute_path,
                                                      const std::string& text) {
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return std::nullopt;
  }

  if (!text.empty()) {
    did_change(absolute_path, text);
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return std::nullopt;
  }

  const std::string uri = path_to_uri(key);
  const ClangFormatConfig style = load_clang_format_for_file(key, workspace_root_);
  const int tab_size = std::max(1, style.effective_tab_width());
  const bool insert_spaces = !style.uses_tab_char();
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"options", {{"tabSize", tab_size},
                                        {"insertSpaces", insert_spaces}}}};

  nlohmann::json result;
  if (!send_lsp_request("textDocument/formatting", std::move(params), 30000, &result)) {
    return std::nullopt;
  }

  if (result.is_null()) {
    return text;
  }
  if (!result.is_array()) {
    return std::nullopt;
  }

  const std::vector<LspTextEdit> edits = parse_lsp_text_edits(result);
  if (edits.empty()) {
    return text;
  }
  return apply_lsp_text_edits(text, edits);
}

std::vector<LspFileEdits> LspClient::rename_symbol(const std::string& absolute_path,
                                                   const std::string& text, int line,
                                                   int character, const std::string& new_name) {
  if (!ready_.load() || absolute_path.empty() || new_name.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return {};
  }

  if (!text.empty()) {
    did_open(absolute_path, text);
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return {};
  }

  const std::string uri = path_to_uri(key);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"position", {{"line", line}, {"character", character}}},
                           {"newName", new_name}};

  nlohmann::json result;
  if (!send_lsp_request("textDocument/rename", std::move(params), 30000, &result)) {
    return {};
  }

  if (result.is_null()) {
    return {};
  }
  return parse_workspace_edit(result);
}

namespace {

nlohmann::json lsp_diagnostic_json(const Diagnostic& diag) {
  nlohmann::json out;
  out["range"] = {{"start", {{"line", diag.line}, {"character", diag.start_col}}},
                  {"end", {{"line", diag.line}, {"character", diag.end_col}}}};
  out["severity"] = static_cast<int>(diag.severity);
  out["message"] = diag.message;
  if (!diag.source.empty()) {
    out["source"] = diag.source;
  }
  return out;
}

std::vector<LspFileEdits> workspace_edit_from_json(const nlohmann::json& edit_json) {
  if (edit_json.is_null()) {
    return {};
  }
  return parse_workspace_edit(edit_json);
}

std::vector<LspFileEdits> edits_from_code_action(const nlohmann::json& action) {
  if (!action.is_object()) {
    return {};
  }
  if (action.contains("edit")) {
    return workspace_edit_from_json(action["edit"]);
  }
  if (action.contains("workspaceEdit")) {
    return workspace_edit_from_json(action["workspaceEdit"]);
  }
  return {};
}

CodeActionItem parse_code_action_item(const nlohmann::json& item) {
  CodeActionItem out;
  if (!item.is_object()) {
    return out;
  }
  if (item.contains("title") && item["title"].is_string()) {
    out.title = item["title"].get<std::string>();
  }
  if (item.contains("kind") && item["kind"].is_string()) {
    out.kind = item["kind"].get<std::string>();
  }
  out.lsp_payload = item;
  out.file_edits = edits_from_code_action(item);
  return out;
}

}  // namespace

std::vector<CodeActionItem> LspClient::code_actions(const CodeActionParams& params) {
  if (!ready_.load() || params.path.empty() || !is_lsp_trackable_path(params.path, params.text)) {
    return {};
  }

  if (!params.text.empty()) {
    did_open(params.path, params.text);
  }

  const std::string key = normalize_lsp_path(params.path);
  if (key.empty()) {
    return {};
  }

  const std::string uri = path_to_uri(key);
  nlohmann::json request = {
      {"textDocument", {{"uri", uri}}},
      {"range",
       {{"start", {{"line", params.line}, {"character", params.start_col}}},
        {"end", {{"line", params.line}, {"character", params.end_col}}}}},
      {"context",
       {{"diagnostics", nlohmann::json::array({lsp_diagnostic_json(params.diagnostic)})},
        {"only", nlohmann::json::array({"quickfix"})}}}};

  nlohmann::json result;
  if (!send_lsp_request("textDocument/codeAction", std::move(request), 15000, &result)) {
    return {};
  }

  std::vector<CodeActionItem> items;
  if (result.is_null()) {
    return items;
  }
  if (!result.is_array()) {
    if (result.is_object()) {
      items.push_back(parse_code_action_item(result));
    }
    return items;
  }

  for (const auto& entry : result) {
    CodeActionItem item = parse_code_action_item(entry);
    if (!item.title.empty()) {
      items.push_back(std::move(item));
    }
  }

  for (CodeActionItem& item : items) {
    if (!item.file_edits.empty()) {
      continue;
    }
    if (item.lsp_payload.contains("command")) {
      continue;
    }
    item.file_edits = resolve_code_action_edits(item.lsp_payload);
  }
  return items;
}

std::vector<LspFileEdits> LspClient::resolve_code_action_edits(const nlohmann::json& action) {
  if (!ready_.load() || action.is_null() || !action.is_object()) {
    return {};
  }

  nlohmann::json result;
  if (!send_lsp_request("codeAction/resolve", action, 15000, &result)) {
    return {};
  }
  return edits_from_code_action(result);
}

namespace {

SymbolKind symbol_kind_from_lsp(int kind) {
  switch (kind) {
    case 3:
      return SymbolKind::kNamespace;
    case 5:
      return SymbolKind::kClass;
    case 23:
      return SymbolKind::kStruct;
    case 6:
    case 9:
      return SymbolKind::kMethod;
    case 12:
      return SymbolKind::kFunction;
    case 8:
    case 13:
    case 22:
    default:
      return SymbolKind::kVariable;
  }
}

CallHierarchyItem parse_call_hierarchy_item(const nlohmann::json& item) {
  CallHierarchyItem out;
  out.kind = SymbolKind::kFunction;
  if (!item.is_object()) {
    return out;
  }
  if (item.contains("name") && item["name"].is_string()) {
    out.name = item["name"].get<std::string>();
  }
  if (item.contains("detail") && item["detail"].is_string()) {
    out.detail = item["detail"].get<std::string>();
  }
  if (item.contains("uri") && item["uri"].is_string()) {
    out.path = normalize_lsp_path(uri_to_path(item["uri"].get<std::string>()));
  }
  const nlohmann::json* range = nullptr;
  if (item.contains("selectionRange") && item["selectionRange"].is_object()) {
    range = &item["selectionRange"];
  } else if (item.contains("range") && item["range"].is_object()) {
    range = &item["range"];
  }
  if (range != nullptr && range->contains("start") && (*range)["start"].is_object()) {
    const auto& start = (*range)["start"];
    if (start.contains("line")) {
      out.line = start["line"].get<int>();
    }
    if (start.contains("character")) {
      out.character = start["character"].get<int>();
    }
  }
  if (item.contains("kind") && item["kind"].is_number_integer()) {
    out.kind = symbol_kind_from_lsp(item["kind"].get<int>());
  }
  out.lsp_payload = item;
  out.valid = !out.path.empty() && !out.name.empty();
  return out;
}

std::vector<CallHierarchyItem> parse_call_hierarchy_items(const nlohmann::json& result) {
  std::vector<CallHierarchyItem> items;
  if (result.is_null()) {
    return items;
  }
  if (result.is_array()) {
    for (const auto& entry : result) {
      CallHierarchyItem item = parse_call_hierarchy_item(entry);
      if (item.valid) {
        items.push_back(std::move(item));
      }
    }
    return items;
  }
  CallHierarchyItem item = parse_call_hierarchy_item(result);
  if (item.valid) {
    items.push_back(std::move(item));
  }
  return items;
}

std::vector<CallHierarchyItem> parse_call_hierarchy_relations(const nlohmann::json& result,
                                                              const char* field) {
  std::vector<CallHierarchyItem> items;
  if (!result.is_array()) {
    return items;
  }
  for (const auto& entry : result) {
    if (!entry.is_object() || !entry.contains(field)) {
      continue;
    }
    CallHierarchyItem item = parse_call_hierarchy_item(entry[field]);
    if (!item.valid) {
      continue;
    }
    if (entry.contains("fromRanges") && entry["fromRanges"].is_array() &&
        !entry["fromRanges"].empty()) {
      const nlohmann::json& range = entry["fromRanges"][0];
      if (range.is_object() && range.contains("start") && range["start"].is_object()) {
        const auto& start = range["start"];
        if (start.contains("line")) {
          item.call_site_line = start["line"].get<int>();
        }
        if (start.contains("character")) {
          item.call_site_character = start["character"].get<int>();
        }
        item.has_call_site = true;
      }
    }
    items.push_back(std::move(item));
  }
  return items;
}

}  // namespace

std::vector<CallHierarchyItem> LspClient::prepare_call_hierarchy(const std::string& absolute_path,
                                                                 const std::string& text,
                                                                 int line, int character) {
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return {};
  }

  if (!text.empty()) {
    did_open(absolute_path, text);
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return {};
  }

  const std::string uri = path_to_uri(key);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"position", {{"line", line}, {"character", character}}}};

  nlohmann::json result;
  if (!send_lsp_request("textDocument/prepareCallHierarchy", std::move(params), 15000, &result)) {
    return {};
  }
  const auto items = parse_call_hierarchy_items(result);
  return items;
}

std::vector<CallHierarchyItem> LspClient::incoming_calls(const CallHierarchyItem& item) {
  if (!ready_.load() || !item.valid || item.lsp_payload.is_null()) {
    return {};
  }

  nlohmann::json params = {{"item", item.lsp_payload}};
  nlohmann::json result;
  if (!send_lsp_request("callHierarchy/incomingCalls", std::move(params), 15000, &result)) {
    return {};
  }
  return parse_call_hierarchy_relations(result, "from");
}

std::vector<CallHierarchyItem> LspClient::outgoing_calls(const CallHierarchyItem& item) {
  if (!ready_.load() || !item.valid || item.lsp_payload.is_null()) {
    return {};
  }

  nlohmann::json params = {{"item", item.lsp_payload}};
  nlohmann::json result;
  if (!send_lsp_request("callHierarchy/outgoingCalls", std::move(params), 15000, &result)) {
    return {};
  }
  return parse_call_hierarchy_relations(result, "to");
}

SemanticTokenDocument LspClient::decode_semantic_tokens_from_data(
    std::vector<int64_t> data, const std::vector<std::string>& token_types) {
  SemanticTokenDocument doc;
  doc.token_types = token_types;
  if (data.empty() || data.size() % 5 != 0) {
    return doc;
  }

  int line = 0;
  int start = 0;
  int max_line = 0;

  for (std::size_t i = 0; i < data.size(); i += 5) {
    const int delta_line = static_cast<int>(data[i]);
    const int delta_start = static_cast<int>(data[i + 1]);
    line += delta_line;
    if (delta_line == 0) {
      start += delta_start;
    } else {
      start = delta_start;
    }
    const int length = static_cast<int>(data[i + 2]);
    if (length <= 0) {
      continue;
    }

    SemanticTokenSpan span;
    span.start_col = start;
    span.length = length;
    span.type = static_cast<int>(data[i + 3]);
    span.modifiers = static_cast<int>(data[i + 4]);

    if (line >= max_line) {
      doc.lines.resize(static_cast<std::size_t>(line + 1));
      max_line = line + 1;
    }
    doc.lines[static_cast<std::size_t>(line)].push_back(span);
  }

  doc.ready = !doc.lines.empty();
  doc.raw_data = std::move(data);
  return doc;
}

// Splices a `textDocument/semanticTokens/full/delta` response's edits into a copy of the
// previously decoded flat token array, per the LSP spec: each edit removes `deleteCount`
// ints starting at `start` and inserts its own `data` there, and edits are applied in the
// order the server sent them (each one's `start`/`deleteCount` is relative to the array as
// left by the previous edit in the same response, not to the original array).
void LspClient::apply_semantic_token_edits(std::vector<int64_t>* data,
                                           const nlohmann::json& edits) {
  if (data == nullptr || !edits.is_array()) {
    return;
  }
  for (const auto& edit : edits) {
    if (!edit.is_object() || !edit.contains("start") || !edit.contains("deleteCount") ||
        !edit["start"].is_number_integer() || !edit["deleteCount"].is_number_integer()) {
      continue;
    }
    const std::size_t start =
        std::min<std::size_t>(static_cast<std::size_t>(edit["start"].get<int64_t>()), data->size());
    const std::size_t delete_count = std::min<std::size_t>(
        static_cast<std::size_t>(edit["deleteCount"].get<int64_t>()), data->size() - start);

    std::vector<int64_t> insertion;
    if (edit.contains("data") && edit["data"].is_array()) {
      insertion.reserve(edit["data"].size());
      for (const auto& value : edit["data"]) {
        insertion.push_back(value.is_number_integer() ? value.get<int64_t>() : 0);
      }
    }

    data->erase(data->begin() + static_cast<std::ptrdiff_t>(start),
               data->begin() + static_cast<std::ptrdiff_t>(start + delete_count));
    data->insert(data->begin() + static_cast<std::ptrdiff_t>(start), insertion.begin(),
                insertion.end());
  }
}

bool LspClient::refresh_semantic_tokens(const std::string& absolute_path) {
  if (!ready_.load() || absolute_path.empty() || !is_lsp_trackable_path(absolute_path) ||
      !semantic_tokens_supported_) {
    return false;
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return false;
  }

  std::string uri;
  std::vector<std::string> token_types;
  uint64_t generation = 0;
  std::string previous_result_id;
  std::vector<int64_t> previous_raw_data;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it == documents_.end()) {
      return false;
    }
    uri = it->second.uri;
    generation = it->second.generation;
    token_types = semantic_token_types_;
    if (token_types.empty()) {
      token_types = default_semantic_token_types();
    }
    // Chain off the last response we decoded (even if it's stale for the current
    // generation -- mark_semantic_tokens_stale() keeps it around for exactly this) so
    // clangd can reply with a small delta instead of the whole file's tokens.
    const auto cached = semantic_token_cache_.find(key);
    if (cached != semantic_token_cache_.end() && !cached->second.result_id.empty() &&
        !cached->second.raw_data.empty()) {
      previous_result_id = cached->second.result_id;
      previous_raw_data = cached->second.raw_data;
    }
  }

  const bool use_delta = !previous_result_id.empty();
  nlohmann::json params = {{"textDocument", {{"uri", uri}}}};
  if (use_delta) {
    params["previousResultId"] = previous_result_id;
  }
  const std::string method = use_delta ? "textDocument/semanticTokens/full/delta"
                                       : "textDocument/semanticTokens/full";

  nlohmann::json result;
  if (!send_lsp_request(method, std::move(params), 30000, &result)) {
    return false;
  }

  if (!result.is_object()) {
    std::lock_guard<std::mutex> lock(mutex_);
    semantic_token_cache_.erase(key);
    return false;
  }

  SemanticTokenDocument decoded;
  if (result.contains("data") && result["data"].is_array()) {
    // Either a plain /full response, or the server decided a full result was cheaper
    // than a delta even though we asked for one -- both carry a flat `data` array.
    std::vector<int64_t> data;
    data.reserve(result["data"].size());
    bool valid = true;
    for (const auto& value : result["data"]) {
      if (!value.is_number_integer()) {
        valid = false;
        break;
      }
      data.push_back(value.get<int64_t>());
    }
    if (!valid) {
      std::lock_guard<std::mutex> lock(mutex_);
      semantic_token_cache_.erase(key);
      return false;
    }
    decoded = decode_semantic_tokens_from_data(std::move(data), token_types);
  } else if (use_delta && result.contains("edits") && result["edits"].is_array()) {
    std::vector<int64_t> merged = previous_raw_data;
    apply_semantic_token_edits(&merged, result["edits"]);
    decoded = decode_semantic_tokens_from_data(std::move(merged), token_types);
  } else {
    std::lock_guard<std::mutex> lock(mutex_);
    semantic_token_cache_.erase(key);
    return false;
  }

  if (result.contains("resultId") && result["resultId"].is_string()) {
    decoded.result_id = result["resultId"].get<std::string>();
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (decoded.ready) {
      decoded.source_generation = generation;
      semantic_token_cache_[key] = std::move(decoded);
      semantic_token_attempts_.erase(key);
      return true;
    }
    semantic_token_cache_.erase(key);
  }
  return false;
}

bool LspClient::ensure_semantic_tokens(const std::string& absolute_path) {
  if (!ready_.load() || absolute_path.empty() || !is_lsp_trackable_path(absolute_path) ||
      !semantic_tokens_supported_) {
    return false;
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return false;
  }

  bool should_fetch = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto doc_it = documents_.find(key);
    if (doc_it == documents_.end()) {
      return false;
    }
    const auto cached = semantic_token_cache_.find(key);
    if (cached != semantic_token_cache_.end() && cached->second.ready &&
        cached->second.source_generation == doc_it->second.generation) {
      return false;
    }

    SemanticTokenAttempt& attempt = semantic_token_attempts_[key];
    const int64_t now = steady_now_ms();
    constexpr int kMaxAttempts = 30;
    // Deliberately not 1000ms: see the comment in lsp/lsp_sync.hpp -- this keeps the
    // semantic-token fetch retry from landing in the same tick as the didChange flush
    // and the diagnostics-display debounce.
    constexpr int64_t kRetryIntervalMs = 1300;
    if (attempt.count >= kMaxAttempts) {
      return false;
    }
    if (attempt.count > 0 && now - attempt.last_ms < kRetryIntervalMs) {
      return false;
    }

    attempt.count += 1;
    attempt.last_ms = now;
    should_fetch = true;
  }

  if (!should_fetch) {
    return false;
  }

  const bool fetched = refresh_semantic_tokens(key);
  if (!fetched) {
  }
  return fetched;
}

SemanticTokenDocument LspClient::semantic_tokens_for_file(const std::string& absolute_path) {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = semantic_token_cache_.find(key);
  if (it == semantic_token_cache_.end()) {
    return {};
  }
  return it->second;
}

bool LspClient::has_ready_semantic_tokens(const std::string& absolute_path) const {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto doc_it = documents_.find(key);
  if (doc_it == documents_.end()) {
    return false;
  }
  const auto it = semantic_token_cache_.find(key);
  return it != semantic_token_cache_.end() && it->second.ready &&
         it->second.source_generation == doc_it->second.generation;
}

uint64_t LspClient::document_generation(const std::string& absolute_path) const {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = documents_.find(key);
  if (it == documents_.end()) {
    return 0;
  }
  return it->second.generation;
}

bool LspClient::wait_for_current_document_parsed(const std::string& absolute_path) {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return false;
  }
  uint64_t generation = 0;
  std::string text;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it == documents_.end()) {
      return false;
    }
    generation = it->second.generation;
    text = it->second.text;
  }
  return wait_for_document_ready(key, generation, parse_wait_timeout_ms(text));
}

bool LspClient::document_has_text(const std::string& absolute_path,
                                  const std::string& text) const {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = documents_.find(key);
  return it != documents_.end() && it->second.text == text;
}

bool LspClient::document_is_open(const std::string& absolute_path) const {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  return documents_.find(key) != documents_.end();
}

bool LspClient::document_diagnostics_current(const std::string& absolute_path) const {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return true;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = documents_.find(key);
  if (it == documents_.end()) {
    return true;
  }
  return it->second.diagnostics_generation >= it->second.generation;
}

int LspClient::parse_wait_timeout_ms(const std::string& text) {
  const int64_t scaled = static_cast<int64_t>(text.size()) / 500;
  return static_cast<int>(std::clamp<int64_t>(500 + scaled, 500, 10000));
}

int LspClient::completion_wait_timeout_ms(const std::string& text, int line) {
  const int64_t base = parse_wait_timeout_ms(text);
  const int64_t line_bonus = std::min<int64_t>(static_cast<int64_t>(std::max(0, line)) * 2, 1500);
  return static_cast<int>(std::clamp<int64_t>(base + line_bonus, 800, 8000));
}

bool LspClient::document_semantic_tokens_cover_line(const std::string& key, uint64_t generation,
                                                    int line) const {
  if (line < 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto doc_it = documents_.find(key);
  if (doc_it == documents_.end() || doc_it->second.generation != generation) {
    return false;
  }
  const auto tok_it = semantic_token_cache_.find(key);
  if (tok_it == semantic_token_cache_.end() || !tok_it->second.ready ||
      tok_it->second.source_generation != generation) {
    return false;
  }
  return line < static_cast<int>(tok_it->second.lines.size());
}

bool LspClient::wait_for_document_ready(const std::string& key, uint64_t generation,
                                        int timeout_ms) {
  if (generation == 0 || timeout_ms <= 0) {
    return true;
  }

  const int64_t deadline = steady_now_ms() + timeout_ms;
  while (steady_now_ms() < deadline) {
    if (intentionally_stopping_.load(std::memory_order_acquire) || !transport_.is_running()) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = documents_.find(key);
      if (it != documents_.end() &&
          it->second.diagnostics_generation >= generation) {
        return true;
      }
    }
    usleep(5000);
  }
  return false;
}

void LspClient::wait_for_completion_ready(const std::string& absolute_path,
                                          const std::string& text, int line,
                                          int timeout_ms) {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty() || timeout_ms <= 0) {
    return;
  }

  if (!text.empty()) {
    did_change(absolute_path, text);
  }

  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it != documents_.end()) {
      generation = it->second.generation;
    }
  }
  if (generation == 0) {
    return;
  }

  const int64_t deadline = steady_now_ms() + timeout_ms;
  while (steady_now_ms() < deadline) {
    if (intentionally_stopping_.load(std::memory_order_acquire) || !transport_.is_running()) {
      return;
    }
    bool ready = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = documents_.find(key);
      if (it != documents_.end() && it->second.generation == generation) {
        if (it->second.idle_generation >= generation) {
          ready = true;
        } else if (it->second.diagnostics_generation >= generation) {
          const auto tok_it = semantic_token_cache_.find(key);
          if (tok_it != semantic_token_cache_.end() && tok_it->second.ready &&
              tok_it->second.source_generation == generation &&
              line < static_cast<int>(tok_it->second.lines.size())) {
            ready = true;
          }
        }
      }
    }
    if (ready) {
      return;
    }
    usleep(50000);
  }

}

void LspClient::sync_document_and_wait(const std::string& absolute_path,
                                       const std::string& text) {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return;
  }

  if (!text.empty()) {
    did_change(absolute_path, text);
  }

  uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it != documents_.end()) {
      generation = it->second.generation;
    }
  }

  if (generation > 0) {
    wait_for_document_ready(key, generation, parse_wait_timeout_ms(text));
  }
}

Diagnostic LspClient::parse_diagnostic(const nlohmann::json& item) {
  Diagnostic diag;
  if (!item.is_object()) {
    return diag;
  }
  if (item.contains("message") && item["message"].is_string()) {
    diag.message = item["message"].get<std::string>();
  }
  if (item.contains("source") && item["source"].is_string()) {
    diag.source = item["source"].get<std::string>();
  }
  if (item.contains("severity") && item["severity"].is_number_integer()) {
    const int sev = item["severity"].get<int>();
    if (sev >= 1 && sev <= 4) {
      diag.severity = static_cast<DiagnosticSeverity>(sev);
    }
  }
  if (item.contains("range") && item["range"].is_object()) {
    const auto& range = item["range"];
    if (range.contains("start") && range["start"].is_object()) {
      const auto& start = range["start"];
      if (start.contains("line")) {
        diag.line = start["line"].get<int>();
      }
      if (start.contains("character")) {
        diag.start_col = start["character"].get<int>();
      }
    }
    if (range.contains("end") && range["end"].is_object()) {
      const auto& end = range["end"];
      if (end.contains("character")) {
        diag.end_col = end["character"].get<int>();
      }
    }
  }
  if (diag.end_col <= diag.start_col) {
    diag.end_col = diag.start_col + 1;
  }
  return diag;
}

DocumentDiagnostics LspClient::parse_publish_diagnostics(const nlohmann::json& params) {
  DocumentDiagnostics doc;
  if (!params.is_object() || !params.contains("uri") || !params["uri"].is_string()) {
    return doc;
  }
  doc.path = uri_to_path(params["uri"].get<std::string>());
  doc.path = normalize_lsp_path(doc.path);
  if (!params.contains("diagnostics") || !params["diagnostics"].is_array()) {
    return doc;
  }
  for (const auto& item : params["diagnostics"]) {
    Diagnostic parsed = parse_diagnostic(item);
    if (!parsed.message.empty()) {
      doc.items.push_back(std::move(parsed));
    }
  }
  return doc;
}

void LspClient::on_lsp_notification(const std::string& method, const nlohmann::json& params) {
  if (method == "$/clangd/fileStatus") {
    if (!params.is_object() || !params.contains("uri") || !params["uri"].is_string()) {
      return;
    }
    std::string path = uri_to_path(params["uri"].get<std::string>());
    path = normalize_lsp_path(path);
    if (path.empty() || !is_lsp_trackable_path(path)) {
      return;
    }
    std::string state;
    if (params.contains("state") && params["state"].is_string()) {
      state = params["state"].get<std::string>();
    }
    if (state != "idle") {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(path);
    if (it != documents_.end()) {
      it->second.idle_generation = it->second.generation;
    }
    return;
  }

  if (method != "textDocument/publishDiagnostics") {
    return;
  }
  DocumentDiagnostics doc = parse_publish_diagnostics(params);
  if (doc.path.empty() || !is_lsp_trackable_path(doc.path)) {
    return;
  }
  const std::string path = doc.path;
  std::function<void(const std::string&)> notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = diagnostics_.find(path);
    const bool has_items = !doc.items.empty();
    auto mark_diagnostics_current = [&]() {
      const auto it = documents_.find(path);
      if (it == documents_.end()) {
        return;
      }
      int diag_version = -1;
      if (params.contains("version") && params["version"].is_number_integer()) {
        diag_version = params["version"].get<int>();
      }
      if (diag_version < 0 || diag_version == it->second.version) {
        it->second.diagnostics_generation = it->second.generation;
        // clangd signals readiness via $/clangd/fileStatus; other servers (rust-analyzer,
        // gopls, …) typically do not. Treat a matching diagnostics publish as "idle" so
        // wait_for_completion_ready does not burn its full timeout.
        it->second.idle_generation = it->second.generation;
      }
    };

    if (existing != diagnostics_.end() && diagnostics_items_equal(existing->second.items, doc.items)) {
      mark_diagnostics_current();
      return;
    }
    if (!has_items) {
      diagnostics_.erase(path);
    } else {
      diagnostics_[path] = std::move(doc);
    }
    mark_diagnostics_current();

    diagnostics_revision_.fetch_add(1, std::memory_order_release);
    notify = diagnostics_notify_callback_;
  }
  if (notify) {
    notify(path);
  }
}

void LspClient::set_diagnostics_notify_callback(
    std::function<void(const std::string& path)> callback) {
  diagnostics_notify_callback_ = std::move(callback);
}

DocumentDiagnostics LspClient::diagnostics_for_file(const std::string& absolute_path) {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty() || !is_lsp_trackable_path(key)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = diagnostics_.find(key);
  if (it == diagnostics_.end()) {
    DocumentDiagnostics empty;
    empty.path = key;
    return empty;
  }
  return it->second;
}

std::vector<DocumentDiagnostics> LspClient::all_diagnostics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<DocumentDiagnostics> out;
  out.reserve(diagnostics_.size());
  for (const auto& entry : diagnostics_) {
    if (is_lsp_trackable_path(entry.first)) {
      out.push_back(entry.second);
    }
  }
  return out;
}

}  // namespace tgdb
