// Live check: TIDE in the Linux VM ↔ llama-server on the host GPU (Metal).
//
// Default: skip (exit 0) if the remote host is unreachable so the unit suite
// stays green without the Mac servers. Fail when the host is up but a path
// breaks, or when the hybrid env is set and nothing answers.
//
//   TUIDE_L2_API_BASE   http://192.168.64.1:8080/v1
//   TUIDE_L2_API_MODEL  alias advertised by llama-server
//   TUIDE_EMBED_HOST    192.168.64.1   (or host.orb.internal)
//   TUIDE_EMBED_PORT    18765
//   TUIDE_LLAMA_LIVE=1  never skip; fail if the host is down
//   TUIDE_LLAMA_SKIP_GENERATE=1  health + embed only (no chat completion)
//   --speed / TUIDE_LLAMA_SPEED=1  bench 14B-style: warmup + decode + prefill (tok/s)
//
// Host: ./tools/run_host_llama.sh
// VM:   ./build/llama_host_comm_test
//       ./build/llama_host_comm_test --speed

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "ai/ai_types.hpp"
#include "ai/embedding_backend.hpp"
#include "ai/llama_backend.hpp"
#include "ai/llama_net.hpp"

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

bool env_flag(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0' && std::string(v) != "0";
}

bool env_set(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0';
}

void set_sock_timeouts(int fd, int timeout_ms) {
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int open_tcp(const std::string& host, int port, int timeout_ms, std::string* error) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    if (error != nullptr) {
      *error = "socket() falló";
    }
    return -1;
  }
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  set_sock_timeouts(fd, timeout_ms);
  if (!tuide::llama_connect_ipv4(fd, host, port, error)) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool tcp_ok(const std::string& host, int port, int timeout_ms) {
  std::string err;
  const int fd = open_tcp(host, port, timeout_ms, &err);
  if (fd < 0) {
    return false;
  }
  ::close(fd);
  return true;
}

bool http_exchange(const std::string& host, int port, const std::string& method,
                   const std::string& path, const std::string& body, int timeout_ms,
                   int* status_out, std::string* response, std::string* error) {
  const int fd = open_tcp(host, port, timeout_ms, error);
  if (fd < 0) {
    return false;
  }

  std::ostringstream req;
  req << method << ' ' << path << " HTTP/1.1\r\n";
  req << "Host: " << host << ":" << port << "\r\n";
  req << "Connection: close\r\n";
  if (method == "POST") {
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
  }
  req << "\r\n";
  if (method == "POST") {
    req << body;
  }
  const std::string raw = req.str();
  std::size_t sent = 0;
  while (sent < raw.size()) {
    const ssize_t n = ::send(fd, raw.data() + sent, raw.size() - sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ::close(fd);
      if (error != nullptr) {
        *error = "send HTTP falló " + host + ":" + std::to_string(port) + " " + path;
      }
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }

  std::string buf;
  buf.reserve(8192);
  char tmp[4096];
  for (;;) {
    const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (n == 0) {
      break;
    }
    buf.append(tmp, static_cast<std::size_t>(n));
    if (buf.size() > 8 * 1024 * 1024) {
      break;
    }
  }
  ::close(fd);

  const auto header_end = buf.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    if (error != nullptr) {
      *error = "respuesta HTTP sin cabeceras de " + host + ":" + std::to_string(port) + " " +
               path;
    }
    return false;
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
  if (status_out != nullptr) {
    *status_out = status;
  }
  if (response != nullptr) {
    *response = std::move(resp_body);
  }
  if (status < 200 || status >= 300) {
    if (error != nullptr) {
      std::string detail = response != nullptr ? *response : std::string();
      if (detail.size() > 180) {
        detail.resize(180);
        detail += "…";
      }
      for (char& ch : detail) {
        if (ch == '\n' || ch == '\r') {
          ch = ' ';
        }
      }
      *error = "HTTP " + std::to_string(status) + " " + host + ":" + std::to_string(port) +
               " " + path + (detail.empty() ? std::string() : (": " + detail));
    }
    return false;
  }
  return true;
}

bool wait_health(const std::string& host, int port, int timeout_ms, std::string* error) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  std::string last;
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    std::string body;
    if (http_exchange(host, port, "GET", "/health", {}, 2000, &status, &body, &last)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  if (error != nullptr) {
    *error = last.empty() ? ("timeout /health " + host + ":" + std::to_string(port)) : last;
  }
  return false;
}

struct Target {
  std::string host;
  int chat_port = 8080;
  int embed_port = 18765;
  std::string api_model;
  bool from_env = false;
};

Target resolve_target() {
  Target t;
  tuide::AiSettings settings;
  tuide::apply_ai_runtime_env(&settings);
  if (env_set("TUIDE_L2_API_MODEL")) {
    t.api_model = settings.level2.api_model;
  }
  t.embed_port =
      settings.level0.embeddings.server_port > 0 ? settings.level0.embeddings.server_port : 18765;

  const bool has_base = env_set("TUIDE_L2_API_BASE");
  const bool has_embed = env_set("TUIDE_EMBED_HOST");
  t.from_env = has_base || has_embed || env_flag("TUIDE_LLAMA_LIVE");

  std::string chat_host;
  if (has_base) {
    chat_host = tuide::llama_normalize_host(settings.level2.api_base, &t.chat_port);
    if (t.chat_port <= 0) {
      t.chat_port = 8080;
    }
  }
  std::string embed_host;
  if (has_embed) {
    embed_host = tuide::llama_normalize_host(settings.level0.embeddings.server_host);
  }

  if (!chat_host.empty() && !tuide::llama_host_is_local(chat_host)) {
    t.host = chat_host;
  } else if (!embed_host.empty() && !tuide::llama_host_is_local(embed_host)) {
    t.host = embed_host;
  }

  if (t.host.empty()) {
    const char* candidates[] = {"192.168.64.1", "host.orb.internal"};
    for (const char* c : candidates) {
      if (tcp_ok(c, t.chat_port, 400) || tcp_ok(c, t.embed_port, 400)) {
        t.host = c;
        break;
      }
    }
  }
  return t;
}

struct SpeedRow {
  std::string name;
  bool ok = false;
  std::string error;
  long wall_ms = 0;
  int prompt_n = 0;
  int predicted_n = 0;
  double prompt_per_second = 0;
  double predicted_per_second = 0;
  std::string content;
};

std::string pad_code(std::size_t chars) {
  static const char kBlock[] =
      "int widget_count(const Panel& p) {\n"
      "  int n = 0;\n"
      "  for (const auto& child : p.children()) {\n"
      "    n += child.visible() ? 1 : 0;\n"
      "  }\n"
      "  return n;\n"
      "}\n";
  std::string s;
  s.reserve(chars + 64);
  while (s.size() < chars) {
    s += kBlock;
  }
  s.resize(chars);
  return s;
}

void fill_speed_from_json(const nlohmann::json& j, SpeedRow* row) {
  if (j.contains("timings") && j["timings"].is_object()) {
    const auto& t = j["timings"];
    if (t.contains("prompt_n") && t["prompt_n"].is_number()) {
      row->prompt_n = t["prompt_n"].get<int>();
    }
    if (t.contains("predicted_n") && t["predicted_n"].is_number()) {
      row->predicted_n = t["predicted_n"].get<int>();
    }
    if (t.contains("prompt_per_second") && t["prompt_per_second"].is_number()) {
      row->prompt_per_second = t["prompt_per_second"].get<double>();
    }
    if (t.contains("predicted_per_second") && t["predicted_per_second"].is_number()) {
      row->predicted_per_second = t["predicted_per_second"].get<double>();
    }
  }
  if (j.contains("usage") && j["usage"].is_object()) {
    const auto& u = j["usage"];
    if (row->prompt_n == 0 && u.contains("prompt_tokens") && u["prompt_tokens"].is_number()) {
      row->prompt_n = u["prompt_tokens"].get<int>();
    }
    if (row->predicted_n == 0 && u.contains("completion_tokens") &&
        u["completion_tokens"].is_number()) {
      row->predicted_n = u["completion_tokens"].get<int>();
    }
  }
  if (row->predicted_per_second <= 0 && row->predicted_n > 0 && row->wall_ms > 0) {
    row->predicted_per_second =
        1000.0 * static_cast<double>(row->predicted_n) / static_cast<double>(row->wall_ms);
  }
}

SpeedRow chat_bench(const std::string& host, int port, const std::string& model,
                    const std::string& name, const std::string& system, const std::string& user,
                    int max_tokens) {
  SpeedRow row;
  row.name = name;
  nlohmann::json payload = {
      {"model", model},
      {"temperature", 0.0},
      {"max_tokens", max_tokens},
      {"messages", nlohmann::json::array({{{"role", "system"}, {"content", system}},
                                          {{"role", "user"}, {"content", user}}})},
  };
  int status = 0;
  std::string body;
  std::string err;
  const auto t0 = std::chrono::steady_clock::now();
  const bool ok =
      http_exchange(host, port, "POST", "/v1/chat/completions", payload.dump(), 180000, &status,
                    &body, &err);
  row.wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
  if (!ok) {
    row.error = err;
    return row;
  }
  try {
    const auto j = nlohmann::json::parse(body);
    fill_speed_from_json(j, &row);
    std::string content;
    std::string parse_err;
    if (tuide::parse_llama_chat_completion(body, &content, &parse_err)) {
      row.content = std::move(content);
      row.ok = true;
    } else {
      row.error = parse_err;
    }
  } catch (const std::exception& ex) {
    row.error = std::string("json: ") + ex.what();
  }
  return row;
}

void print_speed_row(const SpeedRow& row) {
  std::cout << "  " << std::left << std::setw(22) << row.name << std::right << std::setw(8)
            << row.wall_ms << " ms  prompt=" << std::setw(6) << row.prompt_n
            << "  gen=" << std::setw(4) << row.predicted_n;
  std::cout << "  pp=";
  if (row.prompt_per_second > 0) {
    std::cout << std::fixed << std::setprecision(1) << std::setw(8) << row.prompt_per_second;
  } else {
    std::cout << std::setw(8) << "-";
  }
  std::cout << " t/s  tg=";
  if (row.predicted_per_second > 0) {
    std::cout << std::fixed << std::setprecision(1) << std::setw(6) << row.predicted_per_second;
  } else {
    std::cout << std::setw(6) << "-";
  }
  std::cout << " t/s";
  if (!row.ok) {
    std::cout << "  FAIL " << row.error;
  }
  std::cout << '\n';
}

double median_positive(std::vector<double> v) {
  std::vector<double> ok;
  for (double x : v) {
    if (x > 0 && std::isfinite(x)) {
      ok.push_back(x);
    }
  }
  if (ok.empty()) {
    return 0;
  }
  std::sort(ok.begin(), ok.end());
  return ok[ok.size() / 2];
}

int run_speed_bench(const std::string& host, int port, const std::string& model) {
  std::cout << "=== speed host=" << host << ":" << port << " model=" << model << " ===\n";
  std::cout << std::left << std::setw(24) << "  case" << std::right << " wall     tokens"
            << "              prefill           decode\n";

  const auto warmup =
      chat_bench(host, port, model, "warmup", "Reply with the single word pong.", "ping", 8);
  print_speed_row(warmup);
  expect(warmup.ok, "speed warmup: " + warmup.error);

  std::vector<double> tg;
  for (int i = 1; i <= 3; ++i) {
    const auto row = chat_bench(
        host, port, model, "decode_" + std::to_string(i),
        "You are a C++ assistant. Output identifiers only, one per line, no prose.",
        "List 40 valid C++ function names related to a terminal UI editor.", 64);
    print_speed_row(row);
    expect(row.ok, "speed decode_" + std::to_string(i) + ": " + row.error);
    tg.push_back(row.predicted_per_second);
  }

  const std::string l2_user =
      "Project code follows. Reply with JSON {\"action\":\"done\",\"note\":\"ok\"} only.\n\n" +
      pad_code(10000);
  const auto l2_cold =
      chat_bench(host, port, model, "prefill_l2_cold",
                 "You output a single JSON object with key action.", l2_user, 24);
  print_speed_row(l2_cold);
  expect(l2_cold.ok, "speed prefill_l2_cold: " + l2_cold.error);

  const auto l2_cached =
      chat_bench(host, port, model, "prefill_l2_cached",
                 "You output a single JSON object with key action.", l2_user, 24);
  print_speed_row(l2_cached);
  expect(l2_cached.ok, "speed prefill_l2_cached: " + l2_cached.error);

  const std::string remote_user =
      "Project pack follows. Reply with JSON {\"action\":\"done\",\"note\":\"ok\"} only.\n\n" +
      pad_code(36000);
  const auto remote_cold =
      chat_bench(host, port, model, "prefill_remote_cold",
                 "You output a single JSON object with key action.", remote_user, 24);
  print_speed_row(remote_cold);
  expect(remote_cold.ok, "speed prefill_remote_cold: " + remote_cold.error);

  const double med_tg = median_positive(std::move(tg));
  std::cout << std::setprecision(1) << std::fixed;
  std::cout << "  median decode tg=" << med_tg << " t/s\n";
  if (l2_cold.prompt_per_second > 0) {
    std::cout << "  prefill L2 ~10k chars cold=" << l2_cold.prompt_per_second << " t/s";
    if (l2_cached.prompt_per_second > 0) {
      std::cout << "  cached=" << l2_cached.prompt_per_second << " t/s";
    }
    std::cout << '\n';
  }
  if (remote_cold.prompt_per_second > 0) {
    std::cout << "  prefill remote ~36k chars cold=" << remote_cold.prompt_per_second << " t/s "
              << "(prompt_tok=" << remote_cold.prompt_n << ")\n";
  }
  if (warmup.ok && !warmup.content.empty()) {
    std::cout << "  warmup content=" << warmup.content << '\n';
  }
  return failures > 0 ? 1 : 0;
}

int skip(const std::string& reason) {
  std::cout << "SKIP llama_host_comm_test: " << reason << '\n';
  std::cout << "  Arranca el host con ./tools/run_host_llama.sh y, en la VM:\n";
  std::cout << "  export TUIDE_L2_API_BASE=http://192.168.64.1:8080/v1\n";
  std::cout << "  export TUIDE_EMBED_HOST=192.168.64.1\n";
  std::cout << "  (o TUIDE_LLAMA_LIVE=1 para fallar si no hay servidor)\n";
  return 0;
}

int main(int argc, char** argv) {
  bool speed = env_flag("TUIDE_LLAMA_SPEED");
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--speed") {
      speed = true;
    }
  }

  Target t = resolve_target();
  if (speed) {
    t.from_env = true;
  }
  if (t.host.empty()) {
    if (t.from_env) {
      std::cerr << "FAIL: no hay llama-server remoto (TUIDE_L2_API_BASE / TUIDE_EMBED_HOST / "
                   "TUIDE_LLAMA_LIVE)\n";
      return 1;
    }
    return skip("ningún host UTM/OrbStack responde en :8080/:18765");
  }
  if (tuide::llama_host_is_local(t.host)) {
    return skip("host local (" + t.host + "); este test cubre el llama-server fuera de la VM");
  }

  std::cout << "llama_host_comm_test host=" << t.host << " chat=:" << t.chat_port
            << " embed=:" << t.embed_port << '\n';

  const bool chat_up = tcp_ok(t.host, t.chat_port, 800);
  const bool embed_up = tcp_ok(t.host, t.embed_port, 800);
  if (!chat_up && !embed_up) {
    if (t.from_env) {
      std::cerr << "FAIL: " << t.host << " no acepta TCP en :" << t.chat_port << " ni :"
                << t.embed_port << '\n';
      return 1;
    }
    return skip("TCP cerrado en " + t.host);
  }
  expect(chat_up, "TCP chat " + t.host + ":" + std::to_string(t.chat_port));
  expect(embed_up, "TCP embed " + t.host + ":" + std::to_string(t.embed_port));

  const int health_wait_ms = t.from_env ? 60000 : 15000;
  if (chat_up) {
    std::string err;
    expect(wait_health(t.host, t.chat_port, health_wait_ms, &err),
           "GET /health chat: " + err);
  }
  if (embed_up) {
    std::string err;
    expect(wait_health(t.host, t.embed_port, health_wait_ms, &err),
           "GET /health embed: " + err);
  }

  std::string model = t.api_model;
  if (chat_up) {
    int status = 0;
    std::string body;
    std::string err;
    if (http_exchange(t.host, t.chat_port, "GET", "/v1/models", {}, 5000, &status, &body, &err)) {
      try {
        const auto j = nlohmann::json::parse(body);
        if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
          const auto& d0 = j["data"][0];
          if (d0.contains("id") && d0["id"].is_string()) {
            const std::string id = d0["id"].get<std::string>();
            expect(!id.empty(), "GET /v1/models id vacío");
            if (model.empty()) {
              model = id;
            }
            std::cout << "  chat /v1/models id=" << id << '\n';
            if (!env_set("TUIDE_L2_API_MODEL")) {
              model = id;
            }
          } else {
            expect(false, "GET /v1/models sin data[0].id");
          }
        } else {
          expect(false, "GET /v1/models sin data[]");
        }
      } catch (const std::exception& ex) {
        expect(false, std::string("GET /v1/models json: ") + ex.what());
      }
    } else {
      expect(false, "GET /v1/models: " + err);
    }
  }

  if (speed) {
    if (!chat_up) {
      std::cerr << "FAIL: --speed necesita llama-server chat en " << t.host << ":" << t.chat_port
                << '\n';
      return 1;
    }
    if (model.empty()) {
      model = "qwen2.5-coder-14b-instruct-q4_k_m";
    }
    const int rc = run_speed_bench(t.host, t.chat_port, model);
    if (failures > 0) {
      std::cerr << failures << " failure(s)\n";
      return 1;
    }
    std::cout << "llama_host_comm_test speed ok\n";
    return rc;
  }

  if (embed_up) {
    tuide::AiSettings settings;
    settings.level0.embeddings.server_host = t.host;
    settings.level0.embeddings.server_port = t.embed_port;
    settings.level0.embeddings.auto_download = false;
    tuide::apply_ai_runtime_env(&settings);
    settings.level0.embeddings.server_host = t.host;
    settings.level0.embeddings.server_port = t.embed_port;

    tuide::EmbeddingBackend embed;
    std::string err;
    const bool ready = embed.ensure_ready(
        settings, [](const std::string& line) { std::cout << "  " << line << '\n'; }, &err);
    expect(ready, "EmbeddingBackend attach: " + err);
    if (ready) {
      std::vector<float> vec;
      const bool ok = embed.embed_raw("ping", &vec, &err);
      expect(ok, "embed_raw ping: " + err);
      expect(ok && vec.size() >= 8, "embed dim=" + std::to_string(vec.size()));
      if (ok) {
        std::cout << "  embed dim=" << vec.size() << '\n';
      }
    }
  }

  if (chat_up && !env_flag("TUIDE_LLAMA_SKIP_GENERATE")) {
    if (model.empty()) {
      model = "qwen2.5-coder-7b-instruct";
    }
    nlohmann::json payload = {
        {"model", model},
        {"temperature", 0.0},
        {"max_tokens", 8},
        {"messages",
         nlohmann::json::array(
             {{{"role", "system"}, {"content", "Reply with the single word pong."}},
              {{"role", "user"}, {"content", "ping"}}})},
    };
    int status = 0;
    std::string body;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = http_exchange(t.host, t.chat_port, "POST", "/v1/chat/completions",
                                  payload.dump(), 120000, &status, &body, &err);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    expect(ok, "POST /v1/chat/completions: " + err);
    if (ok) {
      std::string content;
      std::string parse_err;
      expect(tuide::parse_llama_chat_completion(body, &content, &parse_err),
             "parse chat: " + parse_err);
      expect(!content.empty(), "chat content vacío");
      std::cout << "  chat model=" << model << " " << ms << "ms content=" << content << '\n';
    }
  }

  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "llama_host_comm_test ok\n";
  return 0;
}
