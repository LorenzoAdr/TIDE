#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tuide::toolpacks {

struct CatalogToolpack {
  std::string id;
  std::string display_name;
  std::string kind;
  std::string version;
  std::vector<std::string> arch;
  std::vector<std::string> os;
  std::string url;
  std::string sha256;
  std::uint64_t size_bytes = 0;
  std::string license;
};

struct Catalog {
  int schema = 1;
  std::string tuide_min_version;
  std::vector<CatalogToolpack> toolpacks;
};

std::optional<Catalog> parse_catalog_json(const std::string& text);
std::optional<Catalog> load_catalog_file(const std::string& path);

// Canonical catalog arch for this process: "x86_64" or "aarch64".
std::string host_catalog_arch();
// Map amd64/x64/arm64 aliases to the catalog spelling.
std::string normalize_catalog_arch(const std::string& raw);
// Empty arch list = legacy "any". Otherwise host must match.
bool catalog_arch_matches_host(const std::vector<std::string>& archs);
// "linux-x86_64" / "linux-aarch64" for asset and cache names.
std::string linux_catalog_archive_infix();

// Fetch catalog.json via curl/wget into downloads cache; returns parsed catalog.
std::optional<Catalog> fetch_catalog(std::string* error_out = nullptr);

std::optional<CatalogToolpack> find_catalog_toolpack(const Catalog& catalog,
                                                     const std::string& id,
                                                     const std::string& version = {});

}  // namespace tuide::toolpacks
