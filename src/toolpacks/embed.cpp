#include "toolpacks/embed.hpp"

#include <fstream>
#include <cstring>
#include <filesystem>

#include "toolpacks/download.hpp"
#include "toolpacks/paths.hpp"

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

constexpr std::size_t kMagicLen = 8;

void write_u16(std::ostream& out, std::uint16_t value) {
  const unsigned char bytes[2] = {
      static_cast<unsigned char>(value & 0xff),
      static_cast<unsigned char>((value >> 8) & 0xff),
  };
  out.write(reinterpret_cast<const char*>(bytes), 2);
}

void write_u32(std::ostream& out, std::uint32_t value) {
  const unsigned char bytes[4] = {
      static_cast<unsigned char>(value & 0xff),
      static_cast<unsigned char>((value >> 8) & 0xff),
      static_cast<unsigned char>((value >> 16) & 0xff),
      static_cast<unsigned char>((value >> 24) & 0xff),
  };
  out.write(reinterpret_cast<const char*>(bytes), 4);
}

void write_u64(std::ostream& out, std::uint64_t value) {
  unsigned char bytes[8];
  for (int i = 0; i < 8; ++i) {
    bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xff);
  }
  out.write(reinterpret_cast<const char*>(bytes), 8);
}

bool read_exact(std::istream& in, char* dest, std::size_t n) {
  in.read(dest, static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(in.gcount()) == n;
}

std::uint16_t read_u16(std::istream& in, bool* ok) {
  unsigned char bytes[2];
  if (!read_exact(in, reinterpret_cast<char*>(bytes), 2)) {
    *ok = false;
    return 0;
  }
  return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}

std::uint32_t read_u32(std::istream& in, bool* ok) {
  unsigned char bytes[4];
  if (!read_exact(in, reinterpret_cast<char*>(bytes), 4)) {
    *ok = false;
    return 0;
  }
  return static_cast<std::uint32_t>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) |
                                    (bytes[3] << 24));
}

std::uint64_t read_u64(std::istream& in, bool* ok) {
  unsigned char bytes[8];
  if (!read_exact(in, reinterpret_cast<char*>(bytes), 8)) {
    *ok = false;
    return 0;
  }
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
  }
  return value;
}

std::string self_exe_path() {
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec).string();
}

}  // namespace

std::optional<EmbedIndex> read_embed_index(const std::string& binary_path) {
  std::ifstream in(binary_path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  in.seekg(0, std::ios::end);
  const auto file_size = static_cast<std::uint64_t>(in.tellg());
  if (file_size < 16) {
    return std::nullopt;
  }
  in.seekg(static_cast<std::streamoff>(file_size - 16), std::ios::beg);
  bool ok = true;
  const std::uint64_t trailer_offset = read_u64(in, &ok);
  char magic[kMagicLen];
  if (!ok || !read_exact(in, magic, kMagicLen)) {
    return std::nullopt;
  }
  if (std::memcmp(magic, kEmbedMagicStr, kMagicLen) != 0) {
    return std::nullopt;
  }
  if (trailer_offset >= file_size - 16) {
    return std::nullopt;
  }

  in.clear();
  in.seekg(static_cast<std::streamoff>(trailer_offset), std::ios::beg);
  char head_magic[kMagicLen];
  if (!read_exact(in, head_magic, kMagicLen) ||
      std::memcmp(head_magic, kEmbedMagicStr, kMagicLen) != 0) {
    return std::nullopt;
  }
  ok = true;
  const std::uint32_t format_version = read_u32(in, &ok);
  const std::uint32_t count = read_u32(in, &ok);
  if (!ok || format_version != 1 || count > 64) {
    return std::nullopt;
  }

  EmbedIndex index;
  index.trailer_offset = trailer_offset;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint16_t id_len = read_u16(in, &ok);
    if (!ok || id_len == 0 || id_len > 256) {
      return std::nullopt;
    }
    std::string id(id_len, '\0');
    if (!read_exact(in, id.data(), id_len)) {
      return std::nullopt;
    }
    const std::uint16_t ver_len = read_u16(in, &ok);
    if (!ok || ver_len == 0 || ver_len > 256) {
      return std::nullopt;
    }
    std::string version(ver_len, '\0');
    if (!read_exact(in, version.data(), ver_len)) {
      return std::nullopt;
    }
    const std::uint64_t blob_size = read_u64(in, &ok);
    if (!ok || blob_size == 0 || blob_size > (1ull << 32)) {
      return std::nullopt;
    }
    const auto blob_offset = static_cast<std::uint64_t>(in.tellg());
    in.seekg(static_cast<std::streamoff>(blob_size), std::ios::cur);
    if (!in) {
      return std::nullopt;
    }
    EmbeddedToolpackInfo info;
    info.id = std::move(id);
    info.version = std::move(version);
    info.blob_offset = blob_offset;
    info.blob_size = blob_size;
    index.entries.push_back(std::move(info));
  }
  return index;
}

bool binary_has_toolpack_embed(const std::string& binary_path) {
  return read_embed_index(binary_path).has_value();
}

std::string extract_embedded_toolpack(const std::string& binary_path,
                                      const EmbeddedToolpackInfo& entry,
                                      const std::string& dest_dir) {
  std::ifstream in(binary_path, std::ios::binary);
  if (!in) {
    return "no se pudo abrir el binario";
  }
  in.seekg(static_cast<std::streamoff>(entry.blob_offset), std::ios::beg);
  std::string blob(static_cast<std::size_t>(entry.blob_size), '\0');
  if (!read_exact(in, blob.data(), blob.size())) {
    return "no se pudo leer el blob embebido";
  }

  std::error_code ec;
  fs::create_directories(fs::path(cache_root()) / "export-work", ec);
  const fs::path archive =
      fs::path(cache_root()) / "export-work" /
      (entry.id + "-" + entry.version + "-embedded.tar.zst");
  {
    std::ofstream out(archive, std::ios::binary | std::ios::trunc);
    if (!out) {
      return "no se pudo escribir blob temporal";
    }
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  }
  fs::remove_all(dest_dir, ec);
  fs::create_directories(dest_dir, ec);
  const std::string err = extract_archive(archive.string(), dest_dir);
  fs::remove(archive, ec);
  return err;
}

std::optional<ResolvedToolpack> resolve_embedded_clangd_toolpack() {
  const std::string exe = self_exe_path();
  if (exe.empty()) {
    return std::nullopt;
  }
  const auto index = read_embed_index(exe);
  if (!index.has_value()) {
    return std::nullopt;
  }
  const EmbeddedToolpackInfo* clangd = nullptr;
  for (const auto& entry : index->entries) {
    if (entry.id == "clangd") {
      clangd = &entry;
      break;
    }
  }
  if (clangd == nullptr) {
    return std::nullopt;
  }

  const fs::path install_root =
      fs::path(cache_root()) / "embedded-toolpacks" / ("clangd-" + clangd->version);
  const fs::path marker = install_root / ".embedded";
  const fs::path binary = install_root / "bin" / "clangd";
  std::error_code ec;
  if (!(fs::is_regular_file(marker, ec) && is_executable_path(binary.string()))) {
    const std::string err = extract_embedded_toolpack(exe, *clangd, install_root.string());
    if (!err.empty()) {
      return std::nullopt;
    }
    std::ofstream marker_out(marker, std::ios::trunc);
    marker_out << clangd->version << '\n';
  }

  // Reuse store resolution against a synthetic layout: point toolpacks root temporarily?
  // Simpler: build ResolvedToolpack directly.
  ResolvedToolpack resolved;
  resolved.id = "clangd";
  resolved.version = clangd->version;
  resolved.root_dir = install_root.string();
  resolved.binary_path = binary.string();
  if (!is_executable_path(resolved.binary_path)) {
    return std::nullopt;
  }
  const fs::path clang_base = install_root / "lib" / "clang";
  if (fs::is_directory(clang_base, ec)) {
    for (const auto& entry : fs::directory_iterator(clang_base, ec)) {
      if (entry.is_directory() && fs::is_directory(entry.path() / "include", ec)) {
        resolved.resource_dir = entry.path().string();
        break;
      }
    }
  }
  return resolved;
}

std::string append_toolpack_trailer(const std::string& output_binary_path,
                                    const std::vector<std::string>& ids,
                                    const std::vector<std::string>& versions,
                                    const std::vector<std::string>& blob_paths) {
  if (ids.size() != versions.size() || ids.size() != blob_paths.size()) {
    return "listas de embed inconsistentes";
  }
  std::fstream out(output_binary_path,
                   std::ios::binary | std::ios::in | std::ios::out | std::ios::ate);
  if (!out) {
    return "no se pudo abrir el binario de salida para append";
  }
  const auto trailer_offset = static_cast<std::uint64_t>(out.tellp());
  out.write(kEmbedMagicStr, static_cast<std::streamsize>(kMagicLen));
  write_u32(out, 1);
  write_u32(out, static_cast<std::uint32_t>(ids.size()));

  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (ids[i].size() > 65535 || versions[i].size() > 65535) {
      return "id/version demasiado largos";
    }
    std::ifstream blob_in(blob_paths[i], std::ios::binary);
    if (!blob_in) {
      return "no se pudo leer blob " + blob_paths[i];
    }
    blob_in.seekg(0, std::ios::end);
    const auto blob_size = static_cast<std::uint64_t>(blob_in.tellg());
    blob_in.seekg(0, std::ios::beg);
    std::string blob(static_cast<std::size_t>(blob_size), '\0');
    if (!read_exact(blob_in, blob.data(), blob.size())) {
      return "lectura incompleta de " + blob_paths[i];
    }

    write_u16(out, static_cast<std::uint16_t>(ids[i].size()));
    out.write(ids[i].data(), static_cast<std::streamsize>(ids[i].size()));
    write_u16(out, static_cast<std::uint16_t>(versions[i].size()));
    out.write(versions[i].data(), static_cast<std::streamsize>(versions[i].size()));
    write_u64(out, blob_size);
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
  }

  write_u64(out, trailer_offset);
  out.write(kEmbedMagicStr, static_cast<std::streamsize>(kMagicLen));
  if (!out) {
    return "fallo al escribir trailer de toolpacks";
  }
  return {};
}

}  // namespace tuide::toolpacks
