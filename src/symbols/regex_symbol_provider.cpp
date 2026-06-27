#include "symbols/regex_symbol_provider.hpp"

#include <cctype>
#include <fstream>
#include <string>

namespace tgdb {

namespace {

SymbolKind kind_from_keyword(const std::string& kw) {
  if (kw == "namespace") {
    return SymbolKind::kNamespace;
  }
  if (kw == "class") {
    return SymbolKind::kClass;
  }
  if (kw == "struct") {
    return SymbolKind::kStruct;
  }
  return SymbolKind::kFunction;
}

std::string kind_prefix(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::kNamespace:
      return "ns ";
    case SymbolKind::kClass:
      return "C ";
    case SymbolKind::kStruct:
      return "S ";
    case SymbolKind::kMethod:
      return "M ";
    case SymbolKind::kVariable:
      return "v ";
    case SymbolKind::kFunction:
    default:
      return "f ";
  }
}

void trim_comment(std::string* line) {
  const auto pos = line->find("//");
  if (pos != std::string::npos) {
    line->erase(pos);
  }
}

bool starts_with_keyword(const std::string& line, const std::string& kw) {
  const auto pos = line.find(kw);
  if (pos == std::string::npos) {
    return false;
  }
  if (pos > 0 && !std::isspace(static_cast<unsigned char>(line[pos - 1]))) {
    return false;
  }
  const auto after = pos + kw.size();
  if (after >= line.size()) {
    return true;
  }
  return std::isspace(static_cast<unsigned char>(line[after]));
}

bool read_identifier_after(const std::string& line, std::size_t from,
                           std::string* out) {
  while (from < line.size() &&
         std::isspace(static_cast<unsigned char>(line[from]))) {
    ++from;
  }
  if (from >= line.size() ||
      !(std::isalpha(static_cast<unsigned char>(line[from])) || line[from] == '_')) {
    return false;
  }
  std::size_t end = from + 1;
  while (end < line.size() &&
         (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) {
    ++end;
  }
  *out = line.substr(from, end - from);
  return true;
}

bool looks_like_function(const std::string& line) {
  const auto open = line.find('(');
  const auto close = line.find(')', open == std::string::npos ? 0 : open);
  if (open == std::string::npos || close == std::string::npos || close < open) {
    return false;
  }
  if (line.find(';') != std::string::npos) {
    return false;
  }
  return line.find('{') != std::string::npos || line.back() == ')';
}

bool read_function_name(const std::string& line, std::string* name) {
  const auto open = line.find('(');
  if (open == std::string::npos) {
    return false;
  }
  std::size_t end = open;
  while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
    --end;
  }
  std::size_t start = end;
  while (start > 0) {
    const char c = line[start - 1];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' ||
        c == '~') {
      --start;
      continue;
    }
    break;
  }
  if (start == end) {
    return false;
  }
  *name = line.substr(start, end - start);
  const auto scope = name->rfind("::");
  if (scope != std::string::npos) {
    *name = name->substr(scope + 2);
  }
  return !name->empty();
}

}  // namespace

std::vector<SymbolInfo> RegexSymbolProvider::symbols_for_file(
    const std::string& path) {
  std::vector<SymbolInfo> symbols;
  if (path.empty()) {
    return symbols;
  }

  std::ifstream input(path);
  if (!input) {
    return symbols;
  }

  std::string line;
  int line_no = 0;
  int depth = 0;
  while (std::getline(input, line)) {
    ++line_no;
    trim_comment(&line);

    for (const char* kw : {"namespace", "class", "struct"}) {
        const std::string keyword = kw;
        if (starts_with_keyword(line, keyword)) {
        std::string name;
        const auto kw_pos = line.find(keyword);
        if (read_identifier_after(line, kw_pos + keyword.size(), &name)) {
          SymbolInfo info;
          info.kind = kind_from_keyword(keyword);
          info.name = kind_prefix(info.kind) + name;
          info.line = line_no;
          info.depth = depth;
          symbols.push_back(std::move(info));
          if (keyword != "namespace") {
            ++depth;
          }
        }
        break;
      }
    }

    if (!looks_like_function(line)) {
      continue;
    }

    std::string name;
    if (!read_function_name(line, &name)) {
      continue;
    }
    if (name == "if" || name == "for" || name == "while" || name == "switch" ||
        name == "catch" || name == "return" || name == "sizeof" ||
        name == "static_cast" || name == "dynamic_cast" ||
        name == "reinterpret_cast" || name == "const_cast") {
      continue;
    }

    SymbolInfo info;
    info.kind = depth > 0 ? SymbolKind::kMethod : SymbolKind::kFunction;
    info.name = kind_prefix(info.kind) + name;
    info.line = line_no;
    info.depth = depth;
    symbols.push_back(std::move(info));
  }
  return symbols;
}

}  // namespace tgdb
