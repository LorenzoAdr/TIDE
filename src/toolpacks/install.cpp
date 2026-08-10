#include "toolpacks/install.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "toolpacks/catalog.hpp"
#include "toolpacks/download.hpp"
#include "toolpacks/manifest.hpp"
#include "toolpacks/paths.hpp"
#include "toolpacks/progress.hpp"
#include "toolpacks/store.hpp"

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

std::string utc_now_iso() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

void parse_id_spec(const std::string& spec, std::string* id, std::string* version) {
  const auto at = spec.find('@');
  if (at == std::string::npos) {
    *id = spec;
    *version = {};
    return;
  }
  *id = spec.substr(0, at);
  *version = spec.substr(at + 1);
}

bool looks_like_payload_root(const fs::path& root) {
  std::error_code ec;
  return fs::is_regular_file(root / "toolpack.json", ec);
}

// If archive extracted a single top-level directory that contains toolpack.json, use it.
fs::path unwrap_payload_root(const fs::path& extract_dir) {
  if (looks_like_payload_root(extract_dir)) {
    return extract_dir;
  }
  std::error_code ec;
  fs::path only_child;
  int children = 0;
  for (const auto& entry : fs::directory_iterator(extract_dir, ec)) {
    ++children;
    only_child = entry.path();
    if (children > 1) {
      break;
    }
  }
  if (children == 1 && fs::is_directory(only_child, ec) && looks_like_payload_root(only_child)) {
    return only_child;
  }
  return extract_dir;
}

void upsert_manifest_entry(Manifest* manifest, ManifestEntry entry) {
  for (auto& existing : manifest->installed) {
    if (existing.id == entry.id) {
      existing = entry;
      existing.active = true;
      // Deactivate other versions of same id handled by single entry model.
      return;
    }
  }
  entry.active = true;
  manifest->installed.push_back(std::move(entry));
}

}  // namespace

InstallResult install_toolpack(const std::string& id_spec, ProgressFn on_progress) {
  InstallResult result;
  std::string id;
  std::string version;
  parse_id_spec(id_spec, &id, &version);
  if (id.empty()) {
    result.message = "id de toolpack vacio";
    return result;
  }

  report_progress(on_progress, 0, id);
  if (const std::string deps = toolpack_host_deps_error(); !deps.empty()) {
    result.message = deps;
    return result;
  }
  if (!toolpacks_root_is_writable()) {
    result.message =
        "directorio de toolpacks no escribible: " + toolpacks_root() +
        " (en AppImage las instalaciones van a ~/.local/share/tuide/toolpacks; "
        "no uses un TUIDE_TOOLPACKS_ROOT de solo lectura)";
    return result;
  }

  std::string catalog_error;
  const auto catalog = fetch_catalog(&catalog_error);
  if (!catalog.has_value()) {
    result.message = catalog_error.empty() ? "no se pudo obtener el catalogo" : catalog_error;
    return result;
  }

  const auto entry = find_catalog_toolpack(*catalog, id, version);
  if (!entry.has_value()) {
    result.message = "toolpack no encontrado en el catalogo: " + id_spec;
    return result;
  }

  std::error_code ec;
  fs::create_directories(downloads_dir(), ec);
  fs::create_directories(toolpacks_root(), ec);

  const std::string archive_name = id + "-" + entry->version + "-linux-x86_64.tar.zst";
  const fs::path archive_path = fs::path(downloads_dir()) / archive_name;
  report_progress(on_progress, 5, id);
  const std::string dl_err =
      download_url(entry->url, archive_path.string(), nest_progress(on_progress, 5, 70, id),
                   entry->size_bytes);
  if (!dl_err.empty()) {
    result.message = dl_err;
    return result;
  }

  report_progress(on_progress, 78, id);
  const std::string digest = file_sha256(archive_path.string());
  if (digest.empty()) {
    result.message = "no se pudo calcular sha256 del archivo descargado";
    return result;
  }
  if (digest != entry->sha256) {
    result.message = "sha256 no coincide (esperado " + entry->sha256 + ", obtenido " + digest + ")";
    return result;
  }

  const fs::path final_root = fs::path(toolpacks_root()) / id / entry->version;
  const fs::path staging = fs::path(toolpacks_root()) / (id + "-" + entry->version + ".tmp");
  fs::remove_all(staging, ec);
  fs::create_directories(staging, ec);

  report_progress(on_progress, 82, id);
  const std::string extract_err = extract_archive(archive_path.string(), staging.string());
  if (!extract_err.empty()) {
    fs::remove_all(staging, ec);
    result.message = extract_err;
    return result;
  }
  report_progress(on_progress, 92, id);

  const fs::path payload = unwrap_payload_root(staging);
  if (!looks_like_payload_root(payload)) {
    fs::remove_all(staging, ec);
    result.message = "el archivo no contiene toolpack.json en la raiz del payload";
    return result;
  }

  const auto meta = load_toolpack_meta((payload / "toolpack.json").string());
  if (!meta.has_value() || meta->id != id) {
    fs::remove_all(staging, ec);
    result.message = "toolpack.json invalido o id distinto de " + id;
    return result;
  }
  if (!is_executable_path((payload / meta->entry.path).string())) {
    fs::remove_all(staging, ec);
    result.message = "binario del toolpack no ejecutable: " + meta->entry.path;
    return result;
  }

  fs::remove_all(final_root, ec);
  fs::create_directories(final_root.parent_path(), ec);
  if (payload == staging) {
    fs::rename(staging, final_root, ec);
  } else {
    fs::rename(payload, final_root, ec);
    fs::remove_all(staging, ec);
  }
  if (ec) {
    result.message = "no se pudo instalar en " + final_root.string() + ": " + ec.message();
    return result;
  }

  Manifest manifest;
  if (const auto existing = load_manifest(manifest_path()); existing.has_value()) {
    manifest = *existing;
  }
  // Drop other versions of same id from disk + manifest.
  Manifest cleaned;
  cleaned.schema = 1;
  for (const auto& old : manifest.installed) {
    if (old.id == id) {
      const fs::path old_root = fs::path(toolpacks_root()) / old.path;
      if (old_root != final_root) {
        fs::remove_all(old_root, ec);
      }
      continue;
    }
    cleaned.installed.push_back(old);
  }
  ManifestEntry installed;
  installed.id = id;
  installed.version = entry->version;
  installed.active = true;
  installed.installed_at = utc_now_iso();
  installed.source = "catalog";
  installed.path = (fs::path(id) / entry->version).string();
  upsert_manifest_entry(&cleaned, installed);
  if (!save_manifest(manifest_path(), cleaned)) {
    result.message = "toolpack extraido pero no se pudo escribir manifest.json";
    return result;
  }

  result.ok = true;
  result.id = id;
  result.version = entry->version;
  result.root_path = final_root.string();
  result.message = "instalado " + id + " " + entry->version + " en " + final_root.string();
  report_progress(on_progress, 100, id);
  return result;
}

InstallResult update_toolpack(const std::string& id, ProgressFn on_progress) {
  if (id.empty()) {
    InstallResult result;
    result.message = "id de toolpack vacio";
    return result;
  }
  return install_toolpack(id, std::move(on_progress));
}

InstallResult remove_toolpack(const std::string& id) {
  InstallResult result;
  result.id = id;
  if (id.empty()) {
    result.message = "id vacio";
    return result;
  }

  auto manifest = load_manifest(manifest_path());
  if (!manifest.has_value()) {
    result.message = "no hay manifest; nada que eliminar";
    return result;
  }

  Manifest cleaned;
  cleaned.schema = manifest->schema;
  bool found = false;
  std::error_code ec;
  for (const auto& entry : manifest->installed) {
    if (entry.id != id) {
      cleaned.installed.push_back(entry);
      continue;
    }
    found = true;
    fs::remove_all(fs::path(toolpacks_root()) / entry.path, ec);
  }
  if (!found) {
    result.message = "toolpack no instalado: " + id;
    return result;
  }
  if (!save_manifest(manifest_path(), cleaned)) {
    result.message = "no se pudo actualizar manifest.json";
    return result;
  }
  result.ok = true;
  result.message = "eliminado " + id;
  return result;
}

}  // namespace tuide::toolpacks
