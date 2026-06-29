#include "lsp/lsp_client.hpp"

#include <array>
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
#include "indexer/index_rules.hpp"
#include "util/compile_commands_setup.hpp"

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

}  // namespace

LspClient::LspClient() = default;

LspClient::~LspClient() {
  stop();
}

bool LspClient::spawn_clangd(const std::string& compile_commands_dir) {
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    return false;
  }

  const std::string& compile_dir = compile_commands_dir;
  const char* clangd_bin = std::getenv("CLANGD_PATH");
  if (clangd_bin == nullptr || clangd_bin[0] == '\0') {
    clangd_bin = "clangd";
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

    if (!compile_dir.empty()) {
      std::string arg = "--compile-commands-dir=" + compile_dir;
      std::array<char*, 3> argv = {const_cast<char*>(clangd_bin),
                                   const_cast<char*>(arg.c_str()), nullptr};
      execvp(clangd_bin, argv.data());
    } else {
      std::array<char*, 2> argv = {const_cast<char*>(clangd_bin), nullptr};
      execvp(clangd_bin, argv.data());
    }
    _exit(127);
  }

  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);
  child_pid_ = pid;
  stdin_write_fd_ = stdin_pipe[1];
  stdout_read_fd_ = stdout_pipe[0];
  return transport_.start(stdin_write_fd_, stdout_read_fd_);
}

bool LspClient::initialize(const std::string& workspace_root) {
  nlohmann::json params;
  params["processId"] = static_cast<int>(getpid());
  params["rootUri"] = path_to_uri(workspace_root);
  params["capabilities"]["textDocument"]["documentSymbol"]["hierarchicalDocumentSymbolSupport"] =
      true;
  params["capabilities"]["textDocument"]["publishDiagnostics"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["completion"]["completionItem"]["snippetSupport"] =
      true;
  params["capabilities"]["textDocument"]["definition"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["declaration"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["hover"] = {{"contentFormat", {"plaintext", "markdown"}}};
  params["capabilities"]["textDocument"]["formatting"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["rename"] = {{"prepareSupport", true}};
  params["capabilities"]["callHierarchy"] = nlohmann::json::object();
  params["capabilities"]["textDocument"]["semanticTokens"] = {
      {"requests", {{"range", false}, {"full", {{"delta", false}}}}},
      {"tokenTypes",
       {"namespace", "type",      "class",   "enum",       "interface", "struct",
        "typeParameter", "parameter", "variable", "property", "enumMember", "event",
        "function",      "method",    "macro",  "keyword",    "modifier",   "comment",
        "string",        "number",    "regexp", "operator",   "decorator"}},
      {"formats", nlohmann::json::array({"relative"})}};
  params["capabilities"]["workspace"]["symbol"]["resolveSupport"] = nlohmann::json::object();
  params["clientInfo"]["name"] = "tgdb";
  params["clientInfo"]["version"] = "0.1.0";

  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "initialize", std::move(params), 15000, &result)) {
    return false;
  }

  load_semantic_legend(result);
  if (result.contains("capabilities") && result["capabilities"].is_object()) {
    const auto& caps = result["capabilities"];
    semantic_tokens_supported_ =
        caps.contains("semanticTokensProvider") && caps["semanticTokensProvider"].is_object();
  }
  transport_.send_notification("initialized", nlohmann::json::object());
  return true;
}

bool LspClient::start(const std::string& workspace_root) {
  stop();
  if (workspace_root.empty()) {
    return false;
  }
  transport_.set_notification_handler([this](const std::string& method,
                                             const nlohmann::json& params) {
    on_lsp_notification(method, params);
  });
  const std::string compile_dir = ensure_compile_commands_for_clangd(workspace_root);
  if (!spawn_clangd(compile_dir)) {
    stop();
    return false;
  }
  workspace_root_ = workspace_root;
  if (!initialize(workspace_root)) {
    stop();
    return false;
  }
  ready_ = true;
  return true;
}

void LspClient::stop() {
  ready_ = false;

  if (child_pid_ > 0) {
    transport_.send_notification("exit", nlohmann::json::object());
  }
  transport_.stop();

  if (stdin_write_fd_ >= 0) {
    ::close(stdin_write_fd_);
    stdin_write_fd_ = -1;
  }
  if (stdout_read_fd_ >= 0) {
    ::close(stdout_read_fd_);
    stdout_read_fd_ = -1;
  }

  if (child_pid_ > 0) {
    int status = 0;
    for (int i = 0; i < 20; ++i) {
      const pid_t result = waitpid(child_pid_, &status, WNOHANG);
      if (result == child_pid_ || result < 0) {
        break;
      }
      usleep(100000);
    }
    if (waitpid(child_pid_, &status, WNOHANG) == 0) {
      kill(child_pid_, SIGTERM);
      waitpid(child_pid_, &status, 0);
    }
    child_pid_ = -1;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  documents_.clear();
  symbol_cache_.clear();
  semantic_token_cache_.clear();
  semantic_token_attempts_.clear();
  diagnostics_.clear();
  diagnostics_revision_.store(0, std::memory_order_release);
  semantic_token_types_.clear();
  semantic_tokens_supported_ = false;
  workspace_root_.clear();
}

int64_t LspClient::steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it != documents_.end()) {
      if (it->second.text == text) {
        return;
      }
      it->second.text = text;
      it->second.version += 1;
      it->second.generation += 1;
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
    transport_.send_notification("textDocument/didOpen", std::move(params));
  } else if (notify_change) {
    nlohmann::json params = {
        {"textDocument", {{"uri", doc.uri}, {"version", doc.version}}},
        {"contentChanges", nlohmann::json::array({{{"text", doc.text}}})}};
    transport_.send_notification("textDocument/didChange", std::move(params));
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(key);
    if (it == documents_.end()) {
      open_new = true;
    } else if (it->second.text == text) {
      return;
    } else {
      it->second.text = text;
      it->second.version += 1;
      it->second.generation += 1;
      doc = it->second;
    }
  }
  if (open_new) {
    did_open(absolute_path, text);
    return;
  }

  nlohmann::json params = {
      {"textDocument", {{"uri", doc.uri}, {"version", doc.version}}},
      {"contentChanges", nlohmann::json::array({{{"text", doc.text}}})}};
  transport_.send_notification("textDocument/didChange", std::move(params));
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
  transport_.send_notification("textDocument/didClose", std::move(params));
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
    if (node.contains("location") && node["location"].contains("range") &&
        node["location"]["range"].contains("start") &&
        node["location"]["range"]["start"].contains("line")) {
      info.line = node["location"]["range"]["start"]["line"].get<int>() + 1;
    } else if (node.contains("range") && node["range"].contains("start") &&
               node["range"]["start"].contains("line")) {
      info.line = node["range"]["start"]["line"].get<int>() + 1;
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
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/documentSymbol", std::move(params), 10000,
                               &result)) {
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
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "workspace/symbol", std::move(params), 10000, &result)) {
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

  if (item.contains("insertTextFormat") && item["insertTextFormat"].is_number_integer()) {
    const int format = item["insertTextFormat"].get<int>();
    out.insert_format =
        format == 2 ? InsertTextFormat::kSnippet : InsertTextFormat::kPlain;
  }

  if (item.contains("insertText") && item["insertText"].is_string()) {
    out.insert_text = item["insertText"].get<std::string>();
  } else if (item.contains("textEdit") && item["textEdit"].is_object()) {
    const auto& edit = item["textEdit"];
    if (edit.contains("newText") && edit["newText"].is_string()) {
      out.insert_text = edit["newText"].get<std::string>();
    }
    if (edit.contains("range") && edit["range"].is_object()) {
      const auto& range = edit["range"];
      if (range.contains("start") && range.contains("end")) {
        out.has_replace_range = true;
        out.replace_line = range["start"]["line"].get<int>();
        out.replace_start = range["start"]["character"].get<int>();
        out.replace_end = range["end"]["character"].get<int>();
      }
    }
  }

  if (out.insert_text.empty()) {
    out.insert_text = out.label;
  }

  return out;
}

std::vector<CompletionItem> LspClient::completions_at(const std::string& absolute_path,
                                                        const std::string& text, int line,
                                                        int character) {
  if (!ready_.load() || absolute_path.empty() ||
      !is_lsp_trackable_path(absolute_path, text)) {
    return {};
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return {};
  }

  if (!text.empty()) {
    did_change(key, text);
  }

  const std::string uri = path_to_uri(key);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"position", {{"line", line}, {"character", character}}},
                           {"context", {{"triggerKind", 1}}}};

  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/completion", std::move(params), 10000,
                               &result)) {
    return {};
  }

  nlohmann::json items;
  if (result.is_array()) {
    items = result;
  } else if (result.is_object() && result.contains("items") && result["items"].is_array()) {
    items = result["items"];
  } else {
    return {};
  }

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
                           {"position", {{"line", line}, {"character", character}}}};

  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, method, std::move(params), 10000, &result)) {
    return loc;
  }
  return parse_location_result(result);
}

SourceLocation LspClient::goto_definition(const std::string& absolute_path,
                                          const std::string& text, int line,
                                          int character) {
  return request_location("textDocument/definition", absolute_path, text, line, character);
}

SourceLocation LspClient::goto_declaration(const std::string& absolute_path,
                                           const std::string& text, int line,
                                           int character) {
  return request_location("textDocument/declaration", absolute_path, text, line, character);
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
    did_change(absolute_path, text);
  }

  const std::string uri = path_to_uri(absolute_path);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"position", {{"line", line}, {"character", character}}}};

  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/hover", std::move(params), 5000, &result)) {
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
    did_open(absolute_path, text);
  }

  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty()) {
    return std::nullopt;
  }

  const std::string uri = path_to_uri(key);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}},
                           {"options", {{"tabSize", 4}, {"insertSpaces", true}}}};

  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/formatting", std::move(params), 30000,
                               &result)) {
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
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/rename", std::move(params), 30000, &result)) {
    return {};
  }

  if (result.is_null()) {
    return {};
  }
  return parse_workspace_edit(result);
}

namespace {

CallHierarchyItem parse_call_hierarchy_item(const nlohmann::json& item) {
  CallHierarchyItem out;
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
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/prepareCallHierarchy", std::move(params), 15000,
                               &result)) {
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
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "callHierarchy/incomingCalls", std::move(params), 15000,
                               &result)) {
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
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "callHierarchy/outgoingCalls", std::move(params), 15000,
                               &result)) {
    return {};
  }
  return parse_call_hierarchy_relations(result, "to");
}

SemanticTokenDocument LspClient::decode_semantic_tokens(
    const nlohmann::json& result, const std::vector<std::string>& token_types) {
  SemanticTokenDocument doc;
  doc.token_types = token_types;
  if (!result.is_object() || !result.contains("data") || !result["data"].is_array()) {
    return doc;
  }

  const auto& data = result["data"];
  if (data.empty() || data.size() % 5 != 0) {
    return doc;
  }

  int line = 0;
  int start = 0;
  int max_line = 0;

  for (std::size_t i = 0; i < data.size(); i += 5) {
    if (!data[i].is_number_integer() || !data[i + 1].is_number_integer() ||
        !data[i + 2].is_number_integer() || !data[i + 3].is_number_integer() ||
        !data[i + 4].is_number_integer()) {
      continue;
    }

    const int delta_line = data[i].get<int>();
    const int delta_start = data[i + 1].get<int>();
    line += delta_line;
    if (delta_line == 0) {
      start += delta_start;
    } else {
      start = delta_start;
    }
    const int length = data[i + 2].get<int>();
    if (length <= 0) {
      continue;
    }

    SemanticTokenSpan span;
    span.start_col = start;
    span.length = length;
    span.type = data[i + 3].get<int>();
    span.modifiers = data[i + 4].get<int>();

    if (line >= max_line) {
      doc.lines.resize(static_cast<std::size_t>(line + 1));
      max_line = line + 1;
    }
    doc.lines[static_cast<std::size_t>(line)].push_back(span);
  }

  doc.ready = !doc.lines.empty();
  return doc;
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
  }

  nlohmann::json params = {{"textDocument", {{"uri", uri}}}};
  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/semanticTokens/full", std::move(params), 5000,
                               &result)) {
    return false;
  }

  SemanticTokenDocument decoded = decode_semantic_tokens(result, token_types);
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
    constexpr int64_t kRetryIntervalMs = 200;
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
  if (method != "textDocument/publishDiagnostics") {
    return;
  }
  DocumentDiagnostics doc = parse_publish_diagnostics(params);
  if (doc.path.empty() || !is_lsp_trackable_path(doc.path)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (doc.items.empty()) {
    diagnostics_.erase(doc.path);
  } else {
    diagnostics_[doc.path] = std::move(doc);
  }
  diagnostics_revision_.fetch_add(1, std::memory_order_release);
}

DocumentDiagnostics LspClient::diagnostics_for_file(const std::string& absolute_path) {
  const std::string key = normalize_lsp_path(absolute_path);
  if (key.empty() || !is_lsp_trackable_path(key)) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = diagnostics_.find(key);
  if (it == diagnostics_.end()) {
    return {};
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
