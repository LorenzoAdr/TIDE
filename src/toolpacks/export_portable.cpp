#include "toolpacks/export_portable.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "toolpacks/embed.hpp"
#include "toolpacks/manifest.hpp"
#include "toolpacks/paths.hpp"
#include "toolpacks/store.hpp"

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

std::string shell_quote(const std::string& value) {
  std::string out = "'";
  for (char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out += ch;
    }
  }
  out += "'";
  return out;
}

std::string self_exe_path() {
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec).string();
}

std::string pack_toolpack_dir(const fs::path& root, const fs::path& archive_out) {
  std::error_code ec;
  fs::create_directories(archive_out.parent_path(), ec);
  fs::remove(archive_out, ec);
  const std::string cmd = "tar -C " + shell_quote(root.string()) + " -cf - . | zstd -q -o " +
                          shell_quote(archive_out.string());
  if (std::system(cmd.c_str()) != 0 || !fs::is_regular_file(archive_out, ec)) {
    return "no se pudo empaquetar " + root.string();
  }
  return {};
}

}  // namespace

ExportResult export_portable(const std::string& source_binary,
                             const std::string& output_path,
                             const std::vector<std::string>& toolpack_ids) {
  ExportResult result;
  const std::string source =
      source_binary.empty() ? self_exe_path() : source_binary;
  if (source.empty()) {
    result.message = "no se pudo resolver el binario fuente";
    return result;
  }
  if (binary_has_toolpack_embed(source)) {
    result.message =
        "el binario fuente ya contiene toolpacks embebidos; usa un nucleo limpio "
        "(sin trailer TUIDTPK1)";
    return result;
  }

  std::vector<std::string> ids = toolpack_ids;
  if (ids.empty()) {
    const auto manifest = load_manifest(manifest_path());
    if (!manifest.has_value() || manifest->installed.empty()) {
      result.message = "no hay toolpacks instalados para exportar";
      return result;
    }
    for (const auto& entry : manifest->installed) {
      if (entry.active) {
        ids.push_back(entry.id);
      }
    }
  }
  if (ids.empty()) {
    result.message = "lista de toolpacks vacia";
    return result;
  }

  std::vector<std::string> versions;
  std::vector<std::string> blob_paths;
  std::vector<fs::path> temp_blobs;
  std::error_code ec;
  const fs::path work = fs::path(cache_root()) / "export-work";
  fs::create_directories(work, ec);

  for (const auto& id : ids) {
    if (id != "clangd") {
      result.message = "piloto: solo se puede embeber 'clangd' (recibido: " + id + ")";
      return result;
    }
    const auto resolved = resolve_installed_toolpack(id);
    if (!resolved.has_value()) {
      result.message = "toolpack no instalado: " + id;
      return result;
    }
    const fs::path blob = work / (id + "-" + resolved->version + "-export.tar.zst");
    const std::string pack_err = pack_toolpack_dir(resolved->root_dir, blob);
    if (!pack_err.empty()) {
      result.message = pack_err;
      return result;
    }
    versions.push_back(resolved->version);
    blob_paths.push_back(blob.string());
    temp_blobs.push_back(blob);
  }

  const fs::path out_path = output_path.empty()
                                ? fs::path("dist") / "tuide-portable"
                                : fs::path(output_path);
  fs::create_directories(out_path.parent_path(), ec);
  fs::remove(out_path, ec);
  fs::copy_file(source, out_path, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    result.message = "no se pudo copiar el nucleo a " + out_path.string() + ": " +
                     ec.message();
    return result;
  }
  fs::permissions(out_path,
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  ec);

  const std::string append_err =
      append_toolpack_trailer(out_path.string(), ids, versions, blob_paths);
  for (const auto& blob : temp_blobs) {
    fs::remove(blob, ec);
  }
  if (!append_err.empty()) {
    fs::remove(out_path, ec);
    result.message = append_err;
    return result;
  }

  // Guard: re-export must fail on this output.
  if (!binary_has_toolpack_embed(out_path.string())) {
    fs::remove(out_path, ec);
    result.message = "trailer no verificable tras export";
    return result;
  }

  result.ok = true;
  result.output_path = out_path.string();
  result.message = "exportado " + out_path.string() + " con " +
                   std::to_string(ids.size()) + " toolpack(s)";
  return result;
}

int run_export_cli(int argc, char** argv) {
  std::string output;
  std::string source;
  std::vector<std::string> ids;
  bool all_installed = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "export-portable") {
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      std::cerr
          << "Uso: tuide export-portable [opciones]\n"
          << "\n"
          << "Embebe toolpacks instalados en una copia del nucleo limpio.\n"
          << "Si el binario fuente ya trae trailer, la exportacion se bloquea.\n"
          << "\n"
          << "Opciones:\n"
          << "  -o, --output PATH          Binario de salida (default: dist/tuide-portable)\n"
          << "  --binary PATH              Nucleo fuente (default: este ejecutable)\n"
          << "  --toolpacks id[,id...]     Toolpacks a embeber (default: instalados activos)\n"
          << "  --all-installed            Embeber todos los toolpacks activos\n";
      return 0;
    }
    if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
      output = argv[++i];
      continue;
    }
    if (arg == "--binary" && i + 1 < argc) {
      source = argv[++i];
      continue;
    }
    if (arg == "--all-installed") {
      all_installed = true;
      continue;
    }
    if (arg == "--toolpacks" && i + 1 < argc) {
      std::string list = argv[++i];
      std::string cur;
      for (char ch : list) {
        if (ch == ',') {
          if (!cur.empty()) {
            ids.push_back(cur);
            cur.clear();
          }
        } else {
          cur.push_back(ch);
        }
      }
      if (!cur.empty()) {
        ids.push_back(cur);
      }
      continue;
    }
    std::cerr << "opcion desconocida: " << arg << '\n';
    return 2;
  }
  (void)all_installed;

  const auto result = export_portable(source, output, ids);
  if (result.ok) {
    std::cout << result.message << '\n';
    return 0;
  }
  std::cerr << "error: " << result.message << '\n';
  return 1;
}

}  // namespace tuide::toolpacks
