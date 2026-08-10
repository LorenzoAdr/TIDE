#pragma once

#include <string>
#include <vector>

#include "toolpacks/install.hpp"
#include "toolpacks/progress.hpp"

namespace tuide::toolpacks {

struct LanguagePackComponent {
  std::string toolpack_id;  // clangd, gdb, …
  bool shared = false;      // e.g. gdb shared across compiled languages
  bool is_lsp = false;
  bool is_dap = false;
};

struct LanguagePack {
  std::string id;             // cpp, rust, …
  std::string name_i18n_key;  // settings.toolpacks.lang.cpp
  std::vector<LanguagePackComponent> components;
};

// Language packs for F10 Toolpacks UI (LSP + optional DAP per language).
const std::vector<LanguagePack>& language_packs();

const LanguagePack* find_language_pack(const std::string& id);

enum class LanguagePackStatus {
  kMissing,
  kPartial,
  kInstalled,
};

struct LanguagePackStatusInfo {
  LanguagePackStatus status = LanguagePackStatus::kMissing;
  std::vector<std::string> missing_ids;
  std::vector<std::string> installed_ids;
};

LanguagePackStatusInfo language_pack_status(const LanguagePack& pack);

InstallResult install_language_pack(const std::string& language_id,
                                    ProgressFn on_progress = {});

// Remove language-specific components only (shared like gdb are kept).
InstallResult remove_language_pack(const std::string& language_id);

}  // namespace tuide::toolpacks
