#include "util/cpp_highlight.hpp"

#include <cctype>
#include <string>
#include <unordered_set>
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

void append_plain(std::string* current, Elements* out) {
  if (current->empty()) {
    return;
  }
  out->push_back(text(*current));
  current->clear();
}

}  // namespace

Element HighlightCppLine(const std::string& line) {
  Elements parts;
  std::string current;
  const std::size_t n = line.size();
  std::size_t i = 0;

  while (i < n) {
    if (line[i] == '/' && i + 1 < n && line[i + 1] == '/') {
      append_plain(&current, &parts);
      parts.push_back(text(line.substr(i)) | color(Color::Green));
      break;
    }

    if (line[i] == '"') {
      append_plain(&current, &parts);
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
      parts.push_back(text(line.substr(i, j - i)) | color(Color::Yellow));
      i = j;
      continue;
    }

    if (line[i] == '\'') {
      append_plain(&current, &parts);
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
      parts.push_back(text(line.substr(i, j - i)) | color(Color::Yellow));
      i = j;
      continue;
    }

    if (line[i] == '#') {
      append_plain(&current, &parts);
      parts.push_back(text(line.substr(i)) | color(Color::Cyan));
      break;
    }

    if (std::isdigit(static_cast<unsigned char>(line[i])) ||
        (line[i] == '.' && i + 1 < n &&
         std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
      append_plain(&current, &parts);
      std::size_t j = i;
      while (j < n &&
             (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.' ||
              line[j] == 'x' || line[j] == 'X')) {
        ++j;
      }
      parts.push_back(text(line.substr(i, j - i)) | color(Color::Magenta));
      i = j;
      continue;
    }

    if (is_ident_start(line[i])) {
      append_plain(&current, &parts);
      std::size_t j = i + 1;
      while (j < n && is_ident_char(line[j])) {
        ++j;
      }
      const std::string word = line.substr(i, j - i);
      if (cpp_keywords().count(word) > 0) {
        parts.push_back(text(word) | color(Color::Blue) | bold);
      } else {
        parts.push_back(text(word));
      }
      i = j;
      continue;
    }

    current.push_back(line[i]);
    ++i;
  }

  append_plain(&current, &parts);
  if (parts.empty()) {
    return text("");
  }
  return hbox(std::move(parts));
}

}  // namespace tgdb
