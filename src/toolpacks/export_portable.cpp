#include "toolpacks/export_portable.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "toolpacks/manifest.hpp"
#include "toolpacks/packaged.hpp"
#include "toolpacks/paths.hpp"
#include "toolpacks/progress.hpp"
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

bool command_exists(const char* name) {
  return std::system((std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0;
}

std::string find_appimagetool() {
  if (const char* env = std::getenv("APPIMAGETOOL"); env != nullptr && env[0] != '\0') {
    std::error_code ec;
    if (fs::is_regular_file(env, ec)) {
      return env;
    }
  }
  if (command_exists("appimagetool")) {
    return "appimagetool";
  }
  return {};
}

void write_text(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::trunc);
  out << text;
}

std::string copy_tree(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::remove_all(to, ec);
  fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) {
    return "no se pudo copiar " + from.string() + " -> " + to.string() + ": " + ec.message();
  }
  return {};
}

std::string build_appdir(const fs::path& appdir, const std::string& source_binary,
                         const std::vector<std::string>& ids, const ProgressFn& on_progress) {
  std::error_code ec;
  fs::remove_all(appdir, ec);
  fs::create_directories(appdir / "usr" / "bin", ec);
  fs::create_directories(appdir / "usr" / "share" / "tuide" / "toolpacks", ec);

  report_progress(on_progress, 8, "nucleo");
  fs::copy_file(source_binary, appdir / "usr" / "bin" / "tuide",
                fs::copy_options::overwrite_existing, ec);
  if (ec) {
    return "no se pudo copiar el nucleo: " + ec.message();
  }
  fs::permissions(appdir / "usr" / "bin" / "tuide",
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  ec);

  Manifest out_manifest;
  out_manifest.schema = 1;
  const auto local = load_manifest(manifest_path());

  const int n = static_cast<int>(ids.size());
  for (int i = 0; i < n; ++i) {
    const auto& id = ids[static_cast<std::size_t>(i)];
    // Copy packs occupy ~10%..65% of the overall export.
    const int pct = n > 0 ? 10 + ((i + 1) * 55) / n : 65;
    report_progress(on_progress, pct, id);
    const auto resolved = resolve_installed_toolpack(id);
    if (!resolved.has_value()) {
      return "toolpack no instalado: " + id;
    }
    const fs::path dest =
        appdir / "usr" / "share" / "tuide" / "toolpacks" / id / resolved->version;
    const std::string copy_err = copy_tree(resolved->root_dir, dest);
    if (!copy_err.empty()) {
      return copy_err;
    }

    ManifestEntry entry;
    entry.id = id;
    entry.version = resolved->version;
    entry.active = true;
    entry.source = "export";
    entry.path = (fs::path(id) / resolved->version).string();
    if (local.has_value()) {
      for (const auto& existing : local->installed) {
        if (existing.id == id && existing.active) {
          entry.installed_at = existing.installed_at;
          break;
        }
      }
    }
    out_manifest.installed.push_back(std::move(entry));
  }

  report_progress(on_progress, 68, "manifest");
  if (!save_manifest(
          (appdir / "usr" / "share" / "tuide" / "toolpacks" / "manifest.json").string(),
          out_manifest)) {
    return "no se pudo escribir manifest.json del AppDir";
  }

  // Bundled packs are read-only inside the AppImage. Keep installs writable under
  // XDG (~/.local/share/tuide/toolpacks) via TUIDE_TOOLPACKS_BUNDLED, not ROOT.
  write_text(appdir / "AppRun",
             "#!/bin/sh\n"
             "HERE=\"$(dirname \"$(readlink -f \"$0\")\")\"\n"
             "export TUIDE_TOOLPACKS_BUNDLED=\"$HERE/usr/share/tuide/toolpacks\"\n"
             "exec \"$HERE/usr/bin/tuide\" \"$@\"\n");
  fs::permissions(appdir / "AppRun",
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  ec);

  write_text(appdir / "tuide.desktop",
             "[Desktop Entry]\n"
             "Type=Application\n"
             "Name=tuide\n"
             "Exec=tuide\n"
             "Icon=tuide\n"
             "Categories=Development;IDE;\n"
             "Terminal=true\n"
             "Comment=Terminal IDE\n");

  // Minimal 1x1 PNG so appimagetool is happy when packaging.
  static const unsigned char kPng1x1[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
      0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
      0x77, 0x53, 0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8,
      0xcf, 0xc0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xfe, 0xd4, 0xef, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  {
    std::ofstream png(appdir / "tuide.png", std::ios::binary | std::ios::trunc);
    png.write(reinterpret_cast<const char*>(kPng1x1), sizeof(kPng1x1));
  }

  write_text(appdir / ".tuide-appdir", "1\n");
  report_progress(on_progress, 72, "AppDir");
  return {};
}

}  // namespace

ExportResult export_portable(const std::string& source_binary,
                             const std::string& output_path,
                             const std::vector<std::string>& toolpack_ids,
                             ExportFormat format, ProgressFn on_progress,
                             bool core_only) {
  ExportResult result;
  report_progress(on_progress, 0);
  const std::string source =
      source_binary.empty() ? self_exe_path() : source_binary;
  if (source.empty()) {
    result.message = "no se pudo resolver el binario fuente";
    return result;
  }
  if (is_packaged_binary(source)) {
    result.message =
        "el binario/fuente ya esta empaquetado; usa un nucleo limpio "
        "(sin AppImage/AppDir ni trailer TUIDTPK1 legado)";
    return result;
  }
  std::error_code ec;
  if (!fs::is_regular_file(source, ec)) {
    result.message = "nucleo fuente no es un fichero: " + source;
    return result;
  }

  std::vector<std::string> ids;
  if (core_only) {
    ids.clear();
  } else {
    ids = toolpack_ids;
    if (ids.empty()) {
      const auto manifest = load_manifest(manifest_path());
      if (!manifest.has_value() || manifest->installed.empty()) {
        result.message = "no hay toolpacks instalados para exportar "
                         "(usa --core-only para AppImage del nucleo)";
        return result;
      }
      for (const auto& entry : manifest->installed) {
        if (entry.active) {
          ids.push_back(entry.id);
        }
      }
    }
    if (ids.empty()) {
      result.message = "lista de toolpacks vacia "
                       "(usa --core-only para AppImage del nucleo)";
      return result;
    }
  }

  report_progress(on_progress, 5);
  const fs::path work = fs::path(cache_root()) / "export-work";
  fs::create_directories(work, ec);
  const fs::path staging_appdir = work / "tuide.AppDir";

  const std::string build_err = build_appdir(staging_appdir, source, ids, on_progress);
  if (!build_err.empty()) {
    fs::remove_all(staging_appdir, ec);
    result.message = build_err;
    return result;
  }

  const auto packs_suffix = [&]() -> std::string {
    if (core_only || ids.empty()) {
      return " (solo nucleo)";
    }
    return " con " + std::to_string(ids.size()) + " toolpack(s)";
  };

  if (format == ExportFormat::kAppDir) {
    report_progress(on_progress, 85, "AppDir");
    const fs::path out_path =
        output_path.empty() ? fs::path("dist") / "tuide.AppDir" : fs::path(output_path);
    fs::create_directories(out_path.parent_path(), ec);
    fs::remove_all(out_path, ec);
    fs::rename(staging_appdir, out_path, ec);
    if (ec) {
      // Cross-device rename may fail; fall back to copy.
      const std::string copy_err = copy_tree(staging_appdir, out_path);
      fs::remove_all(staging_appdir, ec);
      if (!copy_err.empty()) {
        result.message = copy_err;
        return result;
      }
    }
    if (!path_looks_like_appdir(out_path.string())) {
      fs::remove_all(out_path, ec);
      result.message = "AppDir incompleto tras export";
      return result;
    }
    result.ok = true;
    result.output_path = out_path.string();
    result.message = "exportado AppDir " + out_path.string() + packs_suffix();
    report_progress(on_progress, 100, "AppDir");
    return result;
  }

  // AppImage
  const std::string tool = find_appimagetool();
  if (tool.empty()) {
    fs::remove_all(staging_appdir, ec);
    result.message =
        "appimagetool no encontrado (PATH o APPIMAGETOOL). "
        "Usa --format=appdir o instala appimagetool.";
    return result;
  }

  const fs::path out_path = output_path.empty() ? fs::path("dist") / "tuide-x86_64.AppImage"
                                                : fs::path(output_path);
  fs::create_directories(out_path.parent_path(), ec);
  fs::remove(out_path, ec);

  // Prefer mksquashfs next to appimagetool (AppImage of appimagetool) and force ARCH.
  // Expand PATH in-process so /bin/sh does not depend on fragile `$PATH` quoting.
  std::string path_prefix;
  {
    std::error_code tec;
    fs::path tool_path = tool;
    std::string path_value = std::getenv("PATH") != nullptr ? std::getenv("PATH") : "/usr/bin:/bin";
    if (tool != "appimagetool" && fs::is_regular_file(tool_path, tec)) {
      const fs::path dir = tool_path.parent_path();
      if (fs::is_regular_file(dir / "mksquashfs", tec)) {
        path_value = dir.string() + ":" + path_value;
      }
    }
    path_prefix = "env ARCH=x86_64 PATH=" + shell_quote(path_value) + " ";
  }
  report_progress(on_progress, 78, "AppImage");
  const fs::path log_path = work / "appimagetool.log";
  const std::string cmd = path_prefix + shell_quote(tool) + " " +
                          shell_quote(staging_appdir.string()) + " " +
                          shell_quote(out_path.string()) + " >" +
                          shell_quote(log_path.string()) + " 2>&1";
  const int rc = std::system(cmd.c_str());
  fs::remove_all(staging_appdir, ec);
  if (rc != 0 || !fs::is_regular_file(out_path, ec)) {
    std::string detail;
    {
      std::ifstream log(log_path);
      if (log) {
        std::ostringstream oss;
        oss << log.rdbuf();
        detail = oss.str();
        while (!detail.empty() &&
               (detail.back() == '\n' || detail.back() == '\r')) {
          detail.pop_back();
        }
        if (detail.size() > 400) {
          detail = detail.substr(detail.size() - 400);
        }
      }
    }
    result.message = "appimagetool fallo (codigo " + std::to_string(rc) + ")";
    if (!detail.empty()) {
      result.message += ": " + detail;
    }
    return result;
  }
  report_progress(on_progress, 95, "AppImage");
  fs::permissions(out_path,
                  fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                      fs::perms::others_read | fs::perms::others_exec,
                  ec);

  if (!is_packaged_binary(out_path.string())) {
    // AppImage extension should make it packaged.
    result.message = "salida no reconocida como AppImage: " + out_path.string();
    return result;
  }

  result.ok = true;
  result.output_path = out_path.string();
  result.message = "exportado AppImage " + out_path.string() + packs_suffix();
  report_progress(on_progress, 100, "AppImage");
  return result;
}

int run_export_cli(int argc, char** argv) {
  std::string output;
  std::string source;
  std::vector<std::string> ids;
  ExportFormat format = ExportFormat::kAppImage;
  bool core_only = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "export-portable") {
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      std::cerr
          << "Uso: tuide export-portable [opciones]\n"
          << "\n"
          << "Empaqueta un nucleo limpio (+ toolpacks opcionales) como AppImage/AppDir.\n"
          << "Si la fuente ya esta empaquetada, la exportacion se bloquea.\n"
          << "\n"
          << "Opciones:\n"
          << "  -o, --output PATH          Salida (default: dist/tuide-x86_64.AppImage)\n"
          << "  --binary PATH              Nucleo fuente (default: este ejecutable)\n"
          << "  --toolpacks id[,id...]     Toolpacks (default: instalados activos)\n"
          << "  --all-installed            Todos los toolpacks activos\n"
          << "  --core-only                Solo nucleo (sin toolpacks; release oficial)\n"
          << "  --format appimage|appdir   Default: appimage (requiere appimagetool)\n";
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
      continue;
    }
    if (arg == "--core-only") {
      core_only = true;
      continue;
    }
    if (arg.rfind("--format=", 0) == 0 || arg == "--format") {
      std::string fmt;
      if (arg.rfind("--format=", 0) == 0) {
        fmt = arg.substr(std::string("--format=").size());
      } else if (i + 1 < argc) {
        fmt = argv[++i];
      } else {
        std::cerr << "falta valor para --format (appimage|appdir)\n";
        return 2;
      }
      if (fmt == "appimage") {
        format = ExportFormat::kAppImage;
      } else if (fmt == "appdir") {
        format = ExportFormat::kAppDir;
      } else {
        std::cerr << "formato desconocido: " << fmt << " (usa appimage|appdir)\n";
        return 2;
      }
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

  // Infer format from output extension when not overridden by explicit non-default path naming.
  if (!output.empty()) {
    if (path_looks_like_appdir(output) ||
        (output.size() >= 7 &&
         output.compare(output.size() - 7, 7, ".AppDir") == 0)) {
      format = ExportFormat::kAppDir;
    }
  }

  if (core_only && !ids.empty()) {
    std::cerr << "error: --core-only no admite --toolpacks\n";
    return 2;
  }

  const auto result = export_portable(source, output, ids, format, {}, core_only);
  if (result.ok) {
    std::cout << result.message << '\n';
    return 0;
  }
  std::cerr << "error: " << result.message << '\n';
  return 1;
}

}  // namespace tuide::toolpacks
