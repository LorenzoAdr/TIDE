#pragma once

#include "ai/ai_types.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tuide {

inline std::string llama_trim_copy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  return s;
}

// Hostname or IPv4. Strips http(s):// and an optional :port (returned via out_port).
inline std::string llama_normalize_host(std::string host, int* out_port = nullptr) {
  host = llama_trim_copy(std::move(host));
  if (host.rfind("https://", 0) == 0) {
    host = host.substr(8);
  } else if (host.rfind("http://", 0) == 0) {
    host = host.substr(7);
  }
  const auto slash = host.find('/');
  if (slash != std::string::npos) {
    host = host.substr(0, slash);
  }
  const auto colon = host.rfind(':');
  if (colon != std::string::npos && host.find(':') == colon) {
    if (out_port != nullptr) {
      const int parsed = std::atoi(host.c_str() + colon + 1);
      if (parsed > 0 && parsed < 65536) {
        *out_port = parsed;
      }
    }
    host = host.substr(0, colon);
  }
  if (host.empty()) {
    return "127.0.0.1";
  }
  return host;
}

inline bool llama_host_is_local(const std::string& host) {
  const std::string h = llama_normalize_host(host);
  return h == "127.0.0.1" || h == "localhost" || h == "::1" || h == "0.0.0.0";
}

// Env overrides (do not persist). Used so a Linux VM can point at a Mac Metal host
// without rewriting .tuide/config.json:
//   TUIDE_L2_API_BASE, TUIDE_L2_API_MODEL, TUIDE_EMBED_HOST, TUIDE_EMBED_PORT
inline void apply_ai_runtime_env(AiSettings* settings) {
  if (settings == nullptr) {
    return;
  }
  if (const char* v = std::getenv("TUIDE_L2_API_BASE"); v != nullptr && v[0] != '\0') {
    settings->level2.api_base = llama_trim_copy(v);
  }
  if (const char* v = std::getenv("TUIDE_L2_API_MODEL"); v != nullptr && v[0] != '\0') {
    settings->level2.api_model = llama_trim_copy(v);
  }
  if (const char* v = std::getenv("TUIDE_EMBED_HOST"); v != nullptr && v[0] != '\0') {
    int port = 0;
    settings->level0.embeddings.server_host = llama_normalize_host(v, &port);
    if (port > 0) {
      settings->level0.embeddings.server_port = port;
    }
  }
  if (const char* v = std::getenv("TUIDE_EMBED_PORT"); v != nullptr && v[0] != '\0') {
    const int parsed = std::atoi(v);
    if (parsed > 0 && parsed < 65536) {
      settings->level0.embeddings.server_port = parsed;
    }
  }
}

inline bool llama_connect_ipv4(int fd, const std::string& host, int port, std::string* error) {
  const std::string h = llama_normalize_host(host);
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* resolved = nullptr;
  const std::string port_s = std::to_string(port);
  if (::getaddrinfo(h.c_str(), port_s.c_str(), &hints, &resolved) != 0 || resolved == nullptr) {
    if (error != nullptr) {
      *error = "no se resolvió " + h + ":" + port_s;
    }
    return false;
  }
  const int rc = ::connect(fd, resolved->ai_addr, static_cast<socklen_t>(resolved->ai_addrlen));
  ::freeaddrinfo(resolved);
  if (rc != 0) {
    if (error != nullptr) {
      *error = "connect " + h + ":" + port_s + " falló";
    }
    return false;
  }
  return true;
}

}  // namespace tuide
