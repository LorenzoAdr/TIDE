#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tgdb {

enum class NmBindingFilter {
  kAll,
  kUndefined,
  kDefined,
  kText,
  kData,
  kBss,
  kWeak,
};

enum class NmSymbolType {
  kText,
  kData,
  kBss,
  kUndefined,
  kWeak,
  kLocal,
  kDebug,
  kUnknown,
};

struct NmSymbol {
  std::uintptr_t address = 0;
  std::size_t size = 0;
  char raw_type = '?';
  NmSymbolType category = NmSymbolType::kUnknown;
  std::string translated_type;
  std::string name;
  std::string source_file;
  int source_line = 0;
  bool has_address = false;
  bool has_size = false;
};

struct NmReadResult {
  std::vector<NmSymbol> symbols;
  std::string error;
};

NmSymbolType classify_nm_type(char raw_type);
std::string translate_nm_type(char raw_type);
std::vector<NmSymbol> parse_nm_output(const std::string& output);
NmReadResult read_binary_symbols(const std::string& binary_path);
std::optional<std::string> parse_linker_undefined_reference(const std::string& line);

bool is_nm_analyzable_path(const std::string& path);

std::string nm_binding_filter_label(NmBindingFilter filter);
bool nm_symbol_is_defined(const NmSymbol& symbol);
bool nm_symbol_matches_filter(const NmSymbol& symbol, const std::string& query,
                              NmBindingFilter binding_filter);
void filter_nm_symbol_indices(const std::vector<NmSymbol>& symbols, const std::string& query,
                              NmBindingFilter binding_filter, std::vector<int>* out_indices);

}  // namespace tgdb
