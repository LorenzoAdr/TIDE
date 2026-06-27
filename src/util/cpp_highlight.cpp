#include "util/cpp_highlight.hpp"

#include <cctype>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

const std::unordered_set<std::string>& cpp_keywords() {
  static const std::unordered_set<std::string> kKeywords = {
      "alignas", "alignof", "and", "and_eq", "asm", "auto",     "bitand",
      "bitor",   "bool",    "break", "case", "catch", "char",   "class",
      "compl",   "const",   "constexpr", "const_cast", "continue", "decltype",
      "default", "delete",  "do",   "double", "dynamic_cast", "else", "enum",
      "explicit", "export", "extern", "false", "float", "for", "friend",
      "goto",    "if",      "inline", "int", "long", "mutable", "namespace",
      "new",     "noexcept", "not", "not_eq", "nullptr", "operator", "or",
      "or_eq",   "private", "protected", "public", "register", "reinterpret_cast",
      "return",  "short",   "signed", "sizeof", "static", "static_assert",
      "static_cast", "struct", "switch", "template", "this", "thread_local",
      "throw",   "true",    "try",  "typedef", "typeid", "typename", "union",
      "unsigned", "using",  "virtual", "void", "volatile", "wchar_t", "while",
      "xor",     "xor_eq",  "override", "final", "requires", "concept"};
  return kKeywords;
}

bool is_ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

void emit_segment(Elements* out, const std::string& segment, Decorator style, int global_offset,
                  int cursor_col, bool show_cursor, Decorator cursor_style) {
  if (segment.empty()) {
    return;
  }

  const int len = static_cast<int>(segment.size());
  if (!show_cursor || cursor_col < global_offset || cursor_col >= global_offset + len) {
    Element element = text(segment);
    if (style) {
      element = element | style;
    }
    out->push_back(std::move(element));
    return;
  }

  const int rel = cursor_col - global_offset;
  auto emit_part = [&](const std::string& part) {
    if (part.empty()) {
      return;
    }
    Element element = text(part);
    if (style) {
      element = element | style;
    }
    out->push_back(std::move(element));
  };

  emit_part(segment.substr(0, static_cast<std::size_t>(rel)));
  out->push_back(text(segment.substr(static_cast<std::size_t>(rel), 1)) | cursor_style);
  emit_part(segment.substr(static_cast<std::size_t>(rel + 1)));
}

void append_plain(std::string* current, int* plain_start, std::size_t index, Elements* out,
                  int cursor_col, bool show_cursor, Decorator cursor_style) {
  if (current->empty()) {
    return;
  }
  emit_segment(out, *current, Decorator{}, *plain_start, cursor_col, show_cursor, cursor_style);
  current->clear();
  *plain_start = static_cast<int>(index);
}

Element highlight_cpp_line_impl(const std::string& line, int cursor_col, Decorator cursor_style) {
  const bool show_cursor = cursor_col >= 0 && static_cast<bool>(cursor_style);
  Elements parts;
  std::string current;
  int plain_start = 0;
  const std::size_t n = line.size();
  std::size_t i = 0;

  while (i < n) {
    if (line[i] == '/' && i + 1 < n && line[i + 1] == '/') {
      append_plain(&current, &plain_start, i, &parts, cursor_col, show_cursor, cursor_style);
      emit_segment(&parts, line.substr(i), color(Color::Green), static_cast<int>(i), cursor_col,
                   show_cursor, cursor_style);
      break;
    }

    if (line[i] == '"') {
      append_plain(&current, &plain_start, i, &parts, cursor_col, show_cursor, cursor_style);
      std::size_t j = i + 1;
      while (j < n && line[j] != '"') {
        if (line[j] == '\\' && j + 1 < n) {
          ++j;
        }
        ++j;
      }
      if (j < n) {
        ++j;
      }
      emit_segment(&parts, line.substr(i, j - i), color(Color::Yellow), static_cast<int>(i),
                   cursor_col, show_cursor, cursor_style);
      i = j;
      continue;
    }

    if (line[i] == '\'') {
      append_plain(&current, &plain_start, i, &parts, cursor_col, show_cursor, cursor_style);
      std::size_t j = i + 1;
      while (j < n && line[j] != '\'') {
        if (line[j] == '\\' && j + 1 < n) {
          ++j;
        }
        ++j;
      }
      if (j < n) {
        ++j;
      }
      emit_segment(&parts, line.substr(i, j - i), color(Color::Yellow), static_cast<int>(i),
                   cursor_col, show_cursor, cursor_style);
      i = j;
      continue;
    }

    if (line[i] == '#') {
      append_plain(&current, &plain_start, i, &parts, cursor_col, show_cursor, cursor_style);
      emit_segment(&parts, line.substr(i), color(Color::Cyan), static_cast<int>(i), cursor_col,
                   show_cursor, cursor_style);
      break;
    }

    if (std::isdigit(static_cast<unsigned char>(line[i])) ||
        (line[i] == '.' && i + 1 < n &&
         std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
      append_plain(&current, &plain_start, i, &parts, cursor_col, show_cursor, cursor_style);
      std::size_t j = i;
      while (j < n &&
             (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.' ||
              line[j] == 'x' || line[j] == 'X')) {
        ++j;
      }
      emit_segment(&parts, line.substr(i, j - i), color(Color::Magenta), static_cast<int>(i),
                   cursor_col, show_cursor, cursor_style);
      i = j;
      continue;
    }

    if (is_ident_start(line[i])) {
      append_plain(&current, &plain_start, i, &parts, cursor_col, show_cursor, cursor_style);
      std::size_t j = i + 1;
      while (j < n && is_ident_char(line[j])) {
        ++j;
      }
      const std::string word = line.substr(i, j - i);
      const Decorator style =
          cpp_keywords().count(word) > 0 ? color(Color::Blue) | bold : Decorator{};
      emit_segment(&parts, word, style, static_cast<int>(i), cursor_col, show_cursor,
                   cursor_style);
      i = j;
      continue;
    }

    if (current.empty()) {
      plain_start = static_cast<int>(i);
    }
    current.push_back(line[i]);
    ++i;
  }

  append_plain(&current, &plain_start, n, &parts, cursor_col, show_cursor, cursor_style);
  if (parts.empty()) {
    return text("");
  }
  return hbox(std::move(parts));
}

}  // namespace

Element HighlightCppLine(const std::string& line, int cursor_col, Decorator cursor_style) {
  return highlight_cpp_line_impl(line, cursor_col, cursor_style);
}

}  // namespace tgdb
