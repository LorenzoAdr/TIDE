#include "ai/llama_backend.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string shell_quote(const std::string& value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::size_t find_action_object_start(const std::string& raw, std::size_t from) {
  std::size_t search = from;
  while (search < raw.size()) {
    const auto brace = raw.find('{', search);
    if (brace == std::string::npos) {
      return std::string::npos;
    }
    std::size_t i = brace + 1;
    while (i < raw.size() &&
           (raw[i] == ' ' || raw[i] == '\n' || raw[i] == '\r' || raw[i] == '\t')) {
      ++i;
    }
    if (i + 8 <= raw.size() && raw.compare(i, 8, "\"action\"") == 0) {
      return brace;
    }
    search = brace + 1;
  }
  return std::string::npos;
}

std::string scan_balanced_object(const std::string& raw, std::size_t start) {
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  for (std::size_t i = start; i < raw.size(); ++i) {
    const char c = raw[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) {
        return raw.substr(start, i - start + 1);
      }
    }
  }
  return {};
}

// Pull the assistant JSON (or last coherent block) out of noisy llama-cli stdout.
std::string extract_model_text(const std::string& raw) {
  const auto aider_mark = raw.find("<<<<<<< SEARCH");
  if (aider_mark != std::string::npos) {
    std::size_t begin = aider_mark;
    std::size_t line_start = aider_mark;
    while (line_start > 0 && raw[line_start - 1] != '\n') {
      --line_start;
    }
    if (line_start > 0) {
      std::size_t prev_end = line_start - 1;
      std::size_t prev_start = prev_end;
      while (prev_start > 0 && raw[prev_start - 1] != '\n') {
        --prev_start;
      }
      const std::string prev = raw.substr(prev_start, prev_end - prev_start);
      if (prev.find('/') != std::string::npos || prev.find('.') != std::string::npos) {
        begin = prev_start;
      }
    }
    auto end = raw.rfind(">>>>>>> REPLACE");
    if (end == std::string::npos) {
      end = raw.size();
    } else {
      end += std::string(">>>>>>> REPLACE").size();
    }
    return raw.substr(begin, end - begin);
  }

  // Prefer the LAST JSON object that looks like an action (models echo the prompt).
  std::size_t search = 0;
  std::string best;
  while (search < raw.size()) {
    const auto start = find_action_object_start(raw, search);
    if (start == std::string::npos) {
      break;
    }
    const std::string obj = scan_balanced_object(raw, start);
    if (!obj.empty()) {
      best = obj;
    }
    search = start + 1;
  }
  if (!best.empty()) {
    return best;
  }

  const auto start = raw.find('{');
  if (start != std::string::npos) {
    const std::string obj = scan_balanced_object(raw, start);
    if (!obj.empty()) {
      return obj;
    }
  }

  // Fallback: text after the last chat prompt marker.
  const std::string markers[] = {
      "\n> ",
      "assistant\n",
      "<|im_start|>assistant\n",
  };
  std::size_t cut = std::string::npos;
  for (const auto& m : markers) {
    const auto pos = raw.rfind(m);
    if (pos != std::string::npos) {
      const std::size_t after = pos + m.size();
      if (cut == std::string::npos || after > cut) {
        cut = after;
      }
    }
  }
  std::string text = cut == std::string::npos ? raw : raw.substr(cut);
  // Drop trailing interactive prompt / stats.
  const auto stats = text.find("\n[ Prompt:");
  if (stats != std::string::npos) {
    text = text.substr(0, stats);
  }
  const auto exiting = text.find("\nExiting...");
  if (exiting != std::string::npos) {
    text = text.substr(0, exiting);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' ||
                           text.back() == '>')) {
    text.pop_back();
  }
  return text;
}

std::string first_nonempty_lines(const std::string& text, int max_lines) {
  std::istringstream iss(text);
  std::ostringstream out;
  std::string line;
  int n = 0;
  while (std::getline(iss, line)) {
    if (line.empty()) {
      continue;
    }
    out << line << '\n';
    if (++n >= max_lines) {
      break;
    }
  }
  return out.str();
}

constexpr int kL2HttpTimeoutMs = 600000;
constexpr int kL2HealthTimeoutMs = 180000;
constexpr int kL2RecvSliceMs = 1000;

bool ggml_gpu_backend_present(const std::string& lib_dir) {
  static const char* kNames[] = {"libggml-cuda.so", "libggml-hip.so", "libggml-vulkan.so",
                                 "libggml-metal.so", "libggml-cuda.so.0", "libggml-vulkan.so.0"};
  for (const char* name : kNames) {
    if (fs::exists(fs::path(lib_dir) / name)) {
      return true;
    }
  }
  return false;
}

int resolve_n_gpu_layers(const std::string& lib_dir, int configured) {
  if (configured >= 0) {
    return configured;
  }
  return ggml_gpu_backend_present(lib_dir) ? 99 : 0;
}

int resolve_n_threads(int configured) {
  if (configured > 0) {
    return configured;
  }
  const unsigned hc = std::thread::hardware_concurrency();
  return hc > 0 ? static_cast<int>(hc) : 4;
}

std::string read_text_file(const fs::path& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

bool write_text_file(const fs::path& path, const std::string& body) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

std::string make_l2_server_stamp(const std::string& model_path, int n_ctx, int ngl, int threads,
                                 int port) {
  std::ostringstream oss;
  oss << "v1|" << model_path << "|c=" << n_ctx << "|ngl=" << ngl << "|t=" << threads
      << "|p=" << port << "|np=1";
  return oss.str();
}

bool set_sock_timeouts(int fd, int timeout_ms) {
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    return false;
  }
  return ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

bool send_all(int fd, const char* data, std::size_t n) {
  std::size_t off = 0;
  while (off < n) {
    const ssize_t w = ::send(fd, data + off, n - off, MSG_NOSIGNAL);
    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (w == 0) {
      return false;
    }
    off += static_cast<std::size_t>(w);
  }
  return true;
}

bool recv_some(int fd, char* buf, std::size_t cap, std::size_t* got) {
  while (true) {
    const ssize_t r = ::recv(fd, buf, cap, 0);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (got) {
          *got = 0;
        }
        return true;  // timeout slice; caller checks cancel
      }
      return false;
    }
    if (r == 0) {
      return false;  // peer closed
    }
    if (got) {
      *got = static_cast<std::size_t>(r);
    }
    return true;
  }
}

void kill_listeners_on_port(int port) {
  if (port <= 0 || port > 65535) {
    return;
  }
#if defined(__linux__)
  const std::string fuser_cmd = "fuser -k " + std::to_string(port) + "/tcp >/dev/null 2>&1";
  const int fuser_rc = std::system(fuser_cmd.c_str());
  (void)fuser_rc;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
#else
  (void)port;
#endif
}

std::string finish_extract(std::string text) {
  while (!text.empty() && (text.back() == '\0' || text.back() == ' ' || text.back() == '\n' ||
                           text.back() == '\r')) {
    text.pop_back();
  }
  const std::string end_tok = "<|im_end|>";
  const auto pos = text.find(end_tok);
  if (pos != std::string::npos) {
    text = text.substr(0, pos);
  }
  return text;
}

void shrink_prompt_for_ctx(const LlamaCompletionRequest& req, int n_ctx, int* n_predict,
                           std::string* user_text) {
  const int reserve = std::max(512, std::min(*n_predict, 1024));
  const std::size_t max_prompt_chars =
      static_cast<std::size_t>(std::max(2048, (n_ctx - reserve) * 3));
  if (req.system_prompt.size() + user_text->size() > max_prompt_chars) {
    const std::size_t cap = max_prompt_chars > req.system_prompt.size() + 256
                                ? max_prompt_chars - req.system_prompt.size()
                                : 512;
    if (user_text->size() > cap) {
      user_text->resize(cap);
      *user_text += "\n…[user prompt recortado para dejar sitio a n_predict]…\n";
    }
  }
  const int prompt_tok =
      static_cast<int>((req.system_prompt.size() + user_text->size()) / 3 + 32);
  if (prompt_tok + *n_predict + 8 > n_ctx) {
    *n_predict = std::max(256, n_ctx - prompt_tok - 8);
  }
}

}  // namespace

void attach_thinking_json(nlohmann::json& body, const std::optional<bool>& enable_thinking,
                          int reasoning_budget) {
  if (enable_thinking.has_value()) {
    body["chat_template_kwargs"] = nlohmann::json{{"enable_thinking", *enable_thinking}};
  }
  if (reasoning_budget >= 0) {
    body["thinking_budget_tokens"] = reasoning_budget;
    body["reasoning_budget_tokens"] = reasoning_budget;
  }
}

nlohmann::json build_chat_completions_body(const LlamaCompletionRequest& req,
                                           const std::string& model, const std::string& user_text,
                                           bool cache_prompt) {
  nlohmann::json body = {
      {"model", model},
      {"temperature", req.temperature},
      {"max_tokens", req.max_tokens > 0 ? req.max_tokens : 512},
      {"messages",
       nlohmann::json::array(
           {{{"role", "system"}, {"content", req.system_prompt}},
            {{"role", "user"}, {"content", user_text}}})},
  };
  if (cache_prompt) {
    body["cache_prompt"] = true;
  }
  attach_thinking_json(body, req.enable_thinking, req.reasoning_budget);
  return body;
}

bool parse_llama_chat_completion(const std::string& body, std::string* content, std::string* error) {
  if (content == nullptr) {
    if (error) {
      *error = "content nullptr";
    }
    return false;
  }
  try {
    const auto j = nlohmann::json::parse(body);
    if (j.contains("error")) {
      if (error) {
        *error = j["error"].is_string() ? j["error"].get<std::string>() : j["error"].dump();
      }
      return false;
    }
    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
      if (error) {
        *error = "chat completions sin choices";
      }
      return false;
    }
    const auto& c0 = j["choices"][0];
    if (c0.contains("message") && c0["message"].contains("content") &&
        c0["message"]["content"].is_string()) {
      *content = c0["message"]["content"].get<std::string>();
      return true;
    }
    if (c0.contains("text") && c0["text"].is_string()) {
      *content = c0["text"].get<std::string>();
      return true;
    }
    if (error) {
      *error = "chat completions sin content";
    }
    return false;
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("parse chat completions: ") + ex.what();
    }
    return false;
  }
}

LlamaBackend::LlamaBackend() : store_(ModelStore{}) {}

LlamaBackend::~LlamaBackend() {
  stop_completion_server();
}

void LlamaBackend::set_model_path(std::string path) {
  model_path_ = std::move(path);
}

void LlamaBackend::set_cli_path(std::string path) {
  cli_path_ = std::move(path);
  lib_dir_ = store_.library_dir_for_cli(cli_path_);
}

bool LlamaBackend::ready() const {
  if (model_path_.empty() || ::access(model_path_.c_str(), R_OK) != 0) {
    return false;
  }
  if (server_ready_.load()) {
    return true;
  }
  return store_.cli_runnable(cli_path_);
}

std::string LlamaBackend::status_text() const {
  std::ostringstream out;
  out << "cli=" << (cli_path_.empty() ? "(none)" : cli_path_) << '\n';
  out << "lib_dir=" << (lib_dir_.empty() ? "(none)" : lib_dir_) << '\n';
  out << "model=" << (model_path_.empty() ? "(none)" : model_path_) << '\n';
  out << "ready=" << (ready() ? "yes" : "no") << '\n';
  out << "cache=" << store_.cache_dir() << '\n';
  out << "l2_server=" << (server_ready_.load() ? "ready" : "off");
  out << " port=" << server_port_;
  if (server_pid_ > 0) {
    out << " pid=" << server_pid_;
  }
  out << '\n';
  return out.str();
}

bool LlamaBackend::ensure_ready(const AiSettings& settings, const ProgressFn& on_progress,
                                std::string* error) {
  ModelStore store(settings.models_cache_dir.empty() ? ModelStore::default_cache_dir()
                                                     : settings.models_cache_dir);
  store_ = store;

  if (!settings.level1.cli_path.empty()) {
    cli_path_ = settings.level1.cli_path;
  } else {
    cli_path_ = store_.resolve_llama_cli();
    if (cli_path_.empty() || !store_.cli_runnable(cli_path_)) {
      cli_path_ =
          store_.ensure_llama_cli(settings.level1.auto_download, on_progress, error);
    }
  }
  lib_dir_ = store_.library_dir_for_cli(cli_path_);
  if (!store_.cli_runnable(cli_path_)) {
    if (error && error->empty()) {
      *error =
          "llama-cli no ejecutable (¿faltan .so del bundle? prueba /model download_runtime)";
    }
    return false;
  }

  if (!settings.level1.model_path.empty()) {
    model_path_ = settings.level1.model_path;
  } else {
    const AiModelInfo info = resolve_l1_model(settings.level1);
    if (!settings.level1.model_id.empty() && !find_l1_model(settings.level1.model_id)) {
      model_path_ = store_.model_path_for_id(settings.level1.model_id);
      if (::access(model_path_.c_str(), R_OK) != 0) {
        if (error) {
          *error = "modelo custom ausente: " + model_path_;
        }
        return false;
      }
    } else {
      model_path_ =
          store_.ensure_model(info, settings.level1.auto_download, on_progress, error);
    }
  }
  if (model_path_.empty() || ::access(model_path_.c_str(), R_OK) != 0) {
    if (error && error->empty()) {
      *error = "modelo L1 no disponible";
    }
    return false;
  }
  return true;
}

void LlamaBackend::http_close_unlocked() const {
  if (http_fd_ >= 0) {
    ::close(http_fd_);
    http_fd_ = -1;
  }
}

bool LlamaBackend::http_ensure_connected_unlocked(std::string* error) const {
  if (http_fd_ >= 0) {
    return true;
  }
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error) {
      *error = "socket() L2 llama-server falló";
    }
    return false;
  }
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  set_sock_timeouts(fd, kL2RecvSliceMs);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(server_port_));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    if (error) {
      *error = "connect 127.0.0.1:" + std::to_string(server_port_) + " falló";
    }
    return false;
  }
  http_fd_ = fd;
  return true;
}

bool LlamaBackend::http_exchange_unlocked(const std::string& method, const std::string& path,
                                          const std::string& body, int timeout_ms,
                                          std::atomic<bool>* cancel, std::string* response,
                                          std::string* error) const {
  if (response == nullptr) {
    if (error) {
      *error = "response nullptr";
    }
    return false;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  std::ostringstream req;
  req << method << ' ' << path << " HTTP/1.1\r\n";
  req << "Host: 127.0.0.1:" << server_port_ << "\r\n";
  req << "Connection: keep-alive\r\n";
  if (method == "POST") {
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
  }
  req << "\r\n";
  if (method == "POST") {
    req << body;
  }
  const std::string raw = req.str();
  if (!send_all(http_fd_, raw.data(), raw.size())) {
    http_close_unlocked();
    if (error) {
      *error = "send HTTP L2 falló";
    }
    return false;
  }

  auto timed_recv = [&](char* tmp, std::size_t cap, std::size_t* got) -> bool {
    while (true) {
      if (cancel != nullptr && cancel->load()) {
        if (error) {
          *error = "cancelado";
        }
        return false;
      }
      if (std::chrono::steady_clock::now() > deadline) {
        if (error) {
          *error = "timeout HTTP L2";
        }
        return false;
      }
      if (!recv_some(http_fd_, tmp, cap, got)) {
        http_close_unlocked();
        if (error) {
          *error = "recv HTTP L2 falló";
        }
        return false;
      }
      if (*got > 0) {
        return true;
      }
    }
  };

  std::string buf;
  buf.reserve(8192);
  char tmp[4096];
  std::size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    std::size_t got = 0;
    if (!timed_recv(tmp, sizeof(tmp), &got)) {
      return false;
    }
    buf.append(tmp, got);
    header_end = buf.find("\r\n\r\n");
  }

  const std::string headers = buf.substr(0, header_end);
  std::string resp_body = buf.substr(header_end + 4);
  int status = 0;
  {
    const auto sp = headers.find(' ');
    if (sp != std::string::npos) {
      status = std::atoi(headers.c_str() + sp + 1);
    }
  }
  std::size_t content_length = static_cast<std::size_t>(-1);
  bool conn_close = false;
  {
    std::istringstream hs(headers);
    std::string line;
    std::getline(hs, line);
    while (std::getline(hs, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      std::string key = line.substr(0, colon);
      std::string val = line.substr(colon + 1);
      while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
        val.erase(val.begin());
      }
      for (char& c : key) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      for (char& c : val) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (key == "content-length") {
        content_length = static_cast<std::size_t>(std::strtoull(val.c_str(), nullptr, 10));
      } else if (key == "connection" && val.find("close") != std::string::npos) {
        conn_close = true;
      }
    }
  }

  if (content_length != static_cast<std::size_t>(-1)) {
    while (resp_body.size() < content_length) {
      std::size_t got = 0;
      if (!timed_recv(tmp, sizeof(tmp), &got)) {
        return false;
      }
      resp_body.append(tmp, got);
    }
    if (resp_body.size() > content_length) {
      resp_body.resize(content_length);
    }
  }

  if (conn_close) {
    http_close_unlocked();
  }
  if (status < 200 || status >= 300) {
    if (error) {
      std::string detail = resp_body;
      if (detail.size() > 240) {
        detail.resize(240);
        detail += "…";
      }
      for (char& ch : detail) {
        if (ch == '\n' || ch == '\r') {
          ch = ' ';
        }
      }
      *error = "HTTP L2 status " + std::to_string(status) +
               (detail.empty() ? std::string() : (": " + detail));
    }
    return false;
  }
  *response = std::move(resp_body);
  return true;
}

bool LlamaBackend::health_ok() const {
  std::lock_guard lock(http_mu_);
  auto probe = [&](std::string* err) -> bool {
    if (!http_ensure_connected_unlocked(err)) {
      return false;
    }
    std::string body;
    if (!http_exchange_unlocked("GET", "/health", {}, 2000, nullptr, &body, err)) {
      return false;
    }
    return !body.empty();
  };
  std::string err;
  if (probe(&err)) {
    return true;
  }
  http_close_unlocked();
  return probe(&err);
}

bool LlamaBackend::wait_until_healthy(int timeout_ms, std::string* error) const {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (health_ok()) {
      return true;
    }
    if (server_pid_ > 0) {
      int status = 0;
      const pid_t r = ::waitpid(server_pid_, &status, WNOHANG);
      if (r == server_pid_) {
        if (error) {
          *error = "llama-server L2 terminó antes de quedar healthy";
        }
        return false;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (error) {
    *error = "timeout esperando /health L2 en puerto " + std::to_string(server_port_);
  }
  return false;
}

void LlamaBackend::stop_owned_unlocked() {
  {
    std::lock_guard http_lock(http_mu_);
    http_close_unlocked();
  }
  server_ready_.store(false);
  if (server_pid_ > 0) {
    errno = 0;
    if (::kill(server_pid_, SIGTERM) != 0) {
      // A sandbox can hide the detached child's PID (EPERM/ESRCH) while keeping
      // its HTTP listener alive. Never wait on a process we could not signal;
      // the next backend instance can adopt it through health+stamp.
      server_pid_ = -1;
      return;
    }
    int status = 0;
    for (int i = 0; i < 20; ++i) {
      if (::waitpid(server_pid_, &status, WNOHANG) == server_pid_) {
        server_pid_ = -1;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (::waitpid(server_pid_, &status, WNOHANG) == 0) {
      if (::kill(server_pid_, SIGKILL) == 0) {
        ::waitpid(server_pid_, &status, 0);
      }
    }
    server_pid_ = -1;
  }
}

void LlamaBackend::stop_listener_on_port() {
  kill_listeners_on_port(server_port_);
  {
    std::lock_guard http_lock(http_mu_);
    http_close_unlocked();
  }
}

void LlamaBackend::stop_completion_server() {
  std::lock_guard lock(mu_);
  stop_owned_unlocked();
}

bool LlamaBackend::start_completion_server(const std::string& server_bin, const std::string& lib_dir,
                                           const AiLevel2Settings& l2, const ProgressFn& on_progress,
                                           std::string* error) {
  const int ngl = resolve_n_gpu_layers(lib_dir, l2.n_gpu_layers);
  const int threads = resolve_n_threads(l2.n_threads);
  const int n_ctx = l2.n_ctx > 0 ? l2.n_ctx : 8192;
  server_n_ctx_ = n_ctx;
  server_stamp_ = make_l2_server_stamp(model_path_, n_ctx, ngl, threads, server_port_);

  const fs::path stamp_path = fs::path(store_.cache_dir()) / "l2" / "llama-server.stamp";
  const std::string old_stamp = read_text_file(stamp_path);
  const bool health = health_ok();
  if (health && old_stamp == server_stamp_) {
    server_ready_.store(true);
    if (on_progress) {
      on_progress("L2 local: reutilizando llama-server en :" + std::to_string(server_port_) +
                  " (ngl=" + std::to_string(ngl) + " t=" + std::to_string(threads) + " c=" +
                  std::to_string(n_ctx) + ")");
    }
    return true;
  }

  if (health || !old_stamp.empty()) {
    if (on_progress) {
      on_progress("L2 local: reiniciando llama-server con flags nuevos…");
    }
    stop_owned_unlocked();
    for (int attempt = 0; attempt < 50 && health_ok(); ++attempt) {
      stop_listener_on_port();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (health_ok()) {
      if (error) {
        *error = "puerto " + std::to_string(server_port_) +
                 " ocupado; ciérralo o cambia ai.level2.server_port";
      }
      return false;
    }
  }

  std::error_code ec;
  const fs::path log_path = fs::path(store_.cache_dir()) / "l2" / "llama-server.log";
  fs::create_directories(log_path.parent_path(), ec);

  const std::string port_s = std::to_string(server_port_);
  const std::string ctx_s = std::to_string(n_ctx);
  const std::string ngl_s = std::to_string(ngl);
  const std::string threads_s = std::to_string(threads);

  const pid_t pid = ::fork();
  if (pid < 0) {
    if (error) {
      *error = "fork llama-server L2 falló";
    }
    return false;
  }
  if (pid == 0) {
    if (!lib_dir.empty()) {
      const char* prev = std::getenv("LD_LIBRARY_PATH");
      std::string value = lib_dir;
      if (prev != nullptr && prev[0] != '\0') {
        value.push_back(':');
        value += prev;
      }
      ::setenv("LD_LIBRARY_PATH", value.c_str(), 1);
    }
    ::setsid();
    ::close(STDIN_FILENO);
    const int logfd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd >= 0) {
      ::dup2(logfd, STDOUT_FILENO);
      ::dup2(logfd, STDERR_FILENO);
      if (logfd > STDERR_FILENO) {
        ::close(logfd);
      }
    } else {
      ::close(STDOUT_FILENO);
      ::close(STDERR_FILENO);
    }
    // -np 1: full n_ctx in one slot (unlike embeddings, which split KV).
    std::vector<std::string> args = {
        server_bin, "-m",     model_path_, "--host", "127.0.0.1", "--port", port_s,
        "-c",       ctx_s,    "-ngl",      ngl_s,    "-t",        threads_s, "-tb",
        threads_s,  "-np",    "1",         "--log-disable"};
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) {
      argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    ::execv(server_bin.c_str(), argv.data());
    _exit(127);
  }

  server_pid_ = pid;
  if (on_progress) {
    on_progress("L2 local: arrancando llama-server pid=" + std::to_string(pid) + " :" + port_s +
                " ngl=" + ngl_s + " t=" + threads_s + " c=" + ctx_s);
  }
  if (!wait_until_healthy(kL2HealthTimeoutMs, error)) {
    ::kill(server_pid_, SIGTERM);
    int status = 0;
    ::waitpid(server_pid_, &status, 0);
    server_pid_ = -1;
    if (error && error->find("timeout") != std::string::npos) {
      *error += " (ver " + log_path.string() + ")";
    }
    return false;
  }
  // Sandboxed callers may get EPERM even for the child they just spawned.
  // waitpid above is the ownership/liveness check; EPERM is not evidence that
  // another process owns the healthy listener.
  errno = 0;
  if (::kill(server_pid_, 0) != 0 && errno != EPERM) {
    // Some sandbox PID namespaces report ESRCH for the detached child even
    // though its listener is healthy. The port was empty before fork and the
    // child reached the expected health endpoint, so health is authoritative.
    if (!health_ok()) {
      server_pid_ = -1;
      if (error) {
        *error = "llama-server L2 terminó antes de quedar listo";
      }
      return false;
    }
  }
  server_ready_.store(true);
  write_text_file(stamp_path, server_stamp_);
  return true;
}

bool LlamaBackend::ensure_completion_server(const AiSettings& settings, const ProgressFn& on_progress,
                                            std::string* error) {
  std::lock_guard lock(mu_);
  store_ = ModelStore(settings.models_cache_dir.empty() ? ModelStore::default_cache_dir()
                                                        : settings.models_cache_dir);
  server_port_ = settings.level2.server_port > 0 ? settings.level2.server_port : 18766;
  if (model_path_.empty() || ::access(model_path_.c_str(), R_OK) != 0) {
    if (error) {
      *error = "L2 local: modelo ausente para llama-server";
    }
    return false;
  }

  std::string server = store_.resolve_llama_server();
  if (server.empty() || !store_.server_runnable(server)) {
    const bool auto_dl = settings.level2.auto_download || settings.level1.auto_download;
    const std::string cli = store_.ensure_llama_cli(auto_dl, on_progress, error);
    (void)cli;
    server = store_.resolve_llama_server();
  }
  if (server.empty() || !store_.server_runnable(server)) {
    if (error && error->empty()) {
      *error = "L2 local: llama-server no ejecutable (instala ai-runtime)";
    }
    return false;
  }
  server_bin_ = server;
  if (lib_dir_.empty()) {
    lib_dir_ = store_.library_dir_for_cli(server_bin_);
  }
  return start_completion_server(server_bin_, lib_dir_, settings.level2, on_progress, error);
}

LlamaCompletionResult LlamaBackend::complete(const LlamaCompletionRequest& req,
                                             std::atomic<bool>* cancel) const {
  if (!ready()) {
    LlamaCompletionResult result;
    result.error = "backend no listo (cli/model/libs)";
    return result;
  }
  if (server_ready_.load()) {
    return complete_server(req, cancel);
  }
  return complete_cli(req, cancel);
}

LlamaCompletionResult LlamaBackend::complete_server(const LlamaCompletionRequest& req,
                                                    std::atomic<bool>* cancel) const {
  LlamaCompletionResult result;
  if (cancel != nullptr && cancel->load()) {
    result.error = "cancelado";
    return result;
  }

  std::ostringstream user;
  if (!req.history_text.empty()) {
    user << "Historial previo:\n" << req.history_text << "\n---\n";
  }
  user << req.user_prompt;
  std::string user_text = user.str();
  const int n_ctx = server_n_ctx_ > 0 ? server_n_ctx_ : (req.n_ctx > 0 ? req.n_ctx : 8192);
  int n_predict = req.max_tokens > 0 ? req.max_tokens : 512;
  shrink_prompt_for_ctx(req, n_ctx, &n_predict, &user_text);

  LlamaCompletionRequest body_req = req;
  body_req.max_tokens = n_predict;
  if (body_req.reasoning_budget > 0 && n_predict < body_req.reasoning_budget + 128) {
    body_req.reasoning_budget = std::max(0, n_predict - 128);
  }
  nlohmann::json body = build_chat_completions_body(body_req, "l2", user_text, true);
  if (!req.grammar_file.empty()) {
    const std::string grammar = read_text_file(req.grammar_file);
    if (grammar.empty()) {
      result.error = "L2 grammar vacía: " + req.grammar_file;
      return result;
    }
    body["grammar"] = grammar;
  }

  std::string payload;
  try {
    // Ranked maps / pack excerpts can contain truncated UTF-8; strict dump aborts.
    payload = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  } catch (const std::exception& ex) {
    result.error = std::string("json dump L2: ") + ex.what();
    return result;
  }

  std::string response;
  std::string err;
  {
    std::lock_guard lock(http_mu_);
    auto try_once = [&](std::string* e) -> bool {
      if (!http_ensure_connected_unlocked(e)) {
        return false;
      }
      return http_exchange_unlocked("POST", "/v1/chat/completions", payload, kL2HttpTimeoutMs,
                                    cancel, &response, e);
    };
    if (!try_once(&err)) {
      http_close_unlocked();
      std::string err2;
      if (!try_once(&err2)) {
        result.error = err2.empty() ? err : err2;
        return result;
      }
    }
  }

  std::string content;
  if (!parse_llama_chat_completion(response, &content, &err)) {
    {
      std::ofstream out("/tmp/tuide-llama-last.out", std::ios::binary | std::ios::trunc);
      if (out) {
        out << response;
      }
    }
    result.error = err;
    return result;
  }
  std::string text = finish_extract(extract_model_text(content));
  const auto context_blow = content.find("exceeds the available context") != std::string::npos ||
                            response.find("exceeds the available context") != std::string::npos;
  if (context_blow) {
    const std::string role = req.context_role.empty() ? "L2" : req.context_role;
    const std::string hint =
        req.n_ctx_setting_hint.empty() ? std::string("ai.level2.n_ctx") : req.n_ctx_setting_hint;
    result.error = "contexto " + role + " insuficiente (sube " + hint + "; usado -c " +
                   std::to_string(n_ctx) + ")";
    return result;
  }
  if (text.empty()) {
    result.error = "llama-server L2 no produjo texto";
    return result;
  }
  result.ok = true;
  result.text = std::move(text);
  return result;
}

LlamaCompletionResult LlamaBackend::complete_cli(const LlamaCompletionRequest& req,
                                                 std::atomic<bool>* cancel) const {
  LlamaCompletionResult result;
  if (!ready()) {
    result.error = "backend no listo (cli/model/libs)";
    return result;
  }
  if (cancel != nullptr && cancel->load()) {
    result.error = "cancelado";
    return result;
  }

  // Fold history into a single user turn — modern llama-cli defaults to chat/jinja and
  // --single-turn exits cleanly (unlike raw -f which stays interactive).
  std::ostringstream user;
  if (!req.history_text.empty()) {
    user << "Historial previo:\n" << req.history_text << "\n---\n";
  }
  user << req.user_prompt;

  const fs::path sys_path =
      fs::temp_directory_path() / ("tuide-l1-sys-" + std::to_string(::getpid()) + ".txt");
  const fs::path user_path =
      fs::temp_directory_path() / ("tuide-l1-user-" + std::to_string(::getpid()) + ".txt");
  const fs::path err_path =
      fs::temp_directory_path() / ("tuide-l1-err-" + std::to_string(::getpid()) + ".txt");

  const int n_ctx = req.n_ctx > 0 ? req.n_ctx : 2048;
  int n_predict = req.max_tokens > 0 ? req.max_tokens : 512;
  // Leave room to actually generate: llama-cli silently shrinks -n when prompt
  // fills n_ctx, which yields empty / `"hunks":[` truncated JSON.
  const int reserve = std::max(512, std::min(n_predict, 1024));
  const std::size_t max_prompt_chars =
      static_cast<std::size_t>(std::max(2048, (n_ctx - reserve) * 3));
  std::string user_text = user.str();
  if (req.system_prompt.size() + user_text.size() > max_prompt_chars) {
    const std::size_t cap = max_prompt_chars > req.system_prompt.size() + 256
                                ? max_prompt_chars - req.system_prompt.size()
                                : 512;
    if (user_text.size() > cap) {
      user_text.resize(cap);
      user_text += "\n…[user prompt recortado para dejar sitio a n_predict]…\n";
    }
  }
  const int prompt_tok =
      static_cast<int>((req.system_prompt.size() + user_text.size()) / 3 + 32);
  if (prompt_tok + n_predict + 8 > n_ctx) {
    n_predict = std::max(256, n_ctx - prompt_tok - 8);
  }

  {
    std::ofstream sys_out(sys_path);
    std::ofstream user_out(user_path);
    if (!sys_out || !user_out) {
      result.error = "no se pudo escribir prompts temporales";
      return result;
    }
    sys_out << req.system_prompt;
    user_out << user_text;
  }

  std::ostringstream cmd;
  if (!lib_dir_.empty()) {
    cmd << "LD_LIBRARY_PATH=" << shell_quote(lib_dir_);
    if (const char* prev = std::getenv("LD_LIBRARY_PATH"); prev != nullptr && prev[0] != '\0') {
      cmd << ":" << shell_quote(prev);
    }
    cmd << " ";
  }
  // --single-turn: one response then exit. --simple-io: safer for popen.
  // -f is NOT used for the user prompt; use -p @file via shell substitution avoided —
  // pass -p with quoted content from file using $(cat ...) is fragile; use --file for
  // prompt content through -p by reading into the command is huge. Instead write and use:
  //   --system-prompt-file if available? Check... we have -sys. For long prompts use -f
  // only when combined with --no-conversation. Prefer: -sys "$(cat sys)" -p "$(cat user)"
  // with shell_quote of contents — can be large. Files via:
  //   -sys "$(cat file)" works.
  cmd << shell_quote(cli_path_) << " -m " << shell_quote(model_path_) << " -sys \"$(cat "
      << shell_quote(sys_path.string()) << ")\" -p \"$(cat " << shell_quote(user_path.string())
      << ")\" -n " << n_predict << " -c " << n_ctx << " --temp " << req.temperature
      << " --single-turn --simple-io --no-display-prompt";
  if (!req.grammar_file.empty()) {
    cmd << " --grammar-file " << shell_quote(req.grammar_file);
  }
  cmd << " 2>" << shell_quote(err_path.string());

  FILE* pipe = popen(cmd.str().c_str(), "r");
  if (pipe == nullptr) {
    std::error_code ec;
    fs::remove(sys_path, ec);
    fs::remove(user_path, ec);
    fs::remove(err_path, ec);
    result.error = "popen llama-cli failed";
    return result;
  }

  std::array<char, 4096> buf{};
  std::string raw;
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    if (cancel != nullptr && cancel->load()) {
      break;
    }
    // Cap runaway interactive spam (legacy modes).
    if (raw.size() > 2 * 1024 * 1024) {
      break;
    }
    raw += buf.data();
  }
  const int status = pclose(pipe);

  std::string err_text;
  {
    std::ifstream err_in(err_path);
    if (err_in) {
      std::ostringstream oss;
      oss << err_in.rdbuf();
      err_text = oss.str();
    }
  }

  {
    auto dump = [](const fs::path& path, const std::string& body) {
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out) {
        return;
      }
      constexpr std::size_t kCap = 8000;
      if (body.size() <= kCap) {
        out << body;
      } else {
        out << body.substr(0, kCap / 2) << "\n…[truncated]…\n"
            << body.substr(body.size() - kCap / 2);
      }
    };
    dump("/tmp/tuide-llama-last.out", raw);
    dump("/tmp/tuide-llama-last.err", err_text);
  }

  std::error_code ec;
  fs::remove(sys_path, ec);
  fs::remove(user_path, ec);
  fs::remove(err_path, ec);

  if (cancel != nullptr && cancel->load()) {
    result.error = "cancelado";
    return result;
  }

  std::string text = extract_model_text(raw);
  while (!text.empty() && (text.back() == '\0' || text.back() == ' ' || text.back() == '\n' ||
                           text.back() == '\r')) {
    text.pop_back();
  }
  const std::string end_tok = "<|im_end|>";
  const auto pos = text.find(end_tok);
  if (pos != std::string::npos) {
    text = text.substr(0, pos);
  }

  const auto context_blow =
      raw.find("exceeds the available context") != std::string::npos ||
      err_text.find("exceeds the available context") != std::string::npos ||
      text.find("exceeds the available context") != std::string::npos;
  if (context_blow) {
    const std::string role = req.context_role.empty() ? "L1" : req.context_role;
    const std::string hint =
        req.n_ctx_setting_hint.empty() ? std::string("ai.level1.n_ctx") : req.n_ctx_setting_hint;
    result.error = "contexto " + role + " insuficiente (sube " + hint + "; usado -c " +
                   std::to_string(n_ctx) + ")";
    return result;
  }

  if (text.empty()) {
    std::ostringstream msg;
    msg << "llama-cli no produjo texto";
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      msg << " (exit="
          << (WIFEXITED(status) ? std::to_string(WEXITSTATUS(status)) : std::string("signal"))
          << ")";
    }
    if (!err_text.empty()) {
      msg << "\n" << first_nonempty_lines(err_text, 12);
    } else if (!raw.empty()) {
      msg << "\nstdout:\n" << first_nonempty_lines(raw, 12);
    } else {
      msg << " (sin stdout/stderr; ¿LD_LIBRARY_PATH / OOM?)";
    }
    result.error = msg.str();
    return result;
  }

  result.ok = true;
  result.text = std::move(text);
  return result;
}

}  // namespace tuide
