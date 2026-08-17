#include "toolpacks/language_packs.hpp"

#include "toolpacks/install.hpp"
#include "toolpacks/store.hpp"

namespace tuide::toolpacks {
namespace {

const std::vector<LanguagePack>& packs_singleton() {
  // shared gdb is kept across remove_language_pack for compiled languages.
  static const std::vector<LanguagePack> kPacks = {
      {
          "cpp",
          "settings.toolpacks.lang.cpp",
          {
              {"clangd", false, true, false},
              {"gdb", true, false, true},
          },
      },
      {
          "python",
          "settings.toolpacks.lang.python",
          {
              {"python-tools", false, true, true},
          },
      },
      {
          "bash",
          "settings.toolpacks.lang.bash",
          {
              {"bash-ls", false, true, false},
              {"bash-dap", false, false, true},
          },
      },
      {
          "latex",
          "settings.toolpacks.lang.latex",
          {
              {"texlab", false, true, false},
          },
      },
      {
          "rust",
          "settings.toolpacks.lang.rust",
          {
              {"rust-analyzer", false, true, false},
              {"gdb", true, false, true},
          },
      },
      {
          "go",
          "settings.toolpacks.lang.go",
          {
              {"gopls", false, true, false},
              {"gdb", true, false, true},
          },
      },
      {
          "zig",
          "settings.toolpacks.lang.zig",
          {
              {"zls", false, true, false},
              {"gdb", true, false, true},
          },
      },
      {
          "fortran",
          "settings.toolpacks.lang.fortran",
          {
              {"fortls", false, true, false},
              {"gdb", true, false, true},
          },
      },
      {
          "lua",
          "settings.toolpacks.lang.lua",
          {
              {"lua-ls", false, true, false},
          },
      },
      {
          "typescript",
          "settings.toolpacks.lang.typescript",
          {
              {"typescript-ls", false, true, false},
          },
      },
      {
          "cmake",
          "settings.toolpacks.lang.cmake",
          {
              {"neocmakelsp", false, true, false},
          },
      },
      {
          "make",
          "settings.toolpacks.lang.make",
          {
              {"make-ls", false, true, false},
          },
      },
      {
          "yaml",
          "settings.toolpacks.lang.yaml",
          {
              {"yaml-ls", false, true, false},
          },
      },
      {
          "xml",
          "settings.toolpacks.lang.xml",
          {
              {"lemminx", false, true, false},
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

InstallResult install_language_pack(const std::string& language_id, ProgressFn on_progress) {
  InstallResult result;
  const auto* pack = find_language_pack(language_id);
  if (pack == nullptr) {
    result.message = "language pack desconocido: " + language_id;
    return result;
  }

  std::vector<std::string> missing;
  for (const auto& component : pack->components) {
    if (!resolve_installed_toolpack(component.toolpack_id).has_value()) {
      missing.push_back(component.toolpack_id);
    }
  }

  report_progress(on_progress, 0, language_id);
  if (missing.empty()) {
    result.ok = true;
    result.id = language_id;
    result.message = "ya instalado: " + language_id;
    report_progress(on_progress, 100, language_id);
    return result;
  }

  std::vector<std::string> installed_now;
  const int n = static_cast<int>(missing.size());
  for (int i = 0; i < n; ++i) {
    const std::string& toolpack_id = missing[static_cast<std::size_t>(i)];
    const int base = (i * 100) / n;
    const int span = ((i + 1) * 100) / n - base;
    auto one =
        install_toolpack(toolpack_id, nest_progress(on_progress, base, span, toolpack_id));
    if (!one.ok) {
      result.message = one.message;
      return result;
    }
    installed_now.push_back(toolpack_id);
  }

  result.ok = true;
  result.id = language_id;
  result.message = "language pack " + language_id + " listo";
  report_progress(on_progress, 100, language_id);
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
