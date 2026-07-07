#include "parser/tree_sitter_locals.hpp"

#include <algorithm>
#include <climits>

#include "editor/editor_context.hpp"
#include "parser/tree_sitter_ast_utils.hpp"
#include "parser/tree_sitter_language.hpp"

namespace tgdb {

namespace {

const char* kEmbeddedLocalsQuery = R"scm(
(function_definition) @local.scope
(declaration) @local.scope
(lambda_expression) @local.scope
(namespace_definition) @local.scope
(class_specifier) @local.scope
(struct_specifier) @local.scope
(for_range_loop) @local.scope
(while_statement) @local.scope
(for_statement) @local.scope
(do_statement) @local.scope
(if_statement) @local.scope
(switch_statement) @local.scope
(try_statement) @local.scope
(catch_clause) @local.scope
(compound_statement) @local.scope
(parameter_declaration (identifier) @local.definition)
(parameter_declaration (_ (identifier) @local.definition))
(parameter_declaration (_ (_ (identifier) @local.definition)))
(parameter_declaration (_ (_ (_ (identifier) @local.definition))))
(optional_parameter_declaration declarator: (identifier) @local.definition)
(variadic_parameter_declaration declarator: (variadic_declarator (identifier) @local.definition))
(type_parameter_declaration (type_identifier) @local.definition)
(identifier) @local.reference
(call_expression function: (identifier) @_)
)scm";

TSQuery* locals_query() {
  static TSQuery* query = nullptr;
  static uint32_t error_offset = 0;
  static TSQueryError error_type = TSQueryErrorNone;
  if (query == nullptr) {
    query = ts_query_new(tree_sitter_cpp_language(), kEmbeddedLocalsQuery,
                         static_cast<uint32_t>(std::strlen(kEmbeddedLocalsQuery)), &error_offset,
                         &error_type);
  }
  return query;
}

bool is_class_like_ancestor(TSNode node) {
  while (!ts_node_is_null(node)) {
    const char* type = ts_node_type(node);
    if (type != nullptr &&
        (std::strcmp(type, "class_specifier") == 0 || std::strcmp(type, "struct_specifier") == 0)) {
      return true;
    }
    node = ts_node_parent(node);
  }
  return false;
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

SymbolKind scope_kind_for_node(TSNode node) {
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return SymbolKind::kFunction;
  }
  if (std::strcmp(type, "namespace_definition") == 0) {
    return SymbolKind::kNamespace;
  }
  if (std::strcmp(type, "class_specifier") == 0) {
    return SymbolKind::kClass;
  }
  if (std::strcmp(type, "struct_specifier") == 0) {
    return SymbolKind::kStruct;
  }
  if (std::strcmp(type, "function_definition") == 0) {
    return is_class_like_ancestor(ts_node_parent(node)) ? SymbolKind::kMethod : SymbolKind::kFunction;
  }
  if (std::strcmp(type, "declaration") == 0) {
    return SymbolKind::kVariable;
  }
  if (std::strcmp(type, "lambda_expression") == 0) {
    return SymbolKind::kFunction;
  }
  return SymbolKind::kFunction;
}

std::string scope_name_for_node(TSNode node, const std::string& source) {
  const char* type = ts_node_type(node);
  if (type == nullptr) {
    return {};
  }
  if (std::strcmp(type, "lambda_expression") == 0) {
    return "<lambda>";
  }
  if (std::strcmp(type, "namespace_definition") == 0 || std::strcmp(type, "class_specifier") == 0 ||
      std::strcmp(type, "struct_specifier") == 0) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    return ts_identifier_name(name_node, source);
  }
  if (std::strcmp(type, "function_definition") == 0) {
    TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
    return ts_declarator_name(declarator, source);
  }
  if (std::strcmp(type, "declaration") == 0) {
    TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
    return ts_declarator_name(declarator, source);
  }
  if (std::strcmp(type, "compound_statement") == 0) {
    return "<block>";
  }
  if (std::strcmp(type, "while_statement") == 0) {
    return "while";
  }
  if (std::strcmp(type, "for_statement") == 0 || std::strcmp(type, "for_range_loop") == 0) {
    return "for";
  }
  if (std::strcmp(type, "do_statement") == 0) {
    return "do";
  }
  if (std::strcmp(type, "if_statement") == 0) {
    return "if";
  }
  if (std::strcmp(type, "switch_statement") == 0) {
    return "switch";
  }
  if (std::strcmp(type, "try_statement") == 0) {
    return "try";
  }
  if (std::strcmp(type, "catch_clause") == 0) {
    return "catch";
  }
  return {};
}

bool node_strictly_contains(TSNode outer, TSNode inner) {
  if (ts_node_is_null(outer) || ts_node_is_null(inner)) {
    return false;
  }
  if (ts_node_start_byte(outer) == ts_node_start_byte(inner) &&
      ts_node_end_byte(outer) == ts_node_end_byte(inner)) {
    return false;
  }
  return ts_node_start_byte(outer) <= ts_node_start_byte(inner) &&
         ts_node_end_byte(outer) >= ts_node_end_byte(inner);
}

int containment_depth(const std::vector<TSNode>& scopes, TSNode node) {
  int depth = 0;
  for (const TSNode& scope : scopes) {
    if (node_strictly_contains(scope, node)) {
      ++depth;
    }
  }
  return depth;
}

TSNode innermost_scope_containing(const std::vector<TSNode>& scopes, TSNode node) {
  TSNode best{};
  int best_depth = -1;
  for (const TSNode& scope : scopes) {
    if (!node_strictly_contains(scope, node) &&
        !(ts_node_start_byte(scope) == ts_node_start_byte(node) &&
          ts_node_end_byte(scope) == ts_node_end_byte(node))) {
      if (ts_node_start_byte(scope) <= ts_node_start_byte(node) &&
          ts_node_end_byte(scope) >= ts_node_end_byte(node)) {
        const int depth = containment_depth(scopes, scope);
        if (depth > best_depth) {
          best_depth = depth;
          best = scope;
        }
      }
    }
  }
  return best;
}

bool definition_visible_before_point(TSNode def_node, int line, int col) {
  const TSPoint start = ts_node_start_point(def_node);
  if (static_cast<int>(start.row) < line) {
    return true;
  }
  if (static_cast<int>(start.row) == line && static_cast<int>(start.column) <= col) {
    return true;
  }
  return false;
}

std::vector<TSNode> collect_scope_nodes(TSNode root) {
  std::vector<TSNode> scopes;
  TSQuery* query = locals_query();
  if (query == nullptr || ts_node_is_null(root)) {
    return scopes;
  }
  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, root);
  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const char* capture_name = nullptr;
      uint32_t capture_name_len = 0;
      capture_name =
          ts_query_capture_name_for_id(query, match.captures[i].index, &capture_name_len);
      if (capture_name == nullptr || std::strncmp(capture_name, "local.scope", 11) != 0) {
        continue;
      }
      scopes.push_back(match.captures[i].node);
    }
  }
  ts_query_cursor_delete(cursor);
  return scopes;
}

bool is_declaration_scope_node(TSNode node) {
  const char* type = ts_node_type(node);
  return type != nullptr && std::strcmp(type, "declaration") == 0;
}

TSNode innermost_scope_node_at(const std::vector<TSNode>& scopes, int line, int col) {
  TSNode best{};
  int best_span = INT_MAX;
  int best_depth = -1;
  for (const TSNode& scope : scopes) {
    if (!ts_node_contains_point(scope, line, col) || is_declaration_scope_node(scope)) {
      continue;
    }
    const TSPoint start = ts_node_start_point(scope);
    const TSPoint end = ts_node_end_point(scope);
    const int span = std::max(1, static_cast<int>(end.row) - static_cast<int>(start.row) + 1);
    const int depth = containment_depth(scopes, scope);
    if (span < best_span || (span == best_span && depth > best_depth)) {
      best_span = span;
      best_depth = depth;
      best = scope;
    }
  }
  return best;
}

void byte_offset_to_line_col(const std::string& source, uint32_t byte, int* line, int* col) {
  int row = 0;
  int column = 0;
  for (uint32_t i = 0; i < byte && i < source.size(); ++i) {
    if (source[i] == '\n') {
      ++row;
      column = 0;
    } else {
      ++column;
    }
  }
  if (line != nullptr) {
    *line = row;
  }
  if (col != nullptr) {
    *col = column;
  }
}

TSNode compound_body_of_scope(TSNode scope) {
  if (ts_node_is_null(scope)) {
    return {};
  }
  const char* type = ts_node_type(scope);
  if (type != nullptr && std::strcmp(type, "compound_statement") == 0) {
    return scope;
  }

  struct BodyField {
    const char* node_type;
    const char* field;
    uint32_t field_len;
  };
  static const BodyField kBodyFields[] = {
      {"function_definition", "body", 4},
      {"while_statement", "body", 4},
      {"for_statement", "body", 4},
      {"for_range_loop", "body", 4},
      {"do_statement", "body", 4},
      {"switch_statement", "body", 4},
      {"lambda_expression", "body", 4},
      {"catch_clause", "body", 4},
      {"if_statement", "consequence", 11},
      {"namespace_definition", "body", 4},
      {"try_statement", "body", 4},
  };
  if (type != nullptr) {
    for (const BodyField& mapping : kBodyFields) {
      if (std::strcmp(type, mapping.node_type) != 0) {
        continue;
      }
      TSNode child = ts_node_child_by_field_name(scope, mapping.field, mapping.field_len);
      if (!ts_node_is_null(child) && std::strcmp(ts_node_type(child), "compound_statement") == 0) {
        return child;
      }
      return {};
    }
  }

  TSNode best{};
  const auto visit = [&](const auto& self, TSNode node) -> void {
    if (ts_node_is_null(node)) {
      return;
    }
    const char* node_type = ts_node_type(node);
    if (node_type != nullptr && std::strcmp(node_type, "compound_statement") == 0) {
      if (ts_node_is_null(best)) {
        best = node;
      }
    }
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      self(self, ts_node_child(node, i));
    }
  };
  visit(visit, scope);
  return best;
}

BracketPairHighlight bracket_pair_for_compound(TSNode compound, const std::string& source) {
  BracketPairHighlight result;
  if (ts_node_is_null(compound) || std::strcmp(ts_node_type(compound), "compound_statement") != 0) {
    return result;
  }
  const uint32_t start_byte = ts_node_start_byte(compound);
  const uint32_t end_byte = ts_node_end_byte(compound);
  if (start_byte >= source.size() || end_byte > source.size() || end_byte <= start_byte + 1) {
    return result;
  }
  if (source[start_byte] != '{' || source[end_byte - 1] != '}') {
    return result;
  }
  result.valid = true;
  const TSPoint open = ts_node_start_point(compound);
  result.line_a = static_cast<int>(open.row);
  result.col_a = static_cast<int>(open.column);
  byte_offset_to_line_col(source, end_byte - 1, &result.line_b, &result.col_b);
  return result;
}

}  // namespace

std::vector<SymbolInfo> scope_symbols_from_tree(TSNode root, const std::string& source,
                                                const std::string& file_path) {
  std::vector<SymbolInfo> symbols;
  TSQuery* query = locals_query();
  if (query == nullptr || ts_node_is_null(root)) {
    return symbols;
  }

  std::vector<TSNode> scopes;
  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, root);

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const char* capture_name = nullptr;
      uint32_t capture_name_len = 0;
      capture_name =
          ts_query_capture_name_for_id(query, match.captures[i].index, &capture_name_len);
      if (capture_name == nullptr || std::strncmp(capture_name, "local.scope", 11) != 0) {
        continue;
      }
      scopes.push_back(match.captures[i].node);
    }
  }
  ts_query_cursor_delete(cursor);

  for (const TSNode& scope : scopes) {
    std::string raw_name = scope_name_for_node(scope, source);
    if (raw_name.empty()) {
      raw_name = "<scope>";
    }
    SymbolInfo info;
    info.kind = scope_kind_for_node(scope);
    info.name = kind_prefix(info.kind) + raw_name;
    info.line = static_cast<int>(ts_node_start_point(scope).row) + 1;
    info.end_line = static_cast<int>(ts_node_end_point(scope).row) + 1;
    info.depth = containment_depth(scopes, scope);
    info.file = file_path;
    symbols.push_back(std::move(info));
  }

  std::sort(symbols.begin(), symbols.end(), [](const SymbolInfo& a, const SymbolInfo& b) {
    if (a.line != b.line) {
      return a.line < b.line;
    }
    return a.depth < b.depth;
  });
  return symbols;
}

std::vector<const SymbolInfo*> scope_chain_at_point(const std::vector<SymbolInfo>& scope_symbols,
                                                    int line_0based) {
  return scope_chain_at_line(scope_symbols, line_0based);
}

ScopeLineRange innermost_scope_range_from_symbols(const std::vector<SymbolInfo>& scope_symbols,
                                                  int line_0based, int col_0based) {
  ScopeLineRange range;
  if (scope_symbols.empty()) {
    return range;
  }
  const int line_1 = line_0based + 1;
  const SymbolInfo* best = nullptr;
  int best_span = INT_MAX;
  int best_depth = -1;
  for (const SymbolInfo& sym : scope_symbols) {
    if (sym.kind == SymbolKind::kVariable) {
      continue;
    }
    const int end_1 = sym.end_line > 0 ? sym.end_line : sym.line;
    if (line_1 < sym.line || line_1 > end_1) {
      continue;
    }
    const int span = std::max(1, end_1 - sym.line + 1);
    if (span < best_span || (span == best_span && sym.depth > best_depth)) {
      best_span = span;
      best_depth = sym.depth;
      best = &sym;
    }
  }
  if (best == nullptr) {
    return range;
  }
  range.start_line = std::max(0, best->line - 1);
  const int end_line_1 = best->end_line > 0 ? best->end_line : best->line;
  range.end_line = std::max(range.start_line, end_line_1 - 1);
  range.valid = true;
  (void)col_0based;
  return range;
}

BracketPairHighlight scope_bracket_pair_from_tree(TSNode root, const std::string& source,
                                                  int line_0based, int col_0based) {
  const std::vector<TSNode> scopes = collect_scope_nodes(root);
  const TSNode scope = innermost_scope_node_at(scopes, line_0based, col_0based);
  if (ts_node_is_null(scope)) {
    return {};
  }
  return bracket_pair_for_compound(compound_body_of_scope(scope), source);
}

std::vector<FoldRegion> fold_regions_from_tree(TSNode root, const std::string& source) {
  std::vector<FoldRegion> regions;
  if (ts_node_is_null(root)) {
    return regions;
  }
  const std::vector<TSNode> scopes = collect_scope_nodes(root);
  regions.reserve(scopes.size());
  for (const TSNode& scope : scopes) {
    if (is_declaration_scope_node(scope)) {
      continue;
    }
    const BracketPairHighlight braces =
        bracket_pair_for_compound(compound_body_of_scope(scope), source);
    if (!braces.valid || braces.line_b <= braces.line_a) {
      continue;
    }
    regions.push_back({braces.line_a, braces.line_b});
  }
  std::sort(regions.begin(), regions.end(), [](const FoldRegion& a, const FoldRegion& b) {
    if (a.open_line != b.open_line) {
      return a.open_line < b.open_line;
    }
    return a.close_line < b.close_line;
  });
  regions.erase(std::unique(regions.begin(), regions.end(),
                          [](const FoldRegion& a, const FoldRegion& b) {
                            return a.open_line == b.open_line && a.close_line == b.close_line;
                          }),
                regions.end());
  return regions;
}

std::vector<std::string> visible_local_names_at(TSNode root, const std::string& source, int line,
                                                int col) {
  std::vector<std::string> names;
  TSQuery* query = locals_query();
  if (query == nullptr || ts_node_is_null(root)) {
    return names;
  }

  std::vector<TSNode> scopes;
  std::vector<TSNode> definitions;
  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, query, root);

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const char* capture_name = nullptr;
      uint32_t capture_name_len = 0;
      capture_name =
          ts_query_capture_name_for_id(query, match.captures[i].index, &capture_name_len);
      if (capture_name == nullptr) {
        continue;
      }
      if (std::strncmp(capture_name, "local.scope", 11) == 0) {
        scopes.push_back(match.captures[i].node);
      } else if (std::strncmp(capture_name, "local.definition", 16) == 0) {
        definitions.push_back(match.captures[i].node);
      }
    }
  }
  ts_query_cursor_delete(cursor);

  std::unordered_map<std::string, bool> seen;
  for (const TSNode& def_node : definitions) {
    if (!definition_visible_before_point(def_node, line, col)) {
      continue;
    }
    const TSNode scope = innermost_scope_containing(scopes, def_node);
    if (!ts_node_is_null(scope) && !ts_node_contains_point(scope, line, col)) {
      continue;
    }
    if (ts_node_is_null(scope)) {
      if (!ts_node_contains_point(root, line, col)) {
        continue;
      }
    }
    const std::string name = ts_identifier_name(def_node, source);
    if (name.empty() || seen.count(name) > 0) {
      continue;
    }
    seen[name] = true;
    names.push_back(name);
  }

  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace tgdb
