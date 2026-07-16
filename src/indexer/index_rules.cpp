#include "indexer/index_rules.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string lowercase_extension(const std::string& path) {
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

const std::unordered_set<std::string>& binary_extensions() {
  static const std::unordered_set<std::string> kExtensions = {
      ".a",    ".bin",  ".bmp",  ".bz2",  ".class", ".db",   ".dll",  ".dylib",
      ".eot",  ".exe",  ".gif",  ".gz",   ".ico",   ".jar",  ".jpeg", ".jpg",
      ".o",    ".obj",  ".otf",  ".pyc",  ".pyo",   ".png",  ".rar",  ".so",
      ".sqlite", ".tar", ".ttf", ".wasm", ".webp", ".woff", ".woff2", ".xz",
      ".zip",  ".7z",   ".pdf",  ".pptx", ".docx", ".xlsx",
  };
  return kExtensions;
}

}  // namespace

bool should_skip_dir_name(const std::string& name, const IndexFilterOptions& options) {
  if (options.show_all_files) {
    return name.empty() || name == "." || name == "..";
  }
  if (name.empty() || name[0] == '.') {
    return true;
  }
  return name == "build" || name == "cmake-build-debug" ||
         name == "cmake-build-release" || name == "node_modules" ||
         name == "_deps" || name == ".cache" || name == "dist" || name == "out" ||
         name == ".venv" || name == "venv" || name == "__pycache__";
}

bool is_indexed_source_path(const std::string& path) {
  const auto ext = fs::path(path).extension().string();
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
         ext == ".hpp" || ext == ".c" || ext == ".py" || ext == ".pyi" || ext == ".pyw";
}

bool should_list_workspace_path(const std::string& relative_path,
                                const IndexFilterOptions& options) {
  if (relative_path.empty()) {
    return false;
  }
  if (options.show_all_files) {
    return true;
  }
  for (const auto& part : fs::path(relative_path)) {
    if (should_skip_dir_name(part.string(), options)) {
      return false;
    }
  }
  return true;
}

bool should_index_relative_path(const std::string& relative_path,
                                const IndexFilterOptions& options) {
  if (!should_list_workspace_path(relative_path, options)) {
    return false;
  }
  return is_indexed_source_path(relative_path);
}

bool is_probably_binary_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  return binary_extensions().count(lowercase_extension(path)) > 0;
}

bool text_looks_binary(const std::string& text) {
  constexpr std::size_t kSample = 8192;
  const std::size_t limit = std::min(text.size(), kSample);
  for (std::size_t i = 0; i < limit; ++i) {
    if (text[i] == '\0') {
      return true;
    }
  }
  return false;
}

bool is_cpp_header_path(const std::string& path) {
  const std::string ext = lowercase_extension(path);
  return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh";
}

std::vector<std::string> companion_source_paths_for_header(const std::string& header_path) {
  std::vector<std::string> out;
  if (!is_cpp_header_path(header_path)) {
    return out;
  }

  const fs::path header = fs::path(header_path);
  const fs::path dir = header.parent_path();
  const std::string stem = header.stem().string();
  const std::string ext = lowercase_extension(header_path);

  std::vector<std::string> suffixes;
  if (ext == ".h") {
    suffixes = {".c", ".cpp", ".cc", ".cxx"};
  } else {
    suffixes = {".cpp", ".cc", ".cxx"};
  }

  for (const std::string& suffix : suffixes) {
    const fs::path candidate = dir / (stem + suffix);
    std::error_code ec;
    if (!fs::is_regular_file(candidate, ec)) {
      continue;
    }
    out.push_back(fs::absolute(candidate).string());
  }
  return out;
}

bool is_lsp_trackable_path(const std::string& path, const std::string& text) {
  if (!is_indexed_source_path(path)) {
    return false;
  }
  if (is_probably_binary_path(path)) {
    return false;
  }
  if (!text.empty() && text_looks_binary(text)) {
    return false;
  }
  return true;
}

}  // namespace tgdb
