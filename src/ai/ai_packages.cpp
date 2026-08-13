#include "ai/ai_packages.hpp"

#include "ai/model_store.hpp"
#include "i18n/tr.hpp"

namespace tuide {

namespace {

ModelStore make_store(const AiSettings& settings) {
  return ModelStore(settings.models_cache_dir.empty() ? ModelStore::default_cache_dir()
                                                      : settings.models_cache_dir);
}

ModelStore::ProgressFn adapt_progress(const AiPackageProgressFn& on_progress) {
  return [on_progress](const std::string& line) {
    if (!on_progress) {
      return;
    }
    if (line.rfind("__pct__:", 0) == 0) {
      try {
        on_progress(std::stoi(line.substr(8)), {});
      } catch (...) {
      }
      return;
    }
    // Text-only stages (extract, resolve): keep a mid/high % so the strip does not
    // clamp -1 → 0% and look stuck after a finished download.
    int pct = 50;
    if (line.find("extrayendo") != std::string::npos || line.find("extract") != std::string::npos) {
      pct = 90;
    } else if (line.find("ok:") == 0 || line.find("listo") != std::string::npos) {
      pct = 100;
    } else if (line.find("descargando") != std::string::npos ||
               line.find("ModelStore: falta") != std::string::npos) {
      pct = 5;
    }
    on_progress(pct, line);
  };
}

}  // namespace

const std::vector<AiPackage>& ai_packages() {
  static const std::vector<AiPackage> kPacks = {
      {"ai-runtime", "settings.toolpacks.ai.runtime", "settings.toolpacks.ai.runtime.detail",
       90ull * 1024ull * 1024ull},
      {"ai-embed", "settings.toolpacks.ai.embed", "settings.toolpacks.ai.embed.detail",
       84ull * 1024ull * 1024ull},
      {"ai-l1", "settings.toolpacks.ai.l1", "settings.toolpacks.ai.l1.detail",
       1100ull * 1024ull * 1024ull},
      {"ai-l2", "settings.toolpacks.ai.l2", "settings.toolpacks.ai.l2.detail",
       4700ull * 1024ull * 1024ull},
  };
  return kPacks;
}

const AiPackage* find_ai_package(const std::string& id) {
  for (const AiPackage& pack : ai_packages()) {
    if (pack.id == id) {
      return &pack;
    }
  }
  return nullptr;
}

AiPackageInstallStatus ai_package_status(const std::string& id, const AiSettings& settings) {
  ModelStore store = make_store(settings);
  apply_llama_bundle_preference(settings);
  if (id == "ai-runtime") {
    return store.has_llama_cli() && store.has_llama_server() ? AiPackageInstallStatus::Installed
                                                             : AiPackageInstallStatus::Missing;
  }
  if (id == "ai-embed") {
    return store.has_intent_embed_model(default_intent_embed_model())
               ? AiPackageInstallStatus::Installed
               : AiPackageInstallStatus::Missing;
  }
  if (id == "ai-l1") {
    return store.has_model(default_l1_model()) ? AiPackageInstallStatus::Installed
                                               : AiPackageInstallStatus::Missing;
  }
  if (id == "ai-l2") {
    AiModelInfo info = default_l2_model();
    if (settings.level2.model_id == default_l2_model_small().id) {
      info = default_l2_model_small();
    }
    return store.has_l2_model(info) ? AiPackageInstallStatus::Installed
                                    : AiPackageInstallStatus::Missing;
  }
  return AiPackageInstallStatus::Missing;
}

std::string first_missing_ai_package_for_embed(const AiSettings& settings) {
  if (ai_package_status("ai-embed", settings) == AiPackageInstallStatus::Missing) {
    return "ai-embed";
  }
  if (ai_package_status("ai-runtime", settings) == AiPackageInstallStatus::Missing) {
    return "ai-runtime";
  }
  return {};
}

std::string first_missing_ai_package_for_l1(const AiSettings& settings) {
  if (ai_package_status("ai-l1", settings) == AiPackageInstallStatus::Missing) {
    return "ai-l1";
  }
  if (ai_package_status("ai-runtime", settings) == AiPackageInstallStatus::Missing) {
    return "ai-runtime";
  }
  return {};
}

std::string first_missing_ai_package_for_l2(const AiSettings& settings) {
  if (ai_package_status("ai-l2", settings) == AiPackageInstallStatus::Missing) {
    return "ai-l2";
  }
  if (ai_package_status("ai-runtime", settings) == AiPackageInstallStatus::Missing) {
    return "ai-runtime";
  }
  return {};
}

AiPackageInstallResult install_ai_package(const std::string& id, const AiSettings& settings,
                                          const AiPackageProgressFn& on_progress) {
  AiPackageInstallResult result;
  if (find_ai_package(id) == nullptr) {
    result.message = i18n::tr("settings.toolpacks.ai.unknown");
    return result;
  }

  AiSettings local = settings;
  apply_llama_bundle_preference(local);
  ModelStore store = make_store(local);
  auto progress = adapt_progress(on_progress);

  auto ensure_runtime = [&]() -> bool {
    if (store.has_llama_cli() && store.has_llama_server()) {
      return true;
    }
    if (on_progress) {
      on_progress(0, i18n::tr("settings.toolpacks.ai.installing_runtime"));
    }
    std::string error;
    const std::string cli = store.ensure_llama_cli(true, progress, &error);
    if (cli.empty()) {
      result.message = error.empty() ? i18n::tr("settings.toolpacks.ai.install_failed") : error;
      return false;
    }
    return true;
  };

  if (id == "ai-runtime") {
    if (!ensure_runtime()) {
      return result;
    }
    result.ok = true;
    result.message = i18n::tr("settings.toolpacks.ai.install_done");
    return result;
  }

  if (id == "ai-embed") {
    if (!ensure_runtime()) {
      return result;
    }
    if (on_progress) {
      on_progress(0, i18n::tr("settings.toolpacks.ai.installing_embed"));
    }
    std::string error;
    const std::string path =
        store.ensure_intent_embed_model(default_intent_embed_model(), true, progress, &error);
    if (path.empty()) {
      result.message = error.empty() ? i18n::tr("settings.toolpacks.ai.install_failed") : error;
      return result;
    }
    result.ok = true;
    result.message = i18n::tr("settings.toolpacks.ai.install_done");
    return result;
  }

  if (id == "ai-l1") {
    if (!ensure_runtime()) {
      return result;
    }
    if (on_progress) {
      on_progress(0, i18n::tr("settings.toolpacks.ai.installing_l1"));
    }
    std::string error;
    const std::string path = store.ensure_model(default_l1_model(), true, progress, &error);
    if (path.empty()) {
      result.message = error.empty() ? i18n::tr("settings.toolpacks.ai.install_failed") : error;
      return result;
    }
    result.ok = true;
    result.message = i18n::tr("settings.toolpacks.ai.install_done");
    return result;
  }

  if (id == "ai-l2") {
    if (!ensure_runtime()) {
      return result;
    }
    if (on_progress) {
      on_progress(0, i18n::tr("settings.toolpacks.ai.installing_l2"));
    }
    AiModelInfo info = default_l2_model();
    if (settings.level2.model_id == default_l2_model_small().id) {
      info = default_l2_model_small();
    }
    std::string error;
    const std::string path = store.ensure_l2_model(info, true, progress, &error);
    if (path.empty()) {
      result.message = error.empty() ? i18n::tr("settings.toolpacks.ai.install_failed") : error;
      return result;
    }
    result.ok = true;
    result.message = i18n::tr("settings.toolpacks.ai.install_done");
    return result;
  }

  result.message = i18n::tr("settings.toolpacks.ai.unknown");
  return result;
}

}  // namespace tuide
