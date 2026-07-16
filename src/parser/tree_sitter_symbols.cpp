#include "parser/tree_sitter_symbols.hpp"

#include <algorithm>
#include <cstring>

#include "parser/tree_sitter_ast_utils.hpp"
#include "parser/tree_sitter_language.hpp"

namespace tgdb {

namespace {

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

bool is_class_like_ancestor(TSNode node) {
  while (!ts_node_is_null(node)) {
    const char* type = ts_node_type(node);
    if (type != nullptr &&
        (std::strcmp(type, "class_specifier") == 0 || std::strcmp(type, "struct_specifier") == 0 ||
         std::strcmp(type, "class_definition") == 0)) {
      return true;
    }
    node = ts_node_parent(node);
  }
  return false;
}

void append_symbol(std::vector<SymbolInfo>* out, SymbolKind kind, const std::string& raw_name,
                   TSNode node, int depth, const std::string& file_path) {
  if (raw_name.empty() || ts_node_is_null(node)) {
    return;
  }
  SymbolInfo info;
  info.kind = kind;
  info.name = kind_prefix(kind) + raw_name;
  info.line = static_cast<int>(ts_node_start_point(node).row) + 1;
  info.end_line = static_cast<int>(ts_node_end_point(node).row) + 1;
  info.depth = depth;
  info.file = file_path;
  out->push_back(std::move(info));
}

void walk_cpp_symbols(TSNode node, const std::string& source, int depth,
                      const std::string& file_path, std::vector<SymbolInfo>* out) {
  if (ts_node_is_null(node)) {
    return;
  }
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return;
  }

  if (std::strcmp(type, "namespace_definition") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    append_symbol(out, SymbolKind::kNamespace, ts_identifier_name(name_node, source), node, depth,
                  file_path);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    walk_cpp_symbols(body, source, depth + 1, file_path, out);
    return;
  }

  if (std::strcmp(type, "class_specifier") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    append_symbol(out, SymbolKind::kClass, ts_identifier_name(name_node, source), node, depth,
                  file_path);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    walk_cpp_symbols(body, source, depth + 1, file_path, out);
    return;
  }

  if (std::strcmp(type, "struct_specifier") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    append_symbol(out, SymbolKind::kStruct, ts_identifier_name(name_node, source), node, depth,
                  file_path);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    walk_cpp_symbols(body, source, depth + 1, file_path, out);
    return;
  }

  if (std::strcmp(type, "function_definition") == 0) {
    TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
    if (ts_node_is_null(declarator)) {
      return;
    }
    const std::string name = ts_declarator_name(declarator, source);
    const SymbolKind kind =
        is_class_like_ancestor(ts_node_parent(node)) ? SymbolKind::kMethod : SymbolKind::kFunction;
    append_symbol(out, kind, name, node, depth, file_path);
    return;
  }

  if (std::strcmp(type, "declaration") == 0) {
    TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
    if (!ts_node_is_null(declarator)) {
      const char* decl_type = ts_node_type(declarator);
      if (decl_type != nullptr && std::strcmp(decl_type, "function_declarator") == 0) {
        const std::string name = ts_declarator_name(declarator, source);
        const SymbolKind kind = is_class_like_ancestor(ts_node_parent(node)) ? SymbolKind::kMethod
                                                                             : SymbolKind::kFunction;
        append_symbol(out, kind, name, node, depth, file_path);
        return;
      }
    }
  }

  if (std::strcmp(type, "field_declaration") == 0) {
    TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
    if (!ts_node_is_null(declarator)) {
      const char* decl_type = ts_node_type(declarator);
      if (decl_type != nullptr && std::strcmp(decl_type, "function_declarator") == 0) {
        const std::string name = ts_declarator_name(declarator, source);
        append_symbol(out, SymbolKind::kMethod, name, node, depth, file_path);
        return;
      }
    }
  }

  const uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    walk_cpp_symbols(ts_node_child(node, i), source, depth, file_path, out);
  }
}

void walk_python_symbols(TSNode node, const std::string& source, int depth,
                         const std::string& file_path, std::vector<SymbolInfo>* out) {
  if (ts_node_is_null(node)) {
    return;
  }
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return;
  }

  // tree-sitter-python: class_definition / function_definition use field "name"
  // (unlike C++, where function_definition uses "declarator").
  if (std::strcmp(type, "class_definition") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    append_symbol(out, SymbolKind::kClass, ts_identifier_name(name_node, source), node, depth,
                  file_path);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    walk_python_symbols(body, source, depth + 1, file_path, out);
    return;
  }

  if (std::strcmp(type, "function_definition") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    const SymbolKind kind =
        is_class_like_ancestor(ts_node_parent(node)) ? SymbolKind::kMethod : SymbolKind::kFunction;
    append_symbol(out, kind, ts_identifier_name(name_node, source), node, depth, file_path);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    walk_python_symbols(body, source, depth + 1, file_path, out);
    return;
  }

  // decorated_definition wraps class/function; walk children to reach them.
  const uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    walk_python_symbols(ts_node_child(node, i), source, depth, file_path, out);
  }
}

std::vector<SymbolInfo> sort_symbols(std::vector<SymbolInfo> symbols) {
  std::sort(symbols.begin(), symbols.end(), [](const SymbolInfo& a, const SymbolInfo& b) {
    if (a.line != b.line) {
      return a.line < b.line;
    }
    return a.depth < b.depth;
  });
  return symbols;
}

}  // namespace

std::vector<SymbolInfo> extract_symbols_from_tree(TSNode root, const std::string& source,
                                                    const std::string& file_path) {
  std::vector<SymbolInfo> symbols;
  switch (tree_sitter_lang_kind_for_path(file_path)) {
    case TreeSitterLangKind::kPython:
      walk_python_symbols(root, source, 0, file_path, &symbols);
      break;
    case TreeSitterLangKind::kCpp:
    case TreeSitterLangKind::kNone:
      walk_cpp_symbols(root, source, 0, file_path, &symbols);
      break;
  }
  return sort_symbols(std::move(symbols));
}

}  // namespace tgdb
