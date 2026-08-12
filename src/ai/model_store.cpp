#include "ai/model_store.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

bool download_url_to_file(const std::string& url, const std::string& dest,
                          const ModelStore::ProgressFn& on_progress, std::string* error) {
  if (url.empty() || dest.empty()) {
    if (error) {
      *error = "url/dest vacíos";
    }
    return false;
  }
  std::error_code ec;
  fs::create_directories(fs::path(dest).parent_path(), ec);
  const fs::path tmp = fs::path(dest).string() + ".partial";
  fs::remove(tmp, ec);

  if (on_progress) {
    on_progress("descargando: " + url);
  }

  std::string cmd;
  if (command_exists("curl")) {
    cmd = "curl -fL --retry 3 --progress-bar -o " + shell_quote(tmp.string()) + " " +
          shell_quote(url);
  } else if (command_exists("wget")) {
    cmd = "wget -O " + shell_quote(tmp.string()) + " " + shell_quote(url);
  } else {
    if (error) {
      *error = "hace falta curl o wget para descargar modelos";
    }
    return false;
  }

  std::string out;
  if (!run_shell(cmd, &out)) {
    fs::remove(tmp, ec);
    if (error) {
      *error = "download failed: " + out;
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
  if (!download_url_to_file(info.url, path, on_progress, error)) {
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
  if (!download_url_to_file(info.url, path, on_progress, error)) {
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
