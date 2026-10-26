#include "ai/llm_client.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace tuide {
namespace {

// Rodaja de recv/poll: cada cuánto se comprueba cancelación y deadline.
constexpr int kSliceMs = 150;
// Tope del cuerpo de error que guardamos para el mensaje (no queremos 1 MB de HTML).
constexpr std::size_t kErrorBodyCap = 4096;
// Tope de espera que aceptamos de un Retry-After antes de rendirnos.
constexpr int kRetryAfterCapMs = 10000;

std::string trim_copy(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' ||
                        s.front() == '\n')) {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' ||
                        s.back() == '\n')) {
    s.pop_back();
  }
  return s;
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool cancelled(std::atomic<bool>* cancel) {
  return cancel != nullptr && cancel->load();
}

std::string shell_quote(const std::string& value) {
  std::string out = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('\'');
  return out;
}

// Texto de un campo de mensaje que puede venir como string o como array de
// partes ({"type":"text","text":"…"}), según proveedor.
std::string json_field_text(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object() || !obj.contains(key)) {
    return {};
  }
  const auto& v = obj.at(key);
  if (v.is_string()) {
    return v.get<std::string>();
  }
  if (v.is_array()) {
    std::string out;
    for (const auto& part : v) {
      if (part.is_string()) {
        out += part.get<std::string>();
      } else if (part.is_object()) {
        if (part.contains("text") && part["text"].is_string()) {
          out += part["text"].get<std::string>();
        } else if (part.contains("content") && part["content"].is_string()) {
          out += part["content"].get<std::string>();
        }
      }
    }
    return out;
  }
  return {};
}

std::string json_field_string(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object() || !obj.contains(key) || !obj.at(key).is_string()) {
    return {};
  }
  return obj.at(key).get<std::string>();
}

int json_field_int(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object() || !obj.contains(key) || !obj.at(key).is_number_integer()) {
    return 0;
  }
  return obj.at(key).get<int>();
}

LlmErrorKind kind_for_status(int status) {
  if (status == 401 || status == 403) {
    return LlmErrorKind::Auth;
  }
  if (status == 429) {
    return LlmErrorKind::RateLimit;
  }
  if (status >= 500) {
    return LlmErrorKind::ServerError;
  }
  if (status >= 400) {
    return LlmErrorKind::ClientError;
  }
  return LlmErrorKind::BadResponse;
}

// ------------------------------------------------------------------ HTTP

using BodySink = std::function<void(const char*, std::size_t)>;

struct HttpOutcome {
  bool ok = false;
  int status = 0;
  LlmErrorKind kind = LlmErrorKind::None;
  std::string error;
  std::string error_body;
  int retry_after_ms = 0;
};

// Enruta un flujo HTTP: acumula hasta cerrar las cabeceras, decide por status
// si el cuerpo va al sink (2xx) o al buffer de error, y opcionalmente
// des-trocea chunked (el transporte de socket sí; `curl` ya lo hace por su
// cuenta, así que ahí se desactiva).
class HeadRouter {
 public:
  HeadRouter(BodySink sink, bool decode_chunked)
      : sink_(std::move(sink)), decode_chunked_(decode_chunked) {}

  bool push(const char* data, std::size_t n, std::string* err) {
    if (n > 0) {
      buf_.append(data, n);
    }
    while (true) {
      if (!headers_done_) {
        const auto end = buf_.find("\r\n\r\n", scan_from_);
        if (end == std::string::npos) {
          // Evita re-escanear todo el buffer en cada trozo.
          scan_from_ = buf_.size() >= 3 ? buf_.size() - 3 : 0;
          return true;
        }
        const std::string head = buf_.substr(0, end);
        buf_.erase(0, end + 4);
        scan_from_ = 0;
        if (!parse_head(head, err)) {
          return false;
        }
        // 1xx: cabecera informativa (p.ej. "100 Continue" que curl provoca con
        // Expect en cuerpos grandes). La respuesta real viene después.
        if (status_ >= 100 && status_ < 200) {
          status_ = 0;
          continue;
        }
        headers_done_ = true;
        continue;
      }
      if (!drain_body(err)) {
        return false;
      }
      return true;
    }
  }

  bool headers_done() const { return headers_done_; }
  int status() const { return status_; }
  bool conn_close() const { return conn_close_; }
  bool body_complete() const { return body_complete_; }
  // true = la respuesta declara su longitud (chunked o Content-Length). Si no
  // la declara, el único final posible es que el servidor cierre, y entonces
  // un EOF NO es un corte.
  bool framed() const { return chunked_ || content_length_ >= 0; }
  void note_eof() {
    if (!framed()) {
      body_complete_ = true;
    }
  }
  int retry_after_ms() const { return retry_after_ms_; }
  const std::string& error_body() const { return error_body_; }

 private:
  bool parse_head(const std::string& head, std::string* err) {
    std::istringstream hs(head);
    std::string line;
    if (!std::getline(hs, line)) {
      if (err) {
        *err = "respuesta HTTP sin línea de estado";
      }
      return false;
    }
    {
      const auto sp = line.find(' ');
      if (sp == std::string::npos) {
        if (err) {
          *err = "línea de estado HTTP ilegible";
        }
        return false;
      }
      status_ = std::atoi(line.c_str() + sp + 1);
    }
    chunked_ = false;
    content_length_ = -1;
    conn_close_ = false;
    while (std::getline(hs, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const std::string key = ascii_lower(trim_copy(line.substr(0, colon)));
      const std::string val = trim_copy(line.substr(colon + 1));
      const std::string lval = ascii_lower(val);
      if (key == "content-length") {
        content_length_ = std::strtoll(val.c_str(), nullptr, 10);
      } else if (key == "transfer-encoding" && lval.find("chunked") != std::string::npos) {
        chunked_ = true;
      } else if (key == "connection" && lval.find("close") != std::string::npos) {
        conn_close_ = true;
      } else if (key == "retry-after") {
        // Solo el formato en segundos; una http-date se trata como "sin dato".
        const long secs = std::strtol(val.c_str(), nullptr, 10);
        if (secs > 0) {
          retry_after_ms_ = static_cast<int>(secs * 1000);
        }
      }
    }
    return true;
  }

  void emit(const char* data, std::size_t n) {
    if (n == 0) {
      return;
    }
    if (status_ >= 200 && status_ < 300) {
      if (sink_) {
        sink_(data, n);
      }
      return;
    }
    if (error_body_.size() < kErrorBodyCap) {
      error_body_.append(data, std::min(n, kErrorBodyCap - error_body_.size()));
    }
  }

  bool drain_body(std::string* err) {
    if (!chunked_ || !decode_chunked_) {
      if (!buf_.empty()) {
        emit(buf_.data(), buf_.size());
        body_seen_ += buf_.size();
        buf_.clear();
      }
      if (content_length_ >= 0 && body_seen_ >= static_cast<unsigned long long>(content_length_)) {
        body_complete_ = true;
      }
      return true;
    }
    // Decodificador chunked incremental.
    while (true) {
      if (chunk_remaining_ > 0) {
        const std::size_t take = std::min<std::size_t>(chunk_remaining_, buf_.size());
        if (take == 0) {
          return true;
        }
        emit(buf_.data(), take);
        buf_.erase(0, take);
        chunk_remaining_ -= take;
        if (chunk_remaining_ > 0) {
          return true;
        }
        expect_crlf_ = true;
        continue;
      }
      if (expect_crlf_) {
        if (buf_.size() < 2) {
          return true;
        }
        buf_.erase(0, 2);
        expect_crlf_ = false;
        continue;
      }
      const auto eol = buf_.find("\r\n");
      if (eol == std::string::npos) {
        if (buf_.size() > 64) {
          if (err) {
            *err = "cabecera de chunk HTTP inválida";
          }
          return false;
        }
        return true;
      }
      const std::string size_line = buf_.substr(0, eol);
      buf_.erase(0, eol + 2);
      const long long size = std::strtoll(size_line.c_str(), nullptr, 16);
      if (size < 0) {
        if (err) {
          *err = "tamaño de chunk HTTP negativo";
        }
        return false;
      }
      if (size == 0) {
        body_complete_ = true;
        buf_.clear();  // trailers: no nos interesan
        return true;
      }
      chunk_remaining_ = static_cast<std::size_t>(size);
    }
  }

  BodySink sink_;
  bool decode_chunked_ = true;
  std::string buf_;
  std::size_t scan_from_ = 0;
  bool headers_done_ = false;
  int status_ = 0;
  bool chunked_ = false;
  long long content_length_ = -1;
  bool conn_close_ = false;
  bool body_complete_ = false;
  unsigned long long body_seen_ = 0;
  std::size_t chunk_remaining_ = 0;
  bool expect_crlf_ = false;
  std::string error_body_;
  int retry_after_ms_ = 0;
};

std::string build_request_head(const LlmUrl& url, const std::string& api_key,
                               const std::string& payload, bool want_sse) {
  std::ostringstream req;
  req << "POST " << (url.path.empty() ? std::string() : url.path) << "/chat/completions"
      << " HTTP/1.1\r\n";
  req << "Host: " << url.host;
  const bool default_port = (url.https && url.port == 443) || (!url.https && url.port == 80);
  if (!default_port) {
    req << ':' << url.port;
  }
  req << "\r\n";
  req << "User-Agent: tuide/llm-client\r\n";
  req << "Connection: keep-alive\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Accept: " << (want_sse ? "text/event-stream" : "application/json") << "\r\n";
  if (!api_key.empty()) {
    req << "Authorization: Bearer " << api_key << "\r\n";
  }
  req << "Content-Length: " << payload.size() << "\r\n\r\n";
  return req.str();
}

bool send_all_fd(int fd, const char* data, std::size_t n) {
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

// Conexión con timeout real (connect bloqueante ignora SO_SNDTIMEO en Linux).
int connect_with_timeout(const std::string& host, int port, int connect_timeout_ms,
                         std::string* err) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* resolved = nullptr;
  const std::string port_s = std::to_string(port);
  if (::getaddrinfo(host.c_str(), port_s.c_str(), &hints, &resolved) != 0 || resolved == nullptr) {
    if (err) {
      *err = "no se resolvió " + host + ":" + port_s;
    }
    return -1;
  }
  int fd = -1;
  std::string last_err = "connect falló";
  for (addrinfo* ai = resolved; ai != nullptr; ai = ai->ai_next) {
    fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
      last_err = "socket() falló";
      continue;
    }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    int rc = ::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
    if (rc != 0 && errno == EINPROGRESS) {
      pollfd pfd{};
      pfd.fd = fd;
      pfd.events = POLLOUT;
      const int pr = ::poll(&pfd, 1, connect_timeout_ms > 0 ? connect_timeout_ms : 15000);
      if (pr > 0) {
        int soerr = 0;
        socklen_t len = sizeof(soerr);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) == 0 && soerr == 0) {
          rc = 0;
        } else {
          rc = -1;
          last_err = "connect rechazado por " + host + ":" + port_s;
        }
      } else {
        rc = -1;
        last_err = pr == 0 ? ("timeout de connect a " + host + ":" + port_s)
                           : ("poll de connect falló en " + host + ":" + port_s);
      }
    } else if (rc != 0) {
      last_err = "connect " + host + ":" + port_s + " falló";
    }
    if (rc == 0) {
      if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags);  // de vuelta a bloqueante con timeouts de socket
      }
      timeval tv{};
      tv.tv_sec = kSliceMs / 1000;
      tv.tv_usec = (kSliceMs % 1000) * 1000;
      ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      timeval stv{};
      stv.tv_sec = 30;
      ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
      ::freeaddrinfo(resolved);
      return fd;
    }
    ::close(fd);
    fd = -1;
  }
  ::freeaddrinfo(resolved);
  if (err) {
    *err = last_err;
  }
  return -1;
}

// Transporte http:// — socket crudo con keep-alive entre llamadas.
HttpOutcome socket_post(const LlmUrl& url, const std::string& api_key, const std::string& payload,
                        bool want_sse, int timeout_ms, int connect_timeout_ms,
                        std::atomic<bool>* cancel, const BodySink& sink, int* keep_fd) {
  HttpOutcome out;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  const std::string head = build_request_head(url, api_key, payload, want_sse);

  // Dos vueltas: un socket reutilizado puede haber sido cerrado por el otro
  // extremo mientras estaba en reposo. Si muere antes de la primera cabecera,
  // se reintenta con conexión fresca (no cuenta como reintento de la petición).
  for (int round = 0; round < 2; ++round) {
    if (cancelled(cancel)) {
      out.kind = LlmErrorKind::Cancelled;
      out.error = "cancelado";
      return out;
    }
    const bool reused = (keep_fd != nullptr && *keep_fd >= 0);
    int fd = reused ? *keep_fd : -1;
    if (fd < 0) {
      std::string cerr_msg;
      fd = connect_with_timeout(url.host, url.port, connect_timeout_ms, &cerr_msg);
      if (fd < 0) {
        out.kind = LlmErrorKind::Network;
        out.error = cerr_msg;
        return out;
      }
    }
    auto drop = [&]() {
      ::close(fd);
      if (keep_fd != nullptr && *keep_fd == fd) {
        *keep_fd = -1;
      }
      fd = -1;
    };

    if (!send_all_fd(fd, head.data(), head.size()) ||
        !send_all_fd(fd, payload.data(), payload.size())) {
      drop();
      if (reused && round == 0) {
        continue;
      }
      out.kind = LlmErrorKind::Network;
      out.error = "no se pudo enviar la petición";
      return out;
    }

    std::string route_err;
    HeadRouter router(sink, /*decode_chunked=*/true);
    std::array<char, 8192> buf{};
    bool peer_closed = false;
    bool stale_socket = false;
    while (!router.body_complete()) {
      if (cancelled(cancel)) {
        drop();
        out.kind = LlmErrorKind::Cancelled;
        out.error = "cancelado";
        return out;
      }
      if (std::chrono::steady_clock::now() > deadline) {
        drop();
        out.kind = LlmErrorKind::Timeout;
        out.error = "timeout tras " + std::to_string(timeout_ms / 1000) + "s";
        return out;
      }
      const ssize_t r = ::recv(fd, buf.data(), buf.size(), 0);
      if (r < 0) {
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;  // rodaja vacía: vuelve a comprobar cancel/deadline
        }
        drop();
        if (reused && round == 0 && !router.headers_done()) {
          stale_socket = true;  // conexión en reposo que el otro extremo cerró
          break;                // → segunda vuelta con socket fresco
        }
        out.kind = LlmErrorKind::Network;
        out.error = router.headers_done() ? "conexión cortada a media respuesta"
                                          : "recv falló sin recibir cabeceras";
        return out;
      }
      if (r == 0) {
        peer_closed = true;
        break;
      }
      if (!router.push(buf.data(), static_cast<std::size_t>(r), &route_err)) {
        drop();
        out.kind = LlmErrorKind::BadResponse;
        out.error = route_err.empty() ? "respuesta HTTP malformada" : route_err;
        return out;
      }
    }

    if (stale_socket) {
      continue;  // (el fd ya se cerró arriba)
    }
    if (peer_closed) {
      if (!router.headers_done()) {
        drop();
        if (reused && round == 0) {
          continue;  // cerró sin responder: conexión rancia
        }
        out.kind = LlmErrorKind::Network;
        out.error = "el servidor cerró sin responder";
        return out;
      }
      // Sin Content-Length ni chunked, el cierre ES el final del cuerpo.
      router.note_eof();
    }

    out.status = router.status();
    out.retry_after_ms = router.retry_after_ms();
    out.error_body = router.error_body();
    const bool keep = !router.conn_close() && router.body_complete() && !peer_closed;
    if (keep && keep_fd != nullptr) {
      *keep_fd = fd;
    } else {
      drop();
    }
    if (out.status >= 200 && out.status < 300) {
      if (!router.body_complete()) {
        // Enmarcada pero incompleta: el cuerpo que ya salió por el sink vale
        // (la UI lo mostró), pero no se declara ok — un JSON a medias no lo es.
        out.kind = LlmErrorKind::BadResponse;
        out.error = "respuesta cortada antes de completarse";
        return out;
      }
      out.ok = true;
      return out;
    }
    out.kind = kind_for_status(out.status);
    out.error = "HTTP " + std::to_string(out.status);
    return out;
  }

  out.kind = LlmErrorKind::Network;
  if (out.error.empty()) {
    out.error = "no se pudo hablar con " + url.host;
  }
  return out;
}

// Fichero temporal privado (0600): lleva el prompt completo y, en el caso del
// fichero de config de curl, la clave de API. En un /tmp compartido no puede
// quedar legible por otros usuarios.
fs::path write_private_tmp(const std::string& body, const char* suffix, std::string* err) {
  static std::atomic<unsigned> counter{0};
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / "tuide_llm";
  fs::create_directories(dir, ec);
  fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
  const fs::path path =
      dir / ("req_" + std::to_string(::getpid()) + "_" +
             std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + suffix);
  // O_EXCL + 0600 desde la creación: un ofstream dejaría una ventana con los
  // permisos del umask.
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0600);
  if (fd < 0) {
    if (err) {
      *err = std::string("no se pudo crear el temporal ") + path.string();
    }
    return {};
  }
  std::size_t off = 0;
  bool ok = true;
  while (off < body.size()) {
    const ssize_t w = ::write(fd, body.data() + off, body.size() - off);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) {
        continue;
      }
      ok = false;
      break;
    }
    off += static_cast<std::size_t>(w);
  }
  ::close(fd);
  if (!ok) {
    std::error_code rm;
    fs::remove(path, rm);
    if (err) {
      *err = "no se pudo escribir el payload temporal";
    }
    return {};
  }
  return path;
}

// Transporte https:// — subproceso curl. `-i` deja las cabeceras en stdout
// (por eso se reutiliza HeadRouter) y `-N` desactiva el buffer para que el SSE
// llegue a trozos. curl ya des-trocea chunked, así que el router no debe.
HttpOutcome curl_post(const LlmUrl& url, const std::string& api_key, const std::string& payload,
                      bool want_sse, int timeout_ms, int connect_timeout_ms,
                      std::atomic<bool>* cancel, const BodySink& sink) {
  HttpOutcome out;
  std::string tmp_err;
  const fs::path payload_path = write_private_tmp(payload, ".json", &tmp_err);
  if (payload_path.empty()) {
    out.kind = LlmErrorKind::Config;
    out.error = tmp_err;
    return out;
  }
  struct TmpGuard {
    fs::path p;
    ~TmpGuard() {
      if (p.empty()) {
        return;
      }
      std::error_code e;
      fs::remove(p, e);
    }
  };
  TmpGuard payload_guard{payload_path};

  std::ostringstream full_url;
  full_url << (url.https ? "https://" : "http://") << url.host;
  const bool default_port = (url.https && url.port == 443) || (!url.https && url.port == 80);
  if (!default_port) {
    full_url << ':' << url.port;
  }
  full_url << url.path << "/chat/completions";

  // Las opciones van en un fichero de config 0600, no en argv: la cabecera
  // Authorization sería visible en `ps`/`/proc` para cualquier usuario de la
  // máquina (eso hacía el camino viejo con popen).
  std::ostringstream cfg;
  auto cfg_quote = [](const std::string& v) {
    std::string out = "\"";
    for (const char ch : v) {
      if (ch == '"' || ch == '\\') {
        out.push_back('\\');
      }
      out.push_back(ch);
    }
    out.push_back('"');
    return out;
  };
  cfg << "silent\nshow-error\ninclude\nno-buffer\n";
  cfg << "request = POST\n";
  cfg << "url = " << cfg_quote(full_url.str()) << "\n";
  cfg << "max-time = " << std::max(1, timeout_ms / 1000) << "\n";
  cfg << "connect-timeout = " << std::max(1, connect_timeout_ms / 1000) << "\n";
  cfg << "header = " << cfg_quote("Content-Type: application/json") << "\n";
  cfg << "header = "
      << cfg_quote(std::string("Accept: ") +
                   (want_sse ? "text/event-stream" : "application/json"))
      << "\n";
  if (!api_key.empty()) {
    cfg << "header = " << cfg_quote("Authorization: Bearer " + api_key) << "\n";
  }
  cfg << "data-binary = " << cfg_quote("@" + payload_path.string()) << "\n";
  const fs::path cfg_path = write_private_tmp(cfg.str(), ".curlrc", &tmp_err);
  if (cfg_path.empty()) {
    out.kind = LlmErrorKind::Config;
    out.error = tmp_err;
    return out;
  }
  TmpGuard cfg_guard{cfg_path};
  // stderr aparte: si fuese al mismo pipe que el cuerpo podría intercalarse en
  // mitad del SSE, y su texto ("Could not resolve host") se perdía dentro del
  // router cuando no llegaban cabeceras.
  const fs::path err_path = write_private_tmp("", ".err", &tmp_err);
  TmpGuard err_guard{err_path};

  std::ostringstream cmd;
  cmd << "curl --config " << shell_quote(cfg_path.string());
  if (!err_path.empty()) {
    cmd << " 2> " << shell_quote(err_path.string());
  }
  const std::string cmdline = cmd.str();

  int pipefd[2] = {-1, -1};
  if (::pipe(pipefd) != 0) {
    out.kind = LlmErrorKind::Network;
    out.error = "pipe falló";
    return out;
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    out.kind = LlmErrorKind::Network;
    out.error = "fork falló";
    return out;
  }
  if (pid == 0) {
    // Grupo propio para poder matar el árbol entero (mismo patrón que
    // admin_run_shell_safe / TaskRunner::run).
    ::setpgid(0, 0);
    ::close(pipefd[0]);
    ::dup2(pipefd[1], STDOUT_FILENO);
    ::dup2(pipefd[1], STDERR_FILENO);
    if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
      ::close(pipefd[1]);
    }
    const int devnull = ::open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
      ::dup2(devnull, STDIN_FILENO);
      if (devnull != STDIN_FILENO) {
        ::close(devnull);
      }
    }
    ::execl("/bin/sh", "sh", "-c", cmdline.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }

  ::close(pipefd[1]);
  ::setpgid(pid, pid);
  {
    const int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
      ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  HeadRouter router(sink, /*decode_chunked=*/false);
  std::string route_err;
  std::array<char, 8192> buf{};
  bool was_cancelled = false;
  bool timed_out = false;
  bool malformed = false;
  bool eof = false;
  while (!eof) {
    if (cancelled(cancel)) {
      was_cancelled = true;
      ::kill(-pid, SIGTERM);
      break;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      timed_out = true;
      ::kill(-pid, SIGTERM);
      break;
    }
    pollfd pfd{};
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;
    const int pr = ::poll(&pfd, 1, kSliceMs);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (pr == 0) {
      continue;
    }
    const ssize_t n = ::read(pipefd[0], buf.data(), buf.size());
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      break;
    }
    if (n == 0) {
      eof = true;
      break;
    }
    if (!router.push(buf.data(), static_cast<std::size_t>(n), &route_err)) {
      malformed = true;
      ::kill(-pid, SIGTERM);
      break;
    }
  }
  ::close(pipefd[0]);
  if (was_cancelled || timed_out || malformed) {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ::kill(-pid, SIGKILL);
  }
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      status = -1;
      break;
    }
  }
  const int exit_code = (status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;

  if (was_cancelled) {
    out.kind = LlmErrorKind::Cancelled;
    out.error = "cancelado";
    return out;
  }
  if (timed_out) {
    out.kind = LlmErrorKind::Timeout;
    out.error = "timeout tras " + std::to_string(timeout_ms / 1000) + "s";
    return out;
  }
  if (malformed) {
    out.kind = LlmErrorKind::BadResponse;
    out.error = route_err.empty() ? "respuesta HTTP malformada" : route_err;
    return out;
  }
  // Diagnóstico real de curl (DNS, TLS, proxy…): sin esto solo se veía
  // "curl exit=6" sin decir por qué.
  std::string curl_stderr;
  if (!err_path.empty()) {
    std::ifstream ein(err_path, std::ios::binary);
    if (ein) {
      std::ostringstream ess;
      ess << ein.rdbuf();
      curl_stderr = trim_copy(ess.str());
      for (char& ch : curl_stderr) {
        if (ch == '\n' || ch == '\r') {
          ch = ' ';
        }
      }
      if (curl_stderr.size() > 240) {
        curl_stderr.resize(240);
      }
    }
  }
  out.status = router.status();
  out.retry_after_ms = router.retry_after_ms();
  out.error_body = router.error_body();
  if (!router.headers_done()) {
    // 6=DNS, 7=connect, 28=timeout, 35/60=TLS, 127=curl ausente.
    out.kind = exit_code == 28 ? LlmErrorKind::Timeout : LlmErrorKind::Network;
    out.error = exit_code == 127 ? "curl no está disponible en PATH"
                                 : ("curl exit=" + std::to_string(exit_code));
    if (!curl_stderr.empty()) {
      out.error += ": " + curl_stderr;
    } else if (!out.error_body.empty()) {
      out.error += ": " + trim_copy(out.error_body).substr(0, 200);
    }
    return out;
  }
  if (out.status >= 200 && out.status < 300) {
    if (exit_code != 0 && exit_code != 23) {
      // Cabeceras OK pero curl murió a medias (28 = --max-time).
      out.kind = exit_code == 28 ? LlmErrorKind::Timeout : LlmErrorKind::BadResponse;
      out.error = "stream cortado (curl exit=" + std::to_string(exit_code) + ")";
      if (!curl_stderr.empty()) {
        out.error += ": " + curl_stderr;
      }
      return out;
    }
    out.ok = true;
    return out;
  }
  out.kind = kind_for_status(out.status);
  out.error = "HTTP " + std::to_string(out.status);
  return out;
}

}  // namespace

// ---------------------------------------------------------------- públicos

const char* llm_role_name(LlmRole role) {
  switch (role) {
    case LlmRole::System:
      return "system";
    case LlmRole::User:
      return "user";
    case LlmRole::Assistant:
      return "assistant";
    case LlmRole::Tool:
      return "tool";
  }
  return "user";
}

LlmMessage LlmMessage::system_msg(std::string text) {
  LlmMessage m;
  m.role = LlmRole::System;
  m.content = std::move(text);
  return m;
}

LlmMessage LlmMessage::user_msg(std::string text) {
  LlmMessage m;
  m.role = LlmRole::User;
  m.content = std::move(text);
  return m;
}

LlmMessage LlmMessage::assistant_msg(std::string text) {
  LlmMessage m;
  m.role = LlmRole::Assistant;
  m.content = std::move(text);
  return m;
}

LlmMessage LlmMessage::tool_msg(std::string call_id, std::string name, std::string text) {
  LlmMessage m;
  m.role = LlmRole::Tool;
  m.tool_call_id = std::move(call_id);
  m.name = std::move(name);
  m.content = std::move(text);
  return m;
}

void LlmUsage::add(const LlmUsage& other) {
  prompt_tokens += other.prompt_tokens;
  completion_tokens += other.completion_tokens;
  cached_prompt_tokens += other.cached_prompt_tokens;
  reasoning_tokens += other.reasoning_tokens;
  calls += other.calls;
}

const char* llm_error_kind_name(LlmErrorKind kind) {
  switch (kind) {
    case LlmErrorKind::None:
      return "ok";
    case LlmErrorKind::Config:
      return "config";
    case LlmErrorKind::Cancelled:
      return "cancelled";
    case LlmErrorKind::Timeout:
      return "timeout";
    case LlmErrorKind::Network:
      return "network";
    case LlmErrorKind::RateLimit:
      return "rate_limit";
    case LlmErrorKind::ServerError:
      return "server_error";
    case LlmErrorKind::ClientError:
      return "client_error";
    case LlmErrorKind::Auth:
      return "auth";
    case LlmErrorKind::BadResponse:
      return "bad_response";
  }
  return "ok";
}

bool llm_error_is_retryable(LlmErrorKind kind) {
  switch (kind) {
    case LlmErrorKind::Timeout:
    case LlmErrorKind::Network:
    case LlmErrorKind::RateLimit:
    case LlmErrorKind::ServerError:
    case LlmErrorKind::BadResponse:
      return true;
    // Config/ClientError/Auth no mejoran repitiendo; Cancelled es decisión del
    // usuario y reintentarla sería desobedecerle.
    default:
      return false;
  }
}

bool llm_parse_url(const std::string& url, LlmUrl* out, std::string* err) {
  if (out == nullptr) {
    return false;
  }
  std::string s = trim_copy(url);
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  if (s.empty()) {
    if (err) {
      *err = "url vacía";
    }
    return false;
  }
  LlmUrl parsed;
  if (ascii_lower(s).rfind("https://", 0) == 0) {
    parsed.https = true;
    parsed.port = 443;
    s = s.substr(8);
  } else if (ascii_lower(s).rfind("http://", 0) == 0) {
    parsed.https = false;
    parsed.port = 80;
    s = s.substr(7);
  } else {
    if (err) {
      *err = "url sin esquema http(s):// — " + url;
    }
    return false;
  }
  std::string authority = s;
  const auto slash = s.find('/');
  if (slash != std::string::npos) {
    authority = s.substr(0, slash);
    parsed.path = s.substr(slash);
    while (!parsed.path.empty() && parsed.path.back() == '/') {
      parsed.path.pop_back();
    }
  }
  if (authority.empty()) {
    if (err) {
      *err = "url sin host — " + url;
    }
    return false;
  }
  // IPv6 literal entre corchetes: [::1]:8080
  if (authority.front() == '[') {
    const auto close = authority.find(']');
    if (close == std::string::npos) {
      if (err) {
        *err = "url con IPv6 mal formada — " + url;
      }
      return false;
    }
    parsed.host = authority.substr(1, close - 1);
    if (close + 1 < authority.size() && authority[close + 1] == ':') {
      parsed.port = std::atoi(authority.c_str() + close + 2);
    }
  } else {
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(':') == colon) {
      parsed.host = authority.substr(0, colon);
      parsed.port = std::atoi(authority.c_str() + colon + 1);
    } else {
      parsed.host = authority;
    }
  }
  if (parsed.host.empty() || parsed.port <= 0 || parsed.port > 65535) {
    if (err) {
      *err = "host o puerto inválidos — " + url;
    }
    return false;
  }
  *out = parsed;
  return true;
}

nlohmann::json llm_build_chat_body(const LlmEndpoint& endpoint, const LlmRequest& req,
                                   bool stream) {
  nlohmann::json messages = nlohmann::json::array();
  for (const auto& m : req.messages) {
    nlohmann::json jm{{"role", llm_role_name(m.role)}};
    if (m.role == LlmRole::Tool) {
      jm["tool_call_id"] = m.tool_call_id;
      if (!m.name.empty()) {
        jm["name"] = m.name;
      }
      jm["content"] = m.content;
    } else if (m.role == LlmRole::Assistant && !m.tool_calls.empty()) {
      // Varios proveedores rechazan content:"" junto a tool_calls; null es lo
      // que acepta todo el mundo.
      if (m.content.empty()) {
        jm["content"] = nullptr;
      } else {
        jm["content"] = m.content;
      }
      nlohmann::json calls = nlohmann::json::array();
      for (const auto& tc : m.tool_calls) {
        calls.push_back({{"id", tc.id},
                         {"type", "function"},
                         {"function",
                          {{"name", tc.name},
                           {"arguments", tc.arguments.empty() ? "{}" : tc.arguments}}}});
      }
      jm["tool_calls"] = std::move(calls);
    } else {
      jm["content"] = m.content;
    }
    messages.push_back(std::move(jm));
  }

  nlohmann::json body{
      {"model", req.model.empty() ? endpoint.model : req.model},
      {"messages", std::move(messages)},
      {"temperature", req.temperature},
      {"max_tokens", req.max_tokens > 0 ? req.max_tokens : 1024},
  };

  if (!req.tools.empty() && endpoint.caps.tool_calling) {
    nlohmann::json tools = nlohmann::json::array();
    for (const auto& t : req.tools) {
      tools.push_back({{"type", "function"},
                       {"function",
                        {{"name", t.name},
                         {"description", t.description},
                         {"parameters", t.parameters.is_null() ? nlohmann::json::object()
                                                               : t.parameters}}}});
    }
    body["tools"] = std::move(tools);
    if (!req.tool_choice.empty()) {
      body["tool_choice"] = req.tool_choice;
    }
    if (endpoint.caps.parallel_tool_calls) {
      body["parallel_tool_calls"] = true;
    }
  }

  // Salida estructurada, de más específica a menos. Nunca dos a la vez: se
  // pisan y algunos proveedores devuelven 400 si llegan juntas.
  //   1. json_schema  → contrato completo (el proveedor valida los campos)
  //   2. grammar      → GBNF de llama.cpp (precisión sin schema)
  //   3. json_object  → solo "esto debe ser JSON parseable"
  const bool has_schema = !req.json_schema.is_null() && !req.json_schema.empty();
  if (has_schema && endpoint.caps.json_schema) {
    body["response_format"] = {
        {"type", "json_schema"},
        {"json_schema",
         {{"name", req.json_schema_name.empty() ? "response" : req.json_schema_name},
          {"schema", req.json_schema}}}};
  } else if (!req.grammar.empty() && endpoint.caps.grammar) {
    body["grammar"] = req.grammar;
  } else if (req.json_object && endpoint.caps.json_schema) {
    // OJO: OpenAI y DeepSeek rechazan con 400 un `json_object` cuyos mensajes
    // no contengan la palabra "json". Los cuatro prompts de admin_v1 la llevan
    // ("Cada turno UN JSON", "UN JSON por ola", "Elige UNA acción JSON",
    // "Intenta tumbar. UN JSON") — si alguien la quita de uno, el síntoma será
    // un 400 que no parece tener nada que ver.
    body["response_format"] = {{"type", "json_object"}};
  }

  if (stream) {
    body["stream"] = true;
    // Sin esto, OpenAI y compatibles omiten `usage` en el stream y no hay
    // contabilidad de tokens (llama-server lo manda igual, no molesta).
    body["stream_options"] = {{"include_usage", true}};
  }
  if (endpoint.caps.prompt_cache) {
    body["cache_prompt"] = true;  // llama-server; los remotos lo ignoran
  }
  // Mismos nombres de campo que llama_backend.cpp::attach_thinking_json para no
  // tener dos convenciones de "pensar" en el mismo binario.
  if (req.enable_thinking.has_value()) {
    body["chat_template_kwargs"] = nlohmann::json{{"enable_thinking", *req.enable_thinking}};
  }
  if (req.reasoning_budget >= 0) {
    body["thinking_budget_tokens"] = req.reasoning_budget;
    body["reasoning_budget_tokens"] = req.reasoning_budget;
  }
  return body;
}

void llm_parse_usage(const nlohmann::json& usage, LlmUsage* out) {
  if (out == nullptr || !usage.is_object()) {
    return;
  }
  out->prompt_tokens = json_field_int(usage, "prompt_tokens");
  out->completion_tokens = json_field_int(usage, "completion_tokens");
  // OpenAI / llama-server
  if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].is_object()) {
    out->cached_prompt_tokens = json_field_int(usage["prompt_tokens_details"], "cached_tokens");
  }
  // DeepSeek
  if (out->cached_prompt_tokens == 0) {
    out->cached_prompt_tokens = json_field_int(usage, "prompt_cache_hit_tokens");
  }
  if (usage.contains("completion_tokens_details") &&
      usage["completion_tokens_details"].is_object()) {
    out->reasoning_tokens = json_field_int(usage["completion_tokens_details"], "reasoning_tokens");
  }
  out->calls = 1;
}

std::string llm_extract_error_message(const std::string& body) {
  const std::string trimmed = trim_copy(body);
  if (trimmed.empty()) {
    return {};
  }
  try {
    const auto j = nlohmann::json::parse(trimmed);
    if (j.is_object() && j.contains("error")) {
      const auto& e = j["error"];
      if (e.is_string()) {
        return e.get<std::string>();
      }
      if (e.is_object()) {
        const std::string msg = json_field_string(e, "message");
        if (!msg.empty()) {
          return msg;
        }
        return e.dump().substr(0, 300);
      }
    }
    if (j.is_object() && j.contains("message") && j["message"].is_string()) {
      return j["message"].get<std::string>();
    }
  } catch (...) {
    // No era JSON (HTML de un proxy, texto suelto): se devuelve recortado.
  }
  std::string flat = trimmed.substr(0, 300);
  for (char& c : flat) {
    if (c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  return flat;
}

bool llm_parse_chat_json(const nlohmann::json& doc, LlmResponse* out, std::string* err) {
  if (out == nullptr) {
    if (err) {
      *err = "out nullptr";
    }
    return false;
  }
  if (!doc.is_object()) {
    if (err) {
      *err = "respuesta no es un objeto JSON";
    }
    return false;
  }
  if (doc.contains("error")) {
    if (err) {
      const auto& e = doc["error"];
      *err = e.is_string() ? e.get<std::string>() : json_field_string(e, "message");
      if (err->empty()) {
        *err = e.dump().substr(0, 300);
      }
    }
    return false;
  }
  if (doc.contains("usage")) {
    llm_parse_usage(doc["usage"], &out->usage);
  }
  if (!doc.contains("choices") || !doc["choices"].is_array() || doc["choices"].empty()) {
    if (err) {
      *err = "respuesta sin choices";
    }
    return false;
  }
  const auto& c0 = doc["choices"][0];
  out->finish_reason = json_field_string(c0, "finish_reason");
  if (c0.contains("message") && c0["message"].is_object()) {
    const auto& msg = c0["message"];
    out->content = json_field_text(msg, "content");
    out->reasoning = json_field_text(msg, "reasoning_content");
    if (out->reasoning.empty()) {
      out->reasoning = json_field_text(msg, "reasoning");
    }
    if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
      for (const auto& tc : msg["tool_calls"]) {
        LlmToolCall call;
        call.id = json_field_string(tc, "id");
        if (tc.contains("function") && tc["function"].is_object()) {
          call.name = json_field_string(tc["function"], "name");
          call.arguments = json_field_string(tc["function"], "arguments");
        }
        if (!call.name.empty()) {
          out->tool_calls.push_back(std::move(call));
        }
      }
    }
  } else if (c0.contains("text") && c0["text"].is_string()) {
    out->content = c0["text"].get<std::string>();
  }
  if (out->content.empty() && out->tool_calls.empty() && out->reasoning.empty()) {
    if (err) {
      *err = "respuesta sin content ni tool_calls";
    }
    return false;
  }
  out->ok = true;
  return true;
}

bool llm_parse_chat_body(const std::string& body, LlmResponse* out, std::string* err) {
  try {
    return llm_parse_chat_json(nlohmann::json::parse(body), out, err);
  } catch (const std::exception& ex) {
    if (err) {
      *err = std::string("JSON ilegible: ") + ex.what() + " | " + body.substr(0, 200);
    }
    return false;
  }
}

// ---------------------------------------------------------- stream parser

LlmStreamParser::LlmStreamParser(LlmDeltaFn on_delta) : on_delta_(std::move(on_delta)) {}

void LlmStreamParser::feed(const char* data, std::size_t n) {
  if (data == nullptr || n == 0) {
    return;
  }
  raw_.append(data, n);
  pending_.append(data, n);
  std::size_t start = 0;
  while (true) {
    const auto nl = pending_.find('\n', start);
    if (nl == std::string::npos) {
      break;
    }
    std::string line = pending_.substr(start, nl - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    handle_line(std::move(line));
    start = nl + 1;
  }
  if (start > 0) {
    pending_.erase(0, start);
  }
}

void LlmStreamParser::finish() {
  if (!pending_.empty()) {
    std::string line = pending_;
    pending_.clear();
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    handle_line(std::move(line));
  }
}

void LlmStreamParser::handle_line(std::string line) {
  if (line.empty()) {
    return;  // separador de eventos SSE
  }
  if (line.rfind("data:", 0) != 0) {
    // `event:`/`id:`/`:` keep-alive o un cuerpo que no es SSE: se guarda en raw_.
    if (line.rfind("event:", 0) == 0 || line.rfind("id:", 0) == 0 || line.front() == ':') {
      saw_sse_field_ = true;
    }
    return;
  }
  saw_sse_field_ = true;
  std::string payload = trim_copy(line.substr(5));
  if (payload.empty()) {
    return;
  }
  if (payload == "[DONE]") {
    saw_done_ = true;
    return;
  }
  handle_payload(payload);
}

LlmToolCall* LlmStreamParser::tool_call_slot(int index) {
  for (std::size_t i = 0; i < tool_call_order_.size(); ++i) {
    if (tool_call_order_[i] == index) {
      return &out_.tool_calls[i];
    }
  }
  tool_call_order_.push_back(index);
  out_.tool_calls.emplace_back();
  return &out_.tool_calls.back();
}

void LlmStreamParser::handle_payload(const std::string& payload) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(payload);
  } catch (...) {
    return;  // trozo basura: el stream sigue
  }
  if (!j.is_object()) {
    return;
  }
  if (j.contains("usage") && j["usage"].is_object()) {
    LlmUsage u;
    llm_parse_usage(j["usage"], &u);
    if (!u.empty()) {
      // El último chunk trae el total acumulado, no un incremento.
      u.calls = 1;
      out_.usage = u;
    }
  }
  if (j.contains("error")) {
    const auto& e = j["error"];
    out_.error = e.is_string() ? e.get<std::string>() : json_field_string(e, "message");
    if (out_.error.empty()) {
      out_.error = e.dump().substr(0, 300);
    }
    out_.kind = LlmErrorKind::BadResponse;
    return;
  }
  if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
    return;
  }
  const auto& c0 = j["choices"][0];
  const std::string fr = json_field_string(c0, "finish_reason");
  if (!fr.empty()) {
    out_.finish_reason = fr;
  }
  const nlohmann::json* delta = nullptr;
  if (c0.contains("delta") && c0["delta"].is_object()) {
    delta = &c0["delta"];
  } else if (c0.contains("message") && c0["message"].is_object()) {
    // Algún proveedor manda el mensaje completo en un único chunk.
    delta = &c0["message"];
  }
  if (delta == nullptr) {
    return;
  }
  LlmDelta ev;
  ev.text = json_field_text(*delta, "content");
  ev.reasoning = json_field_text(*delta, "reasoning_content");
  if (ev.reasoning.empty()) {
    ev.reasoning = json_field_text(*delta, "reasoning");
  }
  if (!ev.text.empty()) {
    out_.content += ev.text;
  }
  if (!ev.reasoning.empty()) {
    out_.reasoning += ev.reasoning;
  }
  bool any_tool = false;
  if (delta->contains("tool_calls") && (*delta)["tool_calls"].is_array()) {
    for (const auto& tc : (*delta)["tool_calls"]) {
      const int idx = tc.contains("index") && tc["index"].is_number_integer()
                          ? tc["index"].get<int>()
                          : 0;
      LlmToolCall* slot = tool_call_slot(idx);
      const std::string id = json_field_string(tc, "id");
      if (!id.empty()) {
        slot->id = id;
      }
      std::string name;
      std::string args;
      if (tc.contains("function") && tc["function"].is_object()) {
        name = json_field_string(tc["function"], "name");
        args = json_field_string(tc["function"], "arguments");
      }
      if (!name.empty()) {
        slot->name = name;
      }
      if (!args.empty()) {
        slot->arguments += args;
      }
      if (!name.empty() || !args.empty()) {
        any_tool = true;
        LlmDelta tev;
        tev.tool_call_index = idx;
        tev.tool_call_name = name;
        tev.tool_call_arguments = args;
        if (on_delta_) {
          on_delta_(tev);
          emitted_ = true;
        }
      }
    }
  }
  if ((!ev.text.empty() || !ev.reasoning.empty()) && on_delta_) {
    on_delta_(ev);
    emitted_ = true;
  }
  (void)any_tool;
}

LlmResponse LlmStreamParser::take() {
  LlmResponse out = std::move(out_);
  out_ = LlmResponse{};
  // Limpia tool calls a medio construir sin nombre (chunk perdido).
  out.tool_calls.erase(std::remove_if(out.tool_calls.begin(), out.tool_calls.end(),
                                      [](const LlmToolCall& c) { return c.name.empty(); }),
                       out.tool_calls.end());
  out.streamed = true;
  out.emitted = emitted_;
  return out;
}

// ---------------------------------------------------------------- cliente

LlmClient::LlmClient(LlmEndpoint endpoint) : endpoint_(std::move(endpoint)) {}

LlmClient::~LlmClient() {
  std::lock_guard<std::mutex> lock(mu_);
  if (keep_fd_ >= 0) {
    ::close(keep_fd_);
    keep_fd_ = -1;
  }
}

void LlmClient::set_endpoint(LlmEndpoint endpoint) {
  std::lock_guard<std::mutex> lock(mu_);
  const bool same_target = endpoint.api_base == endpoint_.api_base;
  endpoint_ = std::move(endpoint);
  if (!same_target && keep_fd_ >= 0) {
    ::close(keep_fd_);
    keep_fd_ = -1;
  }
}

std::string LlmClient::last_attempts_text() const {
  std::lock_guard<std::mutex> lock(attempts_mu_);
  return attempts_text_;
}

LlmResponse LlmClient::attempt(const LlmRequest& req, const std::string& payload, bool stream,
                               std::atomic<bool>* cancel, const LlmDeltaFn& on_delta,
                               int* retry_after_ms) {
  LlmResponse out;
  LlmUrl url;
  std::string url_err;
  if (!llm_parse_url(endpoint_.api_base, &url, &url_err)) {
    out.kind = LlmErrorKind::Config;
    out.error = url_err;
    return out;
  }
  const int timeout_ms = req.timeout_ms > 0 ? req.timeout_ms : endpoint_.timeout_ms;

  LlmStreamParser parser(on_delta);
  std::string plain;
  BodySink sink;
  if (stream) {
    sink = [&parser](const char* d, std::size_t n) { parser.feed(d, n); };
  } else {
    sink = [&plain](const char* d, std::size_t n) {
      if (plain.size() < (4u << 20)) {
        plain.append(d, n);
      }
    };
  }

  HttpOutcome http;
  if (url.https) {
    http = curl_post(url, endpoint_.api_key, payload, stream, timeout_ms,
                     endpoint_.connect_timeout_ms, cancel, sink);
  } else {
    int fd = keep_fd_;
    http = socket_post(url, endpoint_.api_key, payload, stream, timeout_ms,
                       endpoint_.connect_timeout_ms, cancel, sink, &fd);
    keep_fd_ = fd;
  }
  if (stream) {
    parser.finish();
  }
  out.http_status = http.status;
  out.streamed = stream;
  if (retry_after_ms != nullptr) {
    *retry_after_ms = http.retry_after_ms;
  }

  if (!http.ok) {
    out.kind = http.kind == LlmErrorKind::None ? LlmErrorKind::BadResponse : http.kind;
    out.error = http.error;
    const std::string detail = llm_extract_error_message(http.error_body);
    if (!detail.empty()) {
      out.error += ": " + detail;
    }
    if (stream) {
      // Conserva lo ya recibido: la UI lo mostró y el llamante puede decidir.
      LlmResponse partial = parser.take();
      out.content = std::move(partial.content);
      out.reasoning = std::move(partial.reasoning);
      out.emitted = partial.emitted;
      out.usage = partial.usage;
      if (!out.content.empty() && out.finish_reason.empty()) {
        out.finish_reason = "truncated";
      }
    }
    return out;
  }

  if (stream) {
    LlmResponse parsed = parser.take();
    out.emitted = parsed.emitted;
    // `reasoning` cuenta como contenido: un modelo que agota max_tokens
    // pensando, o que escribe su JSON dentro del bloque de razonamiento (visto
    // con GBNF + thinking), devuelve content vacío y reasoning lleno. El camino
    // no-streaming ya lo acepta (llm_parse_chat_json); este debe hacer lo mismo
    // o se tira una respuesta buena.
    const bool empty =
        parsed.content.empty() && parsed.tool_calls.empty() && parsed.reasoning.empty();
    if (empty && !parser.looks_like_sse()) {
      // El proveedor ignoró `stream` y devolvió el JSON completo de una pieza.
      std::string perr;
      LlmResponse whole;
      if (llm_parse_chat_body(parser.raw_text(), &whole, &perr)) {
        whole.http_status = out.http_status;
        whole.streamed = false;
        whole.emitted = false;
        return whole;
      }
      out.kind = LlmErrorKind::BadResponse;
      out.error = perr.empty() ? "respuesta vacía" : perr;
      return out;
    }
    if (empty) {
      out.kind = LlmErrorKind::BadResponse;
      out.error = parsed.error.empty() ? "stream sin contenido" : parsed.error;
      out.usage = parsed.usage;
      return out;
    }
    if (!parser.saw_done() && parsed.finish_reason.empty()) {
      // Stream cortado a media frase: el texto parcial sirve para la UI pero no
      // se declara ok (un JSON a medias no es útil para el lazo).
      out.content = std::move(parsed.content);
      out.reasoning = std::move(parsed.reasoning);
      out.tool_calls = std::move(parsed.tool_calls);
      out.usage = parsed.usage;
      out.finish_reason = "truncated";
      out.kind = LlmErrorKind::BadResponse;
      out.error = "stream cortado antes de [DONE]";
      return out;
    }
    parsed.http_status = out.http_status;
    parsed.ok = true;
    return parsed;
  }

  std::string perr;
  LlmResponse whole;
  if (!llm_parse_chat_body(plain, &whole, &perr)) {
    // Simétrico del caso anterior: pedimos no-streaming y el proveedor (o un
    // proxy por medio) contestó con SSE de todas formas. Reaprovecha el
    // acumulador en vez de declarar el cuerpo ilegible.
    const std::string head = plain.substr(0, 64);
    const bool looks_sse =
        head.rfind("data:", 0) == 0 || plain.find("\ndata:") != std::string::npos;
    if (looks_sse) {
      LlmStreamParser late;
      late.feed(plain);
      late.finish();
      LlmResponse parsed = late.take();
      if (!parsed.content.empty() || !parsed.tool_calls.empty()) {
        parsed.ok = true;
        parsed.http_status = out.http_status;
        parsed.streamed = false;  // no hubo deltas en vivo
        parsed.emitted = false;
        return parsed;
      }
    }
    out.kind = LlmErrorKind::BadResponse;
    out.error = perr;
    return out;
  }
  whole.http_status = out.http_status;
  return whole;
}

LlmResponse LlmClient::chat(const LlmRequest& req, std::atomic<bool>* cancel,
                            const LlmDeltaFn& on_delta) {
  const auto t0 = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mu_);
  std::string attempts_log;

  LlmResponse out;
  if (endpoint_.api_base.empty()) {
    out.kind = LlmErrorKind::Config;
    out.error = "ai.level2.api_base vacío";
    return out;
  }
  if (req.model.empty() && endpoint_.model.empty()) {
    out.kind = LlmErrorKind::Config;
    out.error = "modelo vacío (ai.level2.api_model)";
    return out;
  }
  {
    LlmUrl probe;
    std::string perr;
    if (!llm_parse_url(endpoint_.api_base, &probe, &perr)) {
      out.kind = LlmErrorKind::Config;
      out.error = perr;
      return out;
    }
  }

  const bool stream = req.stream && endpoint_.caps.streaming && static_cast<bool>(on_delta);
  std::string payload;
  try {
    payload = llm_build_chat_body(endpoint_, req, stream)
                  .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  } catch (const std::exception& ex) {
    out.kind = LlmErrorKind::Config;
    out.error = std::string("no se pudo serializar la petición: ") + ex.what();
    return out;
  }

  const int max_retries = req.max_retries >= 0 ? req.max_retries : endpoint_.max_retries;
  std::mt19937 rng(static_cast<unsigned>(
      std::chrono::steady_clock::now().time_since_epoch().count() & 0xffffffff));

  for (int attempt_no = 1;; ++attempt_no) {
    int retry_after_ms = 0;
    const auto a0 = std::chrono::steady_clock::now();
    out = attempt(req, payload, stream, cancel, on_delta, &retry_after_ms);
    const long a_ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - a0)
            .count());
    out.attempts = attempt_no;
    out.ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count());
    if (!attempts_log.empty()) {
      attempts_log += " | ";
    }
    attempts_log += "#" + std::to_string(attempt_no) + " " +
                    (out.ok ? "ok" : llm_error_kind_name(out.kind)) +
                    (out.http_status > 0 ? (" http=" + std::to_string(out.http_status)) : "") +
                    " " + std::to_string(a_ms) + "ms";
    {
      std::lock_guard<std::mutex> alock(attempts_mu_);
      attempts_text_ = attempts_log;
    }
    if (out.ok) {
      if (out.usage.calls == 0 && (out.usage.prompt_tokens > 0 || out.usage.completion_tokens > 0)) {
        out.usage.calls = 1;
      }
      return out;
    }
    const bool can_retry = llm_error_is_retryable(out.kind) && !out.emitted &&
                           attempt_no <= max_retries && !cancelled(cancel);
    if (!can_retry) {
      return out;
    }
    int wait_ms = std::min(8000, 400 * (1 << std::min(4, attempt_no - 1)));
    wait_ms += static_cast<int>(rng() % 250);
    if (retry_after_ms > 0) {
      if (retry_after_ms > kRetryAfterCapMs) {
        out.error += " (Retry-After " + std::to_string(retry_after_ms / 1000) + "s: no espero)";
        return out;
      }
      wait_ms = std::max(wait_ms, retry_after_ms);
    }
    for (int slept = 0; slept < wait_ms; slept += kSliceMs) {
      if (cancelled(cancel)) {
        out.kind = LlmErrorKind::Cancelled;
        out.error = "cancelado";
        return out;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kSliceMs));
    }
  }
}

}  // namespace tuide
