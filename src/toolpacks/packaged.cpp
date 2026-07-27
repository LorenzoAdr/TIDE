#include "toolpacks/packaged.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace tuide::toolpacks {
namespace {

constexpr const char* kLegacyMagic = "TUIDTPK1";  // 8 bytes
constexpr std::size_t kMagicLen = 8;

bool read_exact(std::istream& in, char* dest, std::size_t n) {
  in.read(dest, static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(in.gcount()) == n;
}

std::uint64_t read_u64_le(std::istream& in, bool* ok) {
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

}  // namespace

bool path_looks_like_appimage(const std::string& path) {
  const fs::path p(path);
  std::string name = p.filename().string();
  for (char& ch : name) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return name.size() >= 9 && name.compare(name.size() - 9, 9, ".appimage") == 0;
}

bool path_looks_like_appdir(const std::string& path) {
  std::error_code ec;
  if (!fs::is_directory(path, ec)) {
    return false;
  }
  const fs::path root(path);
  return fs::is_regular_file(root / "AppRun", ec) &&
         fs::is_regular_file(root / "usr" / "bin" / "tuide", ec);
}

bool binary_has_legacy_toolpack_trailer(const std::string& binary_path) {
  std::ifstream in(binary_path, std::ios::binary);
  if (!in) {
    return false;
  }
  in.seekg(0, std::ios::end);
  const auto file_size = static_cast<std::uint64_t>(in.tellg());
  if (file_size < 16) {
    return false;
  }
  in.seekg(static_cast<std::streamoff>(file_size - 16), std::ios::beg);
  bool ok = true;
  const std::uint64_t trailer_offset = read_u64_le(in, &ok);
  char magic[kMagicLen];
  if (!ok || !read_exact(in, magic, kMagicLen)) {
    return false;
  }
  if (std::memcmp(magic, kLegacyMagic, kMagicLen) != 0) {
    return false;
  }
  if (trailer_offset >= file_size - 16) {
    return false;
  }
  in.clear();
  in.seekg(static_cast<std::streamoff>(trailer_offset), std::ios::beg);
  char head[kMagicLen];
  return read_exact(in, head, kMagicLen) && std::memcmp(head, kLegacyMagic, kMagicLen) == 0;
}

bool is_packaged_binary(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  if (path_looks_like_appdir(path)) {
    return true;
  }
  std::error_code ec;
  if (path_looks_like_appimage(path) && fs::is_regular_file(path, ec)) {
    return true;
  }
  if (fs::is_regular_file(path, ec)) {
    return binary_has_legacy_toolpack_trailer(path);
  }
  return false;
}

}  // namespace tuide::toolpacks
