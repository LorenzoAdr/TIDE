#include "ai/model_store.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace tuide {
namespace {

constexpr const char* kLlamaReleaseTag = "b10333";

bool command_exists(const std::string& name) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return false;
  }
  std::string path = path_env;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find(':', start);
    const std::string dir =
        path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!dir.empty()) {
      const fs::path candidate = fs::path(dir) / name;
      if (::access(candidate.c_str(), X_OK) == 0) {
        return true;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return false;
}

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

std::string find_on_path(const std::string& name) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return {};
  }
  std::string path = path_env;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find(':', start);
    const std::string dir =
        path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!dir.empty()) {
      const fs::path candidate = fs::path(dir) / name;
      if (::access(candidate.c_str(), X_OK) == 0) {
        return candidate.string();
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return {};
}

bool run_shell(const std::string& cmd, std::string* stderr_out) {
  FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
  if (pipe == nullptr) {
    if (stderr_out) {
      *stderr_out = "popen failed";
    }
    return false;
  }
  std::array<char, 4096> buf{};
  std::string out;
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    out += buf.data();
  }
  const int status = pclose(pipe);
  if (stderr_out) {
    *stderr_out = out;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

AiModelInfo default_l1_model() {
  AiModelInfo info;
  info.id = "qwen2.5-1.5b-instruct-q4_k_m";
  info.filename = "qwen2.5-1.5b-instruct-q4_k_m.gguf";
  info.url =
      "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/"
      "qwen2.5-1.5b-instruct-q4_k_m.gguf";
  info.license_note = "Apache-2.0 (Qwen2.5)";
  info.approx_bytes = 1100000000ull;
  return info;
}

AiModelInfo default_intent_embed_model() {
  AiModelInfo info;
  // Converted for llama.cpp (not CrispEmbed). Multilingual enough for ES/EN intents.
  info.id = "nomic-embed-text-v1.5-q4_k_m";
  info.filename = "nomic-embed-text-v1.5.Q4_K_M.gguf";
  info.url =
      "https://huggingface.co/nomic-ai/nomic-embed-text-v1.5-GGUF/resolve/main/"
      "nomic-embed-text-v1.5.Q4_K_M.gguf";
  info.license_note = "Apache-2.0 (nomic-embed-text-v1.5)";
  info.approx_bytes = 84106624ull;
  return info;
}

std::optional<int> parse_download_percent_token(std::string_view text) {
  const auto pct = text.rfind('%');
  if (pct == std::string_view::npos || pct == 0) {
    return std::nullopt;
  }
  std::size_t end = pct;
  while (end > 0 && (std::isdigit(static_cast<unsigned char>(text[end - 1])) != 0 ||
                     text[end - 1] == '.')) {
    --end;
  }
  if (end == pct) {
    return std::nullopt;
  }
  while (end < pct && std::isdigit(static_cast<unsigned char>(text[end])) == 0 &&
         text[end] != '.') {
    ++end;
  }
  if (end >= pct) {
    return std::nullopt;
  }
  try {
    const int value = static_cast<int>(std::stof(std::string(text.substr(end, pct - end))));
    if (value < 0 || value > 100) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

void report_download_percent(const ModelStore::ProgressFn& on_progress, int percent) {
  if (!on_progress) {
    return;
  }
  on_progress("__pct__:" + std::to_string(std::clamp(percent, 0, 100)));
}

std::uint64_t probe_url_content_length(const std::string& url) {
  if (!command_exists("curl")) {
    return 0;
  }
  std::string out;
  if (!run_shell("curl -fsIL --retry 2 --connect-timeout 8 " + shell_quote(url), &out)) {
    return 0;
  }
  // Last Content-Length wins (redirects).
  std::uint64_t best = 0;
  std::istringstream iss(out);
  std::string line;
  while (std::getline(iss, line)) {
    if (line.size() >= 15) {
      std::string lower = line;
      for (char& ch : lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      }
      if (lower.rfind("content-length:", 0) == 0) {
        try {
          const auto n = std::stoull(line.substr(15));
          if (n > 1024) {
            best = n;
          }
        } catch (...) {
        }
      }
    }
  }
  return best;
}

int run_download_with_progress(const std::string& command, const ModelStore::ProgressFn& on_progress,
                               const fs::path& partial_path, std::uint64_t expected_size) {
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return 127;
  }

  std::atomic<bool> done{false};
  std::thread size_poller;
  if (on_progress && expected_size > 0) {
    size_poller = std::thread([&] {
      int last_reported = -1;
      while (!done.load(std::memory_order_acquire)) {
        std::error_code ec;
        const auto sz = fs::file_size(partial_path, ec);
        if (!ec && expected_size > 0) {
          const int pct = static_cast<int>(
              std::min<std::uint64_t>(100, (sz * 100) / expected_size));
          if (pct != last_reported) {
            last_reported = pct;
            report_download_percent(on_progress, pct);
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    });
  }

  std::string acc;
  int last_reported = -1;
  int ch = 0;
  auto flush_token = [&]() {
    if (!on_progress || acc.empty() || expected_size > 0) {
      return;
    }
    const auto pct = parse_download_percent_token(acc);
    if (!pct.has_value() || *pct == last_reported) {
      return;
    }
    last_reported = *pct;
    report_download_percent(on_progress, *pct);
  };
  while ((ch = std::fgetc(pipe)) != EOF) {
    if (ch == '\r' || ch == '\n') {
      flush_token();
      acc.clear();
      continue;
    }
    acc.push_back(static_cast<char>(ch));
    if (ch == '%') {
      flush_token();
    }
    if (acc.size() > 4096) {
      acc.erase(0, acc.size() - 256);
    }
  }
  flush_token();
  const int status = ::pclose(pipe);
  done.store(true, std::memory_order_release);
  if (size_poller.joinable()) {
    size_poller.join();
  }
  if (status == -1) {
    return 127;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 127;
}

bool download_url_to_file(const std::string& url, const std::string& dest,
                          const ModelStore::ProgressFn& on_progress, std::string* error,
                          std::uint64_t expected_size) {
  if (url.empty() || dest.empty()) {
    if (error) {
      *error = "url/dest vacíos";
    }
    return false;
  }
  // Serialize model/runtime downloads so a second chat line cannot race the same .partial.
  static std::mutex download_mu;
  std::lock_guard<std::mutex> lock(download_mu);

  std::error_code ec;
  fs::create_directories(fs::path(dest).parent_path(), ec);
  const fs::path tmp = fs::path(dest).string() + ".partial";
  fs::remove(tmp, ec);

  if (on_progress) {
    on_progress("descargando: " + url);
  }
  report_download_percent(on_progress, 0);

  std::uint64_t size_hint = expected_size;
  if (size_hint == 0) {
    size_hint = probe_url_content_length(url);
  }

  std::string cmd;
  if (command_exists("curl")) {
    if (size_hint > 0) {
      cmd = "curl -fL --retry 3 --connect-timeout 20 --retry-delay 1 -o " +
            shell_quote(tmp.string()) + " " + shell_quote(url) + " 2>/dev/null";
    } else {
      cmd = "curl -fL --retry 3 --connect-timeout 20 --progress-bar -o " +
            shell_quote(tmp.string()) + " " + shell_quote(url) + " 2>&1";
    }
  } else if (command_exists("wget")) {
    if (size_hint > 0) {
      cmd = "wget -q -O " + shell_quote(tmp.string()) + " " + shell_quote(url) + " 2>/dev/null";
    } else {
      cmd = "wget --progress=bar:force -O " + shell_quote(tmp.string()) + " " + shell_quote(url) +
            " 2>&1";
    }
  } else {
    if (error) {
      *error = "hace falta curl o wget para descargar modelos";
    }
    return false;
  }

  const int rc = run_download_with_progress(cmd, on_progress, tmp, size_hint);
  if (rc != 0 || !fs::is_regular_file(tmp, ec)) {
    fs::remove(tmp, ec);
    if (error) {
      *error = "download failed (rc=" + std::to_string(rc) + ")";
    }
    return false;
  }

  fs::rename(tmp, dest, ec);
  if (ec) {
    if (error) {
      *error = "rename failed: " + ec.message();
    }
    return false;
  }
  report_download_percent(on_progress, 100);
  if (on_progress) {
    on_progress("ok: " + dest);
  }
  return true;
}

ModelStore::ModelStore(std::string cache_dir) {
  cache_dir_ = cache_dir.empty() ? default_cache_dir() : std::move(cache_dir);
}

std::string ModelStore::default_cache_dir() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg != nullptr && xdg[0] != '\0') {
    return (fs::path(xdg) / "tuide" / "models").string();
  }
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return (fs::path(home) / ".cache" / "tuide" / "models").string();
  }
  return "tuide-models";
}

std::string ModelStore::model_path(const AiModelInfo& info) const {
  return (fs::path(cache_dir_) / "l1" / info.filename).string();
}

std::string ModelStore::model_path_for_id(const std::string& id) const {
  if (id.empty() || id == default_l1_model().id) {
    return model_path(default_l1_model());
  }
  return (fs::path(cache_dir_) / "l1" / id).string();
}

bool ModelStore::has_model(const AiModelInfo& info) const {
  std::error_code ec;
  const auto p = model_path(info);
  return fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 1024 * 1024;
}

std::string ModelStore::ensure_model(const AiModelInfo& info, bool auto_download,
                                     const ProgressFn& on_progress, std::string* error) const {
  const std::string path = model_path(info);
  if (has_model(info)) {
    return path;
  }
  if (!auto_download) {
    if (error) {
      *error = "modelo ausente: " + path + " (ai.level1.auto_download=false)";
    }
    return {};
  }
  if (on_progress) {
    on_progress("ModelStore: falta " + info.filename + " (~" +
                std::to_string(info.approx_bytes / (1024 * 1024)) + " MB, " + info.license_note +
                ")");
  }
  if (!download_url_to_file(info.url, path, on_progress, error, info.approx_bytes)) {
    return {};
  }
  return path;
}

std::string ModelStore::intent_embed_model_path(const AiModelInfo& info) const {
  return (fs::path(cache_dir_) / "embed" / "intent" / info.filename).string();
}

bool ModelStore::has_intent_embed_model(const AiModelInfo& info) const {
  std::error_code ec;
  const auto p = intent_embed_model_path(info);
  return fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 1024 * 1024;
}

std::string ModelStore::ensure_intent_embed_model(const AiModelInfo& info, bool auto_download,
                                                  const ProgressFn& on_progress,
                                                  std::string* error) const {
  const std::string path = intent_embed_model_path(info);
  if (has_intent_embed_model(info)) {
    return path;
  }
  if (!auto_download) {
    if (error) {
      *error = "modelo intent embed ausente: " + path +
               " (ai.level0.embeddings.auto_download=false)";
    }
    return {};
  }
  if (on_progress) {
    on_progress("ModelStore: falta intent embed " + info.filename + " (~" +
                std::to_string(info.approx_bytes / (1024 * 1024)) + " MB, " + info.license_note +
                ")");
  }
  if (!download_url_to_file(info.url, path, on_progress, error, info.approx_bytes)) {
    return {};
  }
  return path;
}

std::string ModelStore::runtime_dir() const {
  return (fs::path(cache_dir_) / "runtime").string();
}

bool ModelStore::prefer_vulkan_llama_bundle() const {
  if (const char* cpu = std::getenv("TUIDE_LLAMA_CPU"); cpu != nullptr && cpu[0] == '1') {
    return false;
  }
  if (const char* vk = std::getenv("TUIDE_LLAMA_VULKAN"); vk != nullptr && vk[0] == '1') {
    return true;
  }
  return false;
}

std::string ModelStore::llama_bundle_dir_for(const bool vulkan) const {
  std::string id = std::string("llama-") + kLlamaReleaseTag;
  if (vulkan) {
    id += "-vulkan";
  }
  return (fs::path(runtime_dir()) / id).string();
}

std::string ModelStore::llama_bundle_dir() const {
  if (prefer_vulkan_llama_bundle() && has_llama_bundle(true)) {
    return llama_bundle_dir_for(true);
  }
  if (has_llama_bundle(false)) {
    return llama_bundle_dir_for(false);
  }
  return prefer_vulkan_llama_bundle() ? llama_bundle_dir_for(true) : llama_bundle_dir_for(false);
}

std::string ModelStore::llama_cli_path_for(const bool vulkan) const {
  return (fs::path(llama_bundle_dir_for(vulkan)) / "llama-cli").string();
}

std::string ModelStore::llama_server_path_for(const bool vulkan) const {
  return (fs::path(llama_bundle_dir_for(vulkan)) / "llama-server").string();
}

std::string ModelStore::llama_cli_path() const {
  return (fs::path(llama_bundle_dir()) / "llama-cli").string();
}

std::string ModelStore::llama_server_path() const {
  return (fs::path(llama_bundle_dir()) / "llama-server").string();
}

namespace {

bool bundle_has_gpu_backend(const fs::path& dir) {
  std::error_code ec;
#if defined(__linux__)
  return fs::exists(dir / "libggml-vulkan.so", ec);
#elif defined(__APPLE__)
  return fs::exists(dir / "libggml-metal.so", ec);
#else
  (void)dir;
  return true;
#endif
}

std::string llama_archive_name(const bool vulkan) {
  const std::string tag = kLlamaReleaseTag;
#if defined(__linux__)
  if (vulkan) {
#if defined(__aarch64__)
    return "llama-" + tag + "-bin-ubuntu-vulkan-arm64.tar.gz";
#else
    return "llama-" + tag + "-bin-ubuntu-vulkan-x64.tar.gz";
#endif
  }
#if defined(__aarch64__)
  return "llama-" + tag + "-bin-ubuntu-arm64.tar.gz";
#else
  return "llama-" + tag + "-bin-ubuntu-x64.tar.gz";
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__)
  return "llama-" + tag + "-bin-macos-arm64.tar.gz";
#else
  return "llama-" + tag + "-bin-macos-x64.tar.gz";
#endif
#else
  return "llama-" + tag + "-bin-ubuntu-x64.tar.gz";
#endif
}

}  // namespace

bool ModelStore::has_llama_bundle(const bool vulkan) const {
  const std::string cli = llama_cli_path_for(vulkan);
  if (!cli_runnable(cli)) {
    return false;
  }
  if (!vulkan) {
    return true;
  }
  return bundle_has_gpu_backend(fs::path(llama_bundle_dir_for(vulkan)));
}

bool ModelStore::has_llama_cli() const {
  if (prefer_vulkan_llama_bundle() && has_llama_bundle(true)) {
    return true;
  }
  return has_llama_bundle(false);
}

bool ModelStore::cli_runnable(const std::string& cli_path) const {
  if (cli_path.empty() || ::access(cli_path.c_str(), X_OK) != 0) {
    return false;
  }
  const fs::path cli(cli_path);
  const fs::path impl = cli.parent_path() / "libllama-cli-impl.so";
  std::error_code ec;
  if (fs::exists(impl, ec)) {
    return true;
  }
  // Broken cache copy: binary under runtime/ without sibling libs.
  const std::string runtime = runtime_dir();
  if (!runtime.empty() && cli_path.rfind(runtime, 0) == 0) {
    return false;
  }
  // System / PATH install: assume loader finds libs.
  return true;
}

bool ModelStore::server_runnable(const std::string& server_path) const {
  if (server_path.empty() || ::access(server_path.c_str(), X_OK) != 0) {
    return false;
  }
  const fs::path server(server_path);
  const fs::path impl = server.parent_path() / "libllama-server-impl.so";
  std::error_code ec;
  if (fs::exists(impl, ec)) {
    return true;
  }
  const std::string runtime = runtime_dir();
  if (!runtime.empty() && server_path.rfind(runtime, 0) == 0) {
    return false;
  }
  return true;
}

bool ModelStore::has_llama_server() const {
  return server_runnable(llama_server_path());
}

std::string ModelStore::library_dir_for_cli(const std::string& cli_path) const {
  if (cli_path.empty()) {
    return {};
  }
  const fs::path dir = fs::path(cli_path).parent_path();
  std::error_code ec;
  if (fs::exists(dir / "libllama-cli-impl.so", ec) ||
      fs::exists(dir / "libllama-server-impl.so", ec)) {
    return dir.string();
  }
  return {};
}

std::string ModelStore::ensure_llama_bundle(const bool vulkan, const bool auto_download,
                                            const ProgressFn& on_progress,
                                            std::string* error) const {
  if (has_llama_bundle(vulkan)) {
    return llama_cli_path_for(vulkan);
  }
  if (!auto_download) {
    return {};
  }

  const std::string archive_name = llama_archive_name(vulkan);
  const std::string url = "https://github.com/ggml-org/llama.cpp/releases/download/" +
                          std::string(kLlamaReleaseTag) + "/" + archive_name;
  const fs::path archive = fs::path(runtime_dir()) / archive_name;
  std::error_code ec;
  fs::create_directories(runtime_dir(), ec);

  if (on_progress) {
    on_progress(vulkan ? "ModelStore: descargando llama.cpp (Vulkan)…"
                         : "ModelStore: descargando llama.cpp (CPU)…");
  }

  if (!fs::is_regular_file(archive, ec) || fs::file_size(archive, ec) < 1024) {
    if (!download_url_to_file(url, archive.string(), on_progress, error)) {
      return {};
    }
  }
  if (on_progress) {
    on_progress("extrayendo bundle llama-cli + llama-server…");
  }
  const std::string extract_cmd =
      "tar -xzf " + shell_quote(archive.string()) + " -C " + shell_quote(runtime_dir());
  std::string out;
  if (!run_shell(extract_cmd, &out)) {
    if (error) {
      *error = "extract failed: " + out;
    }
    return {};
  }

  const fs::path inner = fs::path(runtime_dir()) / (std::string("llama-") + kLlamaReleaseTag);
  const fs::path target = fs::path(llama_bundle_dir_for(vulkan));
  if (inner != target) {
    if (fs::exists(target, ec)) {
      fs::remove_all(target, ec);
    }
    fs::rename(inner, target, ec);
    if (ec) {
      if (error) {
        *error = "rename bundle failed: " + ec.message();
      }
      return {};
    }
  }

  const fs::path legacy = fs::path(runtime_dir()) / "llama-cli";
  if (fs::exists(legacy, ec) && !cli_runnable(legacy.string())) {
    fs::remove(legacy, ec);
  }

  if (!has_llama_bundle(vulkan)) {
    if (error) {
      *error = "llama-cli/libs no aparecieron tras extraer " + archive_name;
    }
    return {};
  }
  return llama_cli_path_for(vulkan);
}

std::string ModelStore::ensure_llama_cli(bool auto_download, const ProgressFn& on_progress,
                                         std::string* error) const {
  if (has_llama_cli()) {
    if (!prefer_vulkan_llama_bundle() || has_llama_bundle(true) || !auto_download) {
      return llama_cli_path();
    }
    if (on_progress) {
      on_progress("ModelStore: actualizando runtime llama.cpp a Vulkan…");
    }
  }
  const std::string on_path = find_on_path("llama-cli");
  if (!on_path.empty() && cli_runnable(on_path)) {
    return on_path;
  }
  if (!auto_download) {
    if (error) {
      *error = "llama-cli no encontrado (PATH ni cache); instálalo o ai.level1.auto_download=true";
    }
    return {};
  }

  if (prefer_vulkan_llama_bundle()) {
    std::string vk_err;
    const std::string vk = ensure_llama_bundle(true, true, on_progress, &vk_err);
    if (!vk.empty()) {
      return vk;
    }
    if (on_progress) {
      on_progress("bundle Vulkan falló" + (vk_err.empty() ? std::string() : (": " + vk_err)) +
                  "; probando CPU…");
    }
  }

  return ensure_llama_bundle(false, true, on_progress, error);
}

std::string ModelStore::resolve_llama_cli() const {
  if (const char* env = std::getenv("TUIDE_LLAMA_CLI"); env != nullptr && env[0] != '\0') {
    if (cli_runnable(env)) {
      return env;
    }
  }
  const std::string on_path = find_on_path("llama-cli");
  if (!on_path.empty() && cli_runnable(on_path)) {
    return on_path;
  }
  if (has_llama_cli()) {
    return llama_cli_path();
  }
  return {};
}

std::string ModelStore::resolve_llama_server() const {
  if (const char* env = std::getenv("TUIDE_LLAMA_SERVER"); env != nullptr && env[0] != '\0') {
    if (server_runnable(env)) {
      return env;
    }
  }
  const std::string on_path = find_on_path("llama-server");
  if (!on_path.empty() && server_runnable(on_path)) {
    return on_path;
  }
  if (has_llama_server()) {
    return llama_server_path();
  }
  // Same bundle as CLI: ensure_llama_cli extracts server too.
  if (has_llama_cli() && has_llama_server()) {
    return llama_server_path();
  }
  return {};
}

void apply_llama_bundle_preference(const AiSettings& settings) {
#if defined(__linux__)
  if (settings.llama_vulkan_bundle) {
    ::setenv("TUIDE_LLAMA_VULKAN", "1", 1);
  } else {
    ::unsetenv("TUIDE_LLAMA_VULKAN");
  }
#else
  (void)settings;
#endif
}

}  // namespace tuide
