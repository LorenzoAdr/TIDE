#pragma once

#include <cstring>
#include <string>

extern "C" {
#include <tree_sitter/api.h>
}

namespace tgdb {

inline std::string ts_node_text(TSNode node, const std::string& source) {
  const uint32_t start = ts_node_start_byte(node);
  const uint32_t end = ts_node_end_byte(node);
  if (start >= source.size() || end > source.size() || end <= start) {
    return {};
  }
  return source.substr(start, end - start);
}

inline std::string ts_identifier_name(TSNode node, const std::string& source) {
  if (ts_node_is_null(node)) {
    return {};
  }
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return {};
  }
  if (std::strcmp(type, "identifier") == 0 || std::strcmp(type, "field_identifier") == 0 ||
      std::strcmp(type, "namespace_identifier") == 0 || std::strcmp(type, "type_identifier") == 0) {
    return ts_node_text(node, source);
  }
  const uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    const std::string child_name = ts_identifier_name(ts_node_child(node, i), source);
    if (!child_name.empty()) {
      return child_name;
    }
  }
  return {};
}

inline std::string ts_declarator_name(TSNode node, const std::string& source) {
  if (ts_node_is_null(node)) {
    return {};
  }
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return {};
  }
  if (std::strcmp(type, "function_declarator") == 0 || std::strcmp(type, "pointer_declarator") == 0 ||
      std::strcmp(type, "reference_declarator") == 0 || std::strcmp(type, "array_declarator") == 0 ||
      std::strcmp(type, "parenthesized_declarator") == 0) {
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      const std::string name = ts_declarator_name(ts_node_child(node, i), source);
      if (!name.empty()) {
        return name;
      }
    }
    return {};
  }
  if (std::strcmp(type, "qualified_identifier") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
      return ts_identifier_name(name_node, source);
    }
  }
  return ts_identifier_name(node, source);
}

inline bool ts_node_contains_point(TSNode node, int line, int col) {
  if (ts_node_is_null(node)) {
    return false;
  }
  const TSPoint start = ts_node_start_point(node);
  const TSPoint end = ts_node_end_point(node);
  if (line < static_cast<int>(start.row) || line > static_cast<int>(end.row)) {
    return false;
  }
  if (line == static_cast<int>(start.row) && col < static_cast<int>(start.column)) {
    return false;
  }
  if (line == static_cast<int>(end.row) && col > static_cast<int>(end.column)) {
    return false;
  }
  return true;
}

}  // namespace tgdb
