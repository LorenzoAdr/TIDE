#include "ai/l2_brain.hpp"

#include <cstdlib>
#include <unistd.h>

#include "ai/model_store.hpp"

namespace tuide {

LocalL2Brain::LocalL2Brain(LlamaBackend* backend) : backend_(backend) {
  if (backend_ == nullptr) {
    backend_ = &owned_;
  }
}

bool LocalL2Brain::ensure_ready(const AiSettings& settings,
                                const std::function<void(const std::string&)>& on_progress,
                                std::string* error) {
  ModelStore store(settings.models_cache_dir.empty() ? ModelStore::default_cache_dir()
                                                     : settings.models_cache_dir);

  std::string cli = settings.level2.cli_path;
  if (cli.empty()) {
    cli = settings.level1.cli_path;
  }
  if (cli.empty()) {
    cli = store.resolve_llama_cli();
    if (cli.empty() || !store.cli_runnable(cli)) {
      const bool auto_dl = settings.level2.auto_download || settings.level1.auto_download;
      cli = store.ensure_llama_cli(auto_dl, on_progress, error);
    }
  }
  if (!store.cli_runnable(cli)) {
    if (error && error->empty()) {
      *error = "L2 local: llama-cli no ejecutable (instala ai-runtime)";
    }
    return false;
  }
  backend_->set_cli_path(cli);

  std::string model = settings.level2.model_path;
  if (model.empty()) {
    AiModelInfo info = default_l2_model();
    if (settings.level2.model_id == default_l2_model_small().id) {
      info = default_l2_model_small();
    } else if (!settings.level2.model_id.empty() && settings.level2.model_id != info.id) {
      const std::string alt = store.l2_model_path_for_id(settings.level2.model_id);
      if (::access(alt.c_str(), R_OK) == 0) {
        model = alt;
      } else if (error) {
        *error = "L2 local: modelo custom ausente: " + alt +
                 " (usa model_path o id qwen2.5-coder-7b/1.5b)";
        return false;
      } else {
        return false;
      }
    }
    if (model.empty()) {
      model = store.ensure_l2_model(info, settings.level2.auto_download, on_progress, error);
    }
  }
  if (model.empty() || ::access(model.c_str(), R_OK) != 0) {
    if (error && error->empty()) {
      *error =
          "L2 local: modelo ausente (instala paquete ai-l2 o fija ai.level2.model_path)";
    }
    return false;
  }
  backend_->set_model_path(model);
  if (on_progress) {
    on_progress("L2 local ready: " + model);
  }
  return true;
}

L2BrainResult LocalL2Brain::propose(const L2BrainRequest& req, std::atomic<bool>* cancel) {
  L2BrainResult out;
  out.backend = "local";
  if (backend_ == nullptr || !backend_->ready()) {
    out.error = "L2 local backend no ready";
    return out;
  }
  LlamaCompletionRequest creq;
  creq.system_prompt = req.system_prompt;
  creq.user_prompt = req.user_prompt;
  creq.max_tokens = req.max_tokens;
  creq.n_ctx = req.n_ctx;
  creq.temperature = req.temperature;
  const LlamaCompletionResult cr = backend_->complete(creq, cancel);
  out.ok = cr.ok;
  out.text = cr.text;
  out.error = cr.error;
  return out;
}

}  // namespace tuide
