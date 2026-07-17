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

void walk_bash_symbols(TSNode node, const std::string& source, int depth,
                       const std::string& file_path, std::vector<SymbolInfo>* out) {
  if (ts_node_is_null(node)) {
    return;
  }
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return;
  }

  if (std::strcmp(type, "function_definition") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    append_symbol(out, SymbolKind::kFunction, ts_identifier_name(name_node, source), node, depth,
                  file_path);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    walk_bash_symbols(body, source, depth + 1, file_path, out);
    return;
  }

  const uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    walk_bash_symbols(ts_node_child(node, i), source, depth, file_path, out);
  }
}

std::string latex_curly_text(TSNode group, const std::string& source) {
  if (ts_node_is_null(group)) {
    return {};
  }
  const char* type = ts_node_type(group);
  if (type != nullptr) {
    if (std::strcmp(type, "word") == 0 || std::strcmp(type, "label") == 0) {
      return ts_node_text(group, source);
    }
    if (std::strcmp(type, "text") == 0) {
      std::string out;
      const uint32_t count = ts_node_child_count(group);
      for (uint32_t i = 0; i < count; ++i) {
        const std::string part = latex_curly_text(ts_node_child(group, i), source);
        if (!part.empty()) {
          if (!out.empty()) {
            out += ' ';
          }
          out += part;
        }
      }
      return out;
    }
  }
  // Prefer nested text / word / label nodes; fall back to full group content.
  const uint32_t count = ts_node_child_count(group);
  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_child(group, i);
    const char* ctype = ts_node_type(child);
    if (ctype != nullptr &&
        (std::strcmp(ctype, "text") == 0 || std::strcmp(ctype, "word") == 0 ||
         std::strcmp(ctype, "label") == 0 || std::strcmp(ctype, "curly_group_text") == 0 ||
         std::strcmp(ctype, "curly_group") == 0 || std::strcmp(ctype, "curly_group_label") == 0)) {
      const std::string nested = latex_curly_text(child, source);
      if (!nested.empty()) {
        return nested;
      }
    }
  }
  const std::string named = ts_identifier_name(group, source);
  if (!named.empty()) {
    return named;
  }
  std::string raw = ts_node_text(group, source);
  while (!raw.empty() && (raw.front() == '{' || raw.front() == '[')) {
    raw.erase(raw.begin());
  }
  while (!raw.empty() && (raw.back() == '}' || raw.back() == ']')) {
    raw.pop_back();
  }
  return raw;
}

int latex_section_depth(const char* type) {
  if (type == nullptr) {
    return 0;
  }
  if (std::strcmp(type, "part") == 0) {
    return 0;
  }
  if (std::strcmp(type, "chapter") == 0) {
    return 1;
  }
  if (std::strcmp(type, "section") == 0) {
    return 2;
  }
  if (std::strcmp(type, "subsection") == 0) {
    return 3;
  }
  if (std::strcmp(type, "subsubsection") == 0) {
    return 4;
  }
  if (std::strcmp(type, "paragraph") == 0) {
    return 5;
  }
  if (std::strcmp(type, "subparagraph") == 0) {
    return 6;
  }
  return -1;
}

void walk_latex_symbols(TSNode node, const std::string& source, int /*depth*/,
                        const std::string& file_path, std::vector<SymbolInfo>* out) {
  if (ts_node_is_null(node)) {
    return;
  }
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return;
  }

  const int section_depth = latex_section_depth(type);
  if (section_depth >= 0) {
    TSNode text_node = ts_node_child_by_field_name(node, "text", 4);
    if (ts_node_is_null(text_node)) {
      text_node = ts_node_child_by_field_name(node, "name", 4);
    }
    append_symbol(out, SymbolKind::kNamespace, latex_curly_text(text_node, source), node,
                  section_depth, file_path);
  } else if (std::strcmp(type, "label_definition") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    append_symbol(out, SymbolKind::kVariable, latex_curly_text(name_node, source), node, 1,
                  file_path);
  } else if (std::strcmp(type, "begin") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    const std::string env = latex_curly_text(name_node, source);
    if (env == "document" || env == "figure" || env == "table" || env == "equation" ||
        env == "itemize" || env == "enumerate") {
      append_symbol(out, SymbolKind::kClass, env, node, 1, file_path);
    }
  }

  const uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    walk_latex_symbols(ts_node_child(node, i), source, 0, file_path, out);
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
    case TreeSitterLangKind::kBash:
      walk_bash_symbols(root, source, 0, file_path, &symbols);
      break;
    case TreeSitterLangKind::kLatex:
      walk_latex_symbols(root, source, 0, file_path, &symbols);
      break;
    case TreeSitterLangKind::kRust:
    case TreeSitterLangKind::kGo:
    case TreeSitterLangKind::kZig:
    case TreeSitterLangKind::kFortran:
    case TreeSitterLangKind::kLua:
    case TreeSitterLangKind::kJavaScript:
    case TreeSitterLangKind::kTypeScript:
      break;
    case TreeSitterLangKind::kCpp:
    case TreeSitterLangKind::kNone:
      walk_cpp_symbols(root, source, 0, file_path, &symbols);
      break;
  }
  return sort_symbols(std::move(symbols));
}

}  // namespace tgdb
