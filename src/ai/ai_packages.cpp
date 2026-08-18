#include "ai/ai_packages.hpp"

#include <optional>

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

std::optional<AiModelInfo> l1_info_for_package(const std::string& id) {
  if (id == "ai-l1" || id == "ai-l1-1.5b") {
    return default_l1_model();
  }
  if (id == "ai-l1-3b") {
    return find_l1_model("qwen2.5-3b-instruct-q4_k_m");
  }
  if (id == "ai-l1-7b") {
    return find_l1_model("qwen2.5-7b-instruct-q4_k_m");
  }
  return std::nullopt;
}

std::optional<AiModelInfo> l2_info_for_package(const std::string& id) {
  if (id == "ai-l2" || id == "ai-l2-7b") {
    return default_l2_model();
  }
  if (id == "ai-l2-14b") {
    return find_l2_model("qwen2.5-coder-14b-instruct-q4_k_m");
  }
  if (id == "ai-l2-32b") {
    return find_l2_model("qwen2.5-coder-32b-instruct-q4_k_m");
  }
  return std::nullopt;
}

}  // namespace

const std::vector<AiPackage>& ai_packages() {
  static const std::vector<AiPackage> kPacks = {
      {"ai-runtime", "settings.toolpacks.ai.runtime", "settings.toolpacks.ai.runtime.detail",
       90ull * 1024ull * 1024ull},
      {"ai-embed", "settings.toolpacks.ai.embed", "settings.toolpacks.ai.embed.detail",
       84ull * 1024ull * 1024ull},
      {"ai-l1-1.5b", "settings.toolpacks.ai.l1_1_5b", "settings.toolpacks.ai.l1_1_5b.detail",
       1100ull * 1024ull * 1024ull},
      {"ai-l1-3b", "settings.toolpacks.ai.l1_3b", "settings.toolpacks.ai.l1_3b.detail",
       2100ull * 1024ull * 1024ull},
      {"ai-l1-7b", "settings.toolpacks.ai.l1_7b", "settings.toolpacks.ai.l1_7b.detail",
       4700ull * 1024ull * 1024ull},
      {"ai-l2-7b", "settings.toolpacks.ai.l2_7b", "settings.toolpacks.ai.l2_7b.detail",
       4700ull * 1024ull * 1024ull},
      {"ai-l2-14b", "settings.toolpacks.ai.l2_14b", "settings.toolpacks.ai.l2_14b.detail",
       9000ull * 1024ull * 1024ull},
      {"ai-l2-32b", "settings.toolpacks.ai.l2_32b", "settings.toolpacks.ai.l2_32b.detail",
       19900ull * 1024ull * 1024ull},
  };
  return kPacks;
}

const AiPackage* find_ai_package(const std::string& id) {
  for (const AiPackage& pack : ai_packages()) {
    if (pack.id == id) {
      return &pack;
    }
  }
  // Legacy aliases still installable from toasts / CLI.
  if (id == "ai-l1" || id == "ai-l2") {
    static const AiPackage kLegacyL1 = {
        "ai-l1", "settings.toolpacks.ai.l1_1_5b", "settings.toolpacks.ai.l1_1_5b.detail",
        1100ull * 1024ull * 1024ull};
    static const AiPackage kLegacyL2 = {
        "ai-l2", "settings.toolpacks.ai.l2_7b", "settings.toolpacks.ai.l2_7b.detail",
        4700ull * 1024ull * 1024ull};
    return id == "ai-l1" ? &kLegacyL1 : &kLegacyL2;
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
  if (auto l1 = l1_info_for_package(id)) {
    return store.has_model(*l1) ? AiPackageInstallStatus::Installed
                                : AiPackageInstallStatus::Missing;
  }
  if (auto l2 = l2_info_for_package(id)) {
    return store.has_l2_model(*l2) ? AiPackageInstallStatus::Installed
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
  const std::string pack = ai_package_id_for_l1_model(settings.level1.model_id);
  if (ai_package_status(pack, settings) == AiPackageInstallStatus::Missing) {
    return pack;
  }
  if (ai_package_status("ai-runtime", settings) == AiPackageInstallStatus::Missing) {
    return "ai-runtime";
  }
  return {};
}

std::string first_missing_ai_package_for_l2(const AiSettings& settings) {
  const std::string pack = ai_package_id_for_l2_model(settings.level2.model_id);
  if (ai_package_status(pack, settings) == AiPackageInstallStatus::Missing) {
    return pack;
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

  if (auto l1 = l1_info_for_package(id)) {
    if (!ensure_runtime()) {
      return result;
    }
    if (on_progress) {
      on_progress(0, i18n::tr("settings.toolpacks.ai.installing_l1"));
    }
    std::string error;
    const std::string path = store.ensure_model(*l1, true, progress, &error);
    if (path.empty()) {
      result.message = error.empty() ? i18n::tr("settings.toolpacks.ai.install_failed") : error;
      return result;
    }
    result.ok = true;
    result.message = i18n::tr("settings.toolpacks.ai.install_done");
    return result;
  }

  if (auto l2 = l2_info_for_package(id)) {
    if (!ensure_runtime()) {
      return result;
    }
    if (on_progress) {
      on_progress(0, i18n::tr("settings.toolpacks.ai.installing_l2"));
    }
    std::string error;
    const std::string path = store.ensure_l2_model(*l2, true, progress, &error);
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
