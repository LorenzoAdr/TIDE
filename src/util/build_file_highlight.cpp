#include "util/build_file_highlight.hpp"

#include <cctype>
#include <filesystem>
#include <unordered_set>

#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

bool is_ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

const std::unordered_set<std::string>& keywords_for(BuildFileKind kind) {
  static const std::unordered_set<std::string> kMakefile = {
      "ifeq", "ifneq", "else", "endif", "include", "define", "endef", "export",
      "unexport", "vpath", "override", "private", "sinclude", "-include",
  };
  static const std::unordered_set<std::string> kCMake = {
      "cmake_minimum_required", "project", "add_executable", "add_library",
      "target_link_libraries", "target_include_directories", "find_package",
      "set", "if", "elseif", "else", "endif", "foreach", "endforeach",
      "function", "endfunction", "macro", "endmacro", "include", "option",
  };
  static const std::unordered_set<std::string> kShell = {
      "if", "then", "elif", "else", "fi", "for", "do", "done", "while",
      "case", "esac", "function", "export", "local", "return", "source",
  };
  switch (kind) {
    case BuildFileKind::kMakefile:
      return kMakefile;
    case BuildFileKind::kCMake:
      return kCMake;
    case BuildFileKind::kShell:
      return kShell;
    case BuildFileKind::kNone:
      break;
  }
  static const std::unordered_set<std::string> kEmpty;
  return kEmpty;
}

void emit_segment(Elements* out, const std::string& segment, Decorator style, int global_offset,
                  int cursor_col, Decorator cursor_style) {
  if (segment.empty()) {
    return;
  }
  const int len = static_cast<int>(segment.size());
  if (cursor_col < 0 || !cursor_style || cursor_col < global_offset ||
      cursor_col >= global_offset + len) {
    out->push_back(text(segment) | style);
    return;
  }
  const int rel = cursor_col - global_offset;
  if (rel > 0) {
    out->push_back(text(segment.substr(0, static_cast<std::size_t>(rel))) | style);
  }
  out->push_back(text(segment.substr(static_cast<std::size_t>(rel), 1)) | cursor_style);
  if (rel + 1 < len) {
    out->push_back(text(segment.substr(static_cast<std::size_t>(rel + 1))) | style);
  }
}

bool line_has_make_target(const std::string& line, std::size_t colon_pos) {
  if (colon_pos == 0) {
    return false;
  }
  for (std::size_t i = 0; i < colon_pos; ++i) {
    const char ch = line[i];
    if (ch == '#' || ch == '$' || ch == '\t') {
      return false;
    }
  }
  return true;
}

}  // namespace

BuildFileKind detect_build_file_kind(const std::string& path) {
  if (path.empty()) {
    return BuildFileKind::kNone;
  }
  const std::string filename = std::filesystem::path(path).filename().string();
  if (filename == "Makefile" || filename == "makefile" || filename == "GNUmakefile" ||
      (filename.size() > 3 && filename.compare(filename.size() - 3, 3, ".mk") == 0)) {
    return BuildFileKind::kMakefile;
  }
  if (filename == "CMakeLists.txt" ||
      (filename.size() > 6 && filename.compare(filename.size() - 6, 6, ".cmake") == 0)) {
    return BuildFileKind::kCMake;
  }
  const auto ext = std::filesystem::path(path).extension().string();
  if (ext == ".sh" || ext == ".bash" || ext == ".zsh" || filename == ".bashrc" ||
      filename == ".profile") {
    return BuildFileKind::kShell;
  }
  return BuildFileKind::kNone;
}

Element HighlightBuildFileLine(const std::string& line, const int cursor_col,
                               Decorator cursor_style) {
  return HighlightBuildFileLine(BuildFileKind::kMakefile, line, cursor_col, cursor_style);
}

Element HighlightBuildFileLine(BuildFileKind kind, const std::string& line, int cursor_col,
                               Decorator cursor_style) {
  if (kind == BuildFileKind::kNone) {
    return text(line.empty() ? " " : line);
  }

  const Decorator default_style = color(theme::SyntaxDefault());
  const Decorator comment_style = color(theme::SyntaxComment()) | dim;
  const Decorator string_style = color(theme::SyntaxString());
  const Decorator keyword_style = color(theme::BuildFileKeyword()) | bold;
  const Decorator target_style = color(theme::SyntaxFunction());
  const auto& keywords = keywords_for(kind);

  Elements parts;
  int offset = 0;
  std::size_t i = 0;
  while (i < line.size()) {
    if (line[i] == '#') {
      emit_segment(&parts, line.substr(i), comment_style, offset, cursor_col, cursor_style);
      break;
    }
    if (line[i] == '"' || line[i] == '\'') {
      const char quote = line[i];
      std::size_t j = i + 1;
      while (j < line.size() && line[j] != quote) {
        ++j;
      }
      if (j < line.size()) {
        ++j;
      }
      emit_segment(&parts, line.substr(i, j - i), string_style, offset, cursor_col, cursor_style);
      offset += static_cast<int>(j - i);
      i = j;
      continue;
    }
    if (is_ident_start(line[i])) {
      std::size_t j = i + 1;
      while (j < line.size() && is_ident_char(line[j])) {
        ++j;
      }
      const std::string word = line.substr(i, j - i);
      Decorator style = default_style;
      if (keywords.count(word) > 0) {
        style = keyword_style;
      } else if (kind == BuildFileKind::kMakefile) {
        const auto colon = line.find(':', j);
        if (colon != std::string::npos && line_has_make_target(line, colon) &&
            colon == j && i == 0) {
          style = target_style;
        }
      }
      emit_segment(&parts, word, style, offset, cursor_col, cursor_style);
      offset += static_cast<int>(j - i);
      i = j;
      continue;
    }
    emit_segment(&parts, std::string(1, line[i]), default_style, offset, cursor_col,
                 cursor_style);
    offset += 1;
    ++i;
  }

  Element content = parts.empty() ? text(line.empty() ? " " : line) | default_style
                                  : hbox(std::move(parts));
  if (kind == BuildFileKind::kMakefile) {
    return content | bgcolor(theme::BuildFileLineBg());
  }
  return content;
}

}  // namespace tgdb
