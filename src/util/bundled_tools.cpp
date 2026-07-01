#include "util/bundled_tools.hpp"

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#ifdef TGDB_HAS_BUNDLED_CLANGD
#include <zstd.h>
#include "bundled_clangd_manifest.hpp"

extern "C" {
extern const unsigned char _binary_clangd_blob_zst_start[];
extern const unsigned char _binary_clangd_blob_zst_end[];
}
#endif

#ifdef TGDB_HAS_BUNDLED_GDB
#ifndef TGDB_HAS_BUNDLED_CLANGD
#include <zstd.h>
#endif
#include "bundled_gdb_manifest.hpp"

extern "C" {
extern const unsigned char _binary_gdb_blob_zst_start[];
extern const unsigned char _binary_gdb_blob_zst_end[];
}
#endif

#ifdef TGDB_HAS_BUNDLED_RG
#if !defined(TGDB_HAS_BUNDLED_CLANGD) && !defined(TGDB_HAS_BUNDLED_GDB)
#include <zstd.h>
#endif
#include "bundled_rg_manifest.hpp"

extern "C" {
extern const unsigned char _binary_rg_blob_zst_start[];
extern const unsigned char _binary_rg_blob_zst_end[];
}
#endif

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::optional<bool> g_runtime_force_bundled_clangd;
std::optional<bool> g_runtime_force_bundled_gdb;
std::optional<bool> g_runtime_force_bundled_rg;

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

std::optional<bool> parse_env_bool(const char* name) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || raw[0] == '\0') {
    return std::nullopt;
  }
  const std::string value = trim_ascii(raw);
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return std::nullopt;
}

std::string bundled_cache_root() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg != nullptr && xdg[0] != '\0') {
    return (fs::path(xdg) / "tgdb" / "bundled").string();
  }
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return (fs::path(home) / ".cache" / "tgdb" / "bundled").string();
  }
  return {};
}

std::string read_text_file(const fs::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

bool write_text_file(const fs::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return false;
  }
  output << text;
  return static_cast<bool>(output);
}

bool is_executable_file(const std::string& path) {
  return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

std::optional<std::string> find_gdb_on_path() {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr || path_env[0] == '\0') {
    return std::nullopt;
  }
  std::stringstream stream(path_env);
  std::string dir;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) {
      continue;
    }
    const fs::path candidate = fs::path(dir) / "gdb";
    if (is_executable_file(candidate.string())) {
      return candidate.string();
    }
  }
  return std::nullopt;
}

std::optional<std::string> gdb_from_env() {
  const char* raw = std::getenv("GDB_PATH");
  if (raw == nullptr || raw[0] == '\0') {
    return std::nullopt;
  }
  if (!is_executable_file(raw)) {
    return std::nullopt;
  }
  return std::string(raw);
}

#if defined(TGDB_HAS_BUNDLED_CLANGD) || defined(TGDB_HAS_BUNDLED_GDB) || defined(TGDB_HAS_BUNDLED_RG)
std::optional<std::vector<unsigned char>> decompress_zstd_blob(const unsigned char* start,
                                                                 const unsigned char* end) {
  const std::size_t compressed_size = static_cast<std::size_t>(end - start);
  if (compressed_size == 0) {
    return std::nullopt;
  }

  const unsigned long long decompressed_size =
      ZSTD_getFrameContentSize(start, compressed_size);
  if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
      decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return std::nullopt;
  }

  std::vector<unsigned char> output(static_cast<std::size_t>(decompressed_size));
  const std::size_t written =
      ZSTD_decompress(output.data(), output.size(), start, compressed_size);
  if (ZSTD_isError(written) || written != output.size()) {
    return std::nullopt;
  }
  return output;
}
#endif

std::optional<std::string> find_clangd_on_path() {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr || path_env[0] == '\0') {
    return std::nullopt;
  }
  std::stringstream stream(path_env);
  std::string dir;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) {
      continue;
    }
    const fs::path candidate = fs::path(dir) / "clangd";
    if (is_executable_file(candidate.string())) {
      return candidate.string();
    }
  }
  return std::nullopt;
}

std::optional<std::string> find_rg_on_path() {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr || path_env[0] == '\0') {
    return std::nullopt;
  }
  std::stringstream stream(path_env);
  std::string dir;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) {
      continue;
    }
    const fs::path candidate = fs::path(dir) / "rg";
    if (is_executable_file(candidate.string())) {
      return candidate.string();
    }
  }
  return std::nullopt;
}

std::optional<std::string> rg_from_env() {
  const char* raw = std::getenv("RG_PATH");
  if (raw == nullptr || raw[0] == '\0') {
    return std::nullopt;
  }
  if (!is_executable_file(raw)) {
    return std::nullopt;
  }
  return std::string(raw);
}

std::optional<std::string> clangd_from_env() {
  const char* raw = std::getenv("CLANGD_PATH");
  if (raw == nullptr || raw[0] == '\0') {
    return std::nullopt;
  }
  if (!is_executable_file(raw)) {
    return std::nullopt;
  }
  return std::string(raw);
}

std::size_t parse_octal_field(const char* field, std::size_t length) {
  std::size_t value = 0;
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned char ch = static_cast<unsigned char>(field[i]);
    if (ch == '\0' || ch == ' ') {
      break;
    }
    if (ch < '0' || ch > '7') {
      break;
    }
    value = (value << 3) + (ch - '0');
  }
  return value;
}

bool extract_tar_to_directory(const std::vector<unsigned char>& tar_data,
                              const fs::path& output_dir) {
  constexpr std::size_t kBlock = 512;
  if (tar_data.size() % kBlock != 0) {
    return false;
  }

  std::size_t offset = 0;
  while (offset < tar_data.size()) {
    const auto* header = reinterpret_cast<const char*>(tar_data.data() + offset);
    offset += kBlock;

    bool all_zero = true;
    for (std::size_t i = 0; i < kBlock; ++i) {
      if (header[i] != '\0') {
        all_zero = false;
        break;
      }
    }
    if (all_zero) {
      break;
    }

    std::string name(header, header + 100);
    const auto null_pos = name.find('\0');
    if (null_pos != std::string::npos) {
      name.resize(null_pos);
    }
    if (name.empty()) {
      return false;
    }

    const char typeflag = header[156];
    const std::size_t file_size = parse_octal_field(header + 124, 12);
    const std::size_t padded_size = ((file_size + kBlock - 1) / kBlock) * kBlock;
    if (offset + padded_size > tar_data.size()) {
      return false;
    }

    const fs::path target = output_dir / name;
    if (typeflag == '5' || (typeflag == '\0' && file_size == 0 && name.back() == '/')) {
      std::error_code ec;
      fs::create_directories(target, ec);
    } else if (typeflag == '\0' || typeflag == '0') {
      std::error_code ec;
      fs::create_directories(target.parent_path(), ec);
      std::ofstream output(target, std::ios::binary | std::ios::trunc);
      if (!output) {
        return false;
      }
      output.write(reinterpret_cast<const char*>(tar_data.data() + offset),
                   static_cast<std::streamsize>(file_size));
      if (!output) {
        return false;
      }
      std::error_code perm_ec;
      fs::permissions(target, fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                      perm_ec);
    } else {
      return false;
    }

    offset += padded_size;
  }
  return true;
}

#ifdef TGDB_HAS_BUNDLED_CLANGD
std::optional<std::string> find_bundled_resource_dir(const fs::path& install_root) {
  std::error_code ec;
  const fs::path clang_base = install_root / "lib" / "clang";
  if (fs::is_directory(clang_base, ec)) {
    for (const auto& entry : fs::directory_iterator(clang_base, ec)) {
      if (!entry.is_directory()) {
        continue;
      }
      if (fs::is_directory(entry.path() / "include", ec)) {
        return entry.path().string();
      }
    }
  }

  const fs::path manifest_dir = install_root / TGDB_BUNDLED_CLANGD_RESOURCE_SUBDIR;
  if (fs::is_directory(manifest_dir / "include", ec)) {
    return manifest_dir.string();
  }
  return std::nullopt;
}

std::optional<ClangdLocation> resolve_bundled_clangd() {
  const fs::path install_root =
      fs::path(bundled_cache_root()) / ("clangd-" TGDB_BUNDLED_CLANGD_VERSION);
  const fs::path binary_path = install_root / "bin" / "clangd";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TGDB_BUNDLED_CLANGD_BLOB_SHA256) + "\n";

  if (is_executable_file(binary_path.string()) && read_text_file(marker) == expected_marker) {
    if (const auto resource_dir = find_bundled_resource_dir(install_root); resource_dir.has_value()) {
      return ClangdLocation{binary_path.string(), *resource_dir,
                            ClangdLocation::Source::Bundled};
    }
  }

  const auto tar_data = decompress_zstd_blob(_binary_clangd_blob_zst_start,
                                             _binary_clangd_blob_zst_end);
  if (!tar_data.has_value()) {
    return std::nullopt;
  }

  const fs::path temp_root = install_root.string() + ".tmp";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  if (!extract_tar_to_directory(*tar_data, temp_root)) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  if (!is_executable_file((temp_root / "bin" / "clangd").string()) ||
      !find_bundled_resource_dir(temp_root).has_value()) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  fs::remove_all(install_root, ec);
  fs::rename(temp_root, install_root, ec);
  if (ec) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  if (!write_text_file(marker, expected_marker)) {
    return std::nullopt;
  }

  if (const auto resource_dir = find_bundled_resource_dir(install_root); resource_dir.has_value()) {
    return ClangdLocation{binary_path.string(), *resource_dir,
                          ClangdLocation::Source::Bundled};
  }
  return std::nullopt;
}
#endif

#ifdef TGDB_HAS_BUNDLED_GDB
std::optional<GdbLocation> resolve_bundled_gdb() {
  const fs::path install_root =
      fs::path(bundled_cache_root()) / ("gdb-" TGDB_BUNDLED_GDB_VERSION);
  const fs::path binary_path = install_root / "bin" / "gdb";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TGDB_BUNDLED_GDB_BLOB_SHA256) + "\n";

  if (is_executable_file(binary_path.string()) && read_text_file(marker) == expected_marker) {
    return GdbLocation{binary_path.string(), GdbLocation::Source::Bundled};
  }

  const auto tar_data =
      decompress_zstd_blob(_binary_gdb_blob_zst_start, _binary_gdb_blob_zst_end);
  if (!tar_data.has_value()) {
    return std::nullopt;
  }

  const fs::path temp_root = install_root.string() + ".tmp";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  if (!extract_tar_to_directory(*tar_data, temp_root)) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  if (!is_executable_file((temp_root / "bin" / "gdb").string())) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  fs::remove_all(install_root, ec);
  fs::rename(temp_root, install_root, ec);
  if (ec) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  if (!write_text_file(marker, expected_marker)) {
    return std::nullopt;
  }

  return GdbLocation{binary_path.string(), GdbLocation::Source::Bundled};
}
#endif

#ifdef TGDB_HAS_BUNDLED_RG
std::optional<RgLocation> resolve_bundled_rg() {
  const fs::path install_root = fs::path(bundled_cache_root()) / ("rg-" TGDB_BUNDLED_RG_VERSION);
  const fs::path binary_path = install_root / "bin" / "rg";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TGDB_BUNDLED_RG_BLOB_SHA256) + "\n";

  if (is_executable_file(binary_path.string()) && read_text_file(marker) == expected_marker) {
    return RgLocation{binary_path.string(), RgLocation::Source::Bundled};
  }

  const auto tar_data = decompress_zstd_blob(_binary_rg_blob_zst_start, _binary_rg_blob_zst_end);
  if (!tar_data.has_value()) {
    return std::nullopt;
  }

  const fs::path temp_root = install_root.string() + ".tmp";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  if (!extract_tar_to_directory(*tar_data, temp_root)) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  if (!is_executable_file((temp_root / "bin" / "rg").string())) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  fs::remove_all(install_root, ec);
  fs::rename(temp_root, install_root, ec);
  if (ec) {
    fs::remove_all(temp_root, ec);
    return std::nullopt;
  }

  if (!write_text_file(marker, expected_marker)) {
    return std::nullopt;
  }

  return RgLocation{binary_path.string(), RgLocation::Source::Bundled};
}
#endif

}  // namespace

bool has_bundled_clangd() {
#ifdef TGDB_HAS_BUNDLED_CLANGD
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_clangd(bool value) { g_runtime_force_bundled_clangd = value; }

bool should_force_bundled_clangd() {
  if (const auto env = parse_env_bool("TGDB_FORCE_BUNDLED_CLANGD"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_clangd.has_value()) {
    return *g_runtime_force_bundled_clangd;
  }
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_CLANGD
  return true;
#else
  return false;
#endif
}

bool has_bundled_gdb() {
#ifdef TGDB_HAS_BUNDLED_GDB
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_gdb(bool value) { g_runtime_force_bundled_gdb = value; }

bool should_force_bundled_gdb() {
  if (const auto env = parse_env_bool("TGDB_FORCE_BUNDLED_GDB"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_gdb.has_value()) {
    return *g_runtime_force_bundled_gdb;
  }
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_GDB
  return true;
#else
  return false;
#endif
}

std::optional<ClangdLocation> resolve_clangd() {
  if (const auto env_path = clangd_from_env(); env_path.has_value()) {
    return ClangdLocation{*env_path, {}, ClangdLocation::Source::Env};
  }

#ifdef TGDB_HAS_BUNDLED_CLANGD
  if (const auto bundled = resolve_bundled_clangd(); bundled.has_value()) {
    return bundled;
  }
#endif

  if (!should_force_bundled_clangd()) {
    if (const auto path_bin = find_clangd_on_path(); path_bin.has_value()) {
      return ClangdLocation{*path_bin, {}, ClangdLocation::Source::SystemPath};
    }
  }

  return std::nullopt;
}

std::optional<GdbLocation> resolve_gdb() {
  if (const auto env_path = gdb_from_env(); env_path.has_value()) {
    return GdbLocation{*env_path, GdbLocation::Source::Env};
  }

#ifdef TGDB_HAS_BUNDLED_GDB
  if (const auto bundled = resolve_bundled_gdb(); bundled.has_value()) {
    return bundled;
  }
#endif

  if (!should_force_bundled_gdb()) {
    if (const auto path_bin = find_gdb_on_path(); path_bin.has_value()) {
      return GdbLocation{*path_bin, GdbLocation::Source::SystemPath};
    }
  }

  return std::nullopt;
}

bool has_bundled_rg() {
#ifdef TGDB_HAS_BUNDLED_RG
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_rg(bool value) { g_runtime_force_bundled_rg = value; }

bool should_force_bundled_rg() {
  if (const auto env = parse_env_bool("TGDB_FORCE_BUNDLED_RG"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_rg.has_value()) {
    return *g_runtime_force_bundled_rg;
  }
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_RG
  return true;
#else
  return false;
#endif
}

std::optional<RgLocation> resolve_rg() {
  if (const auto env_path = rg_from_env(); env_path.has_value()) {
    return RgLocation{*env_path, RgLocation::Source::Env};
  }

#ifdef TGDB_HAS_BUNDLED_RG
  if (const auto bundled = resolve_bundled_rg(); bundled.has_value()) {
    return bundled;
  }
#endif

  if (!should_force_bundled_rg()) {
    if (const auto path_bin = find_rg_on_path(); path_bin.has_value()) {
      return RgLocation{*path_bin, RgLocation::Source::SystemPath};
    }
  }

  return std::nullopt;
}

}  // namespace tgdb
