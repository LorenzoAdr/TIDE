#include "ai/l2_explore_a.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "parser/tree_sitter_ast_utils.hpp"
#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_symbols.hpp"
#include "symbols/symbol_utils.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tuide {
namespace {

namespace fs = std::filesystem;

std::string read_abs_file(const std::string& abs) {
  std::ifstream in(abs);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<std::string> split_lines(const std::string& source) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : source) {
    if (c == '\n') {
      lines.push_back(cur);
      cur.clear();
    } else if (c != '\r') {
      cur.push_back(c);
    }
  }
  if (!cur.empty() || (!source.empty() && source.back() == '\n')) {
    lines.push_back(cur);
  }
  return lines;
}

TSTree* parse_sync(const std::string& source, const std::string& path) {
  if (source.empty()) {
    return nullptr;
  }
  const TSLanguage* language = tree_sitter_language_for_path(path);
  if (language == nullptr) {
    language = tree_sitter_cpp_language();
  }
  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, language);
  TSTree* tree =
      ts_parser_parse_string(parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
  ts_parser_delete(parser);
  return tree;
}

bool is_control_type(const char* type) {
  if (type == nullptr) {
    return false;
  }
  return std::strcmp(type, "if_statement") == 0 || std::strcmp(type, "switch_statement") == 0 ||
         std::strcmp(type, "for_statement") == 0 || std::strcmp(type, "for_range_loop") == 0 ||
         std::strcmp(type, "while_statement") == 0 || std::strcmp(type, "do_statement") == 0 ||
         std::strcmp(type, "try_statement") == 0;
}

const char* control_kind_name(const char* type) {
  if (type == nullptr) {
    return "";
  }
  if (std::strcmp(type, "if_statement") == 0) {
    return "if";
  }
  if (std::strcmp(type, "switch_statement") == 0) {
    return "switch";
  }
  if (std::strcmp(type, "for_statement") == 0 || std::strcmp(type, "for_range_loop") == 0) {
    return "for";
  }
  if (std::strcmp(type, "while_statement") == 0) {
    return "while";
  }
  if (std::strcmp(type, "do_statement") == 0) {
    return "do";
  }
  if (std::strcmp(type, "try_statement") == 0) {
    return "try";
  }
  return "";
}

bool is_fn_type(const char* type) {
  return type != nullptr && (std::strcmp(type, "function_definition") == 0 ||
                             std::strcmp(type, "lambda_expression") == 0);
}

bool is_type_type(const char* type) {
  return type != nullptr && (std::strcmp(type, "class_specifier") == 0 ||
                             std::strcmp(type, "struct_specifier") == 0);
}

bool is_ns_type(const char* type) {
  return type != nullptr && std::strcmp(type, "namespace_definition") == 0;
}

std::string node_name_guess(TSNode node, const std::string& source) {
  if (ts_node_is_null(node)) {
    return {};
  }
  // Prefer named child identifier / type_identifier
  const uint32_t n = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < n; ++i) {
    TSNode ch = ts_node_named_child(node, i);
    const char* t = ts_node_type(ch);
    if (t == nullptr) {
      continue;
    }
    if (std::strcmp(t, "identifier") == 0 || std::strcmp(t, "type_identifier") == 0 ||
        std::strcmp(t, "namespace_identifier") == 0 ||
        std::strcmp(t, "field_identifier") == 0) {
      const uint32_t a = ts_node_start_byte(ch);
      const uint32_t b = ts_node_end_byte(ch);
      if (b > a && b <= source.size()) {
        return source.substr(a, b - a);
      }
    }
    if (std::strcmp(t, "function_declarator") == 0 || std::strcmp(t, "qualified_identifier") == 0 ||
        std::strcmp(t, "declarator") == 0) {
      const std::string nested = node_name_guess(ch, source);
      if (!nested.empty()) {
        return nested;
      }
    }
  }
  return {};
}

int kind_priority(SymbolKind k) {
  switch (k) {
    case SymbolKind::kClass:
    case SymbolKind::kStruct:
      return 40;
    case SymbolKind::kFunction:
    case SymbolKind::kMethod:
      return 30;
    case SymbolKind::kNamespace:
      return 10;
    default:
      return 0;
  }
}

const SymbolInfo* innermost_fn(const std::vector<SymbolInfo>& syms, int line_1based) {
  const SymbolInfo* best = nullptr;
  int best_span = 1'000'000;
  for (const auto& sym : syms) {
    if (kind_priority(sym.kind) < 30 || sym.line <= 0) {
      continue;
    }
    const int end = sym.end_line > 0 ? sym.end_line : sym.line;
    if (line_1based < sym.line || line_1based > end) {
      continue;
    }
    const int span = end - sym.line;
    if (span < best_span) {
      best_span = span;
      best = &sym;
    }
  }
  return best;
}

std::string scope_chain_from_symbols(const std::vector<SymbolInfo>& syms, int line_1based) {
  std::vector<std::string> parts;
  for (const auto& sym : syms) {
    if (sym.line <= 0) {
      continue;
    }
    const int end = sym.end_line > 0 ? sym.end_line : (sym.line + 5000);
    if (line_1based < sym.line || line_1based > end) {
      continue;
    }
    if (kind_priority(sym.kind) < 10) {
      continue;
    }
    const std::string bare = symbol_insert_name(sym.name);
    if (!bare.empty()) {
      parts.push_back(bare);
    }
  }
  // Prefer outer→inner by increasing line (defs nest)
  std::stable_sort(parts.begin(), parts.end());  // weak; rebuild from depth
  // Better: sort by span size descending for outer first
  struct Item {
    std::string name;
    int span = 0;
    int line = 0;
  };
  std::vector<Item> items;
  for (const auto& sym : syms) {
    if (sym.line <= 0 || kind_priority(sym.kind) < 10) {
      continue;
    }
    const int end = sym.end_line > 0 ? sym.end_line : (sym.line + 5000);
    if (line_1based < sym.line || line_1based > end) {
      continue;
    }
    Item it;
    it.name = symbol_insert_name(sym.name);
    if (it.name.empty()) {
      it.name = sym.name;
    }
    it.span = end - sym.line;
    it.line = sym.line;
    items.push_back(std::move(it));
  }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    if (a.span != b.span) {
      return a.span > b.span;  // outer first
    }
    return a.line < b.line;
  });
  std::ostringstream out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i) {
      out << " → ";
    }
    out << items[i].name;
  }
  return out.str();
}

std::string slice_lines(const std::vector<std::string>& lines, int start_1, int end_1) {
  if (lines.empty()) {
    return {};
  }
  const int n = static_cast<int>(lines.size());
  start_1 = std::max(1, start_1);
  end_1 = std::min(n, end_1);
  if (start_1 > end_1) {
    return {};
  }
  std::ostringstream out;
  for (int i = start_1; i <= end_1; ++i) {
    out << lines[static_cast<std::size_t>(i - 1)] << '\n';
  }
  return out.str();
}

bool before_is_bare_call_prefix(const std::string& before) {
  for (char c : before) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

bool is_identifier_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool preview_has_symbol_call(const std::string& preview, const std::string& symbol) {
  if (symbol.empty()) {
    return false;
  }
  const auto check = [&](const std::string& needle) {
    std::size_t pos = 0;
    while ((pos = preview.find(needle, pos)) != std::string::npos) {
      if (pos == 0 || !is_identifier_char(preview[pos - 1])) {
        return true;
      }
      pos += needle.size();
    }
    return false;
  };
  return check(symbol + "(") || check(symbol + " (");
}

bool looks_like_definition_line(const std::string& line, const std::string& symbol) {
  if (symbol.empty() || line.find(symbol) == std::string::npos) {
    return false;
  }
  std::string t = line;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) {
    t.erase(t.begin());
  }
  if (t.rfind("//", 0) == 0 || t.rfind("/*", 0) == 0) {
    return false;
  }
  const auto paren = t.find(symbol + "(");
  if (paren == std::string::npos) {
    return false;
  }
  const std::string before = t.substr(0, paren);
  // Out-of-line member definition: Qualifier::symbol( … { )
  if (before.find("::" + symbol) != std::string::npos) {
    return true;
  }
  // Typed declaration ending with ; (not a bare call statement)
  if (t.find('{') == std::string::npos && t.find(';') != std::string::npos &&
      !before_is_bare_call_prefix(before)) {
    return true;
  }
  // Typed definition with body on same line: void Foo::bar( … {
  if (t.find('{') != std::string::npos) {
    static const char* kTypeish[] = {"void ",   "static ",  "inline ", "virtual ", "explicit ",
                                     "constexpr ", "const ", "unsigned ", "signed ", "auto ",
                                     "struct ", "class ",  "friend "};
    for (const char* kw : kTypeish) {
      if (before.find(kw) != std::string::npos) {
        return true;
      }
    }
    if (before.find('*') != std::string::npos || before.find('&') != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string rel_from_workspace(const std::string& workspace_root, const std::string& path) {
  if (workspace_root.empty() || path.empty()) {
    return path;
  }
  std::string p = path;
  if (p.size() > workspace_root.size() && p.compare(0, workspace_root.size(), workspace_root) == 0 &&
      (p[workspace_root.size()] == '/' || p[workspace_root.size()] == '\\')) {
    return p.substr(workspace_root.size() + 1);
  }
  return path;
}

bool is_srcish(const std::string& rel) {
  return rel.rfind("src/", 0) == 0 || rel.rfind("include/", 0) == 0 || rel.rfind("lib/", 0) == 0;
}

bool is_trail_product_path(const std::string& rel) {
  if (!is_srcish(rel)) {
    return false;
  }
  if (rel.rfind("tests/", 0) == 0 || rel.rfind("tools/", 0) == 0 ||
      rel.rfind("docs/", 0) == 0 || rel.find("third_party/") != std::string::npos) {
    return false;
  }
  // L2 explore/harness sources mention symptom symbols as strings — not product call paths.
  if (rel.find("/ai/l2_") != std::string::npos || rel.find("/ai/level2_") != std::string::npos) {
    return false;
  }
  return true;
}

std::string compress_ws(std::string s, std::size_t max_len) {
  std::string out;
  out.reserve(s.size());
  bool sp = false;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t') {
      c = ' ';
    }
    if (c == ' ') {
      if (sp || out.empty()) {
        continue;
      }
      sp = true;
      out.push_back(' ');
      continue;
    }
    sp = false;
    out.push_back(c);
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  if (out.size() > max_len) {
    out = out.substr(0, max_len - 1) + "…";
  }
  return out;
}

std::string node_source_slice(TSNode node, const std::string& source, std::size_t max_len) {
  if (ts_node_is_null(node)) {
    return {};
  }
  const uint32_t a = ts_node_start_byte(node);
  const uint32_t b = ts_node_end_byte(node);
  if (b <= a || b > source.size()) {
    return {};
  }
  return compress_ws(source.substr(a, b - a), max_len);
}

// Control header line, e.g. "if (state == Active)" (not just @{if}).
std::string control_condition_text(TSNode ctrl, const std::string& source,
                                   const std::vector<std::string>& lines) {
  if (ts_node_is_null(ctrl)) {
    return {};
  }
  const int start_1 = static_cast<int>(ts_node_start_point(ctrl).row) + 1;
  if (start_1 >= 1 && start_1 <= static_cast<int>(lines.size())) {
    std::string line = lines[static_cast<std::size_t>(start_1 - 1)];
    while (!line.empty() && (line.back() == '{' || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    line = compress_ws(line, 110);
    if (!line.empty()) {
      return line;
    }
  }
  // Fallback: condition field only
  TSNode cond = ts_node_child_by_field_name(ctrl, "condition", 9);
  if (ts_node_is_null(cond)) {
    cond = ts_node_child_by_field_name(ctrl, "right", 5);
  }
  if (ts_node_is_null(cond)) {
    cond = ts_node_child_by_field_name(ctrl, "initializer", 11);
  }
  std::string body = node_source_slice(cond, source, 96);
  if (body.empty()) {
    return {};
  }
  const char* kind = control_kind_name(ts_node_type(ctrl));
  if (kind != nullptr && *kind != '\0') {
    if (!body.empty() && body.front() == '(') {
      return std::string(kind) + " " + body;
    }
    return std::string(kind) + " (" + body + ")";
  }
  return body;
}

// A qualified method name from a signature line or declarator text.
std::string rich_qualified_name(const std::string& sig_or_text, const std::string& bare) {
  if (sig_or_text.empty() || bare.empty()) {
    return {};
  }
  const std::string needle = "::" + bare;
  const auto pos = sig_or_text.rfind(needle);
  if (pos == std::string::npos) {
    return {};
  }
  // Walk left over Class / ns::Class identifiers
  std::size_t start = pos;
  while (start > 0) {
    const char prev = sig_or_text[start - 1];
    if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_' || prev == ':') {
      --start;
      continue;
    }
    break;
  }
  // Avoid leading ':'
  while (start < pos && sig_or_text[start] == ':') {
    ++start;
  }
  std::string q = sig_or_text.substr(start, pos + needle.size() - start);
  if (q.find("::") == std::string::npos) {
    return {};
  }
  return q;
}

std::string function_signature_line(TSNode fn_node, const std::string& source,
                                    const std::vector<std::string>& lines) {
  if (ts_node_is_null(fn_node)) {
    return {};
  }
  const int start_1 = static_cast<int>(ts_node_start_point(fn_node).row) + 1;
  std::string line;
  if (start_1 >= 1 && start_1 <= static_cast<int>(lines.size())) {
    line = lines[static_cast<std::size_t>(start_1 - 1)];
    while (!line.empty() && (line.back() == '{' || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    line = compress_ws(line, 120);
  }
  if (!line.empty()) {
    return line;
  }
  TSNode declarator = ts_node_child_by_field_name(fn_node, "declarator", 10);
  const std::string decl = node_source_slice(declarator, source, 80);
  if (!decl.empty()) {
    return decl;
  }
  return {};
}

}  // namespace

ATrailHop a_trail_enrich_hop(const std::string& abs_path, const std::string& rel_path,
                             int call_line, const std::string& called_symbol) {
  ATrailHop hop;
  hop.path = rel_path.empty() ? abs_path : rel_path;
  hop.call_line = call_line;
  const std::string source = read_abs_file(abs_path);
  if (source.empty() || call_line <= 0) {
    hop.snippet = "(no se pudo leer call site)\n";
    hop.anchor = hop.path + ":" + std::to_string(std::max(1, call_line));
    return hop;
  }
  const auto lines = split_lines(source);
  const std::string& line_txt =
      (call_line >= 1 && call_line <= static_cast<int>(lines.size()))
          ? lines[static_cast<std::size_t>(call_line - 1)]
          : std::string{};

  auto trim_l = [](std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
      s.erase(s.begin());
    }
    return s;
  };
  const std::string trimmed = trim_l(line_txt);
  const bool commentish =
      trimmed.rfind("//", 0) == 0 || trimmed.rfind("/*", 0) == 0 || trimmed.rfind('*') == 0;
  const bool callish_text =
      !called_symbol.empty() && preview_has_symbol_call(line_txt, called_symbol);
  const bool defish = looks_like_definition_line(line_txt, called_symbol);

  TSTree* tree = parse_sync(source, abs_path);
  std::vector<SymbolInfo> syms;
  TSNode root{};
  if (tree != nullptr) {
    root = ts_tree_root_node(tree);
    syms = extract_symbols_from_tree(root, source, hop.path);
  }

  std::string outline_scope;
  const SymbolInfo* fn = innermost_fn(syms, call_line);
  if (fn != nullptr) {
    hop.symbol = symbol_insert_name(fn->name);
    if (hop.symbol.empty()) {
      hop.symbol = fn->name;
    }
    if (fn->name.find("::") != std::string::npos) {
      outline_scope = fn->name;
      if (outline_scope.size() > 2 && outline_scope[1] == ' ' &&
          std::isalpha(static_cast<unsigned char>(outline_scope[0]))) {
        outline_scope = outline_scope.substr(2);
      }
    }
  }
  // Soft outline chain (may lack Class for out-of-line defs)
  const std::string outline_chain = scope_chain_from_symbols(syms, call_line);

  int ctrl_start = 0;
  int ctrl_end = 0;
  int fn_snip_start = 0;
  int fn_snip_end = 0;
  bool saw_call_expr = false;
  std::vector<std::string> controls_inner_to_outer;
  std::vector<std::string> conds_inner_to_outer;
  std::vector<std::string> struct_outer;
  TSNode fn_ast{};

  if (tree != nullptr && !ts_node_is_null(root)) {
    const int row = call_line - 1;
    int col = 0;
    if (!called_symbol.empty()) {
      std::size_t pos = 0;
      while (pos < line_txt.size()) {
        const auto p = line_txt.find(called_symbol + "(", pos);
        if (p == std::string::npos) {
          break;
        }
        if (p == 0 || !is_identifier_char(line_txt[p - 1])) {
          col = static_cast<int>(p);
          break;
        }
        pos = p + called_symbol.size();
      }
    }
    TSPoint p0{static_cast<uint32_t>(row), static_cast<uint32_t>(std::max(0, col))};
    TSPoint p1{static_cast<uint32_t>(row), static_cast<uint32_t>(std::max(0, col) + 1)};
    TSNode leaf = ts_node_descendant_for_point_range(root, p0, p1);
    TSNode n = leaf;
    while (!ts_node_is_null(n)) {
      const char* t = ts_node_type(n);
      if (t != nullptr &&
          (std::strcmp(t, "call_expression") == 0 || std::strcmp(t, "new_expression") == 0)) {
        TSNode fn_node = ts_node_child_by_field_name(n, "function", 8);
        std::string callee;
        if (!ts_node_is_null(fn_node)) {
          const uint32_t a = ts_node_start_byte(fn_node);
          const uint32_t b = ts_node_end_byte(fn_node);
          if (b > a && b <= source.size()) {
            callee = source.substr(a, b - a);
          }
        }
        const auto colons = callee.rfind("::");
        if (colons != std::string::npos) {
          callee = callee.substr(colons + 2);
        }
        const auto lt = callee.find('<');
        if (lt != std::string::npos) {
          callee = callee.substr(0, lt);
        }
        if (called_symbol.empty() || callee == called_symbol) {
          saw_call_expr = true;
        }
      }
      if (is_control_type(t)) {
        const std::string ck = control_kind_name(t);
        if (!ck.empty()) {
          controls_inner_to_outer.push_back(ck);
          const std::string cond = control_condition_text(n, source, lines);
          if (!cond.empty()) {
            conds_inner_to_outer.push_back(cond);
          }
        }
        if (ctrl_start == 0) {
          ctrl_start = static_cast<int>(ts_node_start_point(n).row) + 1;
          ctrl_end = static_cast<int>(ts_node_end_point(n).row) + 1;
        }
      }
      if (is_fn_type(t)) {
        fn_ast = n;
        TSNode declarator = ts_node_child_by_field_name(n, "declarator", 10);
        const std::string decl_bare = ts_declarator_name(declarator, source);
        const std::string decl_full = node_source_slice(declarator, source, 100);
        if (!decl_bare.empty()) {
          hop.symbol = decl_bare;
        } else if (hop.symbol.empty()) {
          hop.symbol = node_name_guess(n, source);
        }
        hop.signature = function_signature_line(n, source, lines);
        // Class::method — declarator text or signature line (ts_declarator_name is bare-only)
        outline_scope = rich_qualified_name(decl_full, hop.symbol);
        if (outline_scope.empty()) {
          outline_scope = rich_qualified_name(hop.signature, hop.symbol);
        }
        fn_snip_start = static_cast<int>(ts_node_start_point(n).row) + 1;
        fn_snip_end = static_cast<int>(ts_node_end_point(n).row) + 1;
        n = ts_node_parent(n);
        break;
      }
      if (is_type_type(t) || is_ns_type(t)) {
        const std::string nm = node_name_guess(n, source);
        if (!nm.empty()) {
          struct_outer.push_back(nm);
        }
      }
      n = ts_node_parent(n);
    }
    while (!ts_node_is_null(n)) {
      const char* t = ts_node_type(n);
      if (is_type_type(t) || is_ns_type(t)) {
        const std::string nm = node_name_guess(n, source);
        if (!nm.empty()) {
          struct_outer.push_back(nm);
        }
      }
      n = ts_node_parent(n);
    }
  }
  if (tree != nullptr) {
    ts_tree_delete(tree);
  }
  (void)fn_ast;

  if (!controls_inner_to_outer.empty()) {
    hop.control_kind = controls_inner_to_outer.front();
    std::ostringstream cc;
    for (std::size_t i = 0; i < controls_inner_to_outer.size(); ++i) {
      if (i) {
        cc << " → ";
      }
      cc << controls_inner_to_outer[i];
    }
    hop.control_chain = cc.str();
  }
  if (!conds_inner_to_outer.empty()) {
    std::ostringstream cd;
    // Innermost first; keep ≤3 conditions
    const std::size_t ncond = std::min<std::size_t>(3, conds_inner_to_outer.size());
    for (std::size_t i = 0; i < ncond; ++i) {
      if (i) {
        cd << " · ";
      }
      cd << conds_inner_to_outer[i];
    }
    hop.control_cond = cd.str();
  }

  // Rich scope: prefer Class::method, else ns → type → fn
  if (!outline_scope.empty()) {
    hop.scope_chain = outline_scope;
  } else if (!struct_outer.empty() || !hop.symbol.empty()) {
    std::ostringstream sc;
    for (auto it = struct_outer.rbegin(); it != struct_outer.rend(); ++it) {
      if (sc.tellp() > 0) {
        sc << " → ";
      }
      sc << *it;
    }
    if (!hop.symbol.empty()) {
      if (sc.tellp() > 0) {
        sc << " → ";
      }
      sc << hop.symbol;
    }
    hop.scope_chain = sc.str();
  } else if (!outline_chain.empty()) {
    hop.scope_chain = outline_chain;
  }

  hop.is_call_site = saw_call_expr || (callish_text && !commentish && !defish);

  const int pad = kATrailCallSitePad;
  int snip_a = std::max(1, call_line - pad);
  int snip_b = std::min(static_cast<int>(lines.size()), call_line + pad);
  if (ctrl_start > 0) {
    if (ctrl_end - ctrl_start <= 28) {
      snip_a = ctrl_start;
      snip_b = std::min(static_cast<int>(lines.size()), ctrl_end);
    } else {
      snip_a = std::max(ctrl_start, call_line - 8);
      snip_b = std::min(ctrl_end, call_line + 12);
    }
  } else if (fn_snip_start > 0 && fn_snip_end - fn_snip_start <= 40) {
    snip_a = fn_snip_start;
    snip_b = std::min(static_cast<int>(lines.size()), fn_snip_end);
  }
  hop.snippet = slice_lines(lines, snip_a, snip_b);
  if (static_cast<int>(hop.snippet.size()) > kATrailMaxHopSnippetChars) {
    hop.snippet = hop.snippet.substr(0, static_cast<std::size_t>(kATrailMaxHopSnippetChars)) +
                  "\n…[hop truncado]…\n";
  }

  if (hop.symbol.empty()) {
    hop.symbol = called_symbol;
  }
  // Anchor prefers rich Class::method when known
  {
    std::string sym_anchor = hop.symbol;
    if (!outline_scope.empty()) {
      sym_anchor = outline_scope;
    }
    hop.anchor = hop.path + ":" + (sym_anchor.empty() ? std::to_string(call_line) : sym_anchor);
  }
  {
    std::ostringstream sum;
    if (!hop.signature.empty()) {
      sum << hop.signature;
    } else {
      sum << hop.symbol;
    }
    if (!hop.control_cond.empty()) {
      sum << " | " << hop.control_cond;
    } else if (!hop.control_chain.empty()) {
      sum << " @" << hop.control_chain;
    }
    if (!called_symbol.empty()) {
      sum << " → " << called_symbol;
    }
    hop.summary = sum.str();
  }
  return hop;
}

std::string path_family_rel(const std::string& rel) {
  std::string p = rel;
  while (!p.empty() && (p[0] == '.' || p[0] == '/')) {
    p.erase(p.begin());
  }
  const auto slash = p.find('/');
  if (slash == std::string::npos) {
    return p.empty() ? "other" : p;
  }
  std::string top = p.substr(0, slash);
  std::string rest = p.substr(slash + 1);
  if ((top == "src" || top == "include" || top == "lib") && !rest.empty()) {
    const auto slash2 = rest.find('/');
    return slash2 == std::string::npos ? rest : rest.substr(0, slash2);
  }
  return top;
}

std::string stem_from_rel(const std::string& rel) {
  std::string base = rel;
  const auto slash = base.find_last_of("/\\");
  if (slash != std::string::npos) {
    base = base.substr(slash + 1);
  }
  const auto dot = base.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return base;
}

std::string hop_identity_key(const ATrailHop& h) {
  if (!h.anchor.empty()) {
    return h.anchor;
  }
  return h.path + ":" + std::to_string(h.call_line) + ":" + h.symbol;
}

int score_climb_parent(const ATrailHop& parent, const ATrailHop& top,
                       const std::string& focus_symbol, const std::string& focus_path_hint) {
  int score = 50;
  if (parent.is_call_site) {
    score += 80;
  }
  if (parent.symbol != top.symbol) {
    score += 100;
  }
  // Upward climb: prefer coherent frames in the same module as the callee.
  if (parent.path == top.path) {
    score += 35;
  }
  if (stem_from_rel(parent.path) == stem_from_rel(top.path)) {
    score += 40;
  }
  if (path_family_rel(parent.path) == path_family_rel(top.path)) {
    score += 25;
  }
  if (!parent.control_chain.empty() || !parent.control_cond.empty()) {
    score += 15;
  }
  if (!focus_path_hint.empty() && parent.path == focus_path_hint) {
    score -= 30;
  }
  if (!focus_symbol.empty() && parent.symbol == focus_symbol) {
    score -= 80;
  }
  if (parent.path == top.path && parent.call_line == top.call_line) {
    score -= 100;
  }
  return score;
}

void dedupe_trail_stack_hops(std::vector<ATrailHop>& hops) {
  if (hops.size() < 2) {
    return;
  }
  std::vector<ATrailHop> clean;
  clean.reserve(hops.size());
  std::unordered_set<std::string> seen_syms;
  std::unordered_set<std::string> seen_keys;
  for (auto& h : hops) {
    const std::string key = hop_identity_key(h);
    if (seen_keys.count(key)) {
      continue;
    }
    if (!h.symbol.empty() && seen_syms.count(h.symbol)) {
      continue;
    }
    seen_keys.insert(key);
    if (!h.symbol.empty()) {
      seen_syms.insert(h.symbol);
    }
    clean.push_back(std::move(h));
  }
  hops = std::move(clean);
}

bool preview_looks_like_call(const std::string& preview, const std::string& symbol) {
  if (symbol.empty() || preview.find(symbol) == std::string::npos) {
    return false;
  }
  std::string t = preview;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) {
    t.erase(t.begin());
  }
  if (t.rfind("//", 0) == 0 || t.rfind("/*", 0) == 0 || (!t.empty() && t[0] == '*')) {
    return false;
  }
  if (!preview_has_symbol_call(preview, symbol)) {
    return false;
  }
  if (looks_like_definition_line(preview, symbol)) {
    return false;
  }
  return true;
}

std::vector<ATrailStack> a_trail_build_stacks(const std::string& workspace_root,
                                              const std::string& focus_symbol,
                                              const std::string& focus_path_hint,
                                              const std::vector<ATrailSearchHit>& search_hits,
                                              int max_stacks, int max_depth) {
  (void)max_depth;
  std::vector<ATrailStack> out;
  if (focus_symbol.empty() || max_stacks <= 0) {
    return out;
  }
  const std::string focus_stem = stem_from_rel(focus_path_hint);
  const std::string focus_fam = path_family_rel(focus_path_hint);

  struct Ranked {
    ATrailSearchHit hit;
    ATrailHop hop;
    int score = 0;
    std::string fam;
    std::string stem;
  };
  std::vector<Ranked> ranked;
  for (const auto& h : search_hits) {
    if (h.line <= 0 || h.path.empty()) {
      continue;
    }
    std::string rel = h.path;
    if (!workspace_root.empty() && fs::path(rel).is_absolute()) {
      rel = rel_from_workspace(workspace_root, rel);
    }
    if (rel.rfind("tests/", 0) == 0 || rel.rfind("tools/", 0) == 0 ||
        rel.rfind("docs/", 0) == 0 || rel.find("third_party/") != std::string::npos ||
        !is_trail_product_path(rel)) {
      continue;
    }
    // Prefer call-ish previews; keep weak hits at low score only if nothing else
    const bool callish = preview_looks_like_call(h.preview, focus_symbol);

    fs::path abs = rel;
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / rel;
    }
    ATrailHop hop =
        a_trail_enrich_hop(abs.lexically_normal().string(), rel, h.line, focus_symbol);

    // Require real call site (TS callee match) — drop comment/string/false hits
    if (!hop.is_call_site) {
      continue;
    }

    Ranked r;
    r.hit = h;
    r.hit.path = rel;
    r.hop = std::move(hop);
    r.fam = path_family_rel(rel);
    r.stem = stem_from_rel(rel);
    r.score = 0;
    if (is_srcish(rel)) {
      r.score += 40;
    }
    if (r.hop.is_call_site) {
      r.score += 80;
    } else if (callish) {
      r.score += 30;
    }
    if (!r.hop.control_chain.empty()) {
      r.score += 15;  // nested control = strong signal
    }
    // Prefer other modules / stems (the bridge we want)
    if (!focus_stem.empty() && r.stem != focus_stem) {
      r.score += 50;
    }
    if (!focus_fam.empty() && r.fam != focus_fam) {
      r.score += 35;
    }
    // Same file as focus definition → often self / nearby noise
    if (!focus_path_hint.empty() && rel.find(focus_path_hint) != std::string::npos) {
      r.score -= 40;
    }
    // Enclosing symbol == focus → self-hit / body, not a caller
    if (!focus_symbol.empty() && r.hop.symbol == focus_symbol) {
      r.score -= 60;
    }
    ranked.push_back(std::move(r));
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const Ranked& a, const Ranked& b) { return a.score > b.score; });

  // Greedy pick by score with soft diversity (fam/stem).
  std::unordered_set<std::string> seen_keys;
  std::unordered_set<std::string> seen_stems;
  std::unordered_set<std::string> seen_fams;
  std::unordered_set<std::string> consumed;  // path:line already emitted
  int sid = 0;
  auto key_of = [](const Ranked& r) {
    return r.hit.path + ":" + std::to_string(r.hit.line);
  };
  auto try_emit = [&](Ranked& r, bool require_new_fam) -> bool {
    if (static_cast<int>(out.size()) >= max_stacks) {
      return false;
    }
    const std::string key = key_of(r);
    if (consumed.count(key) || !seen_keys.insert(key).second) {
      return false;
    }
    if (seen_stems.count(r.stem)) {
      return false;
    }
    if (require_new_fam && seen_fams.count(r.fam) && !seen_fams.empty()) {
      return false;
    }
    consumed.insert(key);
    seen_stems.insert(r.stem);
    seen_fams.insert(r.fam);

    ATrailHop focus;
    focus.symbol = focus_symbol;
    focus.path = focus_path_hint;
    focus.anchor =
        focus_path_hint.empty() ? focus_symbol : (focus_path_hint + ":" + focus_symbol);
    focus.summary = "L0 focus";
    focus.snippet = "(focus)\n";
    focus.is_call_site = true;

    ATrailStack stack;
    ++sid;
    stack.id = "S" + std::to_string(sid);
    stack.hops.push_back(std::move(r.hop));
    stack.hops.push_back(std::move(focus));
    out.push_back(std::move(stack));
    return true;
  };

  for (auto& r : ranked) {
    if (static_cast<int>(out.size()) >= max_stacks) {
      break;
    }
    try_emit(r, /*require_new_fam=*/true);
  }
  for (auto& r : ranked) {
    if (static_cast<int>(out.size()) >= max_stacks) {
      break;
    }
    if (consumed.count(key_of(r))) {
      continue;
    }
    try_emit(r, /*require_new_fam=*/false);
  }
  return out;
}

std::vector<ATrailStack> a_trail_build_full_stacks(
    const std::string& workspace_root, const std::string& focus_symbol,
    const std::string& focus_path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    int max_stacks, int max_depth) {
  std::vector<ATrailStack> out;
  if (!search || focus_symbol.empty() || max_stacks <= 0) {
    return out;
  }
  max_depth = std::max(1, std::min(max_depth, kATrailMaxDepth));
  const auto direct = search(focus_symbol);
  // Seed with depth-1 stacks then climb each
  out = a_trail_build_stacks(workspace_root, focus_symbol, focus_path_hint, direct, max_stacks,
                             1);

  for (auto& stack : out) {
    if (stack.hops.size() < 2) {
      continue;
    }
    // hops: [leaf_caller, L0]. Climb above leaf_caller with real distinct parents.
    std::unordered_set<std::string> seen_syms;
    std::unordered_set<std::string> seen_keys;
    seen_syms.insert(focus_symbol);
    for (const auto& h : stack.hops) {
      seen_keys.insert(hop_identity_key(h));
      if (!h.symbol.empty()) {
        seen_syms.insert(h.symbol);
      }
    }
    for (int d = 1; d < max_depth; ++d) {
      const ATrailHop& top = stack.hops.front();
      if (top.symbol.empty()) {
        break;
      }

      const auto parents = search(top.symbol);
      ATrailHop best_parent;
      int best_score = -1;
      for (const auto& h : parents) {
        if (h.line <= 0 || h.path.empty()) {
          continue;
        }
        std::string rel = h.path;
        if (!workspace_root.empty() && fs::path(rel).is_absolute()) {
          rel = rel_from_workspace(workspace_root, rel);
        }
        if (rel.rfind("tests/", 0) == 0 || rel.rfind("tools/", 0) == 0 ||
            rel.rfind("docs/", 0) == 0 || rel.find("third_party/") != std::string::npos ||
            !is_trail_product_path(rel)) {
          continue;
        }
        if (rel == top.path && h.line == top.call_line) {
          continue;
        }
        if (!preview_looks_like_call(h.preview, top.symbol)) {
          continue;
        }
        fs::path abs = rel;
        if (!abs.is_absolute()) {
          abs = fs::path(workspace_root) / rel;
        }
        ATrailHop parent =
            a_trail_enrich_hop(abs.lexically_normal().string(), rel, h.line, top.symbol);
        if (!parent.is_call_site || parent.symbol.empty() || parent.symbol == top.symbol) {
          continue;
        }
        if (seen_syms.count(parent.symbol)) {
          continue;
        }
        const std::string parent_key = hop_identity_key(parent);
        if (seen_keys.count(parent_key)) {
          continue;
        }
        const int score =
            score_climb_parent(parent, top, focus_symbol, focus_path_hint);
        if (score > best_score) {
          best_score = score;
          best_parent = std::move(parent);
        }
      }
      if (best_score < 0) {
        break;
      }
      seen_syms.insert(best_parent.symbol);
      seen_keys.insert(hop_identity_key(best_parent));
      stack.hops.insert(stack.hops.begin(), std::move(best_parent));
    }
    dedupe_trail_stack_hops(stack.hops);
  }
  return out;
}

std::string lower_ascii(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool text_has_any(const std::string& text, const std::vector<std::string>& needles) {
  const std::string low = lower_ascii(text);
  for (const auto& n : needles) {
    if (n.empty()) {
      continue;
    }
    if (low.find(lower_ascii(n)) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool seeds_match_token(const std::vector<std::string>& seeds, const std::string& token) {
  if (token.empty()) {
    return false;
  }
  const std::string t = lower_ascii(token);
  for (const auto& s : seeds) {
    const std::string ls = lower_ascii(s);
    if (ls.empty()) {
      continue;
    }
    if (t.find(ls) != std::string::npos || ls.find(t) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string path_src_stem(std::string p) {
  std::replace(p.begin(), p.end(), '\\', '/');
  const auto slash = p.find_last_of('/');
  const auto dot = p.rfind('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    p = p.substr(0, dot);
  }
  return p;
}

bool paths_same_unit(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  if (a == b) {
    return true;
  }
  return path_src_stem(a) == path_src_stem(b);
}

bool hop_linked_to_l0(const ATrailHop& hop, const std::string& focus_symbol,
                      const std::string& focus_path, const std::vector<ATrailStack>& stacks) {
  if (paths_same_unit(hop.path, focus_path)) {
    return true;
  }
  if (!focus_symbol.empty() && hop.symbol == focus_symbol) {
    return true;
  }
  for (const auto& st : stacks) {
    for (const auto& h : st.hops) {
      if (paths_same_unit(hop.path, h.path)) {
        return true;
      }
      if (!hop.symbol.empty() && hop.symbol == h.symbol) {
        return true;
      }
    }
  }
  return false;
}

bool keep_detached_cond_hop(const ATrailHop& hop, const std::string& focus_symbol,
                            const std::string& focus_path,
                            const std::vector<ATrailStack>& stacks) {
  if (hop.path.empty()) {
    return false;
  }
  return hop_linked_to_l0(hop, focus_symbol, focus_path, stacks);
}

ATrailCondBranch branch_from_hop(const ATrailHop& hop, const std::string& id,
                                 const std::string& when_text, const std::string& then_text,
                                 const std::string& note = {}) {
  ATrailCondBranch b;
  b.id = id;
  b.when_text = when_text;
  b.then_text = then_text;
  b.note = note;
  b.anchor = hop.anchor;
  b.path = hop.path;
  b.symbol = hop.symbol;
  b.line = hop.call_line;
  b.snippet = hop.snippet;
  return b;
}

int score_on_hop(const ATrailHop& hop, const std::string& focus_symbol,
                 const std::vector<std::string>& seeds) {
  int score = hop.is_call_site ? 100 : 0;
  if (!hop.control_cond.empty() || !hop.control_chain.empty()) {
    score += 20;
  }
  if (seeds_match_token(seeds, hop.symbol) || seeds_match_token(seeds, hop.path)) {
    score += 35;
  }
  if (hop.symbol.find("reindex") != std::string::npos ||
      hop.symbol.find("outline") != std::string::npos ||
      hop.symbol.find("index") != std::string::npos) {
    score -= 70;
  }
  if (!focus_symbol.empty() && hop.symbol == focus_symbol) {
    score -= 80;
  }
  return score;
}

const ATrailHop* pick_on_hop(const std::vector<ATrailStack>& stacks,
                             const std::string& focus_symbol,
                             const std::vector<std::string>& seeds) {
  const ATrailHop* best = nullptr;
  int best_score = -1;
  for (const auto& stack : stacks) {
    if (stack.hops.size() < 2) {
      continue;
    }
    const ATrailHop& caller = stack.hops[stack.hops.size() - 2];
    const int score = score_on_hop(caller, focus_symbol, seeds);
    if (score > best_score) {
      best_score = score;
      best = &caller;
    }
  }
  return best;
}

std::vector<std::string> cxl_search_symbols(const std::vector<std::string>& seeds,
                                            const std::string& focus_symbol) {
  std::vector<std::string> out;
  auto add = [&](const std::string& s) {
    if (s.empty()) {
      return;
    }
    if (std::find(out.begin(), out.end(), s) == out.end()) {
      out.push_back(s);
    }
  };
  for (const auto& seed : seeds) {
    const std::string ls = lower_ascii(seed);
    if (ls.find("cancel") != std::string::npos || ls.find("abort") != std::string::npos) {
      add(seed);
    }
  }
  const std::string lower_focus = lower_ascii(focus_symbol);
  for (const char* prefix : {"begin_", "start_", "enable_", "set_"}) {
    const std::string p(prefix);
    if (lower_focus.rfind(p, 0) == 0 && lower_focus.size() > p.size()) {
      add("cancel_" + focus_symbol.substr(p.size()));
    }
  }
  return out;
}

std::vector<std::string> off_search_symbols(const std::vector<std::string>& seeds,
                                            const std::string& focus_symbol) {
  std::vector<std::string> out;
  auto add = [&](const std::string& s) {
    if (s.empty()) {
      return;
    }
    if (std::find(out.begin(), out.end(), s) == out.end()) {
      out.push_back(s);
    }
  };
  for (const auto& seed : seeds) {
    const std::string ls = lower_ascii(seed);
    if (ls.find("clear") != std::string::npos || ls.find("end") != std::string::npos ||
        ls.find("off") != std::string::npos || ls.find("stop") != std::string::npos) {
      add(seed);
    }
  }
  const std::string lower_focus = lower_ascii(focus_symbol);
  const std::pair<const char*, const char*> complements[] = {
      {"set_", "clear_"},       {"begin_", "end_"},     {"start_", "stop_"},
      {"enable_", "disable_"},  {"open_", "close_"},    {"activate_", "deactivate_"},
  };
  for (const auto& [from, to] : complements) {
    const std::string p(from);
    if (lower_focus.rfind(p, 0) == 0 && lower_focus.size() > p.size()) {
      add(std::string(to) + focus_symbol.substr(p.size()));
    }
  }
  return out;
}

int score_cxl_hop(const ATrailHop& hop, const std::string& query) {
  int score = hop.is_call_site ? 80 : 10;
  if (hop.symbol.find("cancel") != std::string::npos) {
    score += 60;
  }
  if (query.find("agent_cancel_") != std::string::npos) {
    if (hop.snippet.find("agent_cancel_") != std::string::npos) {
      score += 50;
    }
  }
  if (hop.control_cond.find("CancelAgent") != std::string::npos ||
      hop.snippet.find("CancelAgent") != std::string::npos) {
    score += 40;
  }
  if (!hop.control_cond.empty()) {
    score += 15;
  }
  return score;
}

int score_off_hop(const ATrailHop& hop, const std::string& /*query*/) {
  int score = hop.is_call_site ? 70 : 10;
  const std::string text = lower_ascii(hop.symbol + "\n" + hop.snippet);
  if (text.find("end_") != std::string::npos || text.find("clear_") != std::string::npos ||
      text.find("reset_") != std::string::npos || text.find("stop_") != std::string::npos ||
      text.find("disable_") != std::string::npos || text.find("close_") != std::string::npos) {
    score += 55;
  }
  if (hop.snippet.find("store(false)") != std::string::npos) {
    score += 25;
  }
  if (!hop.control_cond.empty()) {
    score += 20;
  }
  return score;
}

ATrailHop best_hop_from_search(
    const std::string& workspace_root, const std::string& query,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    int (*score_fn)(const ATrailHop&, const std::string&) = nullptr) {
  ATrailHop best;
  int best_score = -1;
  if (!search) {
    return best;
  }
  const auto hits = search(query);
  for (const auto& h : hits) {
    if (h.line <= 0 || h.path.empty()) {
      continue;
    }
    std::string rel = h.path;
    if (!workspace_root.empty() && fs::path(rel).is_absolute()) {
      rel = rel_from_workspace(workspace_root, rel);
    }
    if (rel.rfind("tests/", 0) == 0 || rel.rfind("tools/", 0) == 0 || !is_trail_product_path(rel)) {
      continue;
    }
    if (!preview_looks_like_call(h.preview, query) &&
        query.find('_') != std::string::npos &&
        h.preview.find(query) == std::string::npos) {
      continue;
    }
    fs::path abs = rel;
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / rel;
    }
    ATrailHop hop =
        a_trail_enrich_hop(abs.lexically_normal().string(), rel, h.line, query);
    if (!hop.is_call_site && hop.snippet.find(query) == std::string::npos) {
      continue;
    }
    const int score =
        score_fn != nullptr ? score_fn(hop, query) : (hop.is_call_site ? 100 : 10);
    if (score > best_score) {
      best_score = score;
      best = std::move(hop);
    }
  }
  return best;
}

std::vector<ATrailCondBranch> a_trail_build_cond_branches(
    const std::string& workspace_root, const std::string& focus_symbol,
    const std::string& focus_path_hint, const std::vector<std::string>& seeds,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    const std::vector<ATrailStack>& stacks) {
  std::vector<ATrailCondBranch> out;
  if (!search || focus_symbol.empty()) {
    return out;
  }
  if (const ATrailHop* on = pick_on_hop(stacks, focus_symbol, seeds); on != nullptr) {
    std::string when = on->control_cond.empty() ? on->control_chain : on->control_cond;
    if (when.empty()) {
      when = on->signature.empty() ? on->symbol : on->signature;
    }
    std::ostringstream then;
    then << (on->scope_chain.empty() ? on->symbol : on->scope_chain) << " → " << focus_symbol;
    out.push_back(branch_from_hop(*on, "ON", when, then.str()));
  }

  ATrailHop best_cxl;
  int best_cxl_score = -1;
  for (const std::string& q : cxl_search_symbols(seeds, focus_symbol)) {
    ATrailHop hop = best_hop_from_search(workspace_root, q, search, score_cxl_hop);
    if (hop.path.empty()) {
      continue;
    }
    const int score = score_cxl_hop(hop, q);
    if (score > best_cxl_score) {
      best_cxl_score = score;
      best_cxl = std::move(hop);
    }
  }
  if (!best_cxl.path.empty() &&
      !keep_detached_cond_hop(best_cxl, focus_symbol, focus_path_hint, stacks)) {
    best_cxl = {};
    best_cxl_score = -1;
  }
  if (!best_cxl.path.empty()) {
    std::string when = best_cxl.control_cond;
    if (when.empty()) {
      when = best_cxl.control_chain;
    }
    if (when.empty()) {
      when = "cancel / abort solicitado (UI)";
    }
    std::ostringstream then;
    then << (best_cxl.scope_chain.empty() ? best_cxl.symbol : best_cxl.scope_chain);
    if (best_cxl.snippet.find("agent_cancel_") != std::string::npos) {
      then << " → agent_cancel_=true";
    } else if (best_cxl.symbol.find("cancel") != std::string::npos) {
      then << " → cancel_all()";
    }
    out.push_back(branch_from_hop(
        best_cxl, "CXL", when, then.str(),
        "cancel/abort en archivo o cadena de este L0"));
  }

  ATrailHop best_off;
  int best_off_score = -1;
  for (const std::string& q : off_search_symbols(seeds, focus_symbol)) {
    ATrailHop hop = best_hop_from_search(workspace_root, q, search, score_off_hop);
    if (hop.path.empty()) {
      continue;
    }
    const int score = score_off_hop(hop, q);
    if (score > best_off_score) {
      best_off_score = score;
      best_off = std::move(hop);
    }
  }
  if (!best_off.path.empty() &&
      !keep_detached_cond_hop(best_off, focus_symbol, focus_path_hint, stacks)) {
    best_off = {};
    best_off_score = -1;
  }
  if (!best_off.path.empty()) {
    std::string when = best_off.control_cond;
    if (when.empty()) {
      when = "worker/async termina (normal o tras cancel cooperativo)";
    }
    std::ostringstream then;
    then << (best_off.scope_chain.empty() ? best_off.symbol : best_off.scope_chain)
         << " → cleanup / off";
    out.push_back(branch_from_hop(best_off, "OFF", when, then.str()));
  }

  const bool have_cxl = !best_cxl.path.empty();
  const bool have_off = !best_off.path.empty();
  if (have_cxl && have_off) {
    ATrailCondBranch link;
    link.id = "LINK";
    link.when_text = "CXL (UI) vs OFF (worker tail)";
    link.then_text = "CXL → … → OFF (¿el cancel de este L0 llega al cleanup?)";
    link.note = "solo si ambas ramas pertenecen a este L0";
    if (!best_cxl.path.empty()) {
      link.path = best_cxl.path;
      link.anchor = best_cxl.anchor;
    }
    out.push_back(std::move(link));
  }

  return out;
}

}  // namespace tuide
