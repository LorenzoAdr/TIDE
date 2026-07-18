#include "lsp/lsp_uri.hpp"

#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string encode_uri_path(const std::string& path) {
  std::ostringstream out;
  for (unsigned char c : path) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      out << static_cast<char>(c);
    } else {
      static const char hex[] = "0123456789ABCDEF";
      out << '%' << hex[c >> 4] << hex[c & 0xF];
    }
  }
  return out.str();
}

std::string decode_uri_path(const std::string& encoded) {
  std::string out;
  out.reserve(encoded.size());
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    if (encoded[i] == '%' && i + 2 < encoded.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') {
          return c - '0';
        }
        if (c >= 'A' && c <= 'F') {
          return 10 + c - 'A';
        }
        if (c >= 'a' && c <= 'f') {
          return 10 + c - 'a';
        }
        return -1;
      };
      const int hi = hex(encoded[i + 1]);
      const int lo = hex(encoded[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(encoded[i]);
  }
  return out;
}

}  // namespace

std::string path_to_uri(const std::string& absolute_path) {
  if (absolute_path.empty()) {
    return {};
  }
  std::error_code ec;
  const auto canonical = fs::weakly_canonical(fs::path(absolute_path), ec);
  std::string path = ec ? absolute_path : canonical.string();
  if (path.empty()) {
    return {};
  }
  if (path[0] != '/') {
    return "file:///" + encode_uri_path(path);
  }
  return "file://" + encode_uri_path(path);
}

std::string uri_to_path(const std::string& uri) {
  if (uri.rfind("file://", 0) != 0) {
    return uri;
  }
  std::string path = uri.substr(7);
  if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
      path[2] == ':') {
    path = path.substr(1);
  }
  return decode_uri_path(path);
}

std::string language_id_for_path(const std::string& path) {
  const fs::path file_path(path);
  const auto filename = file_path.filename().string();
  const auto ext = file_path.extension().string();
  if (filename == "CMakeLists.txt" || ext == ".cmake") {
    return "cmake";
  }
  if (filename == "Makefile" || filename == "makefile" || filename == "GNUmakefile" ||
      ext == ".mk") {
    return "make";
  }
  if (ext == ".c") {
    return "c";
  }
  if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".cpp" || ext == ".cc" ||
      ext == ".cxx") {
    return "cpp";
  }
  if (ext == ".py" || ext == ".pyi" || ext == ".pyw") {
    return "python";
  }
  if (ext == ".sh" || ext == ".bash") {
    return "shellscript";
  }
  if (ext == ".tex" || ext == ".sty" || ext == ".cls") {
    return "latex";
  }
  if (ext == ".rs") {
    return "rust";
  }
  if (ext == ".go") {
    return "go";
  }
  if (ext == ".zig") {
    return "zig";
  }
  if (ext == ".f" || ext == ".f90" || ext == ".f95" || ext == ".for") {
    return "fortran";
  }
  if (ext == ".lua") {
    return "lua";
  }
  if (ext == ".js" || ext == ".mjs" || ext == ".cjs") {
    return "javascript";
  }
  if (ext == ".ts" || ext == ".tsx") {
    return "typescript";
  }
  return "plaintext";
}

std::string normalize_lsp_path(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code ec;
  const auto canonical = fs::weakly_canonical(fs::path(path), ec);
  return ec ? path : canonical.string();
}

}  // namespace tuide
