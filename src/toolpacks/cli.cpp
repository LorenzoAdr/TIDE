#include "toolpacks/cli.hpp"

#include <iostream>
#include <string>
#include <vector>

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
      << "  doctor               Diagnostico de resolucion (clangd piloto)\n"
      << "  install <id[@ver]>   Instala desde el catalogo (P2)\n"
      << "  update [id]          Actualiza toolpack(s) (P2)\n"
      << "  remove <id>          Elimina un toolpack instalado (P2)\n"
      << "\n"
      << "Variables:\n"
      << "  TUIDE_TOOLPACKS_ROOT          Directorio de toolpacks\n"
      << "  TUIDE_TOOLPACKS_CATALOG_URL   URL de catalog.json\n";
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

int cmd_doctor() {
  std::cout << "toolpacks root: " << toolpacks_root() << '\n';
  std::cout << "manifest:       " << manifest_path() << '\n';
  std::cout << "catalog URL:    " << default_catalog_url() << '\n';

  const auto tp = resolve_clangd_toolpack();
  if (tp.has_value()) {
    std::cout << "clangd toolpack: " << tp->version << '\n';
    std::cout << "  binary:       " << tp->binary_path << '\n';
    if (!tp->resource_dir.empty()) {
      std::cout << "  resource_dir: " << tp->resource_dir << '\n';
    }
  } else {
    std::cout << "clangd toolpack: (no instalado o invalido)\n";
  }

  if (const auto loc = resolve_clangd(); loc.has_value()) {
    const char* src = "unknown";
    switch (loc->source) {
      case ClangdLocation::Source::Env:
        src = "env";
        break;
      case ClangdLocation::Source::Toolpack:
        src = "toolpack";
        break;
      case ClangdLocation::Source::Bundled:
        src = "bundled";
        break;
      case ClangdLocation::Source::SystemPath:
        src = "PATH";
        break;
    }
    std::cout << "resolve_clangd:  " << loc->binary_path << "  [" << src << "]\n";
  } else {
    std::cout << "resolve_clangd:  (no encontrado)\n";
  }
  return 0;
}

int cmd_not_ready(const char* name) {
  std::cerr << "Comando '" << name
            << "' aun no disponible en esta fase (llega en P2).\n";
  return 2;
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
    return cmd_not_ready("install");
  }
  if (cmd == "update") {
    return cmd_not_ready("update");
  }
  if (cmd == "remove") {
    return cmd_not_ready("remove");
  }
  std::cerr << "Comando desconocido: " << cmd << '\n';
  print_usage();
  return 2;
}

}  // namespace tuide::toolpacks
