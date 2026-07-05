#include "util/nm_reader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>

#include "util/shell_utils.hpp"

namespace tgdb {

namespace {

std::string trim_copy(const std::string& value) {
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

std::string to_lower_ascii(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool parse_hex_uintptr(const std::string& text, std::uintptr_t* out) {
  if (text.empty() || out == nullptr) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 16);
    if (consumed != text.size()) {
      return false;
    }
    *out = static_cast<std::uintptr_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_hex_size(const std::string& text, std::size_t* out) {
  if (text.empty() || out == nullptr) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 16);
    if (consumed != text.size()) {
      return false;
    }
    *out = static_cast<std::size_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool looks_like_source_location_line(const std::string& line) {
  const std::string trimmed = trim_copy(line);
  if (trimmed.empty()) {
    return false;
  }
  if (std::isxdigit(static_cast<unsigned char>(trimmed.front()))) {
    return false;
  }
  if (trimmed.find(' ') != std::string::npos || trimmed.find('\t') != std::string::npos) {
    return false;
  }
  const auto colon = trimmed.rfind(':');
  if (colon == std::string::npos || colon + 1 >= trimmed.size()) {
    return false;
  }
  const std::string tail = trimmed.substr(colon + 1);
  if (tail.empty()) {
    return false;
  }
  for (char c : tail) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  const std::string path_part = trimmed.substr(0, colon);
  return path_part.find('/') != std::string::npos || path_part.find('\\') != std::string::npos;
}

bool looks_like_archive_member_header(const std::string& line) {
  const std::string trimmed = trim_copy(line);
  return trimmed.size() > 1 && trimmed.back() == ':' && trimmed.find(' ') == std::string::npos &&
         trimmed.find('\t') == std::string::npos;
}

bool parse_source_location(const std::string& text, std::string* file_out, int* line_out) {
  if (file_out == nullptr || line_out == nullptr) {
    return false;
  }
  const std::string trimmed = trim_copy(text);
  const auto colon = trimmed.rfind(':');
  if (colon == std::string::npos || colon + 1 >= trimmed.size()) {
    return false;
  }
  const std::string tail = trimmed.substr(colon + 1);
  try {
    *line_out = std::stoi(tail);
  } catch (...) {
    return false;
  }
  *file_out = trimmed.substr(0, colon);
  return *line_out > 0 && !file_out->empty();
}

void attach_inline_source_path(NmSymbol* symbol) {
  if (symbol == nullptr || symbol->name.empty()) {
    return;
  }
  const auto colon = symbol->name.rfind(':');
  if (colon == std::string::npos || colon + 1 >= symbol->name.size()) {
    return;
  }
  const std::string tail = symbol->name.substr(colon + 1);
  bool all_digits = !tail.empty();
  for (char c : tail) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      all_digits = false;
      break;
    }
  }
  if (!all_digits) {
    return;
  }
  const std::size_t path_start = symbol->name.find('/');
  if (path_start == std::string::npos) {
    const std::size_t path_start_win = symbol->name.find('\\');
    if (path_start_win == std::string::npos) {
      return;
    }
    std::string file;
    int line = 0;
    const std::string location_token = symbol->name.substr(path_start_win);
    if (!parse_source_location(location_token, &file, &line)) {
      return;
    }
    symbol->source_file = std::move(file);
    symbol->source_line = line;
    symbol->name = trim_copy(symbol->name.substr(0, path_start_win));
    return;
  }
  std::string file;
  int line = 0;
  const std::string location_token = symbol->name.substr(path_start);
  if (!parse_source_location(location_token, &file, &line)) {
    return;
  }
  symbol->source_file = std::move(file);
  symbol->source_line = line;
  symbol->name = trim_copy(symbol->name.substr(0, path_start));
}

bool is_nm_type_char(char c) {
  return std::isupper(static_cast<unsigned char>(c)) ||
         std::islower(static_cast<unsigned char>(c)) || c == '?';
}

void finalize_symbol(NmSymbol* symbol) {
  if (symbol == nullptr) {
    return;
  }
  symbol->category = classify_nm_type(symbol->raw_type);
  symbol->translated_type = translate_nm_type(symbol->raw_type);
}

bool parse_nm_symbol_line(const std::string& line, NmSymbol* symbol) {
  if (symbol == nullptr) {
    return false;
  }
  const std::string trimmed = trim_copy(line);
  if (trimmed.empty()) {
    return false;
  }

  std::istringstream stream(trimmed);
  std::string token;
  std::vector<std::string> tokens;
  while (stream >> token) {
    tokens.push_back(token);
  }
  if (tokens.size() < 2) {
    return false;
  }

  NmSymbol parsed;
  std::size_t index = 0;

  if (parse_hex_uintptr(tokens[index], &parsed.address)) {
    parsed.has_address = true;
    ++index;
  }

  if (index < tokens.size() && parse_hex_size(tokens[index], &parsed.size)) {
    parsed.has_size = true;
    ++index;
  }

  if (index >= tokens.size() || tokens[index].size() != 1 ||
      !is_nm_type_char(tokens[index][0])) {
    return false;
  }
  parsed.raw_type = tokens[index][0];
  ++index;

  if (index >= tokens.size()) {
    return false;
  }

  parsed.name = tokens[index];
  std::string inline_source;
  for (++index; index < tokens.size(); ++index) {
    const std::string& extra = tokens[index];
    if ((extra.find('/') != std::string::npos || extra.find('\\') != std::string::npos) &&
        extra.rfind(':') != std::string::npos) {
      inline_source = extra;
      continue;
    }
    parsed.name.push_back(' ');
    parsed.name += extra;
  }
  if (!inline_source.empty()) {
    parse_source_location(inline_source, &parsed.source_file, &parsed.source_line);
  }

  finalize_symbol(&parsed);
  attach_inline_source_path(&parsed);
  *symbol = std::move(parsed);
  return true;
}

}  // namespace

NmSymbolType classify_nm_type(char raw_type) {
  switch (raw_type) {
    case 'T':
    case 't':
      return NmSymbolType::kText;
    case 'D':
    case 'd':
    case 'R':
    case 'r':
    case 'C':
    case 'c':
    case 'S':
    case 's':
    case 'G':
    case 'g':
      return NmSymbolType::kData;
    case 'B':
    case 'b':
      return NmSymbolType::kBss;
    case 'U':
    case 'u':
      return NmSymbolType::kUndefined;
    case 'W':
    case 'w':
      return NmSymbolType::kWeak;
    case 'N':
    case 'n':
    case 'V':
    case 'v':
      return NmSymbolType::kDebug;
    default:
      if (std::islower(static_cast<unsigned char>(raw_type))) {
        return NmSymbolType::kLocal;
      }
      return NmSymbolType::kUnknown;
  }
}

std::string translate_nm_type(char raw_type) {
  switch (raw_type) {
    case 'T':
      return "Código (global, definido)";
    case 't':
      return "Código (local, definido)";
    case 'D':
      return "Datos (global, definido)";
    case 'd':
      return "Datos (local, definido)";
    case 'B':
      return "BSS (global, definido)";
    case 'b':
      return "BSS (local, definido)";
    case 'U':
      return "Sin definir (referencia externa)";
    case 'u':
      return "Sin definir (local)";
    case 'W':
      return "Débil (global)";
    case 'w':
      return "Débil (local)";
    case 'R':
      return "Solo lectura (global)";
    case 'r':
      return "Solo lectura (local)";
    case 'C':
      return "Común (global)";
    case 'c':
      return "Común (local)";
    case 'A':
      return "Absoluto";
    case 'a':
      return "Absoluto (local)";
    case 'N':
      return "Depuración (nombre)";
    case 'n':
      return "Depuración (local)";
    case 'V':
      return "Objeto débil (global)";
    case 'v':
      return "Objeto débil (local)";
    case '?':
      return "Tipo desconocido";
    default:
      if (std::islower(static_cast<unsigned char>(raw_type))) {
        return "Símbolo local";
      }
      return "Símbolo global";
  }
}

std::vector<NmSymbol> parse_nm_output(const std::string& output) {
  std::vector<NmSymbol> symbols;
  std::istringstream stream(output);
  std::string line;
  NmSymbol* pending = nullptr;

  while (std::getline(stream, line)) {
    if (looks_like_archive_member_header(line)) {
      pending = nullptr;
      continue;
    }
    if (looks_like_source_location_line(line)) {
      if (pending != nullptr) {
        parse_source_location(line, &pending->source_file, &pending->source_line);
      }
      continue;
    }

    NmSymbol symbol;
    if (!parse_nm_symbol_line(line, &symbol)) {
      continue;
    }
    symbols.push_back(std::move(symbol));
    pending = &symbols.back();
  }
  return symbols;
}

NmReadResult read_binary_symbols(const std::string& binary_path) {
  NmReadResult result;
  if (binary_path.empty()) {
    result.error = "Ruta de binario vacía";
    return result;
  }
  std::error_code ec;
  if (!std::filesystem::exists(binary_path, ec)) {
    result.error = "Archivo no encontrado";
    return result;
  }
  if (!is_nm_analyzable_path(binary_path)) {
    result.error = "No es un binario ELF/ar analizable con nm";
    return result;
  }

  const std::string command =
      shell_quote("nm") + " --demangle --print-size " + shell_quote(binary_path);
  const std::string output = run_shell_capture(command, 120);
  if (output.empty()) {
    result.error = "nm no devolvió salida (¿está instalado?)";
    return result;
  }

  result.symbols = parse_nm_output(output);
  if (result.symbols.empty()) {
    result.error = "No se encontraron símbolos";
  }
  return result;
}

std::optional<std::string> parse_linker_undefined_reference(const std::string& line) {
  const std::string trimmed = trim_copy(line);
  const std::string marker = "undefined reference to `";
  const auto start = trimmed.find(marker);
  if (start == std::string::npos) {
    return std::nullopt;
  }
  const auto name_start = start + marker.size();
  const auto end = trimmed.find('\'', name_start);
  if (end == std::string::npos || end <= name_start) {
    return std::nullopt;
  }
  return trimmed.substr(name_start, end - name_start);
}

bool is_nm_analyzable_path(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec) || ec) {
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  char magic[8] = {};
  file.read(magic, sizeof(magic));
  if (file.gcount() >= 4 && magic[0] == '\x7f' && magic[1] == 'E' && magic[2] == 'L' &&
      magic[3] == 'F') {
    return true;
  }
  if (file.gcount() >= 8 && std::string(magic, 8) == "!<arch>\n") {
    return true;
  }
  return false;
}

std::string nm_binding_filter_label(NmBindingFilter filter) {
  switch (filter) {
    case NmBindingFilter::kUndefined:
      return "Sin definir";
    case NmBindingFilter::kDefined:
      return "Definidos";
    case NmBindingFilter::kText:
      return "Código";
    case NmBindingFilter::kData:
      return "Datos";
    case NmBindingFilter::kBss:
      return "BSS";
    case NmBindingFilter::kWeak:
      return "Débiles";
    case NmBindingFilter::kAll:
    default:
      return "Todos";
  }
}

namespace {

bool symbol_matches_binding_filter(const NmSymbol& symbol, NmBindingFilter binding_filter) {
  switch (binding_filter) {
    case NmBindingFilter::kUndefined:
      return symbol.category == NmSymbolType::kUndefined;
    case NmBindingFilter::kDefined:
      return symbol.category != NmSymbolType::kUndefined && symbol.raw_type != 'U' &&
             symbol.raw_type != 'u';
    case NmBindingFilter::kText:
      return symbol.category == NmSymbolType::kText;
    case NmBindingFilter::kData:
      return symbol.category == NmSymbolType::kData;
    case NmBindingFilter::kBss:
      return symbol.category == NmSymbolType::kBss;
    case NmBindingFilter::kWeak:
      return symbol.category == NmSymbolType::kWeak;
    case NmBindingFilter::kAll:
    default:
      return true;
  }
}

bool name_contains_insensitive(const std::string& haystack, const std::string& needle_lower) {
  if (needle_lower.empty()) {
    return true;
  }
  if (haystack.size() < needle_lower.size()) {
    return false;
  }
  for (std::size_t i = 0; i + needle_lower.size() <= haystack.size(); ++i) {
    bool match = true;
    for (std::size_t j = 0; j < needle_lower.size(); ++j) {
      if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
          static_cast<unsigned char>(needle_lower[j])) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool nm_symbol_is_defined(const NmSymbol& symbol) {
  return symbol.category != NmSymbolType::kUndefined && symbol.raw_type != 'U' &&
         symbol.raw_type != 'u';
}

bool nm_symbol_matches_filter(const NmSymbol& symbol, const std::string& query,
                              NmBindingFilter binding_filter) {
  if (!symbol_matches_binding_filter(symbol, binding_filter)) {
    return false;
  }
  if (query.empty()) {
    return true;
  }
  return name_contains_insensitive(symbol.name, to_lower_ascii(query));
}

void filter_nm_symbol_indices(const std::vector<NmSymbol>& symbols, const std::string& query,
                              NmBindingFilter binding_filter, std::vector<int>* out_indices) {
  if (out_indices == nullptr) {
    return;
  }
  out_indices->clear();
  if (symbols.empty()) {
    return;
  }
  if (query.empty() && binding_filter == NmBindingFilter::kAll) {
    out_indices->resize(symbols.size());
    std::iota(out_indices->begin(), out_indices->end(), 0);
    return;
  }
  out_indices->reserve(symbols.size() / 4);
  const std::string needle = to_lower_ascii(query);
  for (int i = 0; i < static_cast<int>(symbols.size()); ++i) {
    const NmSymbol& symbol = symbols[static_cast<std::size_t>(i)];
    if (!symbol_matches_binding_filter(symbol, binding_filter)) {
      continue;
    }
    if (!name_contains_insensitive(symbol.name, needle)) {
      continue;
    }
    out_indices->push_back(i);
  }
}

}  // namespace tgdb
