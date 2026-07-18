#include "util/build_file_highlight.hpp"

#include <cctype>
#include <filesystem>
#include <string_view>
#include <unordered_set>

#include "ui/theme.hpp"

namespace tuide {

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
  static const std::unordered_set<std::string> kYaml = {
      "true", "false", "null", "True", "False", "Null", "NULL",
      "yes", "no", "on", "off", "Yes", "No", "ON", "OFF",
  };
  switch (kind) {
    case BuildFileKind::kMakefile:
      return kMakefile;
    case BuildFileKind::kCMake:
      return kCMake;
    case BuildFileKind::kShell:
      return kShell;
    case BuildFileKind::kYaml:
      return kYaml;
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

bool starts_with(const std::string& line, std::size_t pos, std::string_view prefix) {
  return pos + prefix.size() <= line.size() &&
         line.compare(pos, prefix.size(), prefix) == 0;
}

void highlight_markdown_inline(Elements* parts, const std::string& line, std::size_t start,
                               int* offset, int cursor_col, Decorator cursor_style,
                               Decorator default_style, Decorator code_style,
                               Decorator link_style, Decorator url_style) {
  std::size_t i = start;
  while (i < line.size()) {
    if (line[i] == '`') {
      std::size_t j = i + 1;
      while (j < line.size() && line[j] != '`') {
        ++j;
      }
      if (j < line.size()) {
        ++j;
      }
      emit_segment(parts, line.substr(i, j - i), code_style, *offset, cursor_col, cursor_style);
      *offset += static_cast<int>(j - i);
      i = j;
      continue;
    }
    if (line[i] == '[') {
      const std::size_t close = line.find(']', i + 1);
      if (close != std::string::npos && close + 1 < line.size() && line[close + 1] == '(') {
        const std::size_t close_paren = line.find(')', close + 2);
        if (close_paren != std::string::npos) {
          emit_segment(parts, line.substr(i, close - i + 1), link_style, *offset, cursor_col,
                       cursor_style);
          *offset += static_cast<int>(close - i + 1);
          emit_segment(parts, line.substr(close + 1, close_paren - close), url_style, *offset,
                       cursor_col, cursor_style);
          *offset += static_cast<int>(close_paren - close);
          i = close_paren + 1;
          continue;
        }
      }
    }
    emit_segment(parts, std::string(1, line[i]), default_style, *offset, cursor_col,
                 cursor_style);
    *offset += 1;
    ++i;
  }
}

Element highlight_markdown_line(const std::string& line, int cursor_col,
                                Decorator cursor_style) {
  const Decorator default_style = color(theme::SyntaxDefault());
  const Decorator comment_style = color(theme::SyntaxComment()) | dim;
  const Decorator heading_style = color(theme::SyntaxKeyword()) | bold;
  const Decorator code_style = color(theme::SyntaxString());
  const Decorator marker_style = color(theme::SyntaxProperty());
  const Decorator link_style = color(theme::SyntaxFunction());
  const Decorator url_style = color(theme::SyntaxString()) | dim;

  Elements parts;
  int offset = 0;

  const auto comment_pos = line.find("<!--");
  if (comment_pos != std::string::npos) {
    if (comment_pos > 0) {
      highlight_markdown_inline(&parts, line, 0, &offset, cursor_col, cursor_style, default_style,
                                code_style, link_style, url_style);
    }
    const std::size_t end = line.find("-->", comment_pos);
    const std::size_t comment_end = end == std::string::npos ? line.size() : end + 3;
    emit_segment(&parts, line.substr(comment_pos, comment_end - comment_pos), comment_style, offset,
                 cursor_col, cursor_style);
    if (comment_end < line.size()) {
      offset += static_cast<int>(comment_end - comment_pos);
      highlight_markdown_inline(&parts, line, comment_end, &offset, cursor_col, cursor_style,
                                default_style, code_style, link_style, url_style);
    }
    return parts.empty() ? text(line.empty() ? " " : line) | default_style
                         : hbox(std::move(parts));
  }

  if (starts_with(line, 0, "```")) {
    emit_segment(&parts, line, code_style, offset, cursor_col, cursor_style);
    return hbox(std::move(parts));
  }

  std::size_t i = 0;
  if (!line.empty() && line[0] == '#') {
    std::size_t h = 0;
    while (h < line.size() && h < 6 && line[h] == '#') {
      ++h;
    }
    if (h < line.size() && line[h] == ' ') {
      emit_segment(&parts, line.substr(0, h + 1), heading_style, offset, cursor_col, cursor_style);
      offset += static_cast<int>(h + 1);
      i = h + 1;
    }
  } else if (line.size() >= 2 && line[0] == '>' && line[1] == ' ') {
    emit_segment(&parts, "> ", marker_style, offset, cursor_col, cursor_style);
    offset += 2;
    i = 2;
  } else if (i == 0) {
    if (line.size() >= 2 && (line[0] == '-' || line[0] == '*' || line[0] == '+') &&
        line[1] == ' ') {
      emit_segment(&parts, line.substr(0, 2), marker_style, offset, cursor_col, cursor_style);
      offset += 2;
      i = 2;
    } else {
      std::size_t j = 0;
      while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) {
        ++j;
      }
      if (j > 0 && j + 1 < line.size() && line[j] == '.' && line[j + 1] == ' ') {
        emit_segment(&parts, line.substr(0, j + 2), marker_style, offset, cursor_col,
                     cursor_style);
        offset += static_cast<int>(j + 2);
        i = j + 2;
      }
    }
  }

  highlight_markdown_inline(&parts, line, i, &offset, cursor_col, cursor_style, default_style,
                            code_style, link_style, url_style);
  return parts.empty() ? text(line.empty() ? " " : line) | default_style : hbox(std::move(parts));
}

Element highlight_tex_line(const std::string& line, int cursor_col, Decorator cursor_style) {
  const Decorator default_style = color(theme::SyntaxDefault());
  const Decorator comment_style = color(theme::SyntaxComment()) | dim;
  const Decorator string_style = color(theme::SyntaxString());
  const Decorator macro_style = color(theme::SyntaxMacro()) | bold;
  const Decorator operator_style = color(theme::SyntaxOperator());

  Elements parts;
  int offset = 0;
  std::size_t i = 0;
  while (i < line.size()) {
    if (line[i] == '%') {
      emit_segment(&parts, line.substr(i), comment_style, offset, cursor_col, cursor_style);
      break;
    }
    if (line[i] == '$') {
      const bool display = i + 1 < line.size() && line[i + 1] == '$';
      std::size_t j = i + (display ? 2 : 1);
      while (j < line.size()) {
        if (display) {
          if (j + 1 < line.size() && line[j] == '$' && line[j + 1] == '$') {
            j += 2;
            break;
          }
        } else if (line[j] == '$' && line[j - 1] != '\\') {
          ++j;
          break;
        }
        ++j;
      }
      emit_segment(&parts, line.substr(i, j - i), string_style, offset, cursor_col, cursor_style);
      offset += static_cast<int>(j - i);
      i = j;
      continue;
    }
    if (line[i] == '\\') {
      std::size_t j = i + 1;
      if (j < line.size() && std::isalpha(static_cast<unsigned char>(line[j]))) {
        while (j < line.size() && std::isalpha(static_cast<unsigned char>(line[j]))) {
          ++j;
        }
      } else if (j < line.size()) {
        ++j;
      }
      emit_segment(&parts, line.substr(i, j - i), macro_style, offset, cursor_col, cursor_style);
      offset += static_cast<int>(j - i);
      i = j;
      continue;
    }
    if (line[i] == '{' || line[i] == '}') {
      emit_segment(&parts, std::string(1, line[i]), operator_style, offset, cursor_col,
                   cursor_style);
      offset += 1;
      ++i;
      continue;
    }
    emit_segment(&parts, std::string(1, line[i]), default_style, offset, cursor_col,
                 cursor_style);
    offset += 1;
    ++i;
  }

  return parts.empty() ? text(line.empty() ? " " : line) | default_style : hbox(std::move(parts));
}

bool is_xml_name_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' ||
         c == '.';
}

std::size_t scan_xml_name(const std::string& line, std::size_t start) {
  if (start >= line.size() || !is_xml_name_char(line[start])) {
    return start;
  }
  std::size_t j = start + 1;
  while (j < line.size() && is_xml_name_char(line[j])) {
    ++j;
  }
  return j;
}

Element highlight_xml_line(const std::string& line, int cursor_col, Decorator cursor_style) {
  const Decorator default_style = color(theme::SyntaxDefault());
  const Decorator comment_style = color(theme::SyntaxComment()) | dim;
  const Decorator tag_style = color(theme::SyntaxKeyword()) | bold;
  const Decorator attr_style = color(theme::SyntaxProperty());
  const Decorator string_style = color(theme::SyntaxString());
  const Decorator operator_style = color(theme::SyntaxOperator());

  Elements parts;
  int offset = 0;
  std::size_t i = 0;
  while (i < line.size()) {
    if (starts_with(line, i, "<!--")) {
      const std::size_t end = line.find("-->", i);
      const std::size_t comment_end = end == std::string::npos ? line.size() : end + 3;
      emit_segment(&parts, line.substr(i, comment_end - i), comment_style, offset, cursor_col,
                   cursor_style);
      offset += static_cast<int>(comment_end - i);
      i = comment_end;
      continue;
    }
    if (line[i] == '<') {
      emit_segment(&parts, "<", operator_style, offset, cursor_col, cursor_style);
      offset += 1;
      ++i;

      if (i < line.size() && line[i] == '/') {
        emit_segment(&parts, "/", operator_style, offset, cursor_col, cursor_style);
        offset += 1;
        ++i;
      } else if (i + 1 < line.size() && line[i] == '?' && line[i + 1] == '?') {
        // unlikely
      } else if (i < line.size() && line[i] == '?') {
        emit_segment(&parts, "?", operator_style, offset, cursor_col, cursor_style);
        offset += 1;
        ++i;
        const std::size_t name_end = scan_xml_name(line, i);
        if (name_end > i) {
          emit_segment(&parts, line.substr(i, name_end - i), tag_style, offset, cursor_col,
                       cursor_style);
          offset += static_cast<int>(name_end - i);
          i = name_end;
        }
        while (i < line.size()) {
          if (starts_with(line, i, "?>")) {
            emit_segment(&parts, "?>", operator_style, offset, cursor_col, cursor_style);
            offset += 2;
            i += 2;
            break;
          }
          emit_segment(&parts, std::string(1, line[i]), default_style, offset, cursor_col,
                       cursor_style);
          offset += 1;
          ++i;
        }
        continue;
      } else if (i < line.size() && line[i] == '!') {
        emit_segment(&parts, "!", operator_style, offset, cursor_col, cursor_style);
        offset += 1;
        ++i;
        const std::size_t close = line.find('>', i);
        const std::size_t segment_end = close == std::string::npos ? line.size() : close + 1;
        emit_segment(&parts, line.substr(i, segment_end - i), comment_style, offset, cursor_col,
                     cursor_style);
        offset += static_cast<int>(segment_end - i);
        i = segment_end;
        continue;
      }

      const std::size_t name_end = scan_xml_name(line, i);
      if (name_end > i) {
        emit_segment(&parts, line.substr(i, name_end - i), tag_style, offset, cursor_col,
                     cursor_style);
        offset += static_cast<int>(name_end - i);
        i = name_end;
      }

      while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
          emit_segment(&parts, std::string(1, line[i]), default_style, offset, cursor_col,
                       cursor_style);
          offset += 1;
          ++i;
        }
        if (i >= line.size() || line[i] == '>' || (line[i] == '/' && i + 1 < line.size() &&
                                                    line[i + 1] == '>')) {
          break;
        }

        const std::size_t attr_end = scan_xml_name(line, i);
        if (attr_end > i) {
          emit_segment(&parts, line.substr(i, attr_end - i), attr_style, offset, cursor_col,
                       cursor_style);
          offset += static_cast<int>(attr_end - i);
          i = attr_end;
        } else {
          break;
        }

        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
          emit_segment(&parts, std::string(1, line[i]), default_style, offset, cursor_col,
                       cursor_style);
          offset += 1;
          ++i;
        }
        if (i < line.size() && line[i] == '=') {
          emit_segment(&parts, "=", operator_style, offset, cursor_col, cursor_style);
          offset += 1;
          ++i;
          while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            emit_segment(&parts, std::string(1, line[i]), default_style, offset, cursor_col,
                         cursor_style);
            offset += 1;
            ++i;
          }
          if (i < line.size() && (line[i] == '"' || line[i] == '\'')) {
            const char quote = line[i];
            std::size_t j = i + 1;
            while (j < line.size() && line[j] != quote) {
              ++j;
            }
            if (j < line.size()) {
              ++j;
            }
            emit_segment(&parts, line.substr(i, j - i), string_style, offset, cursor_col,
                         cursor_style);
            offset += static_cast<int>(j - i);
            i = j;
          }
        }
      }

      if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '>') {
        emit_segment(&parts, "/>", operator_style, offset, cursor_col, cursor_style);
        offset += 2;
        i += 2;
        continue;
      }
      if (i < line.size() && line[i] == '>') {
        emit_segment(&parts, ">", operator_style, offset, cursor_col, cursor_style);
        offset += 1;
        ++i;
        continue;
      }
      continue;
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
    emit_segment(&parts, std::string(1, line[i]), default_style, offset, cursor_col,
                 cursor_style);
    offset += 1;
    ++i;
  }

  return parts.empty() ? text(line.empty() ? " " : line) | default_style : hbox(std::move(parts));
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
  if (ext == ".yaml" || ext == ".yml") {
    return BuildFileKind::kYaml;
  }
  if (ext == ".sh" || ext == ".bash" || ext == ".zsh" || filename == ".bashrc" ||
      filename == ".profile") {
    return BuildFileKind::kShell;
  }
  if (ext == ".md" || ext == ".markdown" || ext == ".mdown") {
    return BuildFileKind::kMarkdown;
  }
  if (ext == ".tex" || ext == ".latex" || ext == ".sty" || ext == ".cls" || ext == ".bib") {
    return BuildFileKind::kTex;
  }
  if (ext == ".xml" || ext == ".xhtml" || ext == ".svg" || ext == ".xsl" || ext == ".xslt" ||
      ext == ".plist" || ext == ".rss" || ext == ".atom" || ext == ".csproj" || ext == ".props" ||
      ext == ".targets" || ext == ".ui" || ext == ".qrc") {
    return BuildFileKind::kXml;
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
  if (kind == BuildFileKind::kMarkdown) {
    return highlight_markdown_line(line, cursor_col, cursor_style);
  }
  if (kind == BuildFileKind::kTex) {
    return highlight_tex_line(line, cursor_col, cursor_style);
  }
  if (kind == BuildFileKind::kXml) {
    return highlight_xml_line(line, cursor_col, cursor_style);
  }

  const Decorator default_style = color(theme::SyntaxDefault());
  const Decorator comment_style = color(theme::SyntaxComment()) | dim;
  const Decorator string_style = color(theme::SyntaxString());
  const Decorator keyword_style = color(theme::BuildFileKeyword()) | bold;
  const Decorator target_style = color(theme::SyntaxFunction());
  const Decorator key_style = color(theme::SyntaxProperty());
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
      } else if (kind == BuildFileKind::kYaml) {
        std::size_t k = j;
        while (k < line.size() && line[k] == ' ') {
          ++k;
        }
        if (k < line.size() && line[k] == ':') {
          style = key_style;
        }
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

}  // namespace tuide
