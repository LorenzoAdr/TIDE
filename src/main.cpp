#include "app/application.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool config_is_complete(const tgdb::AppConfig& config) {
  if (config.program.empty()) {
    return false;
  }
  if (config.mode == tgdb::SessionMode::kAttach) {
    return config.attach_pid > 0 || !config.attach_target.empty();
  }
  return true;
}

void print_usage() {
  std::cerr << "Uso: tgdb [opciones] [programa]\n"
            << "Opciones:\n"
            << "  --cwd <dir>         Directorio raíz del workspace\n"
            << "  --args <a>...       Argumentos del programa (después de --args)\n"
            << "  --attach <pid>      Adjuntar a un proceso en ejecución\n"
            << "  --target <host:puerto>  Adjuntar a gdbserver remoto\n"
            << "  -h, --help          Muestra esta ayuda\n"
            << "\n"
            << "Sin argumentos abre el asistente de conexión.\n"
            << "\n"
            << "Ejemplos:\n"
            << "  tgdb\n"
            << "  tgdb ./build/hello\n"
            << "  tgdb --attach 12345 ./build/hello\n"
            << "  tgdb --target localhost:1234 ./build/hello\n";
}

}  // namespace

int main(int argc, char** argv) {
  tgdb::AppConfig config;

  {
    std::error_code ec;
    config.launch_directory =
        std::filesystem::absolute(std::filesystem::current_path(ec)).string();
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage();
      return 0;
    }
    if (arg == "--cwd" && i + 1 < argc) {
      config.workspace_root = argv[++i];
      continue;
    }
    if (arg == "--attach" && i + 1 < argc) {
      config.mode = tgdb::SessionMode::kAttach;
      config.attach_pid = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--target" && i + 1 < argc) {
      config.mode = tgdb::SessionMode::kAttach;
      config.attach_target = argv[++i];
      continue;
    }
    if (arg == "--args") {
      while (i + 1 < argc) {
        config.args.push_back(argv[++i]);
      }
      break;
    }
    if (arg.rfind('-', 0) == 0) {
      std::cerr << "Opción desconocida: " << arg << "\n";
      print_usage();
      return 1;
    }
    config.program = arg;
  }

  if (!config_is_complete(config)) {
    config.use_connection_wizard = true;
  }

  std::error_code ec;
  if (!config.program.empty()) {
    config.program = std::filesystem::absolute(config.program, ec).string();
    if (!std::filesystem::exists(config.program)) {
      std::cerr << "Error: programa no encontrado: " << config.program << "\n";
      return 1;
    }
    if (!std::filesystem::is_regular_file(config.program)) {
      std::cerr << "Error: el programa debe ser un archivo: " << config.program
                << "\n";
      return 1;
    }
  }
  if (!config.workspace_root.empty()) {
    config.workspace_root =
        std::filesystem::absolute(config.workspace_root, ec).string();
    if (!std::filesystem::is_directory(config.workspace_root)) {
      std::cerr << "Error: --cwd debe ser un directorio: "
                << config.workspace_root << "\n";
      return 1;
    }
  }

  tgdb::Application app(std::move(config));
  return app.run();
}
