#include "lsp/lsp_client.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lsp/lsp_uri.hpp"

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

bool LspClient::spawn_clangd(const std::string& workspace_root) {
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    return false;
  }

  const std::string compile_dir = find_compile_commands_dir(workspace_root);
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
    dup2(stdout_pipe[1], STDERR_FILENO);
    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);

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

std::string LspClient::find_compile_commands_dir(
    const std::string& workspace_root) const {
  const fs::path root(workspace_root);
  const fs::path candidates[] = {root / "compile_commands.json",
                                 root / "build" / "compile_commands.json",
                                 root / "cmake-build-debug" / "compile_commands.json",
                                 root / "cmake-build-release" / "compile_commands.json"};
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (fs::is_regular_file(candidate, ec)) {
      return candidate.parent_path().string();
    }
  }
  return {};
}

bool LspClient::initialize(const std::string& workspace_root) {
  nlohmann::json params = {
      {"processId", static_cast<int>(getpid())},
      {"rootUri", path_to_uri(workspace_root)},
      {"capabilities",
       {{"textDocument",
         {{"documentSymbol", {{"hierarchicalDocumentSymbolSupport", true}}},
          {"publishDiagnostics", nlohmann::json::object()}}},
        {"workspace", {{"symbol", {{"resolveSupport", nlohmann::json::object()}}}}}}},
      {"clientInfo", {{"name", "tgdb"}, {"version", "0.1.0"}}};

  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "initialize", std::move(params), 15000, &result)) {
    return false;
  }

  transport_.send_notification("initialized", nlohmann::json::object());
  return true;
}

bool LspClient::start(const std::string& workspace_root) {
  stop();
  if (workspace_root.empty()) {
    return false;
  }
  if (!spawn_clangd(workspace_root)) {
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
  workspace_root_.clear();
}

void LspClient::invalidate_cache(const std::string& absolute_path) {
  symbol_cache_.erase(absolute_path);
}

void LspClient::did_open(const std::string& absolute_path, const std::string& text) {
  if (!ready_.load() || absolute_path.empty()) {
    return;
  }

  DocumentState doc;
  doc.uri = path_to_uri(absolute_path);
  doc.text = text;
  doc.version = 1;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    documents_[absolute_path] = doc;
    invalidate_cache(absolute_path);
  }

  nlohmann::json params = {
      {"textDocument",
       {{"uri", doc.uri},
        {"languageId", language_id_for_path(absolute_path)},
        {"version", doc.version},
        {"text", doc.text}}}};
  transport_.send_notification("textDocument/didOpen", std::move(params));
}

void LspClient::did_change(const std::string& absolute_path, const std::string& text) {
  if (!ready_.load() || absolute_path.empty()) {
    return;
  }

  DocumentState doc;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = documents_.find(absolute_path);
    if (it == documents_.end()) {
      did_open(absolute_path, text);
      return;
    }
    it->second.text = text;
    it->second.version += 1;
    doc = it->second;
    invalidate_cache(absolute_path);
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

  std::string uri;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(absolute_path);
    if (it == documents_.end()) {
      return;
    }
    uri = it->second.uri;
    documents_.erase(it);
    invalidate_cache(absolute_path);
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

std::vector<SymbolInfo> LspClient::document_symbols(const std::string& absolute_path) {
  if (!ready_.load() || absolute_path.empty()) {
    return {};
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cached = symbol_cache_.find(absolute_path);
    if (cached != symbol_cache_.end()) {
      return cached->second;
    }
  }

  std::string text;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = documents_.find(absolute_path);
    if (it != documents_.end()) {
      text = it->second.text;
    }
  }
  if (text.empty()) {
    text = read_file_text(absolute_path);
  }
  if (!text.empty()) {
    did_open(absolute_path, text);
  }

  const std::string uri = path_to_uri(absolute_path);
  nlohmann::json params = {{"textDocument", {{"uri", uri}}}};
  nlohmann::json result;
  const int id = next_request_id_++;
  if (!transport_.send_request(id, "textDocument/documentSymbol", std::move(params), 10000,
                               &result)) {
    return {};
  }

  const std::string relative_file = relative_to_workspace(workspace_root_, absolute_path);
  std::vector<SymbolInfo> symbols;
  flatten_symbols(result, 0, relative_file, &symbols);

  std::lock_guard<std::mutex> lock(mutex_);
  symbol_cache_[absolute_path] = symbols;
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

}  // namespace tgdb
