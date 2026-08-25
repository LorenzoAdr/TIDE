#include "ai/l2_effect_summary.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ai/l2_explore_a.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
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

std::string read_abs_file(const std::string& abs_path) {
  std::ifstream in(abs_path);
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

std::string node_text(TSNode node, const std::string& source, std::size_t max_len = 120) {
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

std::string lower_copy(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool seed_overlap(const std::string& ident, const std::vector<std::string>& seeds) {
  if (ident.empty() || seeds.empty()) {
    return false;
  }
  const std::string id = lower_copy(ident);
  for (const auto& seed : seeds) {
    if (seed.empty()) {
      continue;
    }
    const std::string s = lower_copy(seed);
    if (id.find(s) != std::string::npos || s.find(id) != std::string::npos) {
      return true;
    }
    std::size_t pos = 0;
    while (pos < s.size()) {
      while (pos < s.size() && !std::isalnum(static_cast<unsigned char>(s[pos]))) {
        ++pos;
      }
      const std::size_t start = pos;
      while (pos < s.size() && std::isalnum(static_cast<unsigned char>(s[pos]))) {
        ++pos;
      }
      if (pos > start) {
        const std::string tok = s.substr(start, pos - start);
        if (tok.size() >= 3 && id.find(tok) != std::string::npos) {
          return true;
        }
      }
    }
  }
  return false;
}

bool symbol_matches(const SymbolInfo& sym, const std::string& want) {
  if (want.empty()) {
    return false;
  }
  const std::string bare = symbol_insert_name(sym.name);
  if (bare == want || sym.name == want) {
    return true;
  }
  if (sym.name.size() > want.size() + 2 &&
      sym.name.compare(sym.name.size() - want.size(), want.size(), want) == 0 &&
      sym.name[sym.name.size() - want.size() - 1] == ':') {
    return true;
  }
  return false;
}

bool is_fn_kind(SymbolKind k) {
  return k == SymbolKind::kFunction || k == SymbolKind::kMethod;
}

TSNode find_fn_node_by_symbol(TSNode root, const std::string& source, const std::string& symbol,
                              int hint_line, int* start_line, int* end_line) {
  if (ts_node_is_null(root) || symbol.empty()) {
    return {};
  }

  auto fn_name_from_decl = [](const std::string& decl) -> std::string {
    const auto paren = decl.find('(');
    std::string head = paren == std::string::npos ? decl : decl.substr(0, paren);
    while (!head.empty() && (head.back() == ' ' || head.back() == '\t' || head.back() == '*' ||
                             head.back() == '&')) {
      head.pop_back();
    }
    const auto cc = head.rfind("::");
    if (cc != std::string::npos) {
      return head.substr(cc + 2);
    }
    const auto sp = head.find_last_of(" \t");
    if (sp != std::string::npos) {
      return head.substr(sp + 1);
    }
    return head;
  };

  auto declarator_matches = [&](const std::string& decl_txt) -> bool {
    if (decl_txt.empty()) {
      return false;
    }
    const std::string name = fn_name_from_decl(decl_txt);
    if (name == symbol || name == "~" + symbol) {
      return true;
    }
    if (decl_txt.find(symbol + "::" + symbol) != std::string::npos) {
      return true;
    }
    return false;
  };

  struct Cand {
    TSNode node;
    int start = 0;
    int end = 0;
  };
  std::vector<Cand> cands;

  std::function<void(TSNode)> walk;
  walk = [&](TSNode n) {
    if (ts_node_is_null(n)) {
      return;
    }
    const char* t = ts_node_type(n);
    if (t != nullptr && std::strcmp(t, "function_definition") == 0) {
      TSNode decl = ts_node_child_by_field_name(n, "declarator", 10);
      const std::string decl_txt = node_text(decl, source, 240);
      if (declarator_matches(decl_txt)) {
        Cand c;
        c.node = n;
        c.start = static_cast<int>(ts_node_start_point(n).row) + 1;
        c.end = static_cast<int>(ts_node_end_point(n).row) + 1;
        cands.push_back(c);
      }
    }
    const uint32_t nc = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < nc; ++i) {
      walk(ts_node_named_child(n, i));
    }
  };
  walk(root);

  if (cands.empty()) {
    return {};
  }
  Cand* best = &cands.front();
  if (hint_line > 0) {
    int best_dist = 1'000'000;
    for (auto& c : cands) {
      const int dist = std::abs(c.start - hint_line);
      if (dist < best_dist) {
        best_dist = dist;
        best = &c;
      }
    }
  }
  if (start_line != nullptr) {
    *start_line = best->start;
  }
  if (end_line != nullptr) {
    *end_line = best->end;
  }
  return best->node;
}

bool is_noise_call_name(const std::string& name) {
  if (name.empty() || name.size() <= 2) {
    return true;
  }
  if (name.find('.') != std::string::npos || name.find("->") != std::string::npos) {
    return true;
  }
  static const std::unordered_set<std::string> kNoise = {
      "move", "string", "tr", "empty", "size", "begin", "end", "now",      "milliseconds",
      "to_string", "enabled", "emit", "p",    "fin",   "ok",   "step",     "phase",
      "items",     "query",   "panel", "buffer", "self", "opts", "instance", "captured",
      "nested",    "out",     "r",     "str",   "stderr_text", "string_view"};
  return kNoise.count(lower_copy(name)) > 0;
}

bool is_noise_write_ident(const std::string& id) {
  if (id.empty() || id.size() <= 2) {
    return true;
  }
  if (id.find('_') != std::string::npos) {
    return false;
  }
  static const std::unordered_set<std::string> kNoise = {
      "state", "label", "focus", "region", "kind", "activity", "percent", "out",   "r",
      "result", "steps", "tr",   "recover_note", "breq", "combined", "semantic_mode",
      "active_scope_key", "lsp_items", "lsp_resolved_key", "lsp_resolved_query", "layout_state",
      "text_input_focus", "focus_sync_needed"};
  return kNoise.count(lower_copy(id)) > 0;
}

bool is_noise_write_path(const std::string& path) {
  if (path.empty()) {
    return true;
  }
  // Dotted LHS paths (state.activity, strip.kind) are never generic noise.
  if (path.find('.') != std::string::npos) {
    return false;
  }
  return is_noise_write_ident(path);
}

std::string normalize_write_path(const std::string& path, const std::string& rel_path) {
  (void)rel_path;
  return path;
}

std::string extract_write_lhs_path(TSNode node, const std::string& source, int depth = 0) {
  if (ts_node_is_null(node) || depth > 14) {
    return {};
  }
  const char* t = ts_node_type(node);
  if (t == nullptr) {
    return {};
  }
  if (std::strcmp(t, "identifier") == 0 || std::strcmp(t, "field_identifier") == 0) {
    return node_text(node, source, 48);
  }
  if (std::strcmp(t, "field_expression") == 0) {
    TSNode arg = ts_node_child_by_field_name(node, "argument", 8);
    if (ts_node_is_null(arg)) {
      arg = ts_node_child_by_field_name(node, "operand", 7);
    }
    TSNode field = ts_node_child_by_field_name(node, "field", 5);
    std::string base = extract_write_lhs_path(arg, source, depth + 1);
    std::string fld = ts_node_is_null(field) ? std::string{} : node_text(field, source, 48);
    if (!fld.empty()) {
      if (base.empty()) {
        return fld;
      }
      return base + "." + fld;
    }
    return base;
  }
  if (std::strcmp(t, "subscript_expression") == 0) {
    TSNode arg = ts_node_child_by_field_name(node, "argument", 8);
    if (ts_node_is_null(arg)) {
      arg = ts_node_named_child(node, 0);
    }
    return extract_write_lhs_path(arg, source, depth + 1);
  }
  if (std::strcmp(t, "pointer_expression") == 0 || std::strcmp(t, "reference_expression") == 0 ||
      std::strcmp(t, "parenthesized_expression") == 0 || std::strcmp(t, "binary_expression") == 0 ||
      std::strcmp(t, "unary_expression") == 0) {
    const uint32_t nc = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < nc; ++i) {
      const std::string sub = extract_write_lhs_path(ts_node_named_child(node, i), source, depth + 1);
      if (!sub.empty()) {
        return sub;
      }
    }
    return {};
  }
  const uint32_t nc = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < nc; ++i) {
    const std::string sub = extract_write_lhs_path(ts_node_named_child(node, i), source, depth + 1);
    if (!sub.empty()) {
      return sub;
    }
  }
  return {};
}

bool is_symptom_edge_call(const std::string& call) {
  return a_is_symptom_edge_name(call);
}

void add_symptom_edge_hot(const std::vector<std::string>& calls, std::vector<std::string>* hot) {
  if (hot == nullptr) {
    return;
  }
  for (const auto& c : calls) {
    if (is_symptom_edge_call(c)) {
      if (std::find(hot->begin(), hot->end(), std::string("symptom_edge")) == hot->end()) {
        hot->push_back("symptom_edge");
      }
      break;
    }
  }
}

std::vector<std::string> filter_calls_list(const std::vector<std::string>& in) {
  std::vector<std::string> out;
  out.reserve(in.size());
  for (const auto& c : in) {
    if (!is_noise_call_name(c)) {
      out.push_back(c);
    }
  }
  return out;
}

std::vector<std::string> filter_writes_list(const std::vector<std::string>& in) {
  std::vector<std::string> out;
  out.reserve(in.size());
  for (const auto& w : in) {
    if (!is_noise_write_ident(w)) {
      out.push_back(w);
    }
  }
  return out;
}

std::string extract_call_name(TSNode call, const std::string& source) {
  if (ts_node_is_null(call)) {
    return {};
  }
  TSNode fn = ts_node_child_by_field_name(call, "function", 8);
  if (ts_node_is_null(fn)) {
    fn = ts_node_named_child(call, 0);
  }
  std::string txt = node_text(fn, source, 80);
  if (txt.empty()) {
    return {};
  }
  const auto paren = txt.find('(');
  if (paren != std::string::npos) {
    txt = txt.substr(0, paren);
  }
  const auto colon = txt.rfind("::");
  if (colon != std::string::npos) {
    txt = txt.substr(colon + 2);
  }
  while (!txt.empty() && (txt.back() == '>' || txt.back() == ' ')) {
    txt.pop_back();
  }
  if (txt.rfind("std::", 0) == 0) {
    return {};
  }
  return txt;
}

std::string replace_extension(const std::string& path, const char* ext) {
  const auto dot = path.rfind('.');
  if (dot == std::string::npos) {
    return path + ext;
  }
  return path.substr(0, dot) + ext;
}

bool is_header_path(const std::string& path) {
  return path.size() >= 2 &&
         (path.compare(path.size() - 2, 2, ".h") == 0 ||
          path.rfind(".hpp") == path.size() - 4 ||
          path.rfind(".hh") == path.size() - 3);
}

bool summary_has_body(const EffectSummary& es) {
  return !(es.roles.size() == 1 && es.roles[0] == "unknown" && es.calls.empty() &&
           es.writes.empty() && es.hot.empty());
}

void collect_idents(TSNode node, const std::string& source, std::unordered_set<std::string>* out,
                    int depth = 0) {
  if (out == nullptr || ts_node_is_null(node) || depth > 24) {
    return;
  }
  const char* t = ts_node_type(node);
  if (t != nullptr && (std::strcmp(t, "identifier") == 0 ||
                       std::strcmp(t, "field_identifier") == 0 ||
                       std::strcmp(t, "type_identifier") == 0)) {
    const std::string id = node_text(node, source, 64);
    if (!id.empty() && id.size() <= 48) {
      out->insert(id);
    }
  }
  const uint32_t nc = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < nc; ++i) {
    collect_idents(ts_node_named_child(node, i), source, out, depth + 1);
  }
}

struct BodyScan {
  std::unordered_map<std::string, int> calls;
  std::unordered_map<std::string, int> writes;
  std::unordered_map<std::string, int> reads;
  int if_n = 0;
  int ret_n = 0;
  int early_ret = 0;
  int throw_n = 0;
  int for_n = 0;
  int while_n = 0;
  int try_n = 0;
  int fn_end_line = 0;
};

void record_lhs_write(TSNode left, const std::string& source, const std::string& rel_path,
                      BodyScan* scan) {
  if (scan == nullptr || ts_node_is_null(left)) {
    return;
  }
  std::string path = normalize_write_path(extract_write_lhs_path(left, source), rel_path);
  if (!path.empty() && !is_noise_write_path(path)) {
    ++scan->writes[path];
  }
}

void scan_body(TSNode body, const std::string& source, const std::string& rel_path,
               int fn_start_line, int fn_end_line, BodyScan* scan) {
  if (scan == nullptr || ts_node_is_null(body)) {
    return;
  }
  scan->fn_end_line = fn_end_line;
  std::function<void(TSNode, int)> walk;
  walk = [&](TSNode n, int depth) {
    if (ts_node_is_null(n) || depth > 48) {
      return;
    }
    const char* t = ts_node_type(n);
    if (t == nullptr) {
      return;
    }
    if (std::strcmp(t, "call_expression") == 0) {
      const std::string c = extract_call_name(n, source);
      if (!c.empty() && !is_noise_call_name(c)) {
        ++scan->calls[c];
      }
    } else if (std::strcmp(t, "assignment_expression") == 0) {
      TSNode left = ts_node_child_by_field_name(n, "left", 4);
      TSNode right = ts_node_child_by_field_name(n, "right", 5);
      std::unordered_set<std::string> lhs;
      record_lhs_write(left, source, rel_path, scan);
      collect_idents(left, source, &lhs);
      std::unordered_set<std::string> rhs;
      collect_idents(right, source, &rhs);
      for (const auto& id : rhs) {
        if (!lhs.count(id)) {
          ++scan->reads[id];
        }
      }
    } else if (std::strcmp(t, "return_statement") == 0) {
      ++scan->ret_n;
      const int line = static_cast<int>(ts_node_start_point(n).row) + 1;
      const int span = std::max(1, fn_end_line - fn_start_line);
      if (line < fn_start_line + span * 4 / 5) {
        ++scan->early_ret;
      }
    } else if (std::strcmp(t, "if_statement") == 0) {
      ++scan->if_n;
    } else if (std::strcmp(t, "for_statement") == 0 || std::strcmp(t, "for_range_loop") == 0) {
      ++scan->for_n;
    } else if (std::strcmp(t, "while_statement") == 0 || std::strcmp(t, "do_statement") == 0) {
      ++scan->while_n;
    } else if (std::strcmp(t, "try_statement") == 0) {
      ++scan->try_n;
    } else if (std::strcmp(t, "throw_statement") == 0) {
      ++scan->throw_n;
    }
    const uint32_t nc = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < nc; ++i) {
      walk(ts_node_named_child(n, i), depth + 1);
    }
  };
  walk(body, 0);
}

void detect_hot_from_text(const std::string& body_lower, std::vector<std::string>* hot) {
  if (hot == nullptr) {
    return;
  }
  auto tag = [&](const char* name, const char* needle) {
    if (body_lower.find(needle) != std::string::npos) {
      hot->push_back(name);
    }
  };
  tag("atomic", "atomic");
  tag("lock", "mutex");
  tag("lock", "lock_guard");
  tag("sleep", "sleep");
  tag("timeout", "timeout");
  tag("file_io", "fstream");
  tag("file_io", "open(");
  tag("network", "socket");
  tag("network", "http");
  tag("subprocess", "popen");
  tag("wake", "wake");
  tag("ui_event", "ui_");
  // Solo globals reales (g_foo), no substring en pending_responses_.end
  if (body_lower.find(" g_") != std::string::npos ||
      body_lower.rfind("g_", 0) == 0) {
    hot->push_back("global_write");
  }
  tag("static_mut", "static ");
  tag("todo_fixme", "todo");
  tag("assert_fail", "assert");
}

std::vector<std::string> map_to_sorted_list(const std::unordered_map<std::string, int>& m) {
  std::vector<std::pair<std::string, int>> pairs(m.begin(), m.end());
  std::stable_sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) {
      return a.second > b.second;
    }
    return a.first < b.first;
  });
  std::vector<std::string> out;
  out.reserve(pairs.size());
  for (const auto& p : pairs) {
    out.push_back(p.first);
  }
  return out;
}

std::vector<std::string> derive_roles(const BodyScan& scan, const std::vector<std::string>& hot) {
  std::vector<std::string> roles;
  auto add = [&](const char* r) {
    if (std::find(roles.begin(), roles.end(), r) == roles.end()) {
      roles.push_back(r);
    }
  };
  if (!scan.writes.empty()) {
    add("mutator");
  }
  if (scan.writes.empty() && (!scan.reads.empty() || !scan.calls.empty())) {
    add("query");
  }
  for (const auto& h : hot) {
    if (h == "wake" || h == "ui_event") {
      add("ui");
    }
    if (h == "file_io" || h == "network" || h == "subprocess") {
      add("io");
    }
    if (h == "lock" || h == "atomic") {
      add("lock");
    }
  }
  if (roles.empty() && scan.calls.size() <= 2 && scan.writes.empty()) {
    add("glue");
  }
  if (roles.empty()) {
    add("unknown");
  }
  while (roles.size() > 3) {
    roles.pop_back();
  }
  return roles;
}

std::string join_csv(const std::vector<std::string>& items) {
  std::ostringstream out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i) {
      out << ", ";
    }
    out << items[i];
  }
  return out.str();
}

std::string function_signature_line(TSNode fn_node, const std::string& source,
                                    const std::vector<std::string>& lines) {
  if (ts_node_is_null(fn_node)) {
    return {};
  }
  const int start_1 = static_cast<int>(ts_node_start_point(fn_node).row) + 1;
  if (start_1 >= 1 && start_1 <= static_cast<int>(lines.size())) {
    std::string line = lines[static_cast<std::size_t>(start_1 - 1)];
    while (!line.empty() && (line.back() == '{' || line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    return compress_ws(line, 120);
  }
  TSNode declarator = ts_node_child_by_field_name(fn_node, "declarator", 10);
  return node_text(declarator, source, 120);
}

std::string bare_symbol_from_target(const std::string& target, const std::string& fallback) {
  std::string bare = fallback;
  if (bare.empty()) {
    bare = target;
    const auto colon = bare.rfind(':');
    if (colon != std::string::npos) {
      bare = bare.substr(colon + 1);
    }
  }
  const auto hash = bare.find('#');
  if (hash != std::string::npos) {
    bare = bare.substr(0, hash);
  }
  return bare;
}

int count_seed_hits(const EffectSummary& es, const std::vector<std::string>& seeds) {
  if (seeds.empty()) {
    return 0;
  }
  int n = 0;
  auto bump = [&](const std::vector<std::string>& items) {
    for (const auto& it : items) {
      if (seed_overlap(it, seeds)) {
        ++n;
      }
    }
  };
  bump(es.calls);
  bump(es.writes);
  bump(es.hot);
  for (const auto& s : seeds) {
    if (s.empty()) {
      continue;
    }
    const std::string sl = lower_copy(s);
    if (es.sig.find(s) != std::string::npos || es.symbol.find(s) != std::string::npos) {
      ++n;
    }
    if (es.ctrl.find(sl) != std::string::npos) {
      ++n;
    }
  }
  return n;
}

std::string detect_symbol_kind(const std::string& sig, const std::string& symbol, TSNode fn,
                               bool ts_fallback) {
  if (ts_fallback) {
    return "fallback";
  }
  if (sig.find('~') != std::string::npos || symbol.find('~') == 0) {
    return "dtor";
  }
  if (!ts_node_is_null(fn)) {
    const char* t = ts_node_type(fn);
    if (t != nullptr && std::strcmp(t, "function_definition") == 0) {
      TSNode body = ts_node_child_by_field_name(fn, "body", 4);
      if (ts_node_is_null(body)) {
        return "inline";
      }
    }
  }
  if (sig.find("inline") != std::string::npos) {
    return "inline";
  }
  if (sig.find("::") != std::string::npos || symbol.find("::") != std::string::npos) {
    return "method";
  }
  return "fn";
}

std::string extract_guard_from_body(TSNode body, const std::string& source,
                                    const std::vector<std::string>& lines,
                                    const std::vector<std::string>& seeds,
                                    const std::vector<std::string>& orphans) {
  if (ts_node_is_null(body)) {
    return {};
  }
  std::function<std::string(TSNode, int)> walk;
  walk = [&](TSNode n, int depth) -> std::string {
    if (ts_node_is_null(n) || depth > 32) {
      return {};
    }
    const char* t = ts_node_type(n);
    if (t != nullptr && std::strcmp(t, "if_statement") == 0) {
      const int line_1 = static_cast<int>(ts_node_start_point(n).row) + 1;
      if (line_1 >= 1 && line_1 <= static_cast<int>(lines.size())) {
        std::string line = lines[static_cast<std::size_t>(line_1 - 1)];
        while (!line.empty() && (line.back() == '{' || line.back() == ' ' || line.back() == '\t')) {
          line.pop_back();
        }
        line = compress_ws(line, 96);
        for (const auto& s : seeds) {
          if (seed_overlap(line, {s})) {
            return line;
          }
        }
        for (const auto& o : orphans) {
          if (seed_overlap(line, {o})) {
            return line;
          }
        }
      }
    }
    const uint32_t nc = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < nc; ++i) {
      const std::string g = walk(ts_node_named_child(n, i), depth + 1);
      if (!g.empty()) {
        return g;
      }
    }
    return {};
  };
  return walk(body, 0);
}

std::vector<std::string> lookup_callers_from_snapshot(const SymbolIndexSnapshot* snap,
                                                      const std::string& symbol, int max_n) {
  std::vector<std::string> out;
  if (snap == nullptr || symbol.empty() || max_n <= 0) {
    return out;
  }
  std::unordered_map<std::string, int> by_file;
  for (const auto& r : snap->refs) {
    if (r.name != symbol || r.file.empty()) {
      continue;
    }
    by_file[r.file] += std::max(1, r.count);
  }
  std::vector<std::pair<std::string, int>> ranked(by_file.begin(), by_file.end());
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  for (const auto& p : ranked) {
    if (static_cast<int>(out.size()) >= max_n) {
      break;
    }
    std::string base = p.first;
    const auto slash = base.find_last_of('/');
    if (slash != std::string::npos) {
      base = base.substr(slash + 1);
    }
    out.push_back(base + "x" + std::to_string(p.second));
  }
  return out;
}

std::vector<std::string> collect_orphan_matches(const EffectSummary& es,
                                                const std::vector<std::string>& orphans) {
  std::vector<std::string> out;
  if (orphans.empty()) {
    return out;
  }
  auto try_add = [&](const std::string& id) {
    if (id.empty()) {
      return;
    }
    for (const auto& o : orphans) {
      if (o.size() < 3) {
        continue;
      }
      if (seed_overlap(id, {o})) {
        if (std::find(out.begin(), out.end(), o) == out.end()) {
          out.push_back(o);
        }
        return;
      }
    }
  };
  try_add(es.symbol);
  try_add(es.stem);
  for (const auto& c : es.calls) {
    try_add(c);
  }
  for (const auto& w : es.writes) {
    try_add(w);
  }
  if (out.size() > 4) {
    out.resize(4);
  }
  return out;
}

void split_calls_by_seed(const std::vector<std::string>& calls, const std::vector<std::string>& seeds,
                         std::vector<std::string>* seed_out, std::vector<std::string>* rest_out) {
  if (seed_out == nullptr || rest_out == nullptr) {
    return;
  }
  seed_out->clear();
  rest_out->clear();
  for (const auto& c : calls) {
    if (seed_overlap(c, seeds)) {
      seed_out->push_back(c);
    } else {
      rest_out->push_back(c);
    }
  }
}

std::vector<std::string> collect_seed_matches(const EffectSummary& es,
                                              const std::vector<std::string>& seeds) {
  std::vector<std::string> out;
  if (seeds.empty()) {
    return out;
  }
  auto try_add = [&](const std::string& id) {
    if (id.empty() || !seed_overlap(id, seeds)) {
      return;
    }
    if (std::find(out.begin(), out.end(), id) == out.end()) {
      out.push_back(id);
    }
  };
  for (const auto& c : es.calls) {
    try_add(c);
  }
  for (const auto& w : es.writes) {
    try_add(w);
  }
  for (const auto& h : es.hot) {
    try_add(h);
  }
  for (const auto& r : es.reads) {
    try_add(r);
  }
  try_add(es.symbol);
  if (out.size() > 6) {
    out.resize(6);
  }
  return out;
}

std::string compute_a0_nudge(const EffectSummary& es, const EffectSummaryQuality& q) {
  auto has_hot = [&](const char* tag) {
    return std::find(es.hot.begin(), es.hot.end(), tag) != es.hot.end();
  };
  auto sym_is_generic_cancel = [](const std::string& sym) {
    if (sym == "cancel") {
      return true;
    }
    return sym.size() > 8 && sym.compare(sym.size() - 8, 8, "::cancel") == 0;
  };
  auto stem_is_symptom_module = [](const std::string& stem) {
    if (stem.empty()) {
      return false;
    }
    const std::string s = lower_copy(stem);
    return s.find("ai") != std::string::npos || s.find("ui") != std::string::npos ||
           s.find("console") != std::string::npos || s.find("agent") != std::string::npos;
  };
  const bool ui_hot =
      has_hot("wake") || has_hot("ui_event") || has_hot("symptom_edge");
  const bool glue =
      es.roles.size() == 1 && es.roles[0] == "glue" && es.writes.empty() && !ui_hot;
  const bool mutator =
      !es.writes.empty() ||
      std::find(es.roles.begin(), es.roles.end(), "mutator") != es.roles.end();
  if (sym_is_generic_cancel(es.symbol) && q.seed_hits <= 1 && !ui_hot &&
      !stem_is_symptom_module(es.stem)) {
    return "likely_noise";
  }
  const std::string fam = es.path_fam.empty() ? a_path_family(es.path) : es.path_fam;
  if ((fam == "lsp" || fam == "search" || es.stem.find("editor") != std::string::npos) &&
      !has_hot("symptom_edge") && q.seed_hits == 0) {
    return "likely_lsp_trap";
  }
  if (glue && q.seed_hits == 0) {
    return "likely_glue";
  }
  if (q.seed_hits >= 2 || (q.seed_hits >= 1 && ui_hot) || (q.seed_hits >= 1 && mutator)) {
    if (has_hot("symptom_edge") || a_is_symptom_edge_name(es.symbol)) {
      for (const auto& c : es.calls_seed) {
        if (is_symptom_edge_call(c)) {
          return "expand:trail edge=" + c;
        }
      }
      for (const auto& c : es.calls) {
        if (is_symptom_edge_call(c)) {
          return "expand:trail edge=" + c;
        }
      }
      return "expand:trail";
    }
    if (a_writes_suggest_trail_a0(es.writes) ||
        (es.path_fam == "ui" && mutator && q.seed_hits >= 1)) {
      return "expand:trail";
    }
    if (!es.writes.empty() && q.seed_hits >= 1) {
      return "expand:dataflow suspect=" + es.writes.front();
    }
    if (ui_hot && mutator) {
      return "expand:trail";
    }
    return "expand:peek";
  }
  if ((has_hot("symptom_edge") || a_is_symptom_edge_name(es.symbol)) && (ui_hot || mutator)) {
    return "expand:trail";
  }
  if (q.seed_hits >= 1) {
    return "weak_seed";
  }
  if (q.ts_fallback) {
    return "ts_miss";
  }
  return "no_signal";
}

void finalize_effect_summary_card(EffectSummary* es, const EffectSummaryOpts& opts) {
  if (es == nullptr) {
    return;
  }
  if (es->start_line > 0 && es->end_line >= es->start_line) {
    es->body_lines = es->end_line - es->start_line + 1;
  }
  es->map_score = opts.map_score;
  if (!opts.stem.empty()) {
    es->stem = opts.stem;
  } else if (es->stem.empty()) {
    const std::string& p = es->path;
    std::string base = p;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) {
      base = base.substr(slash + 1);
    }
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) {
      base = base.substr(0, dot);
    }
    es->stem = base;
  }
  if (!opts.map_related.empty()) {
    es->map_related = opts.map_related;
  }
  if (opts.refs_in > 0) {
    es->refs_in = opts.refs_in;
  }
  if (opts.body_sem_permille > 0) {
    es->body_sem_permille = opts.body_sem_permille;
  }
  es->file_rank = opts.file_rank;
  es->file_count = opts.file_count;
  es->dup_stem = opts.dup_stem;
  if (es->path_fam.empty()) {
    es->path_fam = a_path_family(es->path);
  }
  es->orphan_match = collect_orphan_matches(*es, opts.orphans);
  if (opts.symbol_snapshot != nullptr) {
    es->callers = lookup_callers_from_snapshot(opts.symbol_snapshot, es->symbol, 2);
  }
  es->seed_match = collect_seed_matches(*es, opts.seeds);
  EffectSummaryQuality q;
  q.seed_hits = count_seed_hits(*es, opts.seeds);
  q.ts_fallback = es->roles.size() == 1 && es->roles[0] == "unknown" && es->calls.empty() &&
                  es->writes.empty() && es->hot.empty();
  es->nudge = compute_a0_nudge(*es, q);
  es->card_text = tuide::effect_summary_render_card(*es);
  if (static_cast<int>(es->card_text.size()) > opts.max_chars) {
    es->reads.clear();
    es->card_text = tuide::effect_summary_render_card(*es);
  }
  if (static_cast<int>(es->card_text.size()) > opts.max_chars) {
    es->calls.clear();
    es->card_text = tuide::effect_summary_render_card(*es);
  }
  if (static_cast<int>(es->card_text.size()) > opts.max_chars) {
    es->map_score = 0;
    es->card_text = tuide::effect_summary_render_card(*es);
  }
  if (static_cast<int>(es->card_text.size()) > opts.max_chars) {
    es->map_related.clear();
    es->card_text = tuide::effect_summary_render_card(*es);
  }
  if (static_cast<int>(es->card_text.size()) > opts.max_chars) {
    es->callers.clear();
    es->card_text = tuide::effect_summary_render_card(*es);
  }
  if (static_cast<int>(es->card_text.size()) > opts.max_chars) {
    es->guard.clear();
    es->card_text = tuide::effect_summary_render_card(*es);
  }
}

std::vector<std::string> truncate_list_impl(const std::vector<std::string>& items,
                                             const std::vector<std::string>& seeds, int max_n) {
  if (max_n <= 0 || static_cast<int>(items.size()) <= max_n) {
    return items;
  }
  std::vector<std::string> edge_hits;
  std::vector<std::string> seed_hits;
  std::vector<std::string> rest;
  for (const auto& it : items) {
    if (is_symptom_edge_call(it)) {
      edge_hits.push_back(it);
    } else if (seed_overlap(it, seeds)) {
      seed_hits.push_back(it);
    } else {
      rest.push_back(it);
    }
  }
  std::vector<std::string> out;
  out.reserve(static_cast<std::size_t>(max_n));
  auto append = [&](const std::vector<std::string>& src) {
    for (const auto& s : src) {
      if (static_cast<int>(out.size()) >= max_n) {
        return;
      }
      out.push_back(s);
    }
  };
  append(edge_hits);
  append(seed_hits);
  append(rest);
  return out;
}

}  // namespace

std::vector<std::string> effect_summary_truncate_list(const std::vector<std::string>& items,
                                                      const std::vector<std::string>& seeds,
                                                      int max_n) {
  return truncate_list_impl(items, seeds, max_n);
}

EffectSummary effect_summary_build(const std::string& abs_path, const std::string& rel_path,
                                   const std::string& symbol, const std::string& window_hint,
                                   const EffectSummaryOpts& opts) {
  EffectSummary es;
  es.path = rel_path;
  es.symbol = symbol;
  es.window_hint = window_hint;
  es.anchor = rel_path + ":" + symbol;

  const std::string source = read_abs_file(abs_path);
  if (source.empty()) {
    es.sig = symbol + "(?)";
    es.roles = {"unknown"};
    finalize_effect_summary_card(&es, opts);
    return es;
  }

  const auto lines = split_lines(source);
  TSTree* tree = parse_sync(source, abs_path);
  TSNode fn{};
  int start_line = 0;
  int end_line = 0;
  if (tree != nullptr) {
    TSNode root = ts_tree_root_node(tree);
    fn = find_fn_node_by_symbol(root, source, symbol, opts.hint_line, &start_line, &end_line);
    if (ts_node_is_null(fn)) {
      const auto syms = extract_symbols_from_tree(root, source, rel_path);
      for (const auto& sym : syms) {
        if (is_fn_kind(sym.kind) && symbol_matches(sym, symbol)) {
          const int hint = opts.hint_line > 0 ? opts.hint_line : sym.line;
          start_line = sym.line;
          end_line = sym.end_line > 0 ? sym.end_line : sym.line + 80;
          fn = find_fn_node_by_symbol(root, source, symbol_insert_name(sym.name), hint,
                                      &start_line, &end_line);
          if (!ts_node_is_null(fn)) {
            break;
          }
        }
      }
    }
  }

  es.start_line = start_line;
  es.end_line = end_line > 0 ? end_line : start_line;

  if (ts_node_is_null(fn)) {
    if (tree != nullptr) {
      ts_tree_delete(tree);
    }
    if (start_line >= 1 && start_line <= static_cast<int>(lines.size())) {
      es.sig = compress_ws(lines[static_cast<std::size_t>(start_line - 1)], 120);
    } else {
      es.sig = symbol + "(?)";
    }
    es.roles = {"unknown"};
    es.kind = "fallback";
    es.path_fam = a_path_family(rel_path);
    finalize_effect_summary_card(&es, opts);
    return es;
  }

  es.sig = function_signature_line(fn, source, lines);
  TSNode body = ts_node_child_by_field_name(fn, "body", 4);
  BodyScan scan;
  scan_body(body, source, rel_path, start_line, end_line, &scan);

  es.kind = detect_symbol_kind(es.sig, symbol, fn, false);
  es.path_fam = a_path_family(rel_path);
  es.guard = extract_guard_from_body(body, source, lines, opts.seeds, opts.orphans);

  es.calls = effect_summary_truncate_list(map_to_sorted_list(scan.calls), opts.seeds, opts.max_list);
  split_calls_by_seed(es.calls, opts.seeds, &es.calls_seed, &es.calls);
  es.writes =
      effect_summary_truncate_list(map_to_sorted_list(scan.writes), opts.seeds, opts.max_list);
  es.reads = effect_summary_truncate_list(map_to_sorted_list(scan.reads), opts.seeds, opts.max_list);

  std::ostringstream ctrl;
  if (scan.early_ret > 0) {
    ctrl << "early-return×" << scan.early_ret << "; ";
  }
  if (scan.if_n > 0) {
    ctrl << "if×" << scan.if_n << "; ";
  }
  if (scan.ret_n > 0) {
    ctrl << "return×" << scan.ret_n << "; ";
  }
  if (scan.throw_n > 0) {
    ctrl << "throw×" << scan.throw_n << "; ";
  }
  if (scan.for_n + scan.while_n > 0) {
    ctrl << "loop×" << (scan.for_n + scan.while_n) << "; ";
  }
  if (scan.try_n > 0) {
    ctrl << "try×" << scan.try_n << "; ";
  }
  es.ctrl = ctrl.str();
  while (!es.ctrl.empty() && (es.ctrl.back() == ' ' || es.ctrl.back() == ';')) {
    es.ctrl.pop_back();
  }

  const std::string body_lower = lower_copy(node_text(body, source, 8000));
  detect_hot_from_text(body_lower, &es.hot);
  if (scan.early_ret >= 2) {
    es.hot.push_back("early_return");
  }
  if (scan.early_ret >= 1 && scan.ret_n >= 2) {
    es.hot.push_back("multi_return");
  }
  if (scan.throw_n > 0) {
    es.hot.push_back("throws");
  }
  std::sort(es.hot.begin(), es.hot.end());
  es.hot.erase(std::unique(es.hot.begin(), es.hot.end()), es.hot.end());
  add_symptom_edge_hot(es.calls_seed, &es.hot);
  add_symptom_edge_hot(es.calls, &es.hot);
  if (a_is_symptom_edge_name(es.symbol)) {
    if (std::find(es.hot.begin(), es.hot.end(), std::string("symptom_edge")) == es.hot.end()) {
      es.hot.push_back("symptom_edge");
    }
  }

  es.roles = derive_roles(scan, es.hot);
  finalize_effect_summary_card(&es, opts);
  if (tree != nullptr) {
    ts_tree_delete(tree);
  }
  return es;
}

std::string effect_summary_render_card(const EffectSummary& es) {
  std::ostringstream out;
  out << "# ES " << es.path << ":" << es.symbol << '\n';
  out << "sig:    " << (es.sig.empty() ? es.symbol + "(?)" : es.sig) << '\n';
  if (!es.kind.empty()) {
    out << "kind:   " << es.kind << '\n';
  }
  if (!es.path_fam.empty()) {
    out << "path_fam: " << es.path_fam << '\n';
  }
  if (!es.stem.empty()) {
    out << "stem:   " << es.stem << '\n';
  }
  if (es.body_sem_permille > 0 || es.file_rank > 0 || es.dup_stem) {
    out << "l1:     ";
    bool first = true;
    auto append_l1 = [&](const std::string& s) {
      if (!first) {
        out << ' ';
      }
      first = false;
      out << s;
    };
    if (es.body_sem_permille > 0) {
      char buf[24];
      std::snprintf(buf, sizeof(buf), "body=%.2f", es.body_sem_permille / 1000.0);
      append_l1(buf);
    }
    if (es.file_rank > 0 && es.file_count > 0) {
      append_l1("file=" + std::to_string(es.file_rank) + "/" + std::to_string(es.file_count));
    }
    if (es.dup_stem) {
      append_l1("dup_stem");
    }
    out << '\n';
  }
  if (!es.map_related.empty()) {
    out << "map:    " << es.map_related << '\n';
  }
  if (es.refs_in > 0) {
    out << "refs:   in≈" << es.refs_in << '\n';
  }
  if (!es.orphan_match.empty()) {
    out << "orphan: " << join_csv(es.orphan_match) << '\n';
  }
  if (!es.roles.empty()) {
    out << "roles:  " << join_csv(es.roles) << '\n';
  }
  if (!es.calls_seed.empty()) {
    out << "calls_seed: " << join_csv(es.calls_seed) << '\n';
  }
  if (!es.calls.empty()) {
    out << "calls:  " << join_csv(es.calls) << '\n';
  }
  if (!es.writes.empty()) {
    out << "writes: " << join_csv(es.writes) << '\n';
  }
  if (!es.reads.empty()) {
    out << "reads:  " << join_csv(es.reads) << '\n';
  }
  if (!es.ctrl.empty()) {
    out << "ctrl:   " << es.ctrl << '\n';
  }
  if (!es.guard.empty()) {
    out << "guard:  " << es.guard << '\n';
  }
  if (!es.hot.empty()) {
    out << "hot:    " << join_csv(es.hot) << '\n';
  }
  if (!es.seed_match.empty()) {
    out << "seeds:  " << join_csv(es.seed_match) << " (" << es.seed_match.size() << ")\n";
  }
  if (es.body_lines > 0) {
    out << "span:   " << es.body_lines << "L\n";
  }
  if (!es.callers.empty()) {
    out << "callers: " << join_csv(es.callers) << '\n';
  }
  if (!es.nudge.empty()) {
    out << "nudge:  " << es.nudge << '\n';
  }
  if (es.map_score > 0) {
    out << "rank:   " << es.map_score << '\n';
  }
  out << "anchor: " << es.anchor;
  if (es.start_line > 0) {
    out << "  lines:" << es.start_line << "-" << es.end_line;
  }
  if (!es.window_hint.empty()) {
    out << "  window_hint:" << es.window_hint;
  }
  out << '\n';
  return out.str();
}

nlohmann::json effect_summary_to_json(const EffectSummary& es) {
  return {{"path", es.path},
          {"symbol", es.symbol},
          {"anchor", es.anchor},
          {"window_hint", es.window_hint},
          {"start_line", es.start_line},
          {"end_line", es.end_line},
          {"sig", es.sig},
          {"kind", es.kind},
          {"path_fam", es.path_fam},
          {"roles", es.roles},
          {"calls", es.calls},
          {"calls_seed", es.calls_seed},
          {"writes", es.writes},
          {"reads", es.reads},
          {"ctrl", es.ctrl},
          {"guard", es.guard},
          {"hot", es.hot},
          {"seed_match", es.seed_match},
          {"orphan_match", es.orphan_match},
          {"callers", es.callers},
          {"body_lines", es.body_lines},
          {"map_score", es.map_score},
          {"stem", es.stem},
          {"map_related", es.map_related},
          {"refs_in", es.refs_in},
          {"body_sem_permille", es.body_sem_permille},
          {"file_rank", es.file_rank},
          {"file_count", es.file_count},
          {"dup_stem", es.dup_stem},
          {"nudge", es.nudge},
          {"card", es.card_text}};
}

EffectSummary effect_summary_for_queue_item(const std::string& workspace_root,
                                            const AQueueItem& item,
                                            const EffectSummaryOpts& opts) {
  EffectSummaryOpts o = opts;
  if (item.line > 0) {
    o.hint_line = item.line;
  }
  if (item.score > 0.f) {
    o.map_score = static_cast<int>(item.score);
  }
  if (!item.stem.empty()) {
    o.stem = item.stem;
  }
  o.map_related = item.map_related;
  o.refs_in = item.refs_in;
  o.body_sem_permille = item.body_sem_permille;
  o.file_rank = item.file_rank;
  o.file_count = item.file_count;
  o.dup_stem = item.dup_stem;
  const std::string sym = bare_symbol_from_target(item.target, item.symbol);
  std::string rel = item.path;
  if (is_header_path(rel)) {
    const std::string cpp_rel = replace_extension(rel, ".cpp");
    const fs::path abs_cpp = fs::path(workspace_root) / cpp_rel;
    if (fs::exists(abs_cpp)) {
      const EffectSummary from_hpp =
          effect_summary_build((fs::path(workspace_root) / rel).string(), rel, sym,
                               item.window_hint, o);
      const EffectSummary from_cpp =
          effect_summary_build(abs_cpp.string(), cpp_rel, sym, item.window_hint, o);
      if (summary_has_body(from_cpp) || !summary_has_body(from_hpp)) {
        rel = cpp_rel;
      }
    }
  }
  const fs::path abs = fs::path(workspace_root) / rel;
  return effect_summary_build(abs.string(), rel, sym, item.window_hint, o);
}

EffectSummaryQuality effect_summary_quality(const EffectSummary& es,
                                            const std::vector<std::string>& seeds) {
  EffectSummaryQuality q;
  q.card_chars = static_cast<int>(es.card_text.size());
  q.line_count = static_cast<int>(
      std::count(es.card_text.begin(), es.card_text.end(), static_cast<char>('\n')) + 1);
  q.within_budget =
      q.card_chars <= kEffectSummaryMaxChars && q.line_count <= kEffectSummaryMaxLines + 2;
  q.ts_fallback = es.roles.size() == 1 && es.roles[0] == "unknown" && es.calls.empty() &&
                  es.writes.empty() && es.hot.empty();
  q.seed_hits = count_seed_hits(es, seeds);
  return q;
}

int effect_summary_lexical_rerank_score(const EffectSummary& es, const EffectSummaryOpts& opts) {
  int s = es.map_score;
  if (es.body_sem_permille > 0) {
    s += es.body_sem_permille / 2;
  }
  if (es.dup_stem) {
    s -= 80000;
  }
  if (es.file_rank > 1) {
    s -= 25000 * (es.file_rank - 1);
  }
  s += static_cast<int>(es.seed_match.size()) * 40000;
  s += static_cast<int>(es.orphan_match.size()) * 60000;
  auto has_hot = [&](const char* tag) {
    return std::find(es.hot.begin(), es.hot.end(), std::string(tag)) != es.hot.end();
  };
  if (has_hot("symptom_edge")) {
    s += 100000;
  }
  if (has_hot("ui_event") || has_hot("wake")) {
    s += 35000;
  }
  if (es.path.rfind("tests/", 0) == 0 || es.path.find("/tests/") != std::string::npos) {
    s -= 180000;
  }
  if (!es.writes.empty()) {
    s += static_cast<int>(std::min(es.writes.size(), std::size_t{4})) * 12000;
  }
  if (es.nudge.rfind("expand:trail", 0) == 0) {
    s += 90000;
  } else if (es.nudge.rfind("expand:", 0) == 0) {
    s += 45000;
  } else if (es.nudge == "likely_lsp_trap" || es.nudge == "likely_noise" ||
             es.nudge == "likely_glue") {
    s -= 120000;
  } else if (es.nudge == "weak_seed") {
    s += 15000;
  }
  const std::string fam = es.path_fam.empty() ? a_path_family(es.path) : es.path_fam;
  if (fam == "ai" || fam == "ui") {
    s += 50000;
  } else if (fam == "lsp" || fam == "search") {
    s -= 35000;
  }
  if (!opts.query.empty()) {
    if (seed_overlap(es.symbol, {opts.query}) || seed_overlap(es.stem, {opts.query})) {
      s += 30000;
    }
  }
  return s;
}

namespace {

EffectSummaryOpts a0_tranche_es_opts(const AState& st, const A0TrancheBuildOpts* opts) {
  EffectSummaryOpts es_opts;
  es_opts.seeds = st.seeds;
  es_opts.orphans = st.orphans;
  if (es_opts.orphans.empty()) {
    es_opts.orphans = st.seeds;
  }
  if (opts != nullptr && opts->symbol_snapshot != nullptr) {
    es_opts.symbol_snapshot = opts->symbol_snapshot;
  }
  return es_opts;
}

void fill_es_opts_from_item(EffectSummaryOpts* es_opts, const AQueueItem& item) {
  if (es_opts == nullptr) {
    return;
  }
  es_opts->map_score = static_cast<int>(item.score);
  es_opts->stem = item.stem;
  es_opts->map_related = item.map_related;
  es_opts->refs_in = item.refs_in;
  es_opts->body_sem_permille = item.body_sem_permille;
  es_opts->file_rank = item.file_rank;
  es_opts->file_count = item.file_count;
  es_opts->dup_stem = item.dup_stem;
}

}  // namespace

std::vector<AQueueItem> a_order_a0_tranche_by_card(
    const std::string& workspace_root, const std::vector<AQueueItem>& slice, const AState& st,
    int max_n, const A0TrancheBuildOpts* opts, std::vector<A0CardRankRow>* rank_debug) {
  if (max_n <= 0 || slice.empty() || workspace_root.empty()) {
    return {};
  }
  EffectSummaryOpts base_opts = a0_tranche_es_opts(st, opts);
  std::vector<A0CardRankRow> rows;
  rows.reserve(slice.size());
  for (std::size_t i = 0; i < slice.size(); ++i) {
    const auto& item = slice[i];
    EffectSummaryOpts es_opts = base_opts;
    fill_es_opts_from_item(&es_opts, item);
    A0CardRankRow row;
    row.item = item;
    row.slice_rank = static_cast<int>(i) + 1;
    row.es = effect_summary_for_queue_item(workspace_root, item, es_opts);
    row.score = effect_summary_lexical_rerank_score(row.es, es_opts);
    rows.push_back(std::move(row));
  }
  std::stable_sort(rows.begin(), rows.end(),
                   [](const A0CardRankRow& a, const A0CardRankRow& b) { return a.score > b.score; });
  std::vector<AQueueItem> out;
  out.reserve(static_cast<std::size_t>(max_n));
  std::unordered_map<std::string, int> stem_n;
  auto orphan_hit = [&](const A0CardRankRow& row) -> bool {
    return !row.es.orphan_match.empty();
  };
  for (const auto& row : rows) {
    if (static_cast<int>(out.size()) >= max_n) {
      break;
    }
    if (!orphan_hit(row) && stem_n[row.item.stem] >= 2) {
      continue;
    }
    out.push_back(row.item);
    ++stem_n[row.item.stem];
  }
  if (rank_debug != nullptr) {
    *rank_debug = std::move(rows);
  }
  return out;
}

bool a_target_matches_verdict_anchor(const std::string& queue_target,
                                     const std::string& verdict_target) {
  if (queue_target.empty() || verdict_target.empty()) {
    return false;
  }
  if (queue_target == verdict_target) {
    return true;
  }
  auto strip_window = [](std::string t) {
    const auto hash = t.find('#');
    if (hash != std::string::npos) {
      t = t.substr(0, hash);
    }
    return t;
  };
  const std::string qn = strip_window(queue_target);
  const std::string vn = strip_window(verdict_target);
  if (qn == vn) {
    return true;
  }
  const auto qcolon = qn.rfind(':');
  const auto vcolon = vn.rfind(':');
  if (qcolon != std::string::npos && vcolon != std::string::npos) {
    if (qn.substr(0, qcolon) == vn.substr(0, vcolon) &&
        qn.substr(qcolon + 1) == vn.substr(vcolon + 1)) {
      return true;
    }
  }
  if (verdict_target.find(qn) != std::string::npos ||
      queue_target.find(vn) != std::string::npos) {
    return true;
  }
  return false;
}

A0TrancheShown a_build_a0_tranche_shown(const std::string& workspace_root, const AState& st,
                                        int max_cards, const A0TrancheBuildOpts* opts) {
  A0TrancheShown out;
  if (st.queue.empty() || st.cursor >= static_cast<int>(st.queue.size())) {
    return out;
  }
  const int shown_cap = max_cards > 0 ? max_cards : kA0MaxCardsPerTurn;
  const int remain = static_cast<int>(st.queue.size()) - st.cursor;
  if (remain <= 0 || shown_cap <= 0) {
    return out;
  }
  // Consider a wider window for card rerank, then keep only shown_cap.
  const int window = std::min({kA0RerankWindow, remain, std::max(shown_cap, shown_cap * 3)});
  out.slice_n = window;
  std::vector<AQueueItem> slice;
  slice.reserve(static_cast<std::size_t>(window));
  for (int i = 0; i < window; ++i) {
    slice.push_back(st.queue[static_cast<std::size_t>(st.cursor + i)]);
  }
  const auto tranche =
      a_order_a0_tranche_by_card(workspace_root, slice, st, shown_cap, opts, nullptr);
  EffectSummaryOpts es_opts = a0_tranche_es_opts(st, opts);
  for (const auto& item : tranche) {
    fill_es_opts_from_item(&es_opts, item);
    const EffectSummary es = effect_summary_for_queue_item(workspace_root, item, es_opts);
    out.items.push_back(item);
    out.card_chars += static_cast<int>(es.card_text.size());
    if (out.card_chars > kA0MaxCharsPerTurn) {
      out.char_truncated = true;
      out.items.pop_back();
      break;
    }
  }
  return out;
}

}  // namespace tuide
