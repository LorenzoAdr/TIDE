#include "indexer/index_rules.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

#include "app/language_override.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string lowercase_extension(const std::string& path) {
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

const std::unordered_set<std::string>& binary_extensions() {
  static const std::unordered_set<std::string> kExtensions = {
      ".a",    ".apk",  ".bin",  ".bmp",  ".bz2",  ".cab",  ".class", ".db",
      ".deb",  ".dll",  ".dmg",  ".docx", ".dylib", ".eot",  ".exe",  ".gif",
      ".gz",   ".ico",  ".img",  ".iso",  ".jar",  ".jpeg", ".jpg",  ".ko",
      ".lib",  ".msi",  ".o",    ".obj",  ".otf",  ".pdf",  ".png",  ".pptx",
      ".pyc",  ".pyo",  ".rar",  ".rlib", ".rmeta", ".rpm", ".so",   ".sqlite",
      ".tar",  ".tbz2", ".tgz",  ".ttf",  ".wasm", ".webp", ".woff", ".woff2",
      ".xlsx", ".xz",   ".zip",  ".7z",   ".zst",
  };
  return kExtensions;
}

const std::unordered_set<std::string>& build_noise_extensions() {
  // Single-suffix auxiliaries rewritten heavily by LaTeX/latexmk and similar toolchains.
  static const std::unordered_set<std::string> kExtensions = {
      ".aux", ".bbl", ".bcf", ".blg", ".fdb_latexmk", ".fls", ".glo", ".gls",
      ".idx", ".ilg", ".ind", ".lof", ".log", ".lot", ".nav", ".out", ".snm",
      ".toc", ".vrb", ".xdv", ".dvi", ".acn", ".acr", ".alg", ".pyg",
  };
  return kExtensions;
}

bool path_ends_with_ci(const std::string& path, const std::string& suffix) {
  if (path.size() < suffix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    const unsigned char a = static_cast<unsigned char>(path[path.size() - suffix.size() + i]);
    const unsigned char b = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(a) != std::tolower(b)) {
      return false;
    }
  }
  return true;
}

const std::unordered_set<std::string>& lazy_stub_dir_names() {
  static const std::unordered_set<std::string> kNames = {
      ".git",
      "build",
      "cmake-build-debug",
      "cmake-build-release",
      "node_modules",
      "_deps",
      ".cache",
      "dist",
      "out",
      ".venv",
      "venv",
      "__pycache__",
  };
  return kNames;
}

bool path_has_lazy_stub_component(const std::string& relative_path) {
  for (const auto& part : fs::path(relative_path)) {
    if (is_lazy_stub_dir_name(part.string())) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool is_lazy_stub_dir_name(const std::string& name) {
  return lazy_stub_dir_names().count(name) > 0;
}

bool should_show_lazy_stub(const std::string& name, const IndexFilterOptions& options) {
  if (!is_lazy_stub_dir_name(name)) {
    return false;
  }
  return options.show_all_files;
}

bool should_skip_dir_name(const std::string& name, const IndexFilterOptions& options) {
  if (name.empty() || name == "." || name == "..") {
    return true;
  }
  if (is_lazy_stub_dir_name(name)) {
    return true;
  }
  if (options.show_all_files) {
    return false;
  }
  if (name[0] == '.') {
    return true;
  }
  return false;
}

bool is_indexed_source_path(const std::string& path) {
  if (const auto override_id = language_override_for_path(path); override_id.has_value()) {
    return !override_id->empty() && *override_id != "plaintext";
  }
  const fs::path file_path(path);
  const auto filename = file_path.filename().string();
  if (filename == "CMakeLists.txt" || filename == "Makefile" || filename == "makefile" ||
      filename == "GNUmakefile") {
    return true;
  }
  const auto ext = file_path.extension().string();
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
         ext == ".hpp" || ext == ".c" || ext == ".py" || ext == ".pyi" || ext == ".pyw" ||
         ext == ".sh" || ext == ".bash" || ext == ".tex" || ext == ".sty" || ext == ".cls" ||
         ext == ".rs" || ext == ".go" || ext == ".zig" || ext == ".f" || ext == ".f90" ||
         ext == ".f95" || ext == ".for" || ext == ".lua" || ext == ".js" || ext == ".mjs" ||
         ext == ".cjs" || ext == ".ts" || ext == ".tsx" || ext == ".cmake" || ext == ".mk" ||
         ext == ".yaml" || ext == ".yml" || ext == ".xml" || ext == ".xhtml" || ext == ".svg" ||
         ext == ".xsl" || ext == ".xslt";
}

bool is_build_noise_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  // Compound suffixes (extension() only sees the last component).
  if (path_ends_with_ci(path, ".synctex.gz") || path_ends_with_ci(path, ".synctex(busy)") ||
      path_ends_with_ci(path, ".run.xml")) {
    return true;
  }
  return build_noise_extensions().count(lowercase_extension(path)) > 0;
}

bool should_list_workspace_path(const std::string& relative_path,
                                const IndexFilterOptions& options) {
  if (relative_path.empty()) {
    return false;
  }
  // Nunca indexar en bulk rutas bajo stubs pesados (build/, .git/, …).
  if (path_has_lazy_stub_component(relative_path)) {
    return false;
  }
  // Always hide compile auxiliaries — even with show_all_files — so latexmk storms
  // never enqueue explorer Upserts.
  if (is_build_noise_path(relative_path)) {
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

bool should_track_workspace_delete(const std::string& relative_path,
                                   const IndexFilterOptions& options) {
  if (should_list_workspace_path(relative_path, options)) {
    return true;
  }
  // Noise was formerly listed; still accept deletes so make clean clears stale rows.
  if (relative_path.empty() || path_has_lazy_stub_component(relative_path)) {
    return false;
  }
  return is_build_noise_path(relative_path);
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

bool is_file_picker_candidate_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  const std::string ext = lowercase_extension(path);
  if (ext == ".pdf") {
    return true;
  }
  return !is_probably_binary_path(path);
}

std::string normalize_file_picker_exclude_dir(const std::string& workspace_root,
                                              const std::string& raw) {
  std::string path = raw;
  for (char& c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  while (!path.empty() && (path.back() == '/' || path.back() == ' ')) {
    path.pop_back();
  }
  std::size_t start = 0;
  while (start < path.size() && path[start] == ' ') {
    ++start;
  }
  if (start > 0) {
    path.erase(0, start);
  }
  if (path.empty() || path == "." || path == "..") {
    return {};
  }

  const fs::path as_path(path);
  if (!workspace_root.empty()) {
    fs::path abs = as_path;
    if (abs.is_relative()) {
      abs = fs::path(workspace_root) / abs;
    }
    const fs::path root = fs::path(workspace_root).lexically_normal();
    const fs::path rel = abs.lexically_normal().lexically_relative(root);
    if (rel.empty() || rel == "." || *rel.begin() == "..") {
      return {};
    }
    std::string rel_str = rel.generic_string();
    while (!rel_str.empty() && rel_str.back() == '/') {
      rel_str.pop_back();
    }
    if (rel_str.empty() || rel_str == ".") {
      return {};
    }
    return rel_str;
  }

  for (const auto& part : as_path) {
    if (part == "..") {
      return {};
    }
  }
  while (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
    path.erase(0, 2);
  }
  return path;
}

bool file_picker_path_excluded(const std::string& relative_path,
                               const std::vector<std::string>& exclude_dirs) {
  if (relative_path.empty() || exclude_dirs.empty()) {
    return false;
  }
  for (const std::string& dir : exclude_dirs) {
    if (dir.empty()) {
      continue;
    }
    if (relative_path == dir) {
      return true;
    }
    if (relative_path.size() > dir.size() && relative_path.compare(0, dir.size(), dir) == 0 &&
        relative_path[dir.size()] == '/') {
      return true;
    }
  }
  return false;
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

}  // namespace tuide
