#include "toolpacks/language_packs.hpp"

#include "toolpacks/install.hpp"
#include "toolpacks/store.hpp"

namespace tuide::toolpacks {
namespace {

const std::vector<LanguagePack>& packs_singleton() {
  static const std::vector<LanguagePack> kPacks = {
      {
          "cpp",
          "settings.toolpacks.lang.cpp",
          {
              {"clangd", false, true, false},
              {"gdb", true, false, true},
          },
      },
  };
  return kPacks;
}

}  // namespace

const std::vector<LanguagePack>& language_packs() {
  return packs_singleton();
}

const LanguagePack* find_language_pack(const std::string& id) {
  for (const auto& pack : language_packs()) {
    if (pack.id == id) {
      return &pack;
    }
  }
  return nullptr;
}

LanguagePackStatusInfo language_pack_status(const LanguagePack& pack) {
  LanguagePackStatusInfo info;
  for (const auto& component : pack.components) {
    if (resolve_installed_toolpack(component.toolpack_id).has_value()) {
      info.installed_ids.push_back(component.toolpack_id);
    } else {
      info.missing_ids.push_back(component.toolpack_id);
    }
  }
  if (info.missing_ids.empty()) {
    info.status = LanguagePackStatus::kInstalled;
  } else if (info.installed_ids.empty()) {
    info.status = LanguagePackStatus::kMissing;
  } else {
    info.status = LanguagePackStatus::kPartial;
  }
  return info;
}

InstallResult install_language_pack(const std::string& language_id) {
  InstallResult result;
  const auto* pack = find_language_pack(language_id);
  if (pack == nullptr) {
    result.message = "language pack desconocido: " + language_id;
    return result;
  }

  std::vector<std::string> installed_now;
  for (const auto& component : pack->components) {
    if (resolve_installed_toolpack(component.toolpack_id).has_value()) {
      continue;
    }
    auto one = install_toolpack(component.toolpack_id);
    if (!one.ok) {
      result.message = one.message;
      return result;
    }
    installed_now.push_back(component.toolpack_id);
  }

  result.ok = true;
  result.id = language_id;
  if (installed_now.empty()) {
    result.message = "ya instalado: " + language_id;
  } else {
    result.message = "language pack " + language_id + " listo";
  }
  return result;
}

InstallResult remove_language_pack(const std::string& language_id) {
  InstallResult result;
  const auto* pack = find_language_pack(language_id);
  if (pack == nullptr) {
    result.message = "language pack desconocido: " + language_id;
    return result;
  }

  bool removed_any = false;
  for (const auto& component : pack->components) {
    if (component.shared) {
      // Shared debugger stays available for other compiled languages.
      continue;
    }
    auto one = remove_toolpack(component.toolpack_id);
    if (one.ok) {
      removed_any = true;
    }
  }
  result.ok = true;
  result.id = language_id;
  result.message = removed_any ? ("eliminado LSP de " + language_id)
                               : ("nada que eliminar para " + language_id);
  return result;
}

}  // namespace tuide::toolpacks
