#include "toolpacks/catalog.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "toolpacks/download.hpp"
#include "toolpacks/paths.hpp"

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

std::string read_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

std::vector<std::string> string_array(const nlohmann::json& doc, const char* key) {
  std::vector<std::string> out;
  if (!doc.contains(key) || !doc[key].is_array()) {
    return out;
  }
  for (const auto& item : doc[key]) {
    if (item.is_string()) {
      out.push_back(item.get<std::string>());
    }
  }
  return out;
}

}  // namespace

std::optional<Catalog> parse_catalog_json(const std::string& text) {
  if (text.empty()) {
    return std::nullopt;
  }
  try {
    const nlohmann::json doc = nlohmann::json::parse(text);
    Catalog catalog;
    catalog.schema = doc.value("schema", 1);
    catalog.tuide_min_version = doc.value("tuide_min_version", "");
    if (!doc.contains("toolpacks") || !doc["toolpacks"].is_array()) {
      return catalog;
    }
    for (const auto& item : doc["toolpacks"]) {
      CatalogToolpack tp;
      tp.id = item.value("id", "");
      tp.display_name = item.value("display_name", tp.id);
      tp.kind = item.value("kind", "");
      tp.version = item.value("version", "");
      tp.arch = string_array(item, "arch");
      tp.os = string_array(item, "os");
      tp.url = item.value("url", "");
      tp.sha256 = item.value("sha256", "");
      tp.size_bytes = item.value("size_bytes", static_cast<std::uint64_t>(0));
      tp.license = item.value("license", "");
      if (tp.id.empty() || tp.version.empty() || tp.url.empty() || tp.sha256.empty()) {
        continue;
      }
      catalog.toolpacks.push_back(std::move(tp));
    }
    return catalog;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

std::optional<Catalog> load_catalog_file(const std::string& path) {
  return parse_catalog_json(read_file(path));
}

std::optional<Catalog> fetch_catalog(std::string* error_out) {
  const std::string url = default_catalog_url();
  std::error_code ec;
  fs::create_directories(downloads_dir(), ec);
  const std::string dest = (fs::path(downloads_dir()) / "catalog.json").string();

  // Support file:// and plain paths for local testing.
  if (url.rfind("file://", 0) == 0) {
    const std::string local = url.substr(7);
    const auto catalog = load_catalog_file(local);
    if (!catalog.has_value() && error_out != nullptr) {
      *error_out = "no se pudo leer el catalogo local: " + local;
    }
    return catalog;
  }
  if (!url.empty() && url[0] == '/') {
    const auto catalog = load_catalog_file(url);
    if (!catalog.has_value() && error_out != nullptr) {
      *error_out = "no se pudo leer el catalogo: " + url;
    }
    return catalog;
  }

  const std::string err = download_url(url, dest);
  if (!err.empty()) {
    if (error_out != nullptr) {
      *error_out = err;
    }
    return std::nullopt;
  }
  const auto catalog = load_catalog_file(dest);
  if (!catalog.has_value() && error_out != nullptr) {
    *error_out = "catalog.json invalido tras descargar desde " + url;
  }
  return catalog;
}

std::optional<CatalogToolpack> find_catalog_toolpack(const Catalog& catalog,
                                                     const std::string& id,
                                                     const std::string& version) {
  const CatalogToolpack* fallback = nullptr;
  for (const auto& tp : catalog.toolpacks) {
    if (tp.id != id) {
      continue;
    }
    bool arch_ok = tp.arch.empty();
    for (const auto& a : tp.arch) {
      if (a == "x86_64" || a == "amd64") {
        arch_ok = true;
        break;
      }
    }
    if (!arch_ok) {
      continue;
    }
    if (!version.empty()) {
      if (tp.version == version) {
        return tp;
      }
      continue;
    }
    if (fallback == nullptr) {
      fallback = &tp;
    }
  }
  if (fallback != nullptr) {
    return *fallback;
  }
  return std::nullopt;
}

}  // namespace tuide::toolpacks
