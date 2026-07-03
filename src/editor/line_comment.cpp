#include "editor/line_comment.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::size_t first_non_whitespace(const std::string& line) {
  std::size_t index = 0;
  while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index]))) {
    ++index;
  }
  return index;
}

std::size_t trim_trailing_whitespace(std::string* line) {
  while (!line->empty() && std::isspace(static_cast<unsigned char>(line->back()))) {
    line->pop_back();
  }
  return line->size();
}

bool starts_with_at(const std::string& line, std::size_t pos, const std::string& prefix) {
  return pos + prefix.size() <= line.size() && line.compare(pos, prefix.size(), prefix) == 0;
}

LineCommentStyle hash_style() { return {"# ", ""}; }
LineCommentStyle slash_style() { return {"// ", ""}; }
LineCommentStyle dash_style() { return {"-- ", ""}; }
LineCommentStyle html_style() { return {"<!-- ", " -->"}; }

bool extension_in(const std::string& ext, std::initializer_list<const char*> values) {
  for (const char* value : values) {
    if (ext == value) {
      return true;
    }
  }
  return false;
}

}  // namespace

LineCommentStyle line_comment_style_for_path(const std::string& path) {
  if (path.empty()) {
    return slash_style();
  }

  const fs::path file_path(path);
  const std::string filename = lower_copy(file_path.filename().string());
  const std::string ext = lower_copy(file_path.extension().string());

  if (filename == "cmakelists.txt" || filename == "makefile" || filename == "gnumakefile" ||
      filename == "dockerfile" || ext == ".cmake") {
    return hash_style();
  }

  if (extension_in(ext, {".py", ".pyw", ".sh", ".bash", ".zsh", ".fish", ".yaml", ".yml",
                         ".toml", ".rb", ".rake", ".pl", ".pm", ".r", ".nim", ".ex", ".exs",
                         ".ps1", ".conf", ".ini", ".env", ".gitignore", ".dockerignore",
                         ".properties", ".gradle", ".tf", ".hcl", ".nix"})) {
    return hash_style();
  }

  if (extension_in(ext, {".lua", ".sql", ".hs", ".elm", ".tcl"})) {
    return dash_style();
  }

  if (extension_in(ext, {".html", ".htm", ".xhtml", ".xml", ".svg", ".xsl", ".xslt"})) {
    return html_style();
  }

  if (extension_in(ext, {".tex", ".latex", ".sty", ".cls"})) {
    return {"% ", ""};
  }

  if (extension_in(ext, {".vim", ".vimrc"})) {
    return {"\" ", ""};
  }

  if (extension_in(ext, {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh", ".hxx", ".cu",
                         ".java", ".js", ".mjs", ".cjs", ".ts", ".tsx", ".jsx", ".go", ".rs",
                         ".swift", ".kt", ".kts", ".cs", ".scala", ".sc", ".dart", ".php",
                         ".m", ".mm", ".ino", ".glsl", ".hlsl", ".proto", ".jsonc", ".svelte",
                         ".vue"})) {
    return slash_style();
  }

  if (ext == ".json" || ext == ".md" || ext == ".txt") {
    return hash_style();
  }

  return slash_style();
}

void comment_line_text(std::string* line, const LineCommentStyle& style) {
  if (line == nullptr || line->empty()) {
    return;
  }
  const std::size_t pos = first_non_whitespace(*line);
  if (line_is_commented(*line, style)) {
    return;
  }

  if (!style.suffix.empty()) {
    trim_trailing_whitespace(line);
    line->insert(pos, style.prefix);
    line->append(style.suffix);
    return;
  }

  line->insert(pos, style.prefix);
}

bool uncomment_line_text(std::string* line, const LineCommentStyle& style) {
  if (line == nullptr || line->empty()) {
    return false;
  }

  const std::size_t pos = first_non_whitespace(*line);
  if (starts_with_at(*line, pos, style.prefix)) {
    line->erase(pos, style.prefix.size());
    if (!style.suffix.empty()) {
      trim_trailing_whitespace(line);
      if (line->size() >= style.suffix.size() &&
          line->compare(line->size() - style.suffix.size(), style.suffix.size(), style.suffix) ==
              0) {
        line->erase(line->size() - style.suffix.size(), style.suffix.size());
      }
    }
    return true;
  }

  if (style.prefix == "// " && starts_with_at(*line, pos, "//")) {
    std::size_t erase_len = 2;
    if (pos + 2 < line->size() && (*line)[pos + 2] == ' ') {
      erase_len = 3;
    }
    line->erase(pos, erase_len);
    return true;
  }

  if (style.prefix == "# " && starts_with_at(*line, pos, "#")) {
    std::size_t erase_len = 1;
    if (pos + 1 < line->size() && (*line)[pos + 1] == ' ') {
      erase_len = 2;
    }
    line->erase(pos, erase_len);
    return true;
  }

  if (style.prefix == "-- " && starts_with_at(*line, pos, "--")) {
    std::size_t erase_len = 2;
    if (pos + 2 < line->size() && (*line)[pos + 2] == ' ') {
      erase_len = 3;
    }
    line->erase(pos, erase_len);
    return true;
  }

  return false;
}

bool line_is_commented(const std::string& line, const LineCommentStyle& style) {
  if (line.empty()) {
    return false;
  }
  const std::size_t pos = first_non_whitespace(line);
  if (starts_with_at(line, pos, style.prefix)) {
    return true;
  }
  if (style.prefix == "// " && starts_with_at(line, pos, "//")) {
    return true;
  }
  if (style.prefix == "# " && starts_with_at(line, pos, "#")) {
    return true;
  }
  if (style.prefix == "-- " && starts_with_at(line, pos, "--")) {
    return true;
  }
  return false;
}

}  // namespace tgdb
