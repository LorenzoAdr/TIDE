#include "ai/embedding_backend.hpp"

#include "ai/ai_trace.hpp"

#include <array>
#include <algorithm>
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
#include <unordered_set>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide {
namespace {

constexpr int kEmbedBatchChunkDefault = 64;
constexpr int kEmbedHttpTimeoutMs = 120000;

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

std::string make_server_stamp(const std::string& model_path, const AiLevel0EmbedSettings& emb,
                              int ngl, int threads) {
  std::ostringstream oss;
  oss << "v4|" << model_path << "|c=" << emb.n_ctx << "|ngl=" << ngl << "|t=" << threads
      << "|b=" << emb.batch_size << "|ub=" << emb.ubatch_size << "|np=" << emb.n_parallel
      << "|hb=" << emb.http_batch << "|port=" << emb.server_port << "|nou=";
  return oss.str();
}

std::string read_text_file(const fs::path& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool write_text_file(const fs::path& path, const std::string& body) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  out << body;
  return static_cast<bool>(out);
}

// Best-effort: kill processes that have a listening socket on `port` (IPv4).
void kill_listeners_on_port(int port) {
  if (port <= 0 || port > 65535) {
    return;
  }
  std::ifstream tcp("/proc/net/tcp");
  if (!tcp) {
    return;
  }
  std::string line;
  std::getline(tcp, line);  // header
  std::vector<std::string> inodes;
  while (std::getline(tcp, line)) {
    // sl local_address rem_address st ... inode
    std::istringstream iss(line);
    std::string sl, local, rem, st, skip;
    unsigned int txq = 0, rxq = 0, tr = 0, tm = 0;
    unsigned long inode = 0;
    iss >> sl >> local >> rem >> st >> txq >> rxq >> tr >> tm >> skip >> skip >> inode;
    if (st != "0A") {  // TCP_LISTEN
      continue;
    }
    const auto colon = local.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    unsigned int port_hex = 0;
    try {
      port_hex = std::stoul(local.substr(colon + 1), nullptr, 16);
    } catch (...) {
      continue;
    }
    if (static_cast<int>(port_hex) != port) {
      continue;
    }
    if (inode != 0) {
      inodes.push_back(std::to_string(inode));
    }
  }
  if (inodes.empty()) {
    return;
  }

  DIR* proc = ::opendir("/proc");
  if (proc == nullptr) {
    return;
  }
  std::unordered_set<pid_t> pids;
  while (dirent* ent = ::readdir(proc)) {
    if (ent->d_name[0] < '1' || ent->d_name[0] > '9') {
      continue;
    }
    char* end = nullptr;
    const long pid_l = std::strtol(ent->d_name, &end, 10);
    if (end == ent->d_name || *end != '\0' || pid_l <= 1) {
      continue;
    }
    const pid_t pid = static_cast<pid_t>(pid_l);
    const fs::path fd_dir = fs::path("/proc") / ent->d_name / "fd";
    std::error_code ec;
    if (!fs::is_directory(fd_dir, ec)) {
      continue;
    }
    for (const auto& fd : fs::directory_iterator(fd_dir, ec)) {
      if (ec) {
        break;
      }
      std::error_code lec;
      const fs::path target = fs::read_symlink(fd.path(), lec);
      if (lec) {
        continue;
      }
      const std::string t = target.string();
      // socket:[inode]
      if (t.rfind("socket:[", 0) != 0) {
        continue;
      }
      const auto br = t.find(']');
      if (br == std::string::npos) {
        continue;
      }
      const std::string ino = t.substr(8, br - 8);
      for (const auto& want_ino : inodes) {
        if (ino == want_ino) {
          pids.insert(pid);
        }
      }
    }
  }
  ::closedir(proc);

  for (pid_t pid : pids) {
    ::kill(pid, SIGTERM);
  }
  if (!pids.empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for (pid_t pid : pids) {
      if (::kill(pid, 0) == 0) {
        ::kill(pid, SIGKILL);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

#if defined(__linux__)
  // Fallback when /proc inode scan misses the listener (e.g. race or namespace).
  const std::string fuser_cmd =
      "fuser -k " + std::to_string(port) + "/tcp >/dev/null 2>&1";
  (void)std::system(fuser_cmd.c_str());
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
#endif
}

bool parse_embedding_json(const std::string& body, std::vector<float>* out, std::string* error) {
  try {
    const auto j = nlohmann::json::parse(body);
    const nlohmann::json* arr = nullptr;
    if (j.contains("data") && j["data"].is_array() && !j["data"].empty() &&
        j["data"][0].contains("embedding")) {
      arr = &j["data"][0]["embedding"];
    } else if (j.contains("embedding") && j["embedding"].is_array()) {
      arr = &j["embedding"];
    }
    if (arr == nullptr || !arr->is_array() || arr->empty()) {
      if (error) {
        *error = "respuesta embeddings sin vector";
      }
      return false;
    }
    out->clear();
    out->reserve(arr->size());
    for (const auto& v : *arr) {
      out->push_back(v.get<float>());
    }
    return !out->empty();
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("json embeddings: ") + ex.what();
    }
    return false;
  }
}

bool parse_embeddings_batch_json(const std::string& body, std::size_t expect_n,
                                 std::vector<std::vector<float>>* out, std::string* error) {
  try {
    const auto j = nlohmann::json::parse(body);
    if (!j.contains("data") || !j["data"].is_array()) {
      if (error) {
        *error = "respuesta embeddings batch sin data[]";
      }
      return false;
    }
    out->assign(expect_n, {});
    std::size_t filled = 0;
    for (const auto& item : j["data"]) {
      if (!item.contains("embedding") || !item["embedding"].is_array()) {
        continue;
      }
      std::size_t idx = filled;
      if (item.contains("index") && item["index"].is_number_integer()) {
        idx = static_cast<std::size_t>(item["index"].get<int>());
      }
      if (idx >= expect_n) {
        continue;
      }
      std::vector<float> vec;
      vec.reserve(item["embedding"].size());
      for (const auto& v : item["embedding"]) {
        vec.push_back(v.get<float>());
      }
      if (vec.empty()) {
        continue;
      }
      (*out)[idx] = std::move(vec);
      ++filled;
    }
    for (std::size_t i = 0; i < expect_n; ++i) {
      if ((*out)[i].empty()) {
        if (error) {
          *error = "batch embeddings incompleto (faltan vectores)";
        }
        return false;
      }
    }
    return true;
  } catch (const std::exception& ex) {
    if (error) {
      *error = std::string("json embeddings batch: ") + ex.what();
    }
    return false;
  }
}

bool set_sock_timeouts(int fd, int timeout_ms) {
  if (fd < 0) {
    return false;
  }
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    return false;
  }
  if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
    return false;
  }
  return true;
}

bool send_all(int fd, const char* data, std::size_t n) {
  std::size_t sent = 0;
  while (sent < n) {
    const ssize_t r = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (r == 0) {
      return false;
    }
    sent += static_cast<std::size_t>(r);
  }
  return true;
}

bool recv_some(int fd, char* buf, std::size_t cap, std::size_t* got) {
  for (;;) {
    const ssize_t r = ::recv(fd, buf, cap, 0);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    *got = static_cast<std::size_t>(r);
    return true;
  }
}

// Read one HTTP response from an already-written request. Does not close fd.
bool http_recv_response(int fd, std::string* response, std::string* error) {
  if (response == nullptr) {
    if (error) {
      *error = "response nullptr";
    }
    return false;
  }
  std::string buf;
  buf.reserve(8192);
  char tmp[4096];
  std::size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    std::size_t got = 0;
    if (!recv_some(fd, tmp, sizeof(tmp), &got) || got == 0) {
      if (error) {
        *error = "recv headers embeddings falló";
      }
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
      if (key == "content-length") {
        content_length = static_cast<std::size_t>(std::strtoull(val.c_str(), nullptr, 10));
      }
    }
  }
  if (content_length != static_cast<std::size_t>(-1)) {
    while (resp_body.size() < content_length) {
      std::size_t got = 0;
      if (!recv_some(fd, tmp, sizeof(tmp), &got) || got == 0) {
        if (error) {
          *error = "recv body embeddings truncado";
        }
        return false;
      }
      resp_body.append(tmp, got);
    }
    if (resp_body.size() > content_length) {
      resp_body.resize(content_length);
    }
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
      *error = "HTTP embeddings status " + std::to_string(status) +
               (detail.empty() ? std::string() : (": " + detail));
    }
    return false;
  }
  *response = std::move(resp_body);
  return true;
}

}  // namespace

EmbeddingBackend::EmbeddingBackend() : store_(ModelStore{}) {}

EmbeddingBackend::~EmbeddingBackend() {
  stop();
}

bool EmbeddingBackend::ready() const {
  return ready_.load();
}

std::string EmbeddingBackend::status_text() const {
  std::ostringstream oss;
  oss << "L0 intent embed: " << (ready() ? "ready" : "not ready");
  if (!model_path_.empty()) {
    oss << " model=" << model_path_;
  }
  oss << " port=" << port_;
  if (server_pid_ > 0) {
    oss << " pid=" << server_pid_;
  }
  return oss.str();
}

void EmbeddingBackend::http_close_unlocked() const {
  if (http_fd_ >= 0) {
    ::close(http_fd_);
    http_fd_ = -1;
  }
}

bool EmbeddingBackend::http_ensure_connected_unlocked(std::string* error) const {
  if (http_fd_ >= 0) {
    return true;
  }
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error) {
      *error = "socket() embeddings falló";
    }
    return false;
  }
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  set_sock_timeouts(fd, kEmbedHttpTimeoutMs);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    if (error) {
      *error = "connect 127.0.0.1:" + std::to_string(port_) + " falló";
    }
    return false;
  }
  http_fd_ = fd;
  return true;
}

bool EmbeddingBackend::http_exchange_unlocked(const std::string& method, const std::string& path,
                                              const std::string& body, int timeout_ms,
                                              std::string* response, std::string* error) const {
  if (response == nullptr) {
    if (error) {
      *error = "response nullptr";
    }
    return false;
  }
  set_sock_timeouts(http_fd_, timeout_ms);

  std::ostringstream req;
  req << method << ' ' << path << " HTTP/1.1\r\n";
  req << "Host: 127.0.0.1:" << port_ << "\r\n";
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
      *error = "send HTTP embeddings falló";
    }
    return false;
  }

  std::string buf;
  buf.reserve(8192);
  char tmp[4096];
  std::size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    std::size_t got = 0;
    if (!recv_some(http_fd_, tmp, sizeof(tmp), &got) || got == 0) {
      http_close_unlocked();
      if (error) {
        *error = "recv headers embeddings falló";
      }
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
    std::getline(hs, line);  // status
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
      if (!recv_some(http_fd_, tmp, sizeof(tmp), &got) || got == 0) {
        http_close_unlocked();
        if (error) {
          *error = "recv body embeddings truncado";
        }
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
      *error = "HTTP embeddings status " + std::to_string(status) +
               (detail.empty() ? std::string() : (": " + detail));
    }
    return false;
  }
  *response = std::move(resp_body);
  return true;
}

bool EmbeddingBackend::post_embeddings_json(const std::string& payload, std::string* response,
                                            std::string* error) const {
  std::lock_guard lock(http_mu_);
  auto try_once = [&](std::string* err) -> bool {
    if (!http_ensure_connected_unlocked(err)) {
      return false;
    }
    return http_exchange_unlocked("POST", "/v1/embeddings", payload, kEmbedHttpTimeoutMs, response,
                                  err);
  };

  std::string err1;
  if (try_once(&err1)) {
    return true;
  }
  // Reconnect once (stale keep-alive).
  http_close_unlocked();
  std::string err2;
  if (try_once(&err2)) {
    return true;
  }
  if (error) {
    *error = err2.empty() ? err1 : err2;
  }
  return false;
}

bool EmbeddingBackend::post_embeddings_json_ephemeral(const std::string& payload,
                                                      std::string* response,
                                                      std::string* error) const {
  if (response == nullptr) {
    if (error) {
      *error = "response nullptr";
    }
    return false;
  }
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error) {
      *error = "socket() ephemeral embeddings falló";
    }
    return false;
  }
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  set_sock_timeouts(fd, kEmbedHttpTimeoutMs);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    if (error) {
      *error = "connect ephemeral 127.0.0.1:" + std::to_string(port_) + " falló";
    }
    return false;
  }

  std::ostringstream req;
  req << "POST /v1/embeddings HTTP/1.1\r\n";
  req << "Host: 127.0.0.1:" << port_ << "\r\n";
  req << "Connection: close\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << payload.size() << "\r\n\r\n";
  req << payload;
  const std::string raw = req.str();
  if (!send_all(fd, raw.data(), raw.size())) {
    ::close(fd);
    if (error) {
      *error = "send ephemeral embeddings falló";
    }
    return false;
  }
  const bool ok = http_recv_response(fd, response, error);
  ::close(fd);
  return ok;
}

void EmbeddingBackend::stop_owned_unlocked() {
  {
    std::lock_guard http_lock(http_mu_);
    http_close_unlocked();
  }
  ready_.store(false);
  if (server_pid_ > 0) {
    ::kill(server_pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 20; ++i) {
      const pid_t r = ::waitpid(server_pid_, &status, WNOHANG);
      if (r == server_pid_ || r == -1) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (::waitpid(server_pid_, &status, WNOHANG) == 0) {
      ::kill(server_pid_, SIGKILL);
      ::waitpid(server_pid_, &status, 0);
    }
    server_pid_ = -1;
  }
}

void EmbeddingBackend::stop() {
  std::lock_guard lock(mu_);
  stop_owned_unlocked();
}

bool EmbeddingBackend::health_ok() const {
  std::lock_guard lock(http_mu_);
  auto probe = [&](std::string* err) -> bool {
    if (!http_ensure_connected_unlocked(err)) {
      return false;
    }
    std::string body;
    if (!http_exchange_unlocked("GET", "/health", {}, 2000, &body, err)) {
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

bool EmbeddingBackend::wait_until_healthy(int timeout_ms, std::string* error) const {
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
          *error = "llama-server terminó antes de quedar healthy";
        }
        return false;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (error) {
    *error = "timeout esperando /health en puerto " + std::to_string(port_);
  }
  return false;
}

void EmbeddingBackend::stop_listener_on_port() {
  kill_listeners_on_port(port_);
  {
    std::lock_guard http_lock(http_mu_);
    http_close_unlocked();
  }
}

bool EmbeddingBackend::start_server(const std::string& server_bin, const std::string& lib_dir,
                                    const AiLevel0EmbedSettings& emb, const ProgressFn& on_progress,
                                    std::string* error) {
  const int ngl = resolve_n_gpu_layers(lib_dir, emb.n_gpu_layers);
  const int threads = resolve_n_threads(emb.n_threads);
  const int n_ctx = emb.n_ctx > 0 ? emb.n_ctx : 1024;
  const int batch = emb.batch_size > 0 ? emb.batch_size : 2048;
  const int ubatch = emb.ubatch_size > 0 ? emb.ubatch_size : 512;
  const int n_parallel = emb.n_parallel > 0 ? emb.n_parallel : 8;
  server_stamp_ = make_server_stamp(model_path_, emb, ngl, threads);

  const fs::path stamp_path =
      fs::path(store_.cache_dir()) / "embed" / "intent" / "llama-server.stamp";
  const std::string old_stamp = read_text_file(stamp_path);
  const bool health = health_ok();
  if (health && old_stamp == server_stamp_) {
    ready_.store(true);
    if (on_progress) {
      on_progress("L0 embed: reutilizando llama-server en :" + std::to_string(port_) +
                  " (ngl=" + std::to_string(ngl) + " t=" + std::to_string(threads) +
                  " np=" + std::to_string(n_parallel) + ")");
    }
    return true;
  }

  // Stale server (old flags) or nothing listening: take over the port.
  if (health || !old_stamp.empty()) {
    if (on_progress) {
      on_progress("L0 embed: reiniciando llama-server con flags nuevos…");
    }
    // Must NOT call stop() here: ensure_ready already holds mu_ (deadlock).
    stop_owned_unlocked();
    for (int attempt = 0; attempt < 50 && health_ok(); ++attempt) {
      stop_listener_on_port();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (health_ok()) {
      if (error) {
        *error = "puerto " + std::to_string(port_) +
                 " ocupado por otro llama-server; ciérralo o cambia ai.level0.embeddings.server_port";
      }
      return false;
    }
  }

  std::error_code ec;
  const fs::path log_path =
      fs::path(store_.cache_dir()) / "embed" / "intent" / "llama-server.log";
  fs::create_directories(log_path.parent_path(), ec);

  const std::string port_s = std::to_string(port_);
  const std::string ctx_s = std::to_string(n_ctx);
  const std::string ngl_s = std::to_string(ngl);
  const std::string threads_s = std::to_string(threads);
  const std::string batch_s = std::to_string(batch);
  const std::string ubatch_s = std::to_string(ubatch);
  const std::string np_s = std::to_string(n_parallel);

  const pid_t pid = ::fork();
  if (pid < 0) {
    if (error) {
      *error = "fork llama-server falló";
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
    // Keep string storage alive for execv argv pointers.
    // No --kv-unified: a shared KV of size -c serializes multi-input embeds.
    // Each slot gets n_ctx/n_parallel tokens (enough for short signature passages).
    std::vector<std::string> args = {
        server_bin,    "-m", model_path_, "--host", "127.0.0.1", "--port", port_s,
        "--embedding", "--pooling", "mean", "-c", ctx_s, "-ngl", ngl_s, "-t", threads_s,
        "-tb",         threads_s, "-b", batch_s, "-ub", ubatch_s, "-np", np_s,
        "--log-disable"};
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
    on_progress("L0 embed: arrancando llama-server pid=" + std::to_string(pid) + " :" + port_s +
                " ngl=" + ngl_s + " t=" + threads_s + " np=" + np_s + " c=" + ctx_s);
  }
  if (!wait_until_healthy(90000, error)) {
    ::kill(server_pid_, SIGTERM);
    int status = 0;
    ::waitpid(server_pid_, &status, 0);
    server_pid_ = -1;
    if (error && error->find("timeout") != std::string::npos) {
      *error += " (ver " + log_path.string() + ")";
    }
    return false;
  }
  if (::kill(server_pid_, 0) != 0) {
    server_pid_ = -1;
    if (error) {
      if (health_ok()) {
        *error = "llama-server en :" + std::to_string(port_) +
                 " no es el proceso hijo (bind falló; ¿puerto ocupado?)";
      } else {
        *error = "llama-server terminó antes de quedar listo";
      }
    }
    return false;
  }
  ready_.store(true);
  // Warm model + HTTP path so first corpus batch is not 3–4× slower.
  {
    std::vector<std::vector<float>> warm;
    std::string warm_err;
    (void)embed_many_raw({"search_document: warmup"}, &warm, &warm_err);
  }
  write_text_file(stamp_path, server_stamp_);
  return true;
}

bool EmbeddingBackend::ensure_ready(const AiSettings& settings, const ProgressFn& on_progress,
                                    std::string* error) {
  std::lock_guard lock(mu_);
  store_ = ModelStore(settings.models_cache_dir.empty() ? ModelStore::default_cache_dir()
                                                        : settings.models_cache_dir);
  port_ = settings.level0.embeddings.server_port > 0 ? settings.level0.embeddings.server_port
                                                      : 18765;
  http_batch_ = settings.level0.embeddings.http_batch > 0 ? settings.level0.embeddings.http_batch
                                                          : kEmbedBatchChunkDefault;
  n_parallel_ = settings.level0.embeddings.n_parallel > 0 ? settings.level0.embeddings.n_parallel : 8;
  n_ctx_ = settings.level0.embeddings.n_ctx > 0 ? settings.level0.embeddings.n_ctx : 1024;
  // llama-server splits KV across slots: each input must fit in n_ctx/n_parallel tokens.
  // ~3 chars/token is conservative for code identifiers; leave room for task prefix.
  {
    const int slots = std::max(1, n_parallel_);
    const int slot_tokens = std::max(32, n_ctx_ / slots);
    const int usable = std::max(24, slot_tokens - 16);
    max_embed_chars_ = static_cast<std::size_t>(usable) * 3u;
  }

  // Resolve model path early so stamp matches reuse checks.
  AiModelInfo info = default_intent_embed_model();
  if (!settings.level0.embeddings.model_id.empty() &&
      settings.level0.embeddings.model_id != info.id) {
    info.id = settings.level0.embeddings.model_id;
    info.filename = settings.level0.embeddings.model_id;
    if (info.filename.find(".gguf") == std::string::npos) {
      info.filename += ".gguf";
    }
    info.url.clear();
  }
  if (!settings.level0.embeddings.model_path.empty()) {
    model_path_ = settings.level0.embeddings.model_path;
  } else {
    // Peek cache without forcing download yet if already ready with matching stamp.
    model_path_ = store_.ensure_intent_embed_model(
        info, settings.level0.embeddings.auto_download, on_progress, error);
    if (model_path_.empty()) {
      ai_trace(AiTraceChannel::Embed, "model_missing",
               "{\"id\":\"" + ai_trace_escape(info.id) + "\",\"file\":\"" +
                   ai_trace_escape(info.filename) + "\",\"err\":\"" +
                   ai_trace_escape(error ? *error : "") + "\"}");
      return false;
    }
  }

  server_bin_ = store_.resolve_llama_server();
  if (server_bin_.empty()) {
    std::string cli_err;
    store_.ensure_llama_cli(settings.level0.embeddings.auto_download || settings.level1.auto_download,
                            on_progress, &cli_err);
    server_bin_ = store_.resolve_llama_server();
    if (server_bin_.empty()) {
      if (error) {
        *error = cli_err.empty() ? "llama-server no encontrado" : cli_err;
      }
      ai_trace(AiTraceChannel::Embed, "server_bin_missing",
               "{\"err\":\"" + ai_trace_escape(error ? *error : "") + "\"}");
      return false;
    }
  }
  lib_dir_ = store_.library_dir_for_cli(server_bin_);

  const int ngl = resolve_n_gpu_layers(lib_dir_, settings.level0.embeddings.n_gpu_layers);
  const int threads = resolve_n_threads(settings.level0.embeddings.n_threads);
  const std::string want_stamp =
      make_server_stamp(model_path_, settings.level0.embeddings, ngl, threads);
  const fs::path stamp_path =
      fs::path(store_.cache_dir()) / "embed" / "intent" / "llama-server.stamp";
  if (ready() && health_ok() && read_text_file(stamp_path) == want_stamp) {
    server_stamp_ = want_stamp;
    return true;
  }

  ready_.store(false);
  {
    std::lock_guard http_lock(http_mu_);
    http_close_unlocked();
  }

  ai_trace(AiTraceChannel::Embed, "model_resolved",
           "{\"id\":\"" + ai_trace_escape(info.id) + "\",\"path\":\"" +
               ai_trace_escape(model_path_) + "\",\"port\":" + std::to_string(port_) +
               ",\"ngl\":" + std::to_string(ngl) + ",\"t\":" + std::to_string(threads) +
               ",\"np\":" + std::to_string(settings.level0.embeddings.n_parallel) +
               ",\"http_batch\":" + std::to_string(http_batch_) + "}");

  if (server_bin_.empty()) {
    std::string cli_err;
    store_.ensure_llama_cli(
        settings.level0.embeddings.auto_download || settings.level1.auto_download, on_progress,
        &cli_err);
    server_bin_ = store_.resolve_llama_server();
    if (server_bin_.empty()) {
      if (error) {
        *error = cli_err.empty() ? "llama-server no encontrado" : cli_err;
      }
      return false;
    }
    lib_dir_ = store_.library_dir_for_cli(server_bin_);
  }

  const bool started =
      start_server(server_bin_, lib_dir_, settings.level0.embeddings, on_progress, error);
  ai_trace(AiTraceChannel::Embed, "start_server_done",
           "{\"ok\":" + std::string(started ? "true" : "false") + ",\"bin\":\"" +
               ai_trace_escape(server_bin_) + "\",\"pid\":" +
               std::to_string(static_cast<long long>(server_pid_)) + ",\"stamp\":\"" +
               ai_trace_escape(server_stamp_) + "\",\"err\":\"" +
               ai_trace_escape(error ? *error : "") + "\"}");
  return started;
}

bool EmbeddingBackend::embed_raw(const std::string& text, std::vector<float>* out,
                                 std::string* error) const {
  if (out == nullptr) {
    if (error) {
      *error = "out nullptr";
    }
    return false;
  }
  std::vector<std::vector<float>> many;
  if (!embed_many_raw({text}, &many, error)) {
    return false;
  }
  if (many.empty() || many.front().empty()) {
    if (error) {
      *error = "embed_raw vacío";
    }
    return false;
  }
  *out = std::move(many.front());
  return true;
}

bool EmbeddingBackend::embed_many_raw(const std::vector<std::string>& texts,
                                      std::vector<std::vector<float>>* out,
                                      std::string* error) const {
  if (out == nullptr) {
    if (error) {
      *error = "out nullptr";
    }
    return false;
  }
  out->clear();
  if (texts.empty()) {
    return true;
  }
  if (!ready()) {
    if (error) {
      *error = "embedding backend no ready";
    }
    return false;
  }

  out->assign(texts.size(), {});
  const std::size_t chunk_n =
      http_batch_ > 0 ? static_cast<std::size_t>(http_batch_) : static_cast<std::size_t>(kEmbedBatchChunkDefault);
  const std::size_t max_chars = max_embed_chars_ > 0 ? max_embed_chars_ : 400;

  // One keep-alive connection, large batches. Parallel HTTP clients contend on llama-server.
  for (std::size_t base = 0; base < texts.size(); base += chunk_n) {
    const std::size_t n = std::min(chunk_n, texts.size() - base);
    nlohmann::json arr = nlohmann::json::array();
    for (std::size_t i = 0; i < n; ++i) {
      std::string t = texts[base + i];
      if (t.size() > max_chars) {
        t.resize(max_chars);
      }
      arr.push_back(std::move(t));
    }
    const std::string payload = nlohmann::json{{"input", std::move(arr)}}.dump();

    std::string body;
    if (!post_embeddings_json(payload, &body, error)) {
      out->clear();
      return false;
    }

    std::vector<std::vector<float>> chunk;
    if (!parse_embeddings_batch_json(body, n, &chunk, error)) {
      if (n == 1) {
        std::vector<float> one;
        if (parse_embedding_json(body, &one, error)) {
          (*out)[base] = std::move(one);
          continue;
        }
      }
      out->clear();
      return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
      (*out)[base + i] = std::move(chunk[i]);
    }
  }

  for (const auto& v : *out) {
    if (v.empty()) {
      if (error) {
        *error = "embed_many_raw: vector vacío";
      }
      out->clear();
      return false;
    }
  }
  return out->size() == texts.size();
}

bool EmbeddingBackend::embed_prefixed(const std::string& prefix, const std::string& text,
                                      std::vector<float>* out, std::string* error) const {
  return embed_raw(prefix + text, out, error);
}

bool EmbeddingBackend::embed_query(const std::string& text, std::vector<float>* out,
                                   std::string* error) const {
  return embed_prefixed("search_query: ", text, out, error);
}

bool EmbeddingBackend::embed_passage(const std::string& text, std::vector<float>* out,
                                     std::string* error) const {
  return embed_prefixed("search_document: ", text, out, error);
}

bool EmbeddingBackend::embed_passages(const std::vector<std::string>& texts,
                                      std::vector<std::vector<float>>* out,
                                      std::string* error) const {
  std::vector<std::string> prefixed;
  prefixed.reserve(texts.size());
  for (const auto& t : texts) {
    prefixed.push_back("search_document: " + t);
  }
  return embed_many_raw(prefixed, out, error);
}

}  // namespace tuide
