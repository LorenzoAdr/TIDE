#include "util/bundled_tools.hpp"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
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

#ifdef TGDB_HAS_BUNDLED_PYTHON_TOOLS
#if !defined(TGDB_HAS_BUNDLED_CLANGD) && !defined(TGDB_HAS_BUNDLED_GDB) && \
    !defined(TGDB_HAS_BUNDLED_RG)
#include <zstd.h>
#endif
#include "bundled_python_tools_manifest.hpp"

extern "C" {
extern const unsigned char _binary_python_tools_blob_zst_start[];
extern const unsigned char _binary_python_tools_blob_zst_end[];
}
#endif

#ifdef TGDB_HAS_BUNDLED_TEXLAB
#if !defined(TGDB_HAS_BUNDLED_CLANGD) && !defined(TGDB_HAS_BUNDLED_GDB) && \
    !defined(TGDB_HAS_BUNDLED_RG) && !defined(TGDB_HAS_BUNDLED_PYTHON_TOOLS)
#include <zstd.h>
#endif
#include "bundled_texlab_manifest.hpp"
extern "C" {
extern const unsigned char _binary_texlab_blob_zst_start[];
extern const unsigned char _binary_texlab_blob_zst_end[];
}
#endif

#ifdef TGDB_HAS_BUNDLED_BASH_LS
#if !defined(TGDB_HAS_BUNDLED_CLANGD) && !defined(TGDB_HAS_BUNDLED_GDB) && \
    !defined(TGDB_HAS_BUNDLED_RG) && !defined(TGDB_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TGDB_HAS_BUNDLED_TEXLAB)
#include <zstd.h>
#endif
#include "bundled_bash_ls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_bash_ls_blob_zst_start[];
extern const unsigned char _binary_bash_ls_blob_zst_end[];
}
#endif

#ifdef TGDB_HAS_BUNDLED_BASH_DAP
#if !defined(TGDB_HAS_BUNDLED_CLANGD) && !defined(TGDB_HAS_BUNDLED_GDB) && \
    !defined(TGDB_HAS_BUNDLED_RG) && !defined(TGDB_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TGDB_HAS_BUNDLED_TEXLAB) && !defined(TGDB_HAS_BUNDLED_BASH_LS)
#include <zstd.h>
#endif
#include "bundled_bash_dap_manifest.hpp"
extern "C" {
extern const unsigned char _binary_bash_dap_blob_zst_start[];
extern const unsigned char _binary_bash_dap_blob_zst_end[];
}
#endif

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::optional<bool> g_runtime_force_bundled_clangd;
std::optional<bool> g_runtime_force_bundled_gdb;
std::optional<bool> g_runtime_force_bundled_rg;
std::optional<bool> g_runtime_force_bundled_python_tools;

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

#if defined(TGDB_HAS_BUNDLED_CLANGD) || defined(TGDB_HAS_BUNDLED_GDB) || \
    defined(TGDB_HAS_BUNDLED_RG) || defined(TGDB_HAS_BUNDLED_PYTHON_TOOLS) || \
    defined(TGDB_HAS_BUNDLED_TEXLAB) || defined(TGDB_HAS_BUNDLED_BASH_LS) || \
    defined(TGDB_HAS_BUNDLED_BASH_DAP)
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
    // ustar long names: prefix (155) + "/" + name (100)
    const bool is_ustar = std::memcmp(header + 257, "ustar", 5) == 0;
    if (is_ustar) {
      std::string prefix(header + 345, header + 345 + 155);
      const auto prefix_null = prefix.find('\0');
      if (prefix_null != std::string::npos) {
        prefix.resize(prefix_null);
      }
      if (!prefix.empty()) {
        name = prefix + "/" + name;
      }
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
    if (typeflag == '5' || (typeflag == '\0' && file_size == 0 && !name.empty() && name.back() == '/')) {
      std::error_code ec;
      fs::create_directories(target, ec);
    } else if (typeflag == '2') {
      // Symlink: linkname at offset 157 (100 bytes). Prefer packing without symlinks;
      // still accept them so older/partial trees extract cleanly.
      std::string linkname(header + 157, header + 157 + 100);
      const auto link_null = linkname.find('\0');
      if (link_null != std::string::npos) {
        linkname.resize(link_null);
      }
      if (linkname.empty()) {
        return false;
      }
      std::error_code ec;
      fs::create_directories(target.parent_path(), ec);
      fs::remove(target, ec);
      fs::create_symlink(linkname, target, ec);
      if (ec) {
        return false;
      }
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
      // Skip unsupported types (hardlinks, devices, etc.) without failing the whole extract.
      // Python trees packed with cp -aL should only contain files/dirs.
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

#ifdef TGDB_HAS_BUNDLED_PYTHON_TOOLS
std::optional<fs::path> ensure_bundled_python_tools_root() {
  const fs::path install_root =
      fs::path(bundled_cache_root()) / ("python-tools-" TGDB_BUNDLED_PYTHON_TOOLS_VERSION);
  const fs::path langserver = install_root / "bin" / "basedpyright-langserver";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TGDB_BUNDLED_PYTHON_TOOLS_BLOB_SHA256) + "\n";

  if (is_executable_file(langserver.string()) && read_text_file(marker) == expected_marker) {
    return install_root;
  }

  const auto tar_data = decompress_zstd_blob(_binary_python_tools_blob_zst_start,
                                             _binary_python_tools_blob_zst_end);
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

  if (!is_executable_file((temp_root / "bin" / "basedpyright-langserver").string())) {
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

  return install_root;
}

std::optional<BasedpyrightLocation> resolve_bundled_basedpyright() {
  const auto root = ensure_bundled_python_tools_root();
  if (!root.has_value()) {
    return std::nullopt;
  }
  BasedpyrightLocation loc;
  loc.binary_path = (*root / "bin" / "basedpyright-langserver").string();
  loc.needs_stdio_flag = true;
  loc.source = BasedpyrightLocation::Source::Bundled;
  return loc;
}

#if TGDB_BUNDLED_PYTHON_TOOLS_KIND_FULL
std::optional<DebugpyLocation> resolve_bundled_debugpy() {
  const auto root = ensure_bundled_python_tools_root();
  if (!root.has_value()) {
    return std::nullopt;
  }
  const fs::path python = *root / "bin" / "python3";
  if (!is_executable_file(python.string())) {
    return std::nullopt;
  }
  return DebugpyLocation{python.string(), DebugpyLocation::Source::Bundled};
}
#endif
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

bool has_bundled_python_tools() {
#ifdef TGDB_HAS_BUNDLED_PYTHON_TOOLS
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_python_tools(bool value) {
  g_runtime_force_bundled_python_tools = value;
}

bool should_force_bundled_python_tools() {
  if (const auto env = parse_env_bool("TGDB_FORCE_BUNDLED_PYTHON_TOOLS"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_python_tools.has_value()) {
    return *g_runtime_force_bundled_python_tools;
  }
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_PYTHON_TOOLS
  return true;
#else
  return false;
#endif
}

namespace {

std::optional<std::string> find_named_binary_on_path(const std::string& name) {
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr || path_env[0] == '\0' || name.empty()) {
    return std::nullopt;
  }
  std::stringstream stream(path_env);
  std::string dir;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) {
      continue;
    }
    const fs::path candidate = fs::path(dir) / name;
    if (is_executable_file(candidate.string())) {
      return candidate.string();
    }
  }
  return std::nullopt;
}

std::optional<std::string> env_executable(const char* var_name) {
  const char* raw = std::getenv(var_name);
  if (raw == nullptr || raw[0] == '\0') {
    return std::nullopt;
  }
  if (!is_executable_file(raw)) {
    return std::nullopt;
  }
  return std::string(raw);
}

bool python_module_importable(const std::string& python_bin, const std::string& module) {
  if (python_bin.empty() || module.empty()) {
    return false;
  }
  const std::string key = python_bin + '\n' + module;
  static std::mutex mu;
  static std::unordered_map<std::string, bool> cache;
  {
    std::lock_guard<std::mutex> lock(mu);
    if (const auto it = cache.find(key); it != cache.end()) {
      return it->second;
    }
  }
  const std::string cmd = "\"" + python_bin + "\" -c \"import " + module + "\" >/dev/null 2>&1";
  const bool ok = std::system(cmd.c_str()) == 0;
  {
    std::lock_guard<std::mutex> lock(mu);
    cache[key] = ok;
  }
  return ok;
}

}  // namespace

std::optional<BasedpyrightLocation> resolve_basedpyright() {
  if (const auto env_path = env_executable("BASEDPYRIGHT_PATH"); env_path.has_value()) {
    BasedpyrightLocation loc;
    loc.binary_path = *env_path;
    loc.needs_stdio_flag = true;
    loc.source = BasedpyrightLocation::Source::Env;
    return loc;
  }
  if (const auto env_path = env_executable("PYRIGHT_LANGSERVER_PATH"); env_path.has_value()) {
    BasedpyrightLocation loc;
    loc.binary_path = *env_path;
    loc.needs_stdio_flag = true;
    loc.source = BasedpyrightLocation::Source::Env;
    return loc;
  }

#ifdef TGDB_HAS_BUNDLED_PYTHON_TOOLS
  if (const auto bundled = resolve_bundled_basedpyright(); bundled.has_value()) {
    return bundled;
  }
#endif

  if (!should_force_bundled_python_tools()) {
    // Prefer dedicated langserver binaries (not the typecheck CLI).
    static const char* kCandidates[] = {"basedpyright-langserver", "pyright-langserver"};
    for (const char* name : kCandidates) {
      if (const auto path_bin = find_named_binary_on_path(name); path_bin.has_value()) {
        BasedpyrightLocation loc;
        loc.binary_path = *path_bin;
        loc.needs_stdio_flag = true;
        loc.source = BasedpyrightLocation::Source::SystemPath;
        return loc;
      }
    }

    // Fallback: python -m basedpyright.langserver / pyright.langserver
    static const char* kPythons[] = {"python3", "python"};
    for (const char* name : kPythons) {
      if (const auto path_bin = find_named_binary_on_path(name); path_bin.has_value()) {
        if (python_module_importable(*path_bin, "basedpyright")) {
          BasedpyrightLocation loc;
          loc.binary_path = *path_bin;
          loc.needs_stdio_flag = true;
          loc.source = BasedpyrightLocation::Source::SystemPath;
          loc.use_python_module = true;
          loc.python_module = "basedpyright.langserver";
          return loc;
        }
        if (python_module_importable(*path_bin, "pyright")) {
          BasedpyrightLocation loc;
          loc.binary_path = *path_bin;
          loc.needs_stdio_flag = true;
          loc.source = BasedpyrightLocation::Source::SystemPath;
          loc.use_python_module = true;
          loc.python_module = "pyright.langserver";
          return loc;
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<DebugpyLocation> resolve_debugpy() {
  if (const auto env_path = env_executable("DEBUGPY_PYTHON"); env_path.has_value()) {
    if (python_module_importable(*env_path, "debugpy")) {
      return DebugpyLocation{*env_path, DebugpyLocation::Source::Env};
    }
  }
  if (const auto env_path = env_executable("PYTHON"); env_path.has_value()) {
    if (python_module_importable(*env_path, "debugpy")) {
      return DebugpyLocation{*env_path, DebugpyLocation::Source::Env};
    }
  }

#if defined(TGDB_HAS_BUNDLED_PYTHON_TOOLS) && TGDB_BUNDLED_PYTHON_TOOLS_KIND_FULL
  if (const auto bundled = resolve_bundled_debugpy(); bundled.has_value()) {
    return bundled;
  }
  if (should_force_bundled_python_tools()) {
    return std::nullopt;
  }
#endif

  static const char* kPythons[] = {"python3", "python"};
  for (const char* name : kPythons) {
    if (const auto path_bin = find_named_binary_on_path(name); path_bin.has_value()) {
      if (python_module_importable(*path_bin, "debugpy")) {
        return DebugpyLocation{*path_bin, DebugpyLocation::Source::SystemPath};
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> resolve_shellcheck() {
  if (const auto env_path = env_executable("SHELLCHECK_PATH"); env_path.has_value()) {
    return *env_path;
  }
#ifdef TGDB_HAS_BUNDLED_BASH_LS
  {
    const fs::path bundled =
        fs::path(bundled_cache_root()) / ("bash-ls-" TGDB_BUNDLED_BASH_LS_VERSION) / "bin" /
        "shellcheck";
    if (is_executable_file(bundled.string())) {
      return bundled.string();
    }
  }
#endif
  return find_named_binary_on_path("shellcheck");
}

std::optional<std::string> resolve_chktex() {
  if (const auto env_path = env_executable("CHKTEX_PATH"); env_path.has_value()) {
    return *env_path;
  }
#ifdef TGDB_HAS_BUNDLED_TEXLAB
  {
    const fs::path bundled = fs::path(bundled_cache_root()) /
                             ("texlab-" TGDB_BUNDLED_TEXLAB_VERSION) / "bin" / "chktex";
    if (is_executable_file(bundled.string())) {
      return bundled.string();
    }
  }
#endif
  return find_named_binary_on_path("chktex");
}

std::optional<BashLsLocation> resolve_bash_language_server() {
  if (const auto env_path = env_executable("BASH_LANGUAGE_SERVER_PATH"); env_path.has_value()) {
    return BashLsLocation{*env_path, BashLsLocation::Source::Env};
  }
#ifdef TGDB_HAS_BUNDLED_BASH_LS
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("bash-ls-" TGDB_BUNDLED_BASH_LS_VERSION);
    const fs::path binary_path = install_root / "bin" / "bash-language-server";
    const fs::path marker = install_root / ".installed";
    const std::string expected = std::string(TGDB_BUNDLED_BASH_LS_BLOB_SHA256) + "\n";
    // Prefer an existing install immediately. Marker mismatch used to force a synchronous
    // re-extract (100MB+ tar) on every resolve — Status UI calls this every frame.
    if (is_executable_file(binary_path.string())) {
      return BashLsLocation{binary_path.string(), BashLsLocation::Source::Bundled};
    }
    static std::atomic<bool> install_attempted{false};
    if (!install_attempted.exchange(true, std::memory_order_acq_rel)) {
      const auto tar_data =
          decompress_zstd_blob(_binary_bash_ls_blob_zst_start, _binary_bash_ls_blob_zst_end);
      if (tar_data.has_value()) {
        const fs::path temp_root = install_root.string() + ".tmp";
        std::error_code ec;
        fs::remove_all(temp_root, ec);
        fs::create_directories(temp_root, ec);
        if (extract_tar_to_directory(*tar_data, temp_root) &&
            is_executable_file((temp_root / "bin" / "bash-language-server").string())) {
          fs::remove_all(install_root, ec);
          fs::rename(temp_root, install_root, ec);
          if (!ec) {
            write_text_file(marker, expected);
          } else {
            fs::remove_all(temp_root, ec);
          }
        } else {
          fs::remove_all(temp_root, ec);
        }
      }
    }
    if (is_executable_file(binary_path.string())) {
      return BashLsLocation{binary_path.string(), BashLsLocation::Source::Bundled};
    }
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_BASH_LS
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("bash-language-server"); path_bin.has_value()) {
    return BashLsLocation{*path_bin, BashLsLocation::Source::SystemPath};
  }
  return std::nullopt;
}

std::optional<TexlabLocation> resolve_texlab() {
  if (const auto env_path = env_executable("TEXLAB_PATH"); env_path.has_value()) {
    return TexlabLocation{*env_path, TexlabLocation::Source::Env};
  }
#ifdef TGDB_HAS_BUNDLED_TEXLAB
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("texlab-" TGDB_BUNDLED_TEXLAB_VERSION);
    const fs::path binary_path = install_root / "bin" / "texlab";
    const fs::path chktex_path = install_root / "bin" / "chktex";
    const fs::path marker = install_root / ".installed";
    const std::string expected = std::string(TGDB_BUNDLED_TEXLAB_BLOB_SHA256) + "\n";
    auto install_ready = [&]() {
      // Require chktex too: older extracts only had texlab and would stick forever if we
      // only checked the LSP binary (Status UI / resolve hit this path every frame).
      return is_executable_file(binary_path.string()) &&
             is_executable_file(chktex_path.string()) && read_text_file(marker) == expected;
    };
    if (install_ready()) {
      return TexlabLocation{binary_path.string(), TexlabLocation::Source::Bundled};
    }
    static std::atomic<bool> install_attempted{false};
    if (!install_attempted.exchange(true, std::memory_order_acq_rel)) {
      const auto tar_data =
          decompress_zstd_blob(_binary_texlab_blob_zst_start, _binary_texlab_blob_zst_end);
      if (tar_data.has_value()) {
        const fs::path temp_root = install_root.string() + ".tmp";
        std::error_code ec;
        fs::remove_all(temp_root, ec);
        fs::create_directories(temp_root, ec);
        if (extract_tar_to_directory(*tar_data, temp_root) &&
            is_executable_file((temp_root / "bin" / "texlab").string()) &&
            is_executable_file((temp_root / "bin" / "chktex").string())) {
          fs::remove_all(install_root, ec);
          fs::rename(temp_root, install_root, ec);
          if (!ec) {
            write_text_file(marker, expected);
          } else {
            fs::remove_all(temp_root, ec);
          }
        } else {
          fs::remove_all(temp_root, ec);
        }
      }
    }
    if (install_ready()) {
      return TexlabLocation{binary_path.string(), TexlabLocation::Source::Bundled};
    }
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_TEXLAB
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("texlab"); path_bin.has_value()) {
    return TexlabLocation{*path_bin, TexlabLocation::Source::SystemPath};
  }
  return std::nullopt;
}

bool readable_file(const std::string& path) {
  return !path.empty() && ::access(path.c_str(), R_OK) == 0;
}

std::optional<BashDebugAdapterLocation> resolve_bash_debug_adapter() {
#ifdef TGDB_HAS_BUNDLED_BASH_DAP
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("bash-dap-" TGDB_BUNDLED_BASH_DAP_VERSION);
    const fs::path adapter_js = install_root / "adapter" / "bashDebug.js";
    const fs::path bashdb = install_root / "bashdb" / "bashdb";
    const fs::path marker = install_root / ".installed";
    const std::string expected = std::string(TGDB_BUNDLED_BASH_DAP_BLOB_SHA256) + "\n";
    const auto try_build_location = [&]() -> std::optional<BashDebugAdapterLocation> {
      if (!(readable_file(adapter_js.string()) && is_executable_file(bashdb.string()))) {
        return std::nullopt;
      }
      // Reject stale extracts that predate node_modules packaging (initialize hang).
      const fs::path node_modules_marker =
          install_root / "adapter" / "node_modules" / "vscode-debugadapter";
      if (!fs::exists(node_modules_marker)) {
        return std::nullopt;
      }
      if (fs::exists(marker)) {
        if (read_text_file(marker) != expected) {
          return std::nullopt;
        }
      }
      std::string node;
#if TGDB_BUNDLED_BASH_DAP_HAS_NODE
      const fs::path node_path = install_root / "node" / "bin" / "node";
      if (is_executable_file(node_path.string())) {
        node = node_path.string();
      }
#endif
      if (node.empty()) {
#ifdef TGDB_HAS_BUNDLED_BASH_LS
        if (const auto ls = resolve_bash_language_server();
            ls.has_value() && ls->source == BashLsLocation::Source::Bundled) {
          const fs::path n = fs::path(ls->binary_path).parent_path() / "node";
          if (is_executable_file(n.string())) {
            node = n.string();
          }
        }
#endif
      }
      if (node.empty()) {
        if (const auto env = env_executable("TGDB_NODE_BIN"); env.has_value()) {
          node = *env;
        } else if (const auto path_bin = find_named_binary_on_path("node"); path_bin.has_value()) {
          node = *path_bin;
        }
      }
      const auto bash = find_named_binary_on_path("bash");
      if (node.empty() || !bash.has_value()) {
        return std::nullopt;
      }
      BashDebugAdapterLocation loc;
      loc.node_path = node;
      loc.adapter_js_path = adapter_js.string();
      loc.bash_path = *bash;
      loc.bashdb_path = bashdb.string();
      loc.bashdb_lib_path = (install_root / "bashdb").string();
      loc.source = BashDebugAdapterLocation::Source::Bundled;
      return loc;
    };

    // Prefer an existing install immediately (Status UI resolves this every refresh).
    if (auto existing = try_build_location(); existing.has_value()) {
      return existing;
    }

    static std::atomic<bool> install_attempted{false};
    if (!install_attempted.exchange(true, std::memory_order_acq_rel)) {
      const auto tar_data =
          decompress_zstd_blob(_binary_bash_dap_blob_zst_start, _binary_bash_dap_blob_zst_end);
      if (tar_data.has_value()) {
        const fs::path temp_root = install_root.string() + ".tmp";
        std::error_code ec;
        fs::remove_all(temp_root, ec);
        fs::create_directories(temp_root, ec);
        if (extract_tar_to_directory(*tar_data, temp_root) &&
            readable_file((temp_root / "adapter" / "bashDebug.js").string()) &&
            is_executable_file((temp_root / "bashdb" / "bashdb").string())) {
          fs::remove_all(install_root, ec);
          fs::rename(temp_root, install_root, ec);
          if (!ec) {
            write_text_file(marker, expected);
          } else {
            fs::remove_all(temp_root, ec);
          }
        } else {
          fs::remove_all(temp_root, ec);
        }
      }
    }
    if (auto installed = try_build_location(); installed.has_value()) {
      return installed;
    }
#ifdef TGDB_DEFAULT_FORCE_BUNDLED_BASH_DAP
    return std::nullopt;
#endif
  }
#endif

  const auto node = [&]() -> std::optional<std::string> {
    if (const auto env = env_executable("TGDB_NODE_BIN"); env.has_value()) {
      return env;
    }
    return find_named_binary_on_path("node");
  }();
  if (!node.has_value()) {
    return std::nullopt;
  }

  const auto bash = [&]() -> std::optional<std::string> {
    if (const auto env = env_executable("BASH_PATH"); env.has_value()) {
      return env;
    }
    return find_named_binary_on_path("bash");
  }();
  if (!bash.has_value()) {
    return std::nullopt;
  }

  std::string adapter_js;
  if (const char* raw = std::getenv("BASH_DEBUG_ADAPTER"); raw != nullptr && raw[0] != '\0') {
    if (readable_file(raw)) {
      adapter_js = raw;
    }
  }
#ifdef TGDB_BASH_DEBUG_ADAPTER_JS
  if (adapter_js.empty() && readable_file(TGDB_BASH_DEBUG_ADAPTER_JS)) {
    adapter_js = TGDB_BASH_DEBUG_ADAPTER_JS;
  }
#endif
  if (adapter_js.empty()) {
    return std::nullopt;
  }

  std::string bashdb;
  std::string bashdb_lib;
  if (const char* raw = std::getenv("BASHDB_PATH"); raw != nullptr && raw[0] != '\0' &&
      is_executable_file(raw)) {
    bashdb = raw;
  }
#ifdef TGDB_BASHDB_PATH
  if (bashdb.empty() && is_executable_file(TGDB_BASHDB_PATH)) {
    bashdb = TGDB_BASHDB_PATH;
  }
#endif
  if (bashdb.empty()) {
    if (const auto path_bin = find_named_binary_on_path("bashdb"); path_bin.has_value()) {
      bashdb = *path_bin;
    }
  }
  if (bashdb.empty()) {
    return std::nullopt;
  }

  if (const char* raw = std::getenv("BASHDB_LIB"); raw != nullptr && raw[0] != '\0') {
    bashdb_lib = raw;
  }
#ifdef TGDB_BASHDB_LIB
  if (bashdb_lib.empty()) {
    bashdb_lib = TGDB_BASHDB_LIB;
  }
#endif
  if (bashdb_lib.empty()) {
    bashdb_lib = (fs::path(bashdb).parent_path()).string();
  }

  BashDebugAdapterLocation loc;
  loc.node_path = *node;
  loc.adapter_js_path = std::move(adapter_js);
  loc.bash_path = *bash;
  loc.bashdb_path = std::move(bashdb);
  loc.bashdb_lib_path = std::move(bashdb_lib);
  loc.source = BashDebugAdapterLocation::Source::SystemPath;
  return loc;
}

}  // namespace tgdb
