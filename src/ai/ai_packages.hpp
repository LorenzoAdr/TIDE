#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ai/ai_types.hpp"

namespace tuide {

struct AiPackage {
  std::string id;
  std::string name_i18n_key;
  std::string detail_i18n_key;
  std::size_t approx_bytes = 0;
};

enum class AiPackageInstallStatus {
  Installed,
  Missing,
};

struct AiPackageInstallResult {
  bool ok = false;
  std::string message;
};

using AiPackageProgressFn = std::function<void(int percent, std::string_view label)>;

// Runtime (llama.cpp), embed L0, model L1 — downloaded via ModelStore (HF / llama.cpp
// releases), not the GitHub toolpack catalog.
const std::vector<AiPackage>& ai_packages();

const AiPackage* find_ai_package(const std::string& id);

AiPackageInstallStatus ai_package_status(const std::string& id, const AiSettings& settings);

// First missing dependency for L0 semantic routing (embed + runtime).
std::string first_missing_ai_package_for_embed(const AiSettings& settings);
// First missing dependency for L1 agent (model + runtime).
std::string first_missing_ai_package_for_l1(const AiSettings& settings);
// First missing dependency for L2 local coder (model + runtime).
std::string first_missing_ai_package_for_l2(const AiSettings& settings);

// Force-download the pack (and runtime if needed). Safe to call from a worker thread.
AiPackageInstallResult install_ai_package(const std::string& id, const AiSettings& settings,
                                          const AiPackageProgressFn& on_progress);

}  // namespace tuide
