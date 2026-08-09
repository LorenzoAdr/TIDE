#include "toolpacks/cli.hpp"

#include <iostream>
#include <string>

#include "toolpacks/install.hpp"
#include "toolpacks/language_packs.hpp"
#include "toolpacks/manifest.hpp"
#include "toolpacks/paths.hpp"
#include "toolpacks/store.hpp"
#include "util/bundled_tools.hpp"

namespace tuide::toolpacks {
namespace {

void print_usage() {
  std::cerr
      << "Uso: tuide toolpacks <comando>\n"
      << "\n"
      << "Comandos:\n"
      << "  list                 Lista toolpacks instalados\n"
      << "  doctor               Diagnostico de language packs y resolucion\n"
      << "  install <id[@ver]|lang> Instala toolpack o language pack\n"
      << "  update [id]          Actualiza toolpack(s) desde el catalogo\n"
      << "  remove <id|lang>     Elimina toolpack o LSP del language pack\n"
      << "\n"
      << "Variables:\n"
      << "  TUIDE_TOOLPACKS_ROOT          Directorio de toolpacks\n"
      << "  TUIDE_TOOLPACKS_CATALOG_URL   URL o ruta de catalog.json\n";
}

int cmd_list() {
  const auto manifest = load_manifest(manifest_path());
  if (!manifest.has_value() || manifest->installed.empty()) {
    std::cout << "No hay toolpacks instalados en " << toolpacks_root() << '\n';
    return 0;
  }
  std::cout << "Root: " << toolpacks_root() << '\n';
  for (const auto& entry : manifest->installed) {
    std::cout << (entry.active ? "* " : "  ") << entry.id << " " << entry.version
              << "  (" << entry.path << ")"
              << (entry.source.empty() ? "" : " [" + entry.source + "]") << '\n';
  }
  return 0;
}

const char* source_name_clangd(ClangdLocation::Source source) {
  switch (source) {
    case ClangdLocation::Source::Env:
      return "env";
    case ClangdLocation::Source::Toolpack:
      return "toolpack";
    case ClangdLocation::Source::Bundled:
      return "bundled";
    case ClangdLocation::Source::SystemPath:
      return "PATH";
  }
  return "unknown";
}

int cmd_doctor() {
  std::cout << "toolpacks root: " << toolpacks_root() << '\n';
  std::cout << "manifest:       " << manifest_path() << '\n';
  std::cout << "catalog URL:    " << default_catalog_url() << '\n';
  std::cout << '\n';

  std::cout << "Language packs:\n";
  for (const auto& pack : language_packs()) {
    const auto status = language_pack_status(pack);
    const char* label = "missing";
    switch (status.status) {
      case LanguagePackStatus::kInstalled:
        label = "installed";
        break;
      case LanguagePackStatus::kPartial:
        label = "partial";
        break;
      case LanguagePackStatus::kMissing:
        label = "missing";
        break;
    }
    std::cout << "  " << pack.id << ": " << label;
    if (!status.missing_ids.empty()) {
      std::cout << " (faltan:";
      for (const auto& id : status.missing_ids) {
        std::cout << ' ' << id;
      }
      std::cout << ')';
    }
    std::cout << '\n';
  }
  std::cout << '\n';

  std::cout << "Installed toolpacks:\n";
  const auto manifest = load_manifest(manifest_path());
  if (!manifest.has_value() || manifest->installed.empty()) {
    std::cout << "  (ninguno)\n";
  } else {
    for (const auto& entry : manifest->installed) {
      const auto tp = resolve_installed_toolpack(entry.id);
      std::cout << "  " << entry.id << ' ' << entry.version;
      if (tp.has_value()) {
        std::cout << "  ok  " << tp->binary_path;
      } else {
        std::cout << "  INVALIDO";
      }
      std::cout << '\n';
    }
  }
  std::cout << '\n';

  if (const auto loc = resolve_clangd(); loc.has_value()) {
    std::cout << "resolve_clangd: " << loc->binary_path << "  ["
              << source_name_clangd(loc->source) << "]\n";
  } else {
    std::cout << "resolve_clangd: (no encontrado)\n";
  }
  if (const auto loc = resolve_gdb(); loc.has_value()) {
    const char* src = "unknown";
    switch (loc->source) {
      case GdbLocation::Source::Env:
        src = "env";
        break;
      case GdbLocation::Source::Toolpack:
        src = "toolpack";
        break;
      case GdbLocation::Source::Bundled:
        src = "bundled";
        break;
      case GdbLocation::Source::SystemPath:
        src = "PATH";
        break;
    }
    std::cout << "resolve_gdb:    " << loc->binary_path << "  [" << src << "]\n";
  } else {
    std::cout << "resolve_gdb:    (no encontrado)\n";
  }
  if (const auto loc = resolve_make_ls(); loc.has_value()) {
    const char* src = "unknown";
    switch (loc->source) {
      case MakeLsLocation::Source::Env:
        src = "env";
        break;
      case MakeLsLocation::Source::Toolpack:
        src = "toolpack";
        break;
      case MakeLsLocation::Source::Bundled:
        src = "bundled";
        break;
      case MakeLsLocation::Source::SystemPath:
        src = "PATH";
        break;
    }
    std::cout << "resolve_make_ls: " << loc->binary_path << "  [" << src << "]\n";
  }
  return 0;
}

int print_install_result(const InstallResult& result) {
  if (result.ok) {
    std::cout << result.message << '\n';
    return 0;
  }
  std::cerr << "error: " << result.message << '\n';
  return 1;
}

}  // namespace

int run_cli(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "-h" || cmd == "--help" || cmd == "help") {
    print_usage();
    return 0;
  }
  if (cmd == "list") {
    return cmd_list();
  }
  if (cmd == "doctor") {
    return cmd_doctor();
  }
  if (cmd == "install") {
    if (argc < 3) {
      std::cerr << "Uso: tuide toolpacks install <id[@version]|lang>\n";
      return 2;
    }
    const std::string spec = argv[2];
    if (find_language_pack(spec) != nullptr) {
      return print_install_result(install_language_pack(spec));
    }
    return print_install_result(install_toolpack(spec));
  }
  if (cmd == "update") {
    const std::string id = argc >= 3 ? argv[2] : "";
    if (id.empty()) {
      std::cerr << "Uso: tuide toolpacks update <id|lang>\n";
      return 2;
    }
    if (find_language_pack(id) != nullptr) {
      return print_install_result(install_language_pack(id));
    }
    return print_install_result(update_toolpack(id));
  }
  if (cmd == "remove") {
    if (argc < 3) {
      std::cerr << "Uso: tuide toolpacks remove <id|lang>\n";
      return 2;
    }
    const std::string id = argv[2];
    if (find_language_pack(id) != nullptr) {
      return print_install_result(remove_language_pack(id));
    }
    return print_install_result(remove_toolpack(id));
  }
  std::cerr << "Comando desconocido: " << cmd << '\n';
  print_usage();
  return 2;
}

}  // namespace tuide::toolpacks
