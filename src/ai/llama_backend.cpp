#include "ai/llama_backend.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <sys/wait.h>
#include <unistd.h>

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

// Pull the assistant JSON (or last coherent block) out of noisy llama-cli stdout.
std::string extract_model_text(const std::string& raw) {
  // Prefer the LAST JSON object that looks like an action (models echo the prompt).
  std::size_t search = 0;
  std::string best;
  while (search < raw.size()) {
    const auto start = raw.find("{\"action\"", search);
    if (start == std::string::npos) {
      break;
    }
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
          best = raw.substr(start, i - start + 1);
          break;
        }
      }
    }
    search = start + 1;
  }
  if (!best.empty()) {
    return best;
  }

  const auto start = raw.find('{');
  if (start != std::string::npos) {
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

}  // namespace

LlamaBackend::LlamaBackend() : store_(ModelStore{}) {}

void LlamaBackend::set_model_path(std::string path) {
  model_path_ = std::move(path);
}

void LlamaBackend::set_cli_path(std::string path) {
  cli_path_ = std::move(path);
  lib_dir_ = store_.library_dir_for_cli(cli_path_);
}

bool LlamaBackend::ready() const {
  return !model_path_.empty() && ::access(model_path_.c_str(), R_OK) == 0 &&
         store_.cli_runnable(cli_path_);
}

std::string LlamaBackend::status_text() const {
  std::ostringstream out;
  out << "cli=" << (cli_path_.empty() ? "(none)" : cli_path_) << '\n';
  out << "lib_dir=" << (lib_dir_.empty() ? "(none)" : lib_dir_) << '\n';
  out << "model=" << (model_path_.empty() ? "(none)" : model_path_) << '\n';
  out << "ready=" << (ready() ? "yes" : "no") << '\n';
  out << "cache=" << store_.cache_dir() << '\n';
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
    AiModelInfo info = default_l1_model();
    if (!settings.level1.model_id.empty() && settings.level1.model_id != info.id) {
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

LlamaCompletionResult LlamaBackend::complete(const LlamaCompletionRequest& req,
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
  {
    std::ofstream sys_out(sys_path);
    std::ofstream user_out(user_path);
    if (!sys_out || !user_out) {
      result.error = "no se pudo escribir prompts temporales";
      return result;
    }
    sys_out << req.system_prompt;
    user_out << user.str();
  }

  const int n_ctx = req.n_ctx > 0 ? req.n_ctx : 2048;
  const int n_predict = req.max_tokens > 0 ? req.max_tokens : 512;

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
      << " --single-turn --simple-io --no-display-prompt 2>" << shell_quote(err_path.string());

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
    result.error =
        "contexto L1 insuficiente (sube ai.level1.n_ctx; default ahora 4096)";
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
