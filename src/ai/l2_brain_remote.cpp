#include "ai/l2_brain.hpp"
#include "ai/llama_net.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

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

std::string trim_trailing_slash(std::string s) {
  while (!s.empty() && (s.back() == '/' || s.back() == ' ')) {
    s.pop_back();
  }
  return s;
}

std::string resolve_api_key(const AiLevel2Settings& cfg) {
  if (!cfg.api_key.empty()) {
    return cfg.api_key;
  }
  if (const char* env = std::getenv("TUIDE_L2_API_KEY")) {
    if (env[0] != '\0') {
      return env;
    }
  }
  if (const char* env = std::getenv("OPENAI_API_KEY")) {
    if (env[0] != '\0') {
      return env;
    }
  }
  return {};
}

// Prefer last {"action":…} object in noisy responses.
std::string extract_action_json(const std::string& raw) {
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
  const auto brace = raw.find('{');
  if (brace == std::string::npos) {
    return raw;
  }
  return raw.substr(brace);
}

}  // namespace

bool RemoteL2Brain::ensure_ready(const AiSettings& settings,
                                 const std::function<void(const std::string&)>& on_progress,
                                 std::string* error) {
  cfg_ = settings.level2;
  {
    AiSettings overlay;
    overlay.level2 = cfg_;
    apply_ai_runtime_env(&overlay);
    cfg_ = overlay.level2;
  }
  cfg_.api_base = trim_trailing_slash(cfg_.api_base);
  if (cfg_.api_base.empty()) {
    if (error) {
      *error = "L2 remote: ai.level2.api_base vacío";
    }
    return false;
  }
  if (cfg_.api_model.empty()) {
    if (error) {
      *error = "L2 remote: ai.level2.api_model vacío";
    }
    return false;
  }
  // Local OpenAI-compatible servers often need no key; cloud needs one.
  (void)resolve_api_key(cfg_);
  if (on_progress) {
    on_progress("L2 remote ready: " + cfg_.api_base + " model=" + cfg_.api_model);
  }
  return true;
}

L2BrainResult RemoteL2Brain::propose(const L2BrainRequest& req, std::atomic<bool>* cancel) {
  L2BrainResult out;
  out.backend = "remote";
  if (cancel != nullptr && cancel->load()) {
    out.error = "cancelado";
    return out;
  }

  nlohmann::json body = {
      {"model", cfg_.api_model},
      {"temperature", req.temperature},
      {"max_tokens", req.max_tokens},
      {"messages",
       nlohmann::json::array(
           {{{"role", "system"}, {"content", req.system_prompt}},
            {{"role", "user"}, {"content", req.user_prompt}}})},
  };

  const fs::path tmp_dir = fs::temp_directory_path() / "tuide_l2_remote";
  std::error_code ec;
  fs::create_directories(tmp_dir, ec);
  const fs::path payload_path = tmp_dir / ("req_" + std::to_string(::getpid()) + ".json");
  {
    std::ofstream outf(payload_path, std::ios::binary | std::ios::trunc);
    if (!outf) {
      out.error = "no se pudo escribir payload temporal";
      return out;
    }
    outf << body.dump();
  }

  const std::string url = cfg_.api_base + "/chat/completions";
  const std::string key = resolve_api_key(cfg_);
  std::ostringstream cmd;
  cmd << "curl -sS --max-time 300 -X POST " << shell_quote(url)
      << " -H 'Content-Type: application/json'";
  if (!key.empty()) {
    cmd << " -H " << shell_quote("Authorization: Bearer " + key);
  }
  cmd << " --data-binary @" << shell_quote(payload_path.string());

  FILE* pipe = ::popen(cmd.str().c_str(), "r");
  if (pipe == nullptr) {
    out.error = "curl popen falló";
    fs::remove(payload_path, ec);
    return out;
  }
  std::ostringstream raw;
  std::array<char, 4096> buf{};
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    if (cancel != nullptr && cancel->load()) {
      break;
    }
    raw << buf.data();
  }
  const int status = pclose(pipe);
  fs::remove(payload_path, ec);
  if (cancel != nullptr && cancel->load()) {
    out.error = "cancelado";
    return out;
  }
  if (status != 0) {
    out.error = "curl exit=" + std::to_string(WEXITSTATUS(status)) + " body=" + raw.str().substr(0, 400);
    return out;
  }

  try {
    const auto j = nlohmann::json::parse(raw.str());
    if (j.contains("error")) {
      out.error = j["error"].dump();
      return out;
    }
    std::string content;
    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
      const auto& c0 = j["choices"][0];
      if (c0.contains("message") && c0["message"].contains("content") &&
          c0["message"]["content"].is_string()) {
        content = c0["message"]["content"].get<std::string>();
      } else if (c0.contains("text") && c0["text"].is_string()) {
        content = c0["text"].get<std::string>();
      }
    }
    if (content.empty()) {
      out.error = "respuesta remote sin content";
      return out;
    }
    out.ok = true;
    out.text = extract_action_json(content);
    return out;
  } catch (const std::exception& ex) {
    out.error = std::string("parse remote: ") + ex.what() + " raw=" + raw.str().substr(0, 400);
    return out;
  }
}

std::unique_ptr<L2Brain> make_l2_brain(const std::string& mode, LlamaBackend* shared_backend) {
  if (mode == "local") {
    return std::make_unique<LocalL2Brain>(shared_backend);
  }
  if (mode == "remote") {
    return std::make_unique<RemoteL2Brain>();
  }
  return nullptr;
}

}  // namespace tuide
