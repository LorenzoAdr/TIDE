#include "core_analyzer/output_parser.hpp"

#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace tgdb {

namespace {

std::uintptr_t parse_hex_address(const std::string& text) {
  std::string value = text;
  if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
    value = value.substr(2);
  }
  if (value.empty()) {
    return 0;
  }
  return static_cast<std::uintptr_t>(std::strtoull(value.c_str(), nullptr, 16));
}

std::string trim_copy(const std::string& text) {
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }
  std::size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(start, end - start);
}

}  // namespace

std::string build_obj_search_command(const std::string& type_query) {
  std::string query = trim_copy(type_query);
  if (query.empty()) {
    return "obj (void*)0";
  }
  if (query.find('*') != std::string::npos) {
    return "obj (" + query + ")0";
  }
  return "obj (" + query + "*)0";
}

CoreAnalyzerParseResult parse_obj_command_output(const std::string& output) {
  CoreAnalyzerParseResult result;
  result.raw_output = output;

  static const std::regex heap_block_re(
      R"(\[heap block\]\s+(0x[0-9a-fA-F]+)\s*--\s*(0x[0-9a-fA-F]+)\s+size=(\d+))",
      std::regex::icase);
  static const std::regex heap_block_short_re(
      R"(\[heap block\]\s+(0x[0-9a-fA-F]+)\s*--\s*(0x[0-9a-fA-F]+)\s+size=(\d+))",
      std::regex::icase);

  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    std::smatch match;
    if (std::regex_search(line, match, heap_block_re) ||
        std::regex_search(line, match, heap_block_short_re)) {
      CoreAnalyzerInstance instance;
      instance.address_hex = match[1].str();
      instance.address = parse_hex_address(instance.address_hex);
      instance.size = static_cast<std::size_t>(std::stoull(match[3].str()));
      result.instances.push_back(std::move(instance));
      continue;
    }

    static const std::regex single_heap_re(
        R"(\[heap block\]\s+(0x[0-9a-fA-F]+)\s*--\s*(0x[0-9a-fA-F]+))",
        std::regex::icase);
    if (std::regex_search(line, match, single_heap_re)) {
      CoreAnalyzerInstance instance;
      instance.address_hex = match[1].str();
      instance.address = parse_hex_address(instance.address_hex);
      const std::uintptr_t end = parse_hex_address(match[2].str());
      if (end > instance.address) {
        instance.size = end - instance.address;
      }
      result.instances.push_back(std::move(instance));
      continue;
    }

    static const std::regex ref_re(
        R"(\[stack\]\s+thread\s+(\d+).*:\s+(0x[0-9a-fA-F]+))",
        std::regex::icase);
    if (!result.instances.empty() && std::regex_search(line, match, ref_re)) {
      result.instances.back().reference_summary =
          "thread " + match[1].str() + " → " + match[2].str();
    }
  }

  return result;
}

CoreAnalyzerParseResult parse_ref_command_output(const std::string& output) {
  return parse_obj_command_output(output);
}

}  // namespace tgdb
