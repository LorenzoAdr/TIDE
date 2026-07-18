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

#ifdef TUIDE_HAS_BUNDLED_CLANGD
#include <zstd.h>
#include "bundled_clangd_manifest.hpp"

extern "C" {
extern const unsigned char _binary_clangd_blob_zst_start[];
extern const unsigned char _binary_clangd_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_GDB
#ifndef TUIDE_HAS_BUNDLED_CLANGD
#include <zstd.h>
#endif
#include "bundled_gdb_manifest.hpp"

extern "C" {
extern const unsigned char _binary_gdb_blob_zst_start[];
extern const unsigned char _binary_gdb_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_RG
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB)
#include <zstd.h>
#endif
#include "bundled_rg_manifest.hpp"

extern "C" {
extern const unsigned char _binary_rg_blob_zst_start[];
extern const unsigned char _binary_rg_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_PYTHON_TOOLS
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG)
#include <zstd.h>
#endif
#include "bundled_python_tools_manifest.hpp"

extern "C" {
extern const unsigned char _binary_python_tools_blob_zst_start[];
extern const unsigned char _binary_python_tools_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_TEXLAB
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS)
#include <zstd.h>
#endif
#include "bundled_texlab_manifest.hpp"
extern "C" {
extern const unsigned char _binary_texlab_blob_zst_start[];
extern const unsigned char _binary_texlab_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_BASH_LS
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB)
#include <zstd.h>
#endif
#include "bundled_bash_ls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_bash_ls_blob_zst_start[];
extern const unsigned char _binary_bash_ls_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_BASH_DAP
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS)
#include <zstd.h>
#endif
#include "bundled_bash_dap_manifest.hpp"
extern "C" {
extern const unsigned char _binary_bash_dap_blob_zst_start[];
extern const unsigned char _binary_bash_dap_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_RUST_ANALYZER
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP)
#include <zstd.h>
#endif
#include "bundled_rust_analyzer_manifest.hpp"
extern "C" {
extern const unsigned char _binary_rust_analyzer_blob_zst_start[];
extern const unsigned char _binary_rust_analyzer_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_GOPLS
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP) && !defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER)
#include <zstd.h>
#endif
#include "bundled_gopls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_gopls_blob_zst_start[];
extern const unsigned char _binary_gopls_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_ZLS
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP) && !defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) && \
    !defined(TUIDE_HAS_BUNDLED_GOPLS)
#include <zstd.h>
#endif
#include "bundled_zls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_zls_blob_zst_start[];
extern const unsigned char _binary_zls_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_LUA_LS
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP) && !defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) && \
    !defined(TUIDE_HAS_BUNDLED_GOPLS) && !defined(TUIDE_HAS_BUNDLED_ZLS)
#include <zstd.h>
#endif
#include "bundled_lua_ls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_lua_ls_blob_zst_start[];
extern const unsigned char _binary_lua_ls_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_FORTLS
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP) && !defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) && \
    !defined(TUIDE_HAS_BUNDLED_GOPLS) && !defined(TUIDE_HAS_BUNDLED_ZLS) && \
    !defined(TUIDE_HAS_BUNDLED_LUA_LS)
#include <zstd.h>
#endif
#include "bundled_fortls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_fortls_blob_zst_start[];
extern const unsigned char _binary_fortls_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_TSSERVER
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP) && !defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) && \
    !defined(TUIDE_HAS_BUNDLED_GOPLS) && !defined(TUIDE_HAS_BUNDLED_ZLS) && \
    !defined(TUIDE_HAS_BUNDLED_LUA_LS) && !defined(TUIDE_HAS_BUNDLED_FORTLS)
#include <zstd.h>
#endif
#include "bundled_typescript_ls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_typescript_ls_blob_zst_start[];
extern const unsigned char _binary_typescript_ls_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_NEOCMAKELSP
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP) && !defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) && \
    !defined(TUIDE_HAS_BUNDLED_GOPLS) && !defined(TUIDE_HAS_BUNDLED_ZLS) && \
    !defined(TUIDE_HAS_BUNDLED_LUA_LS) && !defined(TUIDE_HAS_BUNDLED_FORTLS) && \
    !defined(TUIDE_HAS_BUNDLED_TSSERVER)
#include <zstd.h>
#endif
#include "bundled_neocmakelsp_manifest.hpp"
extern "C" {
extern const unsigned char _binary_neocmakelsp_blob_zst_start[];
extern const unsigned char _binary_neocmakelsp_blob_zst_end[];
}
#endif

#ifdef TUIDE_HAS_BUNDLED_MAKE_LS
#if !defined(TUIDE_HAS_BUNDLED_CLANGD) && !defined(TUIDE_HAS_BUNDLED_GDB) && \
    !defined(TUIDE_HAS_BUNDLED_RG) && !defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && \
    !defined(TUIDE_HAS_BUNDLED_TEXLAB) && !defined(TUIDE_HAS_BUNDLED_BASH_LS) && \
    !defined(TUIDE_HAS_BUNDLED_BASH_DAP) && !defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) && \
    !defined(TUIDE_HAS_BUNDLED_GOPLS) && !defined(TUIDE_HAS_BUNDLED_ZLS) && \
    !defined(TUIDE_HAS_BUNDLED_LUA_LS) && !defined(TUIDE_HAS_BUNDLED_FORTLS) && \
    !defined(TUIDE_HAS_BUNDLED_TSSERVER) && !defined(TUIDE_HAS_BUNDLED_NEOCMAKELSP)
#include <zstd.h>
#endif
#include "bundled_make_ls_manifest.hpp"
extern "C" {
extern const unsigned char _binary_make_ls_blob_zst_start[];
extern const unsigned char _binary_make_ls_blob_zst_end[];
}
#endif

namespace fs = std::filesystem;

namespace tuide {

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
    return (fs::path(xdg) / "tuide" / "bundled").string();
  }
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return (fs::path(home) / ".cache" / "tuide" / "bundled").string();
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

bool readable_file(const std::string& path) {
  return !path.empty() && ::access(path.c_str(), R_OK) == 0;
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

#if defined(TUIDE_HAS_BUNDLED_CLANGD) || defined(TUIDE_HAS_BUNDLED_GDB) || \
    defined(TUIDE_HAS_BUNDLED_RG) || defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) || \
    defined(TUIDE_HAS_BUNDLED_TEXLAB) || defined(TUIDE_HAS_BUNDLED_BASH_LS) || \
    defined(TUIDE_HAS_BUNDLED_BASH_DAP) || defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) || \
    defined(TUIDE_HAS_BUNDLED_GOPLS) || defined(TUIDE_HAS_BUNDLED_ZLS) || \
    defined(TUIDE_HAS_BUNDLED_LUA_LS) || defined(TUIDE_HAS_BUNDLED_FORTLS) || \
    defined(TUIDE_HAS_BUNDLED_TSSERVER)
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

// Usado por lazy_extract_bundled_tree (definido más abajo).
bool extract_tar_to_directory(const std::vector<unsigned char>& tar_data,
                              const fs::path& output_dir);

#if defined(TUIDE_HAS_BUNDLED_RUST_ANALYZER) || defined(TUIDE_HAS_BUNDLED_GOPLS) || \
    defined(TUIDE_HAS_BUNDLED_ZLS) || defined(TUIDE_HAS_BUNDLED_LUA_LS) || \
    defined(TUIDE_HAS_BUNDLED_FORTLS) || defined(TUIDE_HAS_BUNDLED_TSSERVER)
bool bundled_tree_ready(const fs::path& install_root, const fs::path& binary_path,
                        const std::string& expected_marker) {
  const fs::path marker = install_root / ".installed";
  return is_executable_file(binary_path.string()) &&
         read_text_file(marker) == expected_marker;
}

bool lazy_extract_bundled_tree(const fs::path& install_root, const char* binary_rel,
                               const std::string& expected_marker,
                               const unsigned char* blob_start,
                               const unsigned char* blob_end,
                               std::atomic<bool>& install_attempted) {
  const fs::path binary_path = install_root / binary_rel;
  if (bundled_tree_ready(install_root, binary_path, expected_marker)) {
    return true;
  }
  if (install_attempted.exchange(true, std::memory_order_acq_rel)) {
    return bundled_tree_ready(install_root, binary_path, expected_marker);
  }
  const auto tar_data = decompress_zstd_blob(blob_start, blob_end);
  if (!tar_data.has_value()) {
    return false;
  }
  const fs::path temp_root = install_root.string() + ".tmp";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  if (!extract_tar_to_directory(*tar_data, temp_root) ||
      !is_executable_file((temp_root / binary_rel).string())) {
    fs::remove_all(temp_root, ec);
    return false;
  }
  fs::remove_all(install_root, ec);
  fs::rename(temp_root, install_root, ec);
  if (ec) {
    fs::remove_all(temp_root, ec);
    return false;
  }
  write_text_file(install_root / ".installed", expected_marker);
  return bundled_tree_ready(install_root, binary_path, expected_marker);
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

#ifdef TUIDE_HAS_BUNDLED_CLANGD
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

  const fs::path manifest_dir = install_root / TUIDE_BUNDLED_CLANGD_RESOURCE_SUBDIR;
  if (fs::is_directory(manifest_dir / "include", ec)) {
    return manifest_dir.string();
  }
  return std::nullopt;
}

std::optional<ClangdLocation> resolve_bundled_clangd() {
  const fs::path install_root =
      fs::path(bundled_cache_root()) / ("clangd-" TUIDE_BUNDLED_CLANGD_VERSION);
  const fs::path binary_path = install_root / "bin" / "clangd";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TUIDE_BUNDLED_CLANGD_BLOB_SHA256) + "\n";

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

#ifdef TUIDE_HAS_BUNDLED_GDB
std::optional<GdbLocation> resolve_bundled_gdb() {
  const fs::path install_root =
      fs::path(bundled_cache_root()) / ("gdb-" TUIDE_BUNDLED_GDB_VERSION);
  const fs::path binary_path = install_root / "bin" / "gdb";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TUIDE_BUNDLED_GDB_BLOB_SHA256) + "\n";

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

#ifdef TUIDE_HAS_BUNDLED_RG
std::optional<RgLocation> resolve_bundled_rg() {
  const fs::path install_root = fs::path(bundled_cache_root()) / ("rg-" TUIDE_BUNDLED_RG_VERSION);
  const fs::path binary_path = install_root / "bin" / "rg";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TUIDE_BUNDLED_RG_BLOB_SHA256) + "\n";

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

#ifdef TUIDE_HAS_BUNDLED_PYTHON_TOOLS
std::optional<fs::path> ensure_bundled_python_tools_root() {
  const fs::path install_root =
      fs::path(bundled_cache_root()) / ("python-tools-" TUIDE_BUNDLED_PYTHON_TOOLS_VERSION);
  const fs::path langserver = install_root / "bin" / "basedpyright-langserver";
  const fs::path marker = install_root / ".installed";
  const std::string expected_marker = std::string(TUIDE_BUNDLED_PYTHON_TOOLS_BLOB_SHA256) + "\n";

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

#if TUIDE_BUNDLED_PYTHON_TOOLS_KIND_FULL
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
#ifdef TUIDE_HAS_BUNDLED_CLANGD
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_clangd(bool value) { g_runtime_force_bundled_clangd = value; }

bool should_force_bundled_clangd() {
  if (const auto env = parse_env_bool("TUIDE_FORCE_BUNDLED_CLANGD"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_clangd.has_value()) {
    return *g_runtime_force_bundled_clangd;
  }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_CLANGD
  return true;
#else
  return false;
#endif
}

bool has_bundled_gdb() {
#ifdef TUIDE_HAS_BUNDLED_GDB
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_gdb(bool value) { g_runtime_force_bundled_gdb = value; }

bool should_force_bundled_gdb() {
  if (const auto env = parse_env_bool("TUIDE_FORCE_BUNDLED_GDB"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_gdb.has_value()) {
    return *g_runtime_force_bundled_gdb;
  }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_GDB
  return true;
#else
  return false;
#endif
}

std::optional<ClangdLocation> resolve_clangd() {
  if (const auto env_path = clangd_from_env(); env_path.has_value()) {
    return ClangdLocation{*env_path, {}, ClangdLocation::Source::Env};
  }

#ifdef TUIDE_HAS_BUNDLED_CLANGD
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

#ifdef TUIDE_HAS_BUNDLED_GDB
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
#ifdef TUIDE_HAS_BUNDLED_RG
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_rg(bool value) { g_runtime_force_bundled_rg = value; }

bool should_force_bundled_rg() {
  if (const auto env = parse_env_bool("TUIDE_FORCE_BUNDLED_RG"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_rg.has_value()) {
    return *g_runtime_force_bundled_rg;
  }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_RG
  return true;
#else
  return false;
#endif
}

std::optional<RgLocation> resolve_rg() {
  if (const auto env_path = rg_from_env(); env_path.has_value()) {
    return RgLocation{*env_path, RgLocation::Source::Env};
  }

#ifdef TUIDE_HAS_BUNDLED_RG
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
#ifdef TUIDE_HAS_BUNDLED_PYTHON_TOOLS
  return true;
#else
  return false;
#endif
}

void set_runtime_force_bundled_python_tools(bool value) {
  g_runtime_force_bundled_python_tools = value;
}

bool should_force_bundled_python_tools() {
  if (const auto env = parse_env_bool("TUIDE_FORCE_BUNDLED_PYTHON_TOOLS"); env.has_value()) {
    return *env;
  }
  if (g_runtime_force_bundled_python_tools.has_value()) {
    return *g_runtime_force_bundled_python_tools;
  }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_PYTHON_TOOLS
  return true;
#else
  return false;
#endif
}

namespace {

std::optional<std::string> find_named_binary_on_path(const std::string& name) {
  if (name.empty()) {
    return std::nullopt;
  }
  const char* path_env = std::getenv("PATH");
  if (path_env != nullptr && path_env[0] != '\0') {
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
  }
  // Desktop / IDE launches often omit ~/.cargo/bin and ~/.local/bin from PATH.
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    const fs::path home_path(home);
    const fs::path extras[] = {
        home_path / ".cargo" / "bin" / name,
        home_path / "go" / "bin" / name,
        home_path / ".local" / "bin" / name,
    };
    for (const fs::path& candidate : extras) {
      if (is_executable_file(candidate.string())) {
        return candidate.string();
      }
    }
  }
  return std::nullopt;
}

// rust-analyzer on PATH is often a rustup shim; reject it unless `--version` works.
bool executable_responds_to_version(const std::string& path) {
  if (!is_executable_file(path)) {
    return false;
  }
  static std::mutex mu;
  static std::unordered_map<std::string, bool> cache;
  {
    std::lock_guard<std::mutex> lock(mu);
    if (const auto it = cache.find(path); it != cache.end()) {
      return it->second;
    }
  }
  const std::string cmd = "\"" + path + "\" --version >/dev/null 2>&1";
  const bool ok = std::system(cmd.c_str()) == 0;
  std::lock_guard<std::mutex> lock(mu);
  cache[path] = ok;
  return ok;
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

#ifdef TUIDE_HAS_BUNDLED_PYTHON_TOOLS
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

#if defined(TUIDE_HAS_BUNDLED_PYTHON_TOOLS) && TUIDE_BUNDLED_PYTHON_TOOLS_KIND_FULL
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
#ifdef TUIDE_HAS_BUNDLED_BASH_LS
  {
    const fs::path bundled =
        fs::path(bundled_cache_root()) / ("bash-ls-" TUIDE_BUNDLED_BASH_LS_VERSION) / "bin" /
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
#ifdef TUIDE_HAS_BUNDLED_TEXLAB
  {
    const fs::path bundled = fs::path(bundled_cache_root()) /
                             ("texlab-" TUIDE_BUNDLED_TEXLAB_VERSION) / "bin" / "chktex";
    if (is_executable_file(bundled.string())) {
      return bundled.string();
    }
  }
#endif
  return find_named_binary_on_path("chktex");
}

std::optional<std::string> resolve_gfortran() {
  if (const auto env_path = env_executable("GFORTRAN_PATH"); env_path.has_value()) {
    return *env_path;
  }
  if (const auto env_path = env_executable("TUIDE_GFORTRAN"); env_path.has_value()) {
    return *env_path;
  }
  static const char* kCandidates[] = {"gfortran", "gfortran-14", "gfortran-13", "gfortran-12"};
  for (const char* name : kCandidates) {
    if (const auto path_bin = find_named_binary_on_path(name); path_bin.has_value()) {
      return *path_bin;
    }
  }
  return std::nullopt;
}

std::optional<BashLsLocation> resolve_bash_language_server() {
  if (const auto env_path = env_executable("BASH_LANGUAGE_SERVER_PATH"); env_path.has_value()) {
    return BashLsLocation{*env_path, BashLsLocation::Source::Env};
  }
#ifdef TUIDE_HAS_BUNDLED_BASH_LS
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("bash-ls-" TUIDE_BUNDLED_BASH_LS_VERSION);
    const fs::path binary_path = install_root / "bin" / "bash-language-server";
    const fs::path marker = install_root / ".installed";
    const std::string expected = std::string(TUIDE_BUNDLED_BASH_LS_BLOB_SHA256) + "\n";
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
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_BASH_LS
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
#ifdef TUIDE_HAS_BUNDLED_TEXLAB
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("texlab-" TUIDE_BUNDLED_TEXLAB_VERSION);
    const fs::path binary_path = install_root / "bin" / "texlab";
    const fs::path chktex_path = install_root / "bin" / "chktex";
    const fs::path marker = install_root / ".installed";
    const std::string expected = std::string(TUIDE_BUNDLED_TEXLAB_BLOB_SHA256) + "\n";
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
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_TEXLAB
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("texlab"); path_bin.has_value()) {
    return TexlabLocation{*path_bin, TexlabLocation::Source::SystemPath};
  }
  return std::nullopt;
}

std::optional<RustAnalyzerLocation> resolve_rust_analyzer() {
  auto accept = [](const std::string& path,
                   RustAnalyzerLocation::Source source) -> std::optional<RustAnalyzerLocation> {
    if (!executable_responds_to_version(path)) {
      return std::nullopt;
    }
    return RustAnalyzerLocation{path, source};
  };

  if (const auto env_path = env_executable("TUIDE_RUST_ANALYZER"); env_path.has_value()) {
    if (auto loc = accept(*env_path, RustAnalyzerLocation::Source::Env)) {
      return loc;
    }
  }
#ifdef TUIDE_HAS_BUNDLED_RUST_ANALYZER
  {
    const fs::path install_root = fs::path(bundled_cache_root()) /
                                  ("rust-analyzer-" TUIDE_BUNDLED_RUST_ANALYZER_VERSION);
    const fs::path binary_path = install_root / "bin" / "rust-analyzer";
    const std::string expected = std::string(TUIDE_BUNDLED_RUST_ANALYZER_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/rust-analyzer", expected,
                                  _binary_rust_analyzer_blob_zst_start,
                                  _binary_rust_analyzer_blob_zst_end, install_attempted)) {
      if (auto loc = accept(binary_path.string(), RustAnalyzerLocation::Source::Bundled)) {
        return loc;
      }
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_RUST_ANALYZER
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("rust-analyzer"); path_bin.has_value()) {
    if (auto loc = accept(*path_bin, RustAnalyzerLocation::Source::SystemPath)) {
      return loc;
    }
  }
  return std::nullopt;
}

std::optional<GoplsLocation> resolve_gopls() {
  if (const auto env_path = env_executable("TUIDE_GOPLS"); env_path.has_value()) {
    return GoplsLocation{*env_path, GoplsLocation::Source::Env};
  }
#ifdef TUIDE_HAS_BUNDLED_GOPLS
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("gopls-" TUIDE_BUNDLED_GOPLS_VERSION);
    const fs::path binary_path = install_root / "bin" / "gopls";
    const std::string expected = std::string(TUIDE_BUNDLED_GOPLS_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/gopls", expected,
                                  _binary_gopls_blob_zst_start, _binary_gopls_blob_zst_end,
                                  install_attempted)) {
      return GoplsLocation{binary_path.string(), GoplsLocation::Source::Bundled};
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_GOPLS
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("gopls"); path_bin.has_value()) {
    return GoplsLocation{*path_bin, GoplsLocation::Source::SystemPath};
  }
  return std::nullopt;
}

std::optional<ZlsLocation> resolve_zls() {
  if (const auto env_path = env_executable("TUIDE_ZLS"); env_path.has_value()) {
    return ZlsLocation{*env_path, ZlsLocation::Source::Env};
  }
#ifdef TUIDE_HAS_BUNDLED_ZLS
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("zls-" TUIDE_BUNDLED_ZLS_VERSION);
    const fs::path binary_path = install_root / "bin" / "zls";
    const std::string expected = std::string(TUIDE_BUNDLED_ZLS_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/zls", expected, _binary_zls_blob_zst_start,
                                  _binary_zls_blob_zst_end, install_attempted)) {
      return ZlsLocation{binary_path.string(), ZlsLocation::Source::Bundled};
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_ZLS
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("zls"); path_bin.has_value()) {
    return ZlsLocation{*path_bin, ZlsLocation::Source::SystemPath};
  }
  return std::nullopt;
}

std::optional<FortlsLocation> resolve_fortls() {
  if (const auto env_path = env_executable("TUIDE_FORTLS"); env_path.has_value()) {
    FortlsLocation loc;
    loc.binary_path = *env_path;
    loc.source = FortlsLocation::Source::Env;
    return loc;
  }
#ifdef TUIDE_HAS_BUNDLED_FORTLS
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("fortls-" TUIDE_BUNDLED_FORTLS_VERSION);
    const fs::path binary_path = install_root / "bin" / "fortls";
    const std::string expected = std::string(TUIDE_BUNDLED_FORTLS_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/fortls", expected,
                                  _binary_fortls_blob_zst_start, _binary_fortls_blob_zst_end,
                                  install_attempted)) {
      return FortlsLocation{binary_path.string(), false, {}, FortlsLocation::Source::Bundled};
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_FORTLS
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("fortls"); path_bin.has_value()) {
    return FortlsLocation{*path_bin, false, {}, FortlsLocation::Source::SystemPath};
  }
  static const char* kPythons[] = {"python3", "python"};
  for (const char* name : kPythons) {
    if (const auto path_bin = find_named_binary_on_path(name); path_bin.has_value()) {
      if (python_module_importable(*path_bin, "fortls")) {
        FortlsLocation loc;
        loc.binary_path = *path_bin;
        loc.use_python_module = true;
        loc.python_module = "fortls";
        loc.source = FortlsLocation::Source::SystemPath;
        return loc;
      }
    }
  }
  return std::nullopt;
}

std::optional<LuaLsLocation> resolve_lua_language_server() {
  if (const auto env_path = env_executable("TUIDE_LUA_LS"); env_path.has_value()) {
    return LuaLsLocation{*env_path, LuaLsLocation::Source::Env};
  }
#ifdef TUIDE_HAS_BUNDLED_LUA_LS
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("lua-language-server-" TUIDE_BUNDLED_LUA_LS_VERSION);
    const fs::path binary_path = install_root / "bin" / "lua-language-server";
    const std::string expected = std::string(TUIDE_BUNDLED_LUA_LS_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/lua-language-server", expected,
                                  _binary_lua_ls_blob_zst_start, _binary_lua_ls_blob_zst_end,
                                  install_attempted)) {
      return LuaLsLocation{binary_path.string(), LuaLsLocation::Source::Bundled};
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_LUA_LS
    return std::nullopt;
#endif
  }
#endif
  static const char* kCandidates[] = {"lua-language-server", "lua-language-server.exe"};
  for (const char* name : kCandidates) {
    if (const auto path_bin = find_named_binary_on_path(name); path_bin.has_value()) {
      return LuaLsLocation{*path_bin, LuaLsLocation::Source::SystemPath};
    }
  }
  return std::nullopt;
}

std::optional<TypescriptLsLocation> resolve_typescript_language_server() {
  if (const auto env_path = env_executable("TUIDE_TYPESCRIPT_LS"); env_path.has_value()) {
    TypescriptLsLocation loc;
    loc.binary_path = *env_path;
    loc.needs_stdio_flag = true;
    loc.source = TypescriptLsLocation::Source::Env;
    return loc;
  }
#ifdef TUIDE_HAS_BUNDLED_TSSERVER
  {
    const fs::path install_root = fs::path(bundled_cache_root()) /
                                  ("typescript-ls-" TUIDE_BUNDLED_TYPESCRIPT_LS_VERSION);
    const fs::path binary_path = install_root / "bin" / "typescript-language-server";
    const std::string expected = std::string(TUIDE_BUNDLED_TYPESCRIPT_LS_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/typescript-language-server", expected,
                                  _binary_typescript_ls_blob_zst_start,
                                  _binary_typescript_ls_blob_zst_end, install_attempted)) {
      TypescriptLsLocation loc;
      loc.binary_path = binary_path.string();
      loc.needs_stdio_flag = true;
      loc.source = TypescriptLsLocation::Source::Bundled;
      return loc;
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_TSSERVER
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("typescript-language-server");
      path_bin.has_value()) {
    TypescriptLsLocation loc;
    loc.binary_path = *path_bin;
    loc.needs_stdio_flag = true;
    loc.source = TypescriptLsLocation::Source::SystemPath;
    return loc;
  }
  const auto node = [&]() -> std::optional<std::string> {
    if (const auto env = env_executable("TUIDE_NODE_BIN"); env.has_value()) {
      return env;
    }
    return find_named_binary_on_path("node");
  }();
  if (node.has_value()) {
    const fs::path node_path = *node;
    const std::array<fs::path, 3> candidates = {
        node_path.parent_path().parent_path() / "lib" / "node_modules" /
            "typescript-language-server" / "lib" / "cli.js",
        node_path.parent_path() / "node_modules" / "typescript-language-server" / "lib" / "cli.js",
        fs::path("/usr/lib/node_modules/typescript-language-server/lib/cli.js"),
    };
    for (const fs::path& script : candidates) {
      if (readable_file(script.string())) {
        TypescriptLsLocation loc;
        loc.binary_path = *node;
        loc.needs_stdio_flag = false;
        loc.use_node_script = true;
        loc.script_path = script.string();
        loc.source = TypescriptLsLocation::Source::SystemPath;
        return loc;
      }
    }
  }
  return std::nullopt;
}

std::optional<NeocmakelspLocation> resolve_neocmakelsp() {
  if (const auto env_path = env_executable("TUIDE_NEOCMAKELSP"); env_path.has_value()) {
    return NeocmakelspLocation{*env_path, NeocmakelspLocation::Source::Env};
  }
#ifdef TUIDE_HAS_BUNDLED_NEOCMAKELSP
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("neocmakelsp-" TUIDE_BUNDLED_NEOCMAKELSP_VERSION);
    const fs::path binary_path = install_root / "bin" / "neocmakelsp";
    const std::string expected = std::string(TUIDE_BUNDLED_NEOCMAKELSP_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/neocmakelsp", expected,
                                  _binary_neocmakelsp_blob_zst_start,
                                  _binary_neocmakelsp_blob_zst_end, install_attempted)) {
      return NeocmakelspLocation{binary_path.string(), NeocmakelspLocation::Source::Bundled};
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_NEOCMAKELSP
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("neocmakelsp"); path_bin.has_value()) {
    return NeocmakelspLocation{*path_bin, NeocmakelspLocation::Source::SystemPath};
  }
  return std::nullopt;
}

std::optional<MakeLsLocation> resolve_make_ls() {
  if (const auto env_path = env_executable("TUIDE_MAKE_LS"); env_path.has_value()) {
    return MakeLsLocation{*env_path, MakeLsLocation::Source::Env};
  }
#ifdef TUIDE_HAS_BUNDLED_MAKE_LS
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("make-ls-" TUIDE_BUNDLED_MAKE_LS_VERSION);
    const fs::path binary_path = install_root / "bin" / "make-ls";
    const std::string expected = std::string(TUIDE_BUNDLED_MAKE_LS_BLOB_SHA256) + "\n";
    static std::atomic<bool> install_attempted{false};
    if (lazy_extract_bundled_tree(install_root, "bin/make-ls", expected,
                                  _binary_make_ls_blob_zst_start, _binary_make_ls_blob_zst_end,
                                  install_attempted)) {
      return MakeLsLocation{binary_path.string(), MakeLsLocation::Source::Bundled};
    }
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_MAKE_LS
    return std::nullopt;
#endif
  }
#endif
  if (const auto path_bin = find_named_binary_on_path("make-ls"); path_bin.has_value()) {
    return MakeLsLocation{*path_bin, MakeLsLocation::Source::SystemPath};
  }
  return std::nullopt;
}

std::optional<BashDebugAdapterLocation> resolve_bash_debug_adapter() {
#ifdef TUIDE_HAS_BUNDLED_BASH_DAP
  {
    const fs::path install_root =
        fs::path(bundled_cache_root()) / ("bash-dap-" TUIDE_BUNDLED_BASH_DAP_VERSION);
    const fs::path adapter_js = install_root / "adapter" / "bashDebug.js";
    const fs::path bashdb = install_root / "bashdb" / "bashdb";
    const fs::path marker = install_root / ".installed";
    const std::string expected = std::string(TUIDE_BUNDLED_BASH_DAP_BLOB_SHA256) + "\n";
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
#if TUIDE_BUNDLED_BASH_DAP_HAS_NODE
      const fs::path node_path = install_root / "node" / "bin" / "node";
      if (is_executable_file(node_path.string())) {
        node = node_path.string();
      }
#endif
      if (node.empty()) {
#ifdef TUIDE_HAS_BUNDLED_BASH_LS
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
        if (const auto env = env_executable("TUIDE_NODE_BIN"); env.has_value()) {
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
#ifdef TUIDE_DEFAULT_FORCE_BUNDLED_BASH_DAP
    return std::nullopt;
#endif
  }
#endif

  const auto node = [&]() -> std::optional<std::string> {
    if (const auto env = env_executable("TUIDE_NODE_BIN"); env.has_value()) {
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
#ifdef TUIDE_BASH_DEBUG_ADAPTER_JS
  if (adapter_js.empty() && readable_file(TUIDE_BASH_DEBUG_ADAPTER_JS)) {
    adapter_js = TUIDE_BASH_DEBUG_ADAPTER_JS;
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
#ifdef TUIDE_BASHDB_PATH
  if (bashdb.empty() && is_executable_file(TUIDE_BASHDB_PATH)) {
    bashdb = TUIDE_BASHDB_PATH;
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
#ifdef TUIDE_BASHDB_LIB
  if (bashdb_lib.empty()) {
    bashdb_lib = TUIDE_BASHDB_LIB;
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

}  // namespace tuide
