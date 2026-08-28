#include "ai/l2_effect_slice.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "ai/l2_effect_summary.hpp"
#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_symbols.hpp"
#include "symbols/symbol_utils.hpp"

extern "C" {
#include <tree_sitter/api.h>
}

namespace tuide {
namespace {

namespace fs = std::filesystem;

bool is_product_path(const std::string& rel) {
  if (rel.rfind("tests/", 0) == 0 || rel.rfind("tools/", 0) == 0 ||
      rel.rfind("docs/", 0) == 0 || rel.find("third_party/") != std::string::npos) {
    return false;
  }
  return rel.rfind("src/", 0) == 0 || rel.find('/') == std::string::npos;
}

bool symbol_has_morph_action_prefix(const std::string& symbol) {
  static const char* const kPrefixes[] = {"set_", "clear_", "cancel", "begin_", "end_"};
  for (const char* prefix : kPrefixes) {
    if (symbol.rfind(prefix, 0) == 0) {
      return true;
    }
  }
  return false;
}

std::string lower_ascii_copy(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

bool query_tokens_imply_action(const std::vector<std::string>& tokens) {
  static const char* const kActions[] = {"cancel", "clear", "stop", "halt", "reset", "abort"};
  for (const auto& token : tokens) {
    for (const char* action : kActions) {
      if (token == action) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> query_tokens_from_text(const std::string& query, std::size_t max_n) {
  std::vector<std::string> out;
  std::string token;
  auto flush = [&]() {
    if (token.size() < 3) {
      token.clear();
      return;
    }
    for (char& ch : token) {
      if (ch >= 'A' && ch <= 'Z') {
        ch = static_cast<char>(ch - 'A' + 'a');
      }
    }
    if (std::find(out.begin(), out.end(), token) == out.end()) {
      out.push_back(token);
    }
    token.clear();
  };
  for (unsigned char ch : query) {
    if (std::isalnum(ch) || ch == '_') {
      token.push_back(static_cast<char>(ch));
      continue;
    }
    flush();
    if (out.size() >= max_n) {
      break;
    }
  }
  flush();
  if (out.size() > max_n) {
    out.resize(max_n);
  }
  return out;
}

float macro_facet_bonus(const EffectSlice* s, const std::unordered_set<std::string>& direct_ids,
                        const std::unordered_map<std::string, const EffectNode*>& by_id) {
  if (s == nullptr || s->query.empty()) {
    return 0.f;
  }
  const auto tokens = query_tokens_from_text(s->query, 16);
  if (tokens.empty()) {
    return 0.f;
  }
  float bonus = 0.f;
  bool zone_mutator = false;
  for (const std::string& id : direct_ids) {
    auto nit = by_id.find(id);
    if (nit == by_id.end() || nit->second->kind != EffectNodeKind::Fn) {
      continue;
    }
    const EffectNode& node = *nit->second;
    const std::string sym_lower = lower_ascii_copy(node.symbol);
    const std::string member_lower = lower_ascii_copy(node.id);
    zone_mutator = zone_mutator || symbol_has_morph_action_prefix(sym_lower);
    for (const auto& token : tokens) {
      if (token.size() < 3) {
        continue;
      }
      if (sym_lower.find(token) != std::string::npos ||
          member_lower.find(token) != std::string::npos) {
        bonus += 0.03f;
      }
    }
  }
  if (query_tokens_imply_action(tokens) && zone_mutator) {
    bonus += 0.04f;
  }
  return std::min(0.12f, bonus);
}

bool node_is_direct_writer(const EffectSlice* s, const std::string& id) {
  if (s == nullptr) {
    return false;
  }
  for (const auto& fact : s->facts) {
    if (fact.kind == EffectFactKind::Write && fact.from == id) {
      return true;
    }
  }
  return false;
}

bool is_slice_path(const std::string& rel) {
  if (rel.rfind("tests/fixtures/", 0) == 0) {
    return true;
  }
  return is_product_path(rel);
}

template <typename Fn>
void parallel_for_n(std::size_t n, Fn fn) {
  if (n == 0) {
    return;
  }
  if (n == 1) {
    fn(0);
    return;
  }
  unsigned w = std::thread::hardware_concurrency();
  if (w == 0) {
    w = 4;
  }
  if (w > n) {
    w = static_cast<unsigned>(n);
  }
  std::atomic<std::size_t> next{0};
  std::vector<std::thread> workers;
  workers.reserve(w);
  for (unsigned i = 0; i < w; ++i) {
    workers.emplace_back([&]() {
      for (;;) {
        const std::size_t idx = next.fetch_add(1);
        if (idx >= n) {
          break;
        }
        fn(idx);
      }
    });
  }
  for (auto& t : workers) {
    t.join();
  }
}

std::string stem_of(const std::string& path) {
  std::string base = path;
  const auto slash = base.find_last_of("/\\");
  if (slash != std::string::npos) {
    base = base.substr(slash + 1);
  }
  const auto colon = base.find(':');
  if (colon != std::string::npos) {
    base = base.substr(0, colon);
  }
  const auto dot = base.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return base;
}

std::string read_abs(const std::string& abs) {
  std::ifstream in(abs);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string rel_of(const std::string& workspace_root, const std::string& path) {
  if (path.empty()) {
    return {};
  }
  if (!workspace_root.empty() && path.rfind(workspace_root, 0) == 0 &&
      path.size() > workspace_root.size() &&
      (path[workspace_root.size()] == '/' || path[workspace_root.size()] == '\\')) {
    return path.substr(workspace_root.size() + 1);
  }
  return path;
}

std::string abs_of(const std::string& workspace_root, const std::string& rel) {
  if (rel.empty()) {
    return {};
  }
  fs::path p(rel);
  if (p.is_absolute()) {
    return p.lexically_normal().string();
  }
  return (fs::path(workspace_root) / rel).lexically_normal().string();
}

std::string node_text(TSNode node, const std::string& source, std::size_t max_n) {
  if (ts_node_is_null(node) || source.empty()) {
    return {};
  }
  const uint32_t a = ts_node_start_byte(node);
  const uint32_t b = ts_node_end_byte(node);
  if (b <= a || a >= source.size()) {
    return {};
  }
  std::string t = source.substr(a, std::min<std::size_t>(b - a, max_n));
  while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) {
    t.pop_back();
  }
  return t;
}

void collect_idents(TSNode node, const std::string& source, std::unordered_set<std::string>* out,
                    int depth) {
  if (out == nullptr || ts_node_is_null(node) || depth > 20) {
    return;
  }
  const char* t = ts_node_type(node);
  if (t != nullptr && (std::strcmp(t, "identifier") == 0 ||
                       std::strcmp(t, "field_identifier") == 0)) {
    const std::string id = node_text(node, source, 48);
    if (id.size() >= 2) {
      out->insert(id);
    }
  }
  const uint32_t n = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < n; ++i) {
    collect_idents(ts_node_named_child(node, i), source, out, depth + 1);
  }
}

void collect_assigns(TSNode node, const std::string& source, std::unordered_set<std::string>* out,
                     int depth) {
  if (out == nullptr || ts_node_is_null(node) || depth > 24) {
    return;
  }
  const char* t = ts_node_type(node);
  if (t != nullptr && std::strcmp(t, "assignment_expression") == 0) {
    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    collect_idents(left, source, out, 0);
  }
  const uint32_t n = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < n; ++i) {
    collect_assigns(ts_node_named_child(node, i), source, out, depth + 1);
  }
}

void collect_calls(TSNode node, const std::string& source, std::vector<std::string>* out,
                   int depth) {
  if (out == nullptr || ts_node_is_null(node) || depth > 28) {
    return;
  }
  const char* t = ts_node_type(node);
  if (t != nullptr && std::strcmp(t, "call_expression") == 0) {
    TSNode fn = ts_node_child_by_field_name(node, "function", 8);
    std::string name = node_text(fn, source, 64);
    const auto col = name.rfind("::");
    if (col != std::string::npos) {
      name = name.substr(col + 2);
    }
    const auto dot = name.rfind('.');
    if (dot != std::string::npos) {
      name = name.substr(dot + 1);
    }
    const auto sp = name.rfind('>');
    if (sp != std::string::npos) {
      name = name.substr(sp + 1);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '(')) {
      name.pop_back();
    }
    if (name.size() >= 2) {
      out->push_back(name);
    }
  }
  const uint32_t n = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < n; ++i) {
    collect_calls(ts_node_named_child(node, i), source, out, depth + 1);
  }
}

bool looks_like_lambda_call(TSNode node) {
  if (ts_node_is_null(node)) {
    return false;
  }
  const char* t = ts_node_type(node);
  return t != nullptr && std::strcmp(t, "lambda_expression") == 0;
}

void find_lambdas(TSNode node, std::vector<TSNode>* out, int depth) {
  if (out == nullptr || ts_node_is_null(node) || depth > 28) {
    return;
  }
  if (looks_like_lambda_call(node)) {
    out->push_back(node);
  }
  const uint32_t n = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < n; ++i) {
    find_lambdas(ts_node_named_child(node, i), out, depth + 1);
  }
}

std::string bare_member(const std::string& path) {
  std::string s = path;
  const auto arrow = s.rfind("->");
  if (arrow != std::string::npos) {
    s = s.substr(arrow + 2);
  }
  const auto dot = s.rfind('.');
  if (dot != std::string::npos) {
    s = s.substr(dot + 1);
  }
  return s;
}

bool noise_member(const std::string& m) {
  if (m.size() < 3) {
    return true;
  }
  if (m[0] >= 'A' && m[0] <= 'Z') {
    return true;
  }
  static const char* kSkip[] = {"this", "self",  "tmp", "end",  "begin", "empty", "size",
                                "load", "store", "lock", "unlock", "count", "data", "get",
                                "set",  "layout", "state", "mutex", "order", "acquire"};
  for (const char* s : kSkip) {
    if (m == s) {
      return true;
    }
  }
  if (m.find("memory_order") != std::string::npos) {
    return true;
  }
  return false;
}

bool cond_mentions(const std::string& cond, const std::string& ident) {
  if (ident.size() < 2 || cond.empty()) {
    return false;
  }
  std::size_t p = 0;
  while ((p = cond.find(ident, p)) != std::string::npos) {
    const bool left = p == 0 || !(std::isalnum(static_cast<unsigned char>(cond[p - 1])) ||
                                  cond[p - 1] == '_');
    const std::size_t e = p + ident.size();
    const bool right = e >= cond.size() || !(std::isalnum(static_cast<unsigned char>(cond[e])) ||
                                             cond[e] == '_');
    if (left && right) {
      return true;
    }
    p = e;
  }
  return false;
}

bool glue_fn_symbol(const std::string& name) {
  if (name.rfind("ensure_", 0) == 0 || name.rfind("bootstrap_", 0) == 0) {
    return true;
  }
  return name == "complete_cli" || name == "complete_server";
}

bool hub_member(const std::string& m) {
  static const char* kHub[] = {"kind", "path", "line", "name", "type", "index", "uri", "text",
                               "file", "data", "root", "error", "selected", "ready_",
                               "cache_valid_", "want_worker", "quoted", "open"};
  for (const char* s : kHub) {
    if (m == s) {
      return true;
    }
  }
  if (m == "stdin" || m == "stdout" || m == "stderr") {
    return true;
  }
  if (m.size() >= 3 && m.compare(m.size() - 3, 3, "_fd") == 0) {
    return true;
  }
  if (m.size() >= 4 && m.compare(m.size() - 4, 4, "_fd_") == 0) {
    return true;
  }
  return false;
}

void split_ident_tokens(const std::string& s, std::unordered_set<std::string>* out) {
  if (out == nullptr) {
    return;
  }
  std::string tok;
  auto flush = [&]() {
    if (tok.size() >= 3) {
      out->insert(tok);
    }
    tok.clear();
  };
  for (unsigned char c : s) {
    if (std::isalnum(c)) {
      tok.push_back(static_cast<char>(std::tolower(c)));
    } else {
      flush();
    }
  }
  flush();
}

bool query_unlocks_member(const std::string& query, const std::string& member) {
  if (query.empty() || member.size() < 3) {
    return false;
  }
  if (cond_mentions(query, member)) {
    return true;
  }
  std::unordered_set<std::string> qt;
  std::unordered_set<std::string> mt;
  split_ident_tokens(query, &qt);
  split_ident_tokens(member, &mt);
  for (const auto& t : mt) {
    if (qt.count(t)) {
      return true;
    }
  }
  static const std::pair<const char*, const char*> kAlias[] = {
      {"path", "ruta archivo file filepath uri"},
      {"kind", "tipo clase"},
      {"line", "linea"},
      {"name", "nombre"},
      {"type", "tipo"},
      {"index", "indice"},
      {"uri", "ruta url"},
      {"file", "archivo"},
      {"text", "texto"},
      {"root", "raiz raíz workspace"},
      {"stdin_fd_", "stdin descriptor fd pipe"},
      {"stdout_fd_", "stdout descriptor fd pipe"},
      {"stderr_fd_", "stderr descriptor fd pipe"},
      {"ready_", "ready responder responde listo respuesta"},
      {"error", "error fallo failed"},
      {"selected", "seleccionado seleccion selected tab overlay"},
      {"cache_valid_", "cache pty terminal pantalla"},
      {"want_worker", "worker async job cola"},
      {"quoted", "quote shell escaped"},
      {"open", "abrir open picker overlay"},
  };
  for (const auto& a : kAlias) {
    if (member != a.first) {
      continue;
    }
    std::unordered_set<std::string> aliases;
    split_ident_tokens(a.second, &aliases);
    for (const auto& t : aliases) {
      if (qt.count(t) || cond_mentions(query, t)) {
        return true;
      }
    }
  }
  return false;
}

std::string latch_member_key(const std::string& symbol_or_id) {
  std::string m = symbol_or_id;
  if (m.rfind("latch:", 0) == 0) {
    m = m.substr(6);
  }
  const auto c = m.rfind(':');
  if (c != std::string::npos && m.find('/') == std::string::npos) {
    m = m.substr(c + 1);
  }
  return m;
}

bool latch_muted(const EffectSlice* s, const std::string& member) {
  if (s == nullptr || !hub_member(member)) {
    return false;
  }
  if (query_unlocks_member(s->query, member)) {
    return false;
  }
  return s->unlocked_members.count(member) == 0;
}

TSTree* parse_file(const std::string& source, const std::string& path) {
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

std::string fn_node_name(TSNode fn, const std::string& source) {
  TSNode decl = ts_node_child_by_field_name(fn, "declarator", 10);
  if (ts_node_is_null(decl)) {
    return {};
  }
  std::function<std::string(TSNode)> walk;
  walk = [&](TSNode n) -> std::string {
    if (ts_node_is_null(n)) {
      return {};
    }
    const char* t = ts_node_type(n);
    if (t != nullptr && (std::strcmp(t, "identifier") == 0 ||
                         std::strcmp(t, "field_identifier") == 0)) {
      return node_text(n, source, 64);
    }
    const uint32_t c = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < c; ++i) {
      const std::string got = walk(ts_node_named_child(n, i));
      if (!got.empty()) {
        return got;
      }
    }
    return {};
  };
  return walk(decl);
}

void each_switch_case(TSNode sw, const std::function<void(TSNode)>& fn) {
  if (ts_node_is_null(sw) || !fn) {
    return;
  }
  TSNode body = ts_node_child_by_field_name(sw, "body", 4);
  if (ts_node_is_null(body)) {
    body = sw;
  }
  std::function<void(TSNode, int)> walk;
  walk = [&](TSNode n, int depth) {
    if (ts_node_is_null(n) || depth > 8) {
      return;
    }
    const char* t = ts_node_type(n);
    if (t != nullptr && std::strcmp(t, "case_statement") == 0) {
      fn(n);
      return;
    }
    const uint32_t c = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < c; ++i) {
      walk(ts_node_named_child(n, i), depth + 1);
    }
  };
  walk(body, 0);
}

TSNode find_fn_body(TSNode root, const std::string& source, const std::string& symbol,
                    int* start_line) {
  TSNode found{};
  std::function<void(TSNode)> walk;
  walk = [&](TSNode n) {
    if (!ts_node_is_null(found) || ts_node_is_null(n)) {
      return;
    }
    const char* t = ts_node_type(n);
    if (t != nullptr && std::strcmp(t, "function_definition") == 0) {
      const std::string name = fn_node_name(n, source);
      if (name == symbol) {
        found = n;
        if (start_line != nullptr) {
          *start_line = static_cast<int>(ts_node_start_point(n).row) + 1;
        }
        return;
      }
    }
    const uint32_t c = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < c; ++i) {
      walk(ts_node_named_child(n, i));
    }
  };
  walk(root);
  if (ts_node_is_null(found)) {
    return {};
  }
  TSNode body = ts_node_child_by_field_name(found, "body", 4);
  return body;
}

float fact_weight(EffectFactKind k) {
  switch (k) {
    case EffectFactKind::Call:
      return 1.f;
    case EffectFactKind::EnterCtrl:
    case EffectFactKind::Contains:
      return 1.f;
    case EffectFactKind::Then:
    case EffectFactKind::Case:
      return 1.4f;
    case EffectFactKind::Else:
    case EffectFactKind::Default:
      return 1.1f;
    case EffectFactKind::Fallthrough:
      return 1.f;
    case EffectFactKind::Wrap:
      return 1.5f;
    case EffectFactKind::Write:
    case EffectFactKind::Read:
      return 2.f;
    case EffectFactKind::Handoff:
      return 1.6f;
  }
  return 1.f;
}

std::string fn_id(const std::string& path, const std::string& symbol) {
  return "fn:" + path + ":" + symbol;
}
std::string ctrl_id(const std::string& path, int line, const std::string& kind) {
  return "ctrl:" + path + ":" + std::to_string(line) + ":" + kind;
}
std::string latch_id(const std::string& member) { return "latch:" + member; }
std::string handoff_id(const std::string& path, int line) {
  return "handoff:" + path + ":" + std::to_string(line);
}

EffectNode* find_mut(EffectSlice* s, const std::string& id) {
  if (s == nullptr) {
    return nullptr;
  }
  for (auto& n : s->nodes) {
    if (n.id == id) {
      return &n;
    }
  }
  return nullptr;
}

bool has_fact(const EffectSlice& s, const std::string& from, const std::string& to,
              EffectFactKind k) {
  for (const auto& f : s.facts) {
    if (f.from == from && f.to == to && f.kind == k) {
      return true;
    }
  }
  return false;
}

bool add_node(EffectSlice* s, EffectNode n) {
  if (s == nullptr || n.id.empty()) {
    return false;
  }
  if (find_mut(s, n.id) != nullptr) {
    return false;
  }
  const int fn_n = effect_slice_count_kind(*s, EffectNodeKind::Fn);
  if (n.kind == EffectNodeKind::Fn) {
    if (fn_n >= kEffectSliceMaxNodes) {
      return false;
    }
  } else if (static_cast<int>(s->nodes.size()) >= kEffectSliceMaxNodes + 400) {
    return false;
  }
  if (n.stem.empty()) {
    n.stem = stem_of(n.path.empty() ? n.symbol : n.path);
  }
  if (n.anchor.empty()) {
    if (n.kind == EffectNodeKind::Fn) {
      n.anchor = n.path + ":" + n.symbol;
    } else if (n.kind == EffectNodeKind::Latch) {
      n.anchor = n.id;
    } else {
      n.anchor = n.path + ":" + std::to_string(n.line);
    }
  }
  s->nodes.push_back(std::move(n));
  return true;
}

bool add_fact(EffectSlice* s, EffectFact f) {
  if (s == nullptr || f.from.empty() || f.to.empty() || f.from == f.to) {
    return false;
  }
  if (has_fact(*s, f.from, f.to, f.kind)) {
    return false;
  }
  const bool vital = f.kind == EffectFactKind::Write || f.kind == EffectFactKind::Read ||
                     f.kind == EffectFactKind::Handoff;
  if (!vital && static_cast<int>(s->facts.size()) >= kEffectSliceMaxFacts) {
    return false;
  }
  if (vital && static_cast<int>(s->facts.size()) >= kEffectSliceMaxFacts + 2000) {
    return false;
  }
  f.id = "e" + std::to_string(s->facts.size() + 1);
  if (f.w_edge <= 0.f) {
    f.w_edge = fact_weight(f.kind);
  }
  s->facts.push_back(std::move(f));
  return true;
}

std::string file_src(EffectSlice* s, const EffectSliceDeps& deps, const std::string& rel) {
  if (s == nullptr || rel.empty()) {
    return {};
  }
  auto it = s->file_source.find(rel);
  if (it != s->file_source.end()) {
    return it->second;
  }
  const std::string src = read_abs(abs_of(deps.workspace_root, rel));
  s->file_source[rel] = src;
  return src;
}

std::vector<std::pair<std::string, int>> file_fns(EffectSlice* s, const EffectSliceDeps& deps,
                                                  const std::string& rel) {
  std::vector<std::pair<std::string, int>> out;
  const std::string source = file_src(s, deps, rel);
  if (source.empty()) {
    return out;
  }
  TSTree* tree = parse_file(source, rel);
  if (tree == nullptr) {
    return out;
  }
  const auto syms = extract_symbols_from_tree(ts_tree_root_node(tree), source, rel);
  ts_tree_delete(tree);
  std::unordered_set<std::string> seen;
  for (const auto& sym : syms) {
    if (sym.kind != SymbolKind::kFunction && sym.kind != SymbolKind::kMethod) {
      continue;
    }
    std::string name = symbol_insert_name(sym.name);
    const auto col = name.rfind("::");
    if (col != std::string::npos) {
      name = name.substr(col + 2);
    }
    if (name.empty() || !seen.insert(name).second) {
      continue;
    }
    out.push_back({name, sym.line});
  }
  return out;
}

struct FnMentions {
  std::vector<std::string> writes;
  std::vector<std::string> reads;
  std::vector<std::string> calls;
  std::unordered_set<std::string> cond_idents;
  int start_line = 0;
};

EffectFnMentions to_stored(const FnMentions& m) {
  EffectFnMentions o;
  o.writes = m.writes;
  o.reads = m.reads;
  o.calls = m.calls;
  return o;
}

bool skip_call_name(const std::string& name) {
  if (name.size() < 3) {
    return true;
  }
  static const char* kSkip[] = {
      "sizeof", "push_back", "emplace_back", "insert", "erase",  "find",
      "begin",  "end",       "size",         "empty",  "move",   "forward",
      "make_shared", "make_unique", "to_string", "substr", "append"};
  for (const char* s : kSkip) {
    if (name == s) {
      return true;
    }
  }
  return false;
}

FnMentions summarize_fn_compute(const EffectSlice& s, const EffectSliceDeps& deps,
                                const EffectNode& fn) {
  FnMentions m;
  if (fn.path.empty() || fn.symbol.empty()) {
    return m;
  }
  const std::string abs = abs_of(deps.workspace_root, fn.path);
  EffectSummaryOpts opts;
  opts.seeds = s.seeds;
  opts.query = s.query;
  opts.hint_line = fn.line;
  const EffectSummary es = effect_summary_build(abs, fn.path, fn.symbol, "", opts);
  m.writes = es.writes;
  m.reads = es.reads;
  m.start_line = es.start_line;
  std::unordered_set<std::string> callset;
  for (const auto& c : es.calls) {
    if (!skip_call_name(c)) {
      callset.insert(c);
    }
  }
  for (const auto& c : es.calls_seed) {
    if (!skip_call_name(c)) {
      callset.insert(c);
    }
  }
  const std::string source = read_abs(abs);
  TSTree* tree = parse_file(source, fn.path);
  if (tree != nullptr) {
    int start = fn.line;
    TSNode body = find_fn_body(ts_tree_root_node(tree), source, fn.symbol, &start);
    std::vector<std::string> more;
    collect_calls(body, source, &more, 0);
    if (start > 0) {
      m.start_line = start;
    }
    for (const auto& c : more) {
      if (!skip_call_name(c)) {
        callset.insert(c);
      }
    }
    ts_tree_delete(tree);
  }
  m.calls.assign(callset.begin(), callset.end());
  return m;
}

void summarize_fn(EffectSlice* s, const EffectSliceDeps& deps, EffectNode* fn) {
  if (s == nullptr || fn == nullptr || fn->path.empty() || fn->symbol.empty()) {
    return;
  }
  const std::string key = fn->id;
  if (s->summarized.count(key)) {
    return;
  }
  s->summarized.insert(key);
  FnMentions m = summarize_fn_compute(*s, deps, *fn);
  if (fn->line <= 0 && m.start_line > 0) {
    fn->line = m.start_line;
  }
  s->mentions[fn->id] = to_stored(m);
}

void add_file_inventory(EffectSlice* s, const EffectSliceDeps& deps, const std::string& rel) {
  if (s == nullptr || rel.empty()) {
    return;
  }
  const auto fns = file_fns(s, deps, rel);
  for (const auto& [name, line] : fns) {
    if (static_cast<int>(s->nodes.size()) >= kEffectSliceMaxNodes) {
      break;
    }
    EffectNode n;
    n.id = fn_id(rel, name);
    n.kind = EffectNodeKind::Fn;
    n.path = rel;
    n.symbol = name;
    n.line = line;
    n.stem = stem_of(rel);
    add_node(s, std::move(n));
  }
}

void add_siblings(EffectSlice* s, const EffectSliceDeps& deps, const std::string& rel) {
  if (s == nullptr || rel.empty() || !s->add_siblings) {
    return;
  }
  add_file_inventory(s, deps, rel);
}

void ingest_caller_hits(EffectSlice* s, const EffectSliceDeps& deps, const EffectNode& callee,
                        const std::vector<ATrailSearchHit>& hits) {
  if (s == nullptr || callee.symbol.empty()) {
    return;
  }
  int added = 0;
  for (const auto& h : hits) {
    if (added >= kEffectSliceMaxCallersPerFn) {
      break;
    }
    std::string rel = rel_of(deps.workspace_root, h.path);
    if (rel.empty() || !is_slice_path(rel)) {
      continue;
    }
    const std::string abs = abs_of(deps.workspace_root, rel);
    const ATrailHop hop = a_trail_enrich_hop(abs, rel, h.line, callee.symbol);
    if (!hop.is_call_site || hop.symbol.empty() || hop.symbol == callee.symbol) {
      continue;
    }
    EffectNode n;
    n.id = fn_id(rel, hop.symbol);
    n.kind = EffectNodeKind::Fn;
    n.path = rel;
    n.symbol = hop.symbol;
    n.line = hop.call_line;
    n.stem = stem_of(rel);
    add_node(s, n);
    EffectFact f;
    f.from = n.id;
    f.to = callee.id;
    f.kind = EffectFactKind::Call;
    if (add_fact(s, f)) {
      ++added;
    }
  }
}

void ensure_callers(EffectSlice* s, const EffectSliceDeps& deps, const EffectNode& callee) {
  if (s == nullptr || !deps.search || callee.symbol.empty()) {
    return;
  }
  if (!s->searched.insert(callee.symbol).second) {
    return;
  }
  ingest_caller_hits(s, deps, callee, deps.search(callee.symbol));
}

std::string fn_in_slice_id(const EffectSlice& s, const std::string& name) {
  for (const auto& n : s.nodes) {
    if (n.kind == EffectNodeKind::Fn && n.symbol == name) {
      return n.id;
    }
  }
  return {};
}

void hop_down_from(EffectSlice* s, const EffectSliceDeps& deps,
                   const std::vector<std::string>& origin_ids) {
  if (s == nullptr) {
    return;
  }
  std::vector<std::string> unresolved;
  std::unordered_set<std::string> seen_unresolved;
  for (const auto& id : origin_ids) {
    auto it = s->mentions.find(id);
    if (it == s->mentions.end()) {
      continue;
    }
    for (const auto& c : it->second.calls) {
      if (skip_call_name(c)) {
        continue;
      }
      const std::string to = fn_in_slice_id(*s, c);
      if (to.empty() && seen_unresolved.insert(c).second) {
        unresolved.push_back(c);
      }
    }
  }
  if (deps.search && !unresolved.empty()) {
    std::vector<std::vector<ATrailSearchHit>> bags(unresolved.size());
    parallel_for_n(unresolved.size(), [&](std::size_t i) {
      bags[i] = deps.search(unresolved[i]);
    });
    for (std::size_t i = 0; i < unresolved.size(); ++i) {
      const std::string& name = unresolved[i];
      if (!fn_in_slice_id(*s, name).empty()) {
        continue;
      }
      for (const auto& h : bags[i]) {
        std::string rel = rel_of(deps.workspace_root, h.path);
        if (rel.empty() || !is_slice_path(rel)) {
          continue;
        }
        const auto fns = file_fns(s, deps, rel);
        bool added = false;
        for (const auto& [fn, line] : fns) {
          if (fn != name) {
            continue;
          }
          EffectNode n;
          n.id = fn_id(rel, name);
          n.kind = EffectNodeKind::Fn;
          n.path = rel;
          n.symbol = name;
          n.line = line;
          n.stem = stem_of(rel);
          add_node(s, n);
          added = true;
          break;
        }
        if (added) {
          break;
        }
      }
    }
  }
  for (const auto& id : origin_ids) {
    auto it = s->mentions.find(id);
    if (it == s->mentions.end()) {
      continue;
    }
    for (const auto& c : it->second.calls) {
      if (skip_call_name(c)) {
        continue;
      }
      std::string to = fn_in_slice_id(*s, c);
      if (to.empty()) {
        continue;
      }
      EffectFact f;
      f.from = id;
      f.to = to;
      f.kind = EffectFactKind::Call;
      add_fact(s, f);
    }
  }
}

void hop_up_from(EffectSlice* s, const EffectSliceDeps& deps,
                const std::vector<std::string>& origin_ids) {
  if (s == nullptr || !deps.search) {
    return;
  }
  std::vector<std::string> symbols;
  std::vector<EffectNode> callees;
  std::unordered_set<std::string> seen_sym;
  for (const auto& id : origin_ids) {
    const EffectNode* n = effect_slice_find_node(*s, id);
    if (n == nullptr || n->kind != EffectNodeKind::Fn || n->symbol.empty()) {
      continue;
    }
    if (!s->searched.insert(n->symbol).second) {
      continue;
    }
    if (!seen_sym.insert(n->symbol).second) {
      continue;
    }
    symbols.push_back(n->symbol);
    callees.push_back(*n);
  }
  if (symbols.empty()) {
    return;
  }
  std::vector<std::vector<ATrailSearchHit>> bags(symbols.size());
  parallel_for_n(symbols.size(), [&](std::size_t i) { bags[i] = deps.search(symbols[i]); });
  for (std::size_t i = 0; i < callees.size(); ++i) {
    ingest_caller_hits(s, deps, callees[i], bags[i]);
  }
}

void summarize_ids_parallel(EffectSlice* s, const EffectSliceDeps& deps,
                            const std::vector<std::string>& ids) {
  if (s == nullptr) {
    return;
  }
  std::vector<std::size_t> work;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const EffectNode* n = effect_slice_find_node(*s, ids[i]);
    if (n == nullptr || n->kind != EffectNodeKind::Fn || n->path.empty() || n->symbol.empty()) {
      continue;
    }
    if (s->summarized.count(n->id)) {
      continue;
    }
    work.push_back(i);
  }
  if (work.empty()) {
    return;
  }
  struct Bag {
    std::string id;
    int line = 0;
    FnMentions m;
  };
  std::vector<Bag> bags(work.size());
  parallel_for_n(work.size(), [&](std::size_t wi) {
    const EffectNode* n = effect_slice_find_node(*s, ids[work[wi]]);
    if (n == nullptr) {
      return;
    }
    bags[wi].id = n->id;
    bags[wi].m = summarize_fn_compute(*s, deps, *n);
    bags[wi].line = n->line > 0 ? n->line : bags[wi].m.start_line;
  });
  for (auto& b : bags) {
    if (b.id.empty()) {
      continue;
    }
    s->summarized.insert(b.id);
    s->mentions[b.id] = to_stored(b.m);
    EffectNode* n = find_mut(s, b.id);
    if (n != nullptr && n->line <= 0 && b.line > 0) {
      n->line = b.line;
    }
  }
}

void rebuild_latches_and_ctrl(EffectSlice* s, const EffectSliceDeps& deps) {
  if (s == nullptr) {
    return;
  }
  std::unordered_map<std::string, int> fns_per_file;
  std::unordered_set<std::string> dense_paths;
  for (const auto& n0 : s->nodes) {
    if (n0.kind != EffectNodeKind::Fn || n0.path.empty()) {
      continue;
    }
    ++fns_per_file[n0.path];
    if (n0.seed) {
      dense_paths.insert(n0.path);
    }
  }
  for (const auto& p : s->inventory_paths) {
    dense_paths.insert(p);
  }
  for (const auto& [p, n] : fns_per_file) {
    if (n >= 2) {
      dense_paths.insert(p);
    }
  }
  std::unordered_map<std::string, std::unordered_set<std::string>> member_fns;
  auto note = [&](const std::string& fn, const std::string& raw) {
    const std::string m = bare_member(raw);
    if (noise_member(m)) {
      return;
    }
    member_fns[m].insert(fn);
  };
  for (const auto& n : s->nodes) {
    if (n.kind != EffectNodeKind::Fn) {
      continue;
    }
    const auto it = s->mentions.find(n.id);
    if (it == s->mentions.end()) {
      continue;
    }
    for (const auto& w : it->second.writes) {
      note(n.id, w);
    }
    for (const auto& r : it->second.reads) {
      note(n.id, r);
    }
  }

  std::unordered_set<std::string> latch_members;
  for (const auto& [m, fns] : member_fns) {
    if (fns.size() >= 2) {
      latch_members.insert(m);
      EffectNode ln;
      ln.id = latch_id(m);
      ln.kind = EffectNodeKind::Latch;
      ln.symbol = m;
      add_node(s, ln);
    }
  }

  auto fn_in_slice = [&](const std::string& name) -> std::string {
    for (const auto& n : s->nodes) {
      if (n.kind == EffectNodeKind::Fn && n.symbol == name) {
        return n.id;
      }
    }
    return {};
  };

  std::vector<std::string> fn_ids;
  for (const auto& n0 : s->nodes) {
    if (n0.kind == EffectNodeKind::Fn && !n0.path.empty()) {
      fn_ids.push_back(n0.id);
    }
  }
  for (const auto& fnid : fn_ids) {
    const EffectNode* np = effect_slice_find_node(*s, fnid);
    if (np == nullptr) {
      continue;
    }
    const EffectNode n = *np;
    const auto mit = s->mentions.find(n.id);
    if (mit != s->mentions.end()) {
      for (const auto& w : mit->second.writes) {
        const std::string m = bare_member(w);
        if (!latch_members.count(m)) {
          continue;
        }
        EffectFact f;
        f.from = n.id;
        f.to = latch_id(m);
        f.kind = EffectFactKind::Write;
        f.member = m;
        add_fact(s, f);
      }
      for (const auto& c : mit->second.calls) {
        const std::string to = fn_in_slice(c);
        if (to.empty()) {
          continue;
        }
        EffectFact f;
        f.from = n.id;
        f.to = to;
        f.kind = EffectFactKind::Call;
        add_fact(s, f);
      }
    }

    if (dense_paths.find(n.path) == dense_paths.end()) {
      continue;
    }

    const std::string source = file_src(s, deps, n.path);
    if (source.empty()) {
      continue;
    }
    TSTree* tree = parse_file(source, n.path);
    if (tree == nullptr) {
      continue;
    }
    int start = n.line;
    TSNode body = find_fn_body(ts_tree_root_node(tree), source, n.symbol, &start);
    if (ts_node_is_null(body)) {
      ts_tree_delete(tree);
      continue;
    }

    int ctrl_n = 0;
    std::unordered_set<uint32_t> seen_bytes;
    std::function<void(TSNode)> walk_ctrl;
    walk_ctrl = [&](TSNode node) {
      if (ts_node_is_null(node) || ctrl_n >= kEffectSliceMaxCtrlPerFn) {
        return;
      }
      const uint32_t sb = ts_node_start_byte(node);
      if (!seen_bytes.insert(sb).second) {
        return;
      }
      const char* t = ts_node_type(node);
      const bool is_if = t != nullptr && std::strcmp(t, "if_statement") == 0;
      const bool is_sw = t != nullptr && std::strcmp(t, "switch_statement") == 0;
      const bool is_loop =
          t != nullptr && (std::strcmp(t, "while_statement") == 0 ||
                           std::strcmp(t, "for_statement") == 0 ||
                           std::strcmp(t, "for_range_loop") == 0 ||
                           std::strcmp(t, "do_statement") == 0);
      if (is_if || is_sw || is_loop) {
        TSNode cond = ts_node_child_by_field_name(node, "condition", 9);
        const std::string cond_txt = node_text(cond, source, 160);
        std::vector<std::string> then_calls;
        TSNode then_n = ts_node_child_by_field_name(node, "consequence", 12);
        if (ts_node_is_null(then_n)) {
          then_n = ts_node_child_by_field_name(node, "body", 4);
        }
        collect_calls(then_n, source, &then_calls, 0);
        TSNode else_n = ts_node_child_by_field_name(node, "alternative", 11);
        std::vector<std::string> else_calls;
        collect_calls(else_n, source, &else_calls, 0);

        std::string hit_latch;
        for (const auto& m : latch_members) {
          if (cond_mentions(cond_txt, m)) {
            hit_latch = m;
            break;
          }
        }
        bool hits_slice_call = false;
        auto touch_call = [&](const std::string& c) {
          if (!fn_in_slice(c).empty()) {
            hits_slice_call = true;
          }
        };
        for (const auto& c : then_calls) {
          touch_call(c);
        }
        for (const auto& c : else_calls) {
          touch_call(c);
        }

        if (is_sw) {
          each_switch_case(node, [&](TSNode ch) {
            std::vector<std::string> case_calls;
            collect_calls(ch, source, &case_calls, 0);
            for (const auto& c : case_calls) {
              touch_call(c);
            }
          });
        }

        if (!hit_latch.empty() || hits_slice_call) {
          const int line = static_cast<int>(ts_node_start_point(node).row) + 1;
          EffectCtrlKind ck = EffectCtrlKind::If;
          if (is_sw) {
            ck = EffectCtrlKind::Switch;
          } else if (is_loop) {
            ck = EffectCtrlKind::Loop;
          } else {
            bool only_return = false;
            if (!ts_node_is_null(then_n)) {
              // guard: consequence is (or starts with) return
              std::function<bool(TSNode)> has_ret;
              has_ret = [&](TSNode x) -> bool {
                if (ts_node_is_null(x)) {
                  return false;
                }
                const char* xt = ts_node_type(x);
                if (xt != nullptr && std::strcmp(xt, "return_statement") == 0) {
                  return true;
                }
                const uint32_t xc = ts_node_named_child_count(x);
                for (uint32_t i = 0; i < xc && i < 3; ++i) {
                  if (has_ret(ts_node_named_child(x, i))) {
                    return true;
                  }
                }
                return false;
              };
              only_return = has_ret(then_n) && then_calls.empty();
            }
            if (only_return) {
              ck = EffectCtrlKind::Guard;
            }
          }
          EffectNode cn;
          cn.id = ctrl_id(n.path, line, effect_ctrl_kind_name(ck));
          cn.kind = EffectNodeKind::Ctrl;
          cn.ctrl_kind = ck;
          cn.path = n.path;
          cn.line = line;
          cn.parent_fn = n.id;
          cn.cond = cond_txt;
          cn.stem = n.stem;
          if (add_node(s, cn)) {
            ++ctrl_n;
          }
          EffectFact contains;
          contains.from = n.id;
          contains.to = cn.id;
          contains.kind = EffectFactKind::Contains;
          add_fact(s, contains);
          EffectFact enter;
          enter.from = n.id;
          enter.to = cn.id;
          enter.kind = EffectFactKind::EnterCtrl;
          add_fact(s, enter);
          if (!hit_latch.empty()) {
            EffectFact rd;
            rd.from = latch_id(hit_latch);
            rd.to = cn.id;
            rd.kind = EffectFactKind::Read;
            rd.member = hit_latch;
            add_fact(s, rd);
          }
          auto emit_then = [&](const std::vector<std::string>& calls, EffectFactKind fk) {
            for (const auto& c : calls) {
              const std::string to = fn_in_slice(c);
              if (to.empty()) {
                continue;
              }
              EffectFact th;
              th.from = cn.id;
              th.to = to;
              th.kind = fk;
              add_fact(s, th);
            }
          };
          emit_then(then_calls, EffectFactKind::Then);
          emit_then(else_calls, EffectFactKind::Else);
          std::unordered_set<std::string> then_writes;
          collect_assigns(then_n, source, &then_writes, 0);
          for (const auto& id : then_writes) {
            const std::string m = bare_member(id);
            if (!latch_members.count(m)) {
              continue;
            }
            EffectFact tw;
            tw.from = cn.id;
            tw.to = latch_id(m);
            tw.kind = EffectFactKind::Write;
            tw.member = m;
            add_fact(s, tw);
          }

          if (is_sw) {
            each_switch_case(node, [&](TSNode ch) {
              std::vector<std::string> case_calls;
              collect_calls(ch, source, &case_calls, 0);
              bool useful = false;
              for (const auto& c : case_calls) {
                if (!fn_in_slice(c).empty()) {
                  useful = true;
                }
              }
              std::unordered_set<std::string> ids;
              collect_idents(ch, source, &ids, 0);
              for (const auto& id : ids) {
                if (latch_members.count(id)) {
                  useful = true;
                }
              }
              if (!useful) {
                return;
              }
              const int cline = static_cast<int>(ts_node_start_point(ch).row) + 1;
              EffectNode cas;
              cas.id = ctrl_id(n.path, cline, "case");
              cas.kind = EffectNodeKind::Ctrl;
              cas.ctrl_kind = EffectCtrlKind::Case;
              cas.path = n.path;
              cas.line = cline;
              cas.parent_fn = n.id;
              cas.parent_switch = cn.id;
              cas.stem = n.stem;
              cas.cond = node_text(ch, source, 80);
              add_node(s, cas);
              EffectFact swc;
              swc.from = cn.id;
              swc.to = cas.id;
              swc.kind = EffectFactKind::Contains;
              add_fact(s, swc);
              EffectFact cse;
              cse.from = cn.id;
              cse.to = cas.id;
              cse.kind = EffectFactKind::Case;
              add_fact(s, cse);
              emit_then(case_calls, EffectFactKind::Then);
              for (const auto& c : case_calls) {
                const std::string to = fn_in_slice(c);
                if (to.empty()) {
                  continue;
                }
                EffectFact th;
                th.from = cas.id;
                th.to = to;
                th.kind = EffectFactKind::Then;
                add_fact(s, th);
              }
            });
          }
        }
      }

      // Handoff: lambda in this fn that calls a slice fn.
      if (t != nullptr && std::strcmp(t, "lambda_expression") == 0) {
        std::vector<std::string> lam_calls;
        collect_calls(node, source, &lam_calls, 0);
        for (const auto& c : lam_calls) {
          const std::string to = fn_in_slice(c);
          if (to.empty()) {
            continue;
          }
          const int line = static_cast<int>(ts_node_start_point(node).row) + 1;
          EffectNode hn;
          hn.id = handoff_id(n.path, line);
          hn.kind = EffectNodeKind::Handoff;
          hn.path = n.path;
          hn.line = line;
          hn.parent_fn = n.id;
          hn.stem = n.stem;
          hn.symbol = c;
          add_node(s, hn);
          EffectFact hf;
          hf.from = n.id;
          hf.to = hn.id;
          hf.kind = EffectFactKind::Handoff;
          add_fact(s, hf);
          EffectFact ht;
          ht.from = hn.id;
          ht.to = to;
          ht.kind = EffectFactKind::Call;
          add_fact(s, ht);
        }
      }

      const uint32_t c = ts_node_named_child_count(node);
      for (uint32_t i = 0; i < c; ++i) {
        walk_ctrl(ts_node_named_child(node, i));
      }
    };
    walk_ctrl(body);
    ts_tree_delete(tree);
  }
}

void run_ppr(EffectSlice* s) {
  if (s == nullptr || s->nodes.empty()) {
    return;
  }
  const int n = static_cast<int>(s->nodes.size());
  std::unordered_map<std::string, int> idx;
  for (int i = 0; i < n; ++i) {
    idx[s->nodes[static_cast<std::size_t>(i)].id] = i;
  }
  std::vector<float> restart(static_cast<std::size_t>(n), 0.f);
  std::unordered_map<std::string, int> latch_poles;
  for (const auto& f : s->facts) {
    if (f.kind == EffectFactKind::Write || f.kind == EffectFactKind::Read) {
      ++latch_poles[f.member.empty() ? f.to : f.member];
    }
  }
  float rsum = 0.f;
  for (int i = 0; i < n; ++i) {
    const auto& node = s->nodes[static_cast<std::size_t>(i)];
    float r = 0.f;
    if (node.prior_sem > 0.f) {
      r += 3.f * node.prior_sem;
    }
    if (node.kind == EffectNodeKind::Latch) {
      const std::string mem = latch_member_key(node.symbol.empty() ? node.id : node.symbol);
      if (!latch_muted(s, mem)) {
        const int poles = std::max(latch_poles[node.symbol], 0);
        if (poles >= 2) {
          r += 0.15f;
        }
      }
    }
    if (node.kind == EffectNodeKind::Ctrl) {
      for (const auto& f : s->facts) {
        if (f.to == node.id && f.kind == EffectFactKind::Read) {
          r += 0.15f;
          break;
        }
      }
    }
    if (node.kind == EffectNodeKind::Handoff) {
      r += 0.15f;
    }
    if (node.seed) {
      r += 0.4f;
    }
    restart[static_cast<std::size_t>(i)] = r;
    rsum += r;
  }
  if (rsum <= 1e-9f) {
    const float u = 1.f / static_cast<float>(n);
    for (float& v : restart) {
      v = u;
    }
  } else {
    for (float& v : restart) {
      v /= rsum;
    }
  }

  std::vector<std::vector<std::pair<int, float>>> outg(static_cast<std::size_t>(n));
  std::vector<float> outw(static_cast<std::size_t>(n), 0.f);
  for (const auto& f : s->facts) {
    auto a = idx.find(f.from);
    auto b = idx.find(f.to);
    if (a == idx.end() || b == idx.end()) {
      continue;
    }
    outg[static_cast<std::size_t>(a->second)].push_back({b->second, f.w_edge});
    outw[static_cast<std::size_t>(a->second)] += f.w_edge;
  }

  // Selective upstream return: a polarity-complete latch may return part of its
  // light to semantically relevant writers. Keep calls/control edges directed;
  // reversing those rewards broad hubs rather than causal state transitions.
  std::unordered_map<std::string, std::unordered_set<std::string>> latch_writers;
  std::unordered_set<std::string> latches_with_readers;
  for (const auto& f : s->facts) {
    if (f.kind == EffectFactKind::Write) {
      latch_writers[f.to].insert(f.from);
    } else if (f.kind == EffectFactKind::Read) {
      latches_with_readers.insert(f.from);
    }
  }
  for (const auto& f : s->facts) {
    if (f.kind != EffectFactKind::Write || !latches_with_readers.count(f.to)) {
      continue;
    }
    const auto wit = latch_writers.find(f.to);
    if (wit == latch_writers.end() || wit->second.size() < 2) {
      continue;
    }
    const auto latch_it = idx.find(f.to);
    const auto writer_it = idx.find(f.from);
    if (latch_it == idx.end() || writer_it == idx.end()) {
      continue;
    }
    const EffectNode& writer = s->nodes[static_cast<std::size_t>(writer_it->second)];
    if (!writer.seed && writer.prior_sem < 0.50f) {
      continue;
    }
    const float polarity =
        std::min(1.f, 0.25f * static_cast<float>(wit->second.size()));
    const float semantic = std::clamp(writer.prior_sem, 0.f, 1.f);
    const float reverse_weight =
        f.w_edge * 0.35f * polarity * (0.35f + 0.65f * semantic);
    outg[static_cast<std::size_t>(latch_it->second)].push_back(
        {writer_it->second, reverse_weight});
    outw[static_cast<std::size_t>(latch_it->second)] += reverse_weight;
  }

  const float d = kEffectSlicePprDamp;
  auto diffuse = [&](const std::vector<float>& teleport) {
    std::vector<float> mass = teleport;
    for (int it = 0; it < kEffectSlicePprIters; ++it) {
      std::vector<float> next(static_cast<std::size_t>(n), 0.f);
      for (int i = 0; i < n; ++i) {
        next[static_cast<std::size_t>(i)] +=
            (1.f - d) * teleport[static_cast<std::size_t>(i)];
      }
      for (int i = 0; i < n; ++i) {
        const float m = mass[static_cast<std::size_t>(i)];
        if (m <= 0.f) {
          continue;
        }
        const float ow = outw[static_cast<std::size_t>(i)];
        if (ow <= 1e-9f) {
          for (int j = 0; j < n; ++j) {
            next[static_cast<std::size_t>(j)] +=
                d * m * teleport[static_cast<std::size_t>(j)];
          }
          continue;
        }
        for (const auto& [j, w] : outg[static_cast<std::size_t>(i)]) {
          next[static_cast<std::size_t>(j)] += d * m * (w / ow);
        }
      }
      mass.swap(next);
    }
    return mass;
  };

  std::vector<float> mass = diffuse(restart);
  for (int i = 0; i < n; ++i) {
    auto& node = s->nodes[static_cast<std::size_t>(i)];
    node.mass = mass[static_cast<std::size_t>(i)];
    if (node.kind == EffectNodeKind::Latch) {
      const std::string mem = latch_member_key(node.symbol.empty() ? node.id : node.symbol);
      if (latch_muted(s, mem)) {
        node.mass *= 0.15f;
      }
    } else if (node.kind == EffectNodeKind::Fn && glue_fn_symbol(node.symbol)) {
      node.mass *= 0.4f;
    }
  }
}

void rank_constellations(EffectSlice* s, int k) {
  if (s == nullptr) {
    return;
  }
  s->constellations.clear();
  if (s->nodes.empty() || k <= 0) {
    return;
  }

  std::unordered_map<std::string, const EffectNode*> by_id;
  float total_mass = 0.f;
  for (const auto& n : s->nodes) {
    by_id[n.id] = &n;
    if (!n.cold) {
      total_mass += std::max(0.f, n.mass);
    }
  }

  auto add_unique = [](std::vector<std::string>* dst, const std::string& value) {
    if (dst != nullptr && !value.empty() &&
        std::find(dst->begin(), dst->end(), value) == dst->end()) {
      dst->push_back(value);
    }
  };

  std::vector<EffectConstellation> candidates;
  for (const auto& center : s->nodes) {
    if (center.kind != EffectNodeKind::Latch || center.cold) {
      continue;
    }
    const std::string member =
        latch_member_key(center.symbol.empty() ? center.id : center.symbol);
    const bool query_tied =
        query_unlocks_member(s->query, member) || s->unlocked_members.count(member) > 0;
    if (latch_muted(s, member) && !query_tied) {
      continue;
    }

    EffectConstellation c;
    c.center_id = center.id;
    c.member = member;
    std::unordered_set<std::string> node_set{center.id};
    std::unordered_set<std::string> fact_set;
    std::unordered_set<std::string> direct_fns;

    for (const auto& f : s->facts) {
      if (f.from != center.id && f.to != center.id) {
        continue;
      }
      const std::string other = f.from == center.id ? f.to : f.from;
      auto oit = by_id.find(other);
      if (oit == by_id.end() || oit->second->cold) {
        continue;
      }
      node_set.insert(other);
      if (!f.id.empty()) {
        fact_set.insert(f.id);
      }
      if (f.kind == EffectFactKind::Write) {
        add_unique(&c.writer_ids, other);
        if (oit->second->kind == EffectNodeKind::Fn) {
          direct_fns.insert(other);
        }
      } else if (f.kind == EffectFactKind::Read) {
        add_unique(&c.control_ids, other);
        if (!oit->second->parent_fn.empty()) {
          auto pit = by_id.find(oit->second->parent_fn);
          if (pit != by_id.end() && !pit->second->cold) {
            node_set.insert(pit->first);
            direct_fns.insert(pit->first);
            add_unique(&c.reader_ids, pit->first);
          }
        } else {
          add_unique(&c.reader_ids, other);
        }
      }
    }

    // A constellation is state geometry: require at least a writer and another
    // pole, unless the latch itself is explicitly tied to the query.
    const bool has_poles = c.writer_ids.size() >= 2 || !c.reader_ids.empty();
    if (c.writer_ids.empty() || (!has_poles && !query_tied && center.prior_sem < 0.2f)) {
      continue;
    }

    // Add handoffs owned by direct functions and one call hop around the core.
    // This captures orchestration stems without turning the region into a full BFS.
    for (const auto& n : s->nodes) {
      if (n.cold || n.parent_fn.empty() || !direct_fns.count(n.parent_fn)) {
        continue;
      }
      if (n.kind == EffectNodeKind::Handoff) {
        node_set.insert(n.id);
        add_unique(&c.handoff_ids, n.id);
      }
    }
    const float neighbor_floor = std::max(0.003f, center.mass * 0.25f);
    const std::unordered_set<std::string> core = node_set;
    for (const auto& f : s->facts) {
      if (f.kind != EffectFactKind::Call && f.kind != EffectFactKind::Handoff) {
        continue;
      }
      const bool from_core = core.count(f.from) > 0;
      const bool to_core = core.count(f.to) > 0;
      if (from_core == to_core) {
        continue;
      }
      const std::string other = from_core ? f.to : f.from;
      auto oit = by_id.find(other);
      if (oit == by_id.end() || oit->second->cold) {
        continue;
      }
      const EffectNode& neighbor = *oit->second;
      if (!neighbor.seed && neighbor.mass < neighbor_floor && neighbor.prior_sem < 0.58f &&
          f.kind != EffectFactKind::Handoff) {
        continue;
      }
      node_set.insert(other);
      if (!f.id.empty()) {
        fact_set.insert(f.id);
      }
      if (neighbor.kind == EffectNodeKind::Handoff) {
        add_unique(&c.handoff_ids, other);
      }
    }

    c.node_ids.assign(node_set.begin(), node_set.end());
    c.fact_ids.assign(fact_set.begin(), fact_set.end());
    std::stable_sort(c.node_ids.begin(), c.node_ids.end());
    std::stable_sort(c.fact_ids.begin(), c.fact_ids.end());

    std::unordered_map<std::string, std::pair<float, float>> core_stem_evidence;
    std::unordered_map<std::string, std::pair<float, float>> context_stem_evidence;
    std::unordered_set<std::string> direct_role(c.writer_ids.begin(), c.writer_ids.end());
    direct_role.insert(c.reader_ids.begin(), c.reader_ids.end());
    float zone_mass = 0.f;
    float max_sem = center.prior_sem;
    bool has_direct_seed = false;
    bool has_ctrl = !c.control_ids.empty();
    for (const auto& id : c.node_ids) {
      auto it = by_id.find(id);
      if (it == by_id.end()) {
        continue;
      }
      const EffectNode& n = *it->second;
      zone_mass += std::max(0.f, n.mass);
      max_sem = std::max(max_sem, n.prior_sem);
      has_ctrl = has_ctrl || n.kind == EffectNodeKind::Ctrl;
      if (n.kind != EffectNodeKind::Fn || n.stem.empty()) {
        continue;
      }
      const bool direct = direct_role.count(id) > 0;
      has_direct_seed = has_direct_seed || (direct && n.seed);
      float contribution =
          (direct ? n.mass : 0.25f * n.mass) +
          (direct ? 0.35f : 0.15f) * std::max(0.f, n.prior_sem);
      if (n.seed && direct) {
        contribution += 0.35f;
      }
      contribution += direct ? 0.30f : 0.02f;
      auto& evidence = direct ? core_stem_evidence[n.stem] : context_stem_evidence[n.stem];
      if (contribution > evidence.first) {
        evidence.second = evidence.first;
        evidence.first = contribution;
      } else if (contribution > evidence.second) {
        evidence.second = contribution;
      }
    }
    c.mass_coverage = total_mass > 1e-9f ? std::min(1.f, zone_mass / total_mass) : 0.f;

    auto ranked_stems = [](const auto& evidence_by_stem) {
      std::vector<std::pair<std::string, float>> stems;
      stems.reserve(evidence_by_stem.size());
      for (const auto& [stem, evidence] : evidence_by_stem) {
        stems.push_back({stem, evidence.first + 0.35f * evidence.second});
      }
      std::stable_sort(stems.begin(), stems.end(), [](const auto& a, const auto& b) {
        if (std::fabs(a.second - b.second) > 1e-6f) {
          return a.second > b.second;
        }
        return a.first < b.first;
      });
      return stems;
    };
    const auto core_stems = ranked_stems(core_stem_evidence);
    const auto context_stems = ranked_stems(context_stem_evidence);
    for (const auto& [stem, score] : core_stems) {
      (void)score;
      c.core_stems.push_back(stem);
    }
    for (const auto& [stem, score] : context_stems) {
      (void)score;
      if (!core_stem_evidence.count(stem)) {
        c.context_stems.push_back(stem);
      }
    }
    for (std::size_t i = 0; i < core_stems.size(); ++i) {
      if (i < 2) {
        c.primary_stems.push_back(core_stems[i].first);
      } else {
        c.peripheral_stems.push_back(core_stems[i].first);
      }
    }
    for (const auto& stem : c.context_stems) {
      if (std::find(c.peripheral_stems.begin(), c.peripheral_stems.end(), stem) ==
          c.peripheral_stems.end()) {
        c.peripheral_stems.push_back(stem);
      }
    }

    const float semantic = std::clamp(max_sem, 0.f, 1.f);
    const float mass_component = std::min(1.f, c.mass_coverage * 3.f);
    float polarity = 0.f;
    if (c.writer_ids.size() >= 2 && !c.reader_ids.empty()) {
      polarity = 1.f;
    } else if (c.writer_ids.size() >= 2 || !c.reader_ids.empty()) {
      polarity = 0.7f;
    } else if (!c.writer_ids.empty()) {
      polarity = 0.3f;
    }
    const float control = has_ctrl ? 1.f : 0.f;
    // Extra stems matter only when the latch itself or an explicit handoff
    // supplies causal support. Merely importing a call neighbor earns nothing.
    const float causal_support =
        core_stem_evidence.size() > 1 ? 1.f : (c.handoff_ids.empty() ? 0.f : 0.6f);
    c.score = 0.30f * semantic + 0.25f * mass_component + 0.20f * polarity +
              0.10f * control + 0.15f * causal_support;
    if (query_tied) {
      c.score += 0.08f;
    }
    if (has_direct_seed) {
      c.score += 0.04f;
    }
    std::ostringstream why;
    why << "latch " << member << ": " << c.writer_ids.size() << " writers, "
        << c.reader_ids.size() << " readers";
    if (core_stem_evidence.size() > 1) {
      why << ", " << core_stem_evidence.size() << " causal stems";
    }
    if (!c.context_stems.empty()) {
      why << ", " << c.context_stems.size() << " context stems";
    }
    c.why = why.str();
    candidates.push_back(std::move(c));
  }

  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const EffectConstellation& a, const EffectConstellation& b) {
                     if (std::fabs(a.score - b.score) > 1e-6f) {
                       return a.score > b.score;
                     }
                     return a.center_id < b.center_id;
                   });
  if (static_cast<int>(candidates.size()) > k) {
    candidates.resize(static_cast<std::size_t>(k));
  }
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    candidates[i].id = "C" + std::to_string(i + 1);
  }
  s->constellations = std::move(candidates);
}

void rank_macro_constellations(EffectSlice* s, int k) {
  if (s == nullptr) {
    return;
  }
  s->macro_constellations.clear();
  if (s->constellations.empty() || k <= 0) {
    return;
  }

  std::unordered_map<std::string, const EffectNode*> by_id;
  for (const auto& node : s->nodes) {
    by_id[node.id] = &node;
  }
  std::unordered_map<std::string, std::vector<std::string>> call_adj;
  std::unordered_map<std::string, std::vector<std::string>> callers;
  for (const auto& fact : s->facts) {
    if (fact.kind != EffectFactKind::Call) {
      continue;
    }
    auto from = by_id.find(fact.from);
    auto to = by_id.find(fact.to);
    if (from == by_id.end() || to == by_id.end() ||
        from->second->kind != EffectNodeKind::Fn || to->second->kind != EffectNodeKind::Fn) {
      continue;
    }
    call_adj[fact.from].push_back(fact.to);
    call_adj[fact.to].push_back(fact.from);
    callers[fact.to].push_back(fact.from);
  }
  std::vector<std::pair<std::string, std::string>> explicit_handoffs;
  for (const auto& handoff : s->nodes) {
    if (handoff.kind != EffectNodeKind::Handoff || handoff.parent_fn.empty()) {
      continue;
    }
    for (const auto& fact : s->facts) {
      if (fact.kind != EffectFactKind::Call || fact.from != handoff.id) {
        continue;
      }
      auto target = by_id.find(fact.to);
      if (target == by_id.end() || target->second->kind != EffectNodeKind::Fn) {
        continue;
      }
      explicit_handoffs.push_back({handoff.parent_fn, fact.to});
      call_adj[handoff.parent_fn].push_back(fact.to);
      call_adj[fact.to].push_back(handoff.parent_fn);
      callers[fact.to].push_back(handoff.parent_fn);
    }
  }
  auto macro_hub = [&](const std::string& id) {
    auto it = call_adj.find(id);
    return it != call_adj.end() && it->second.size() > 12;
  };

  std::vector<std::unordered_set<std::string>> direct(s->constellations.size());
  for (std::size_t i = 0; i < s->constellations.size(); ++i) {
    direct[i].insert(s->constellations[i].writer_ids.begin(),
                     s->constellations[i].writer_ids.end());
    direct[i].insert(s->constellations[i].reader_ids.begin(),
                     s->constellations[i].reader_ids.end());
  }
  struct MergeWitness {
    float strength = 0.f;
    std::string label;
  };
  auto bridge_reaches = [&](const std::string& bridge,
                            const std::unordered_set<std::string>& targets) {
    std::unordered_set<std::string> seen{bridge};
    std::vector<std::pair<std::string, int>> queue{{bridge, 0}};
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
      const auto [cur, depth] = queue[qi];
      if (targets.count(cur)) {
        return true;
      }
      if (depth >= 2 || (depth > 0 && macro_hub(cur))) {
        continue;
      }
      auto ait = call_adj.find(cur);
      if (ait == call_adj.end()) {
        continue;
      }
      for (const std::string& next : ait->second) {
        if (seen.insert(next).second) {
          queue.push_back({next, depth + 1});
        }
      }
    }
    return false;
  };
  auto merge_witness = [&](int ai, int bi) {
    const auto& a = direct[static_cast<std::size_t>(ai)];
    const auto& b = direct[static_cast<std::size_t>(bi)];
    for (const std::string& id : a) {
      if (b.count(id)) {
        return MergeWitness{1.f, "shared-direct:" + id};
      }
    }
    for (const auto& [from, to] : explicit_handoffs) {
      if ((a.count(from) && b.count(to)) || (b.count(from) && a.count(to)) ||
          (a.count(to) && b.count(from)) || (b.count(to) && a.count(from))) {
        return MergeWitness{0.9f, "handoff:" + from + "->" + to};
      }
    }
    for (const auto& node : s->nodes) {
      if (node.kind != EffectNodeKind::Fn || !node.query_hit || node.cold ||
          macro_hub(node.id) || a.count(node.id) || b.count(node.id)) {
        continue;
      }
      if (bridge_reaches(node.id, a) && bridge_reaches(node.id, b)) {
        return MergeWitness{0.7f, "query-bridge:" + node.id};
      }
    }
    return MergeWitness{};
  };
  auto coupling = [&](int a, int b) {
    return merge_witness(a, b).strength;
  };

  std::vector<std::vector<int>> groups;
  for (int i = 0; i < static_cast<int>(s->constellations.size()); ++i) {
    groups.push_back({i});
  }
  while (groups.size() > 1) {
    float best = 0.f;
    std::size_t best_a = 0;
    std::size_t best_b = 0;
    for (std::size_t a = 0; a < groups.size(); ++a) {
      for (std::size_t b = a + 1; b < groups.size(); ++b) {
        float complete = 1.f;
        for (int ai : groups[a]) {
          for (int bi : groups[b]) {
            complete = std::min(complete, coupling(ai, bi));
          }
        }
        if (complete >= 0.6f && complete > best) {
          best = complete;
          best_a = a;
          best_b = b;
        }
      }
    }
    if (best < 0.6f) {
      break;
    }
    groups[best_a].insert(groups[best_a].end(), groups[best_b].begin(),
                          groups[best_b].end());
    groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(best_b));
  }

  float total_mass = 0.f;
  for (const auto& node : s->nodes) {
    total_mass += std::max(0.f, node.mass);
  }
  std::vector<EffectMacroConstellation> out;
  for (const auto& group : groups) {
    EffectMacroConstellation macro;
    float base_score = 0.f;
    std::unordered_set<std::string> node_ids;
    std::unordered_set<std::string> direct_ids;
    std::unordered_set<std::string> primary_stems;
    for (int ci : group) {
      const auto& nucleus = s->constellations[static_cast<std::size_t>(ci)];
      macro.nucleus_ids.push_back(nucleus.id);
      base_score = std::max(base_score, nucleus.score);
      node_ids.insert(nucleus.node_ids.begin(), nucleus.node_ids.end());
      direct_ids.insert(direct[static_cast<std::size_t>(ci)].begin(),
                        direct[static_cast<std::size_t>(ci)].end());
      primary_stems.insert(nucleus.primary_stems.begin(), nucleus.primary_stems.end());
    }
    if (group.size() > 1) {
      macro.merge_strength = 1.f;
      for (std::size_t a = 0; a < group.size(); ++a) {
        for (std::size_t b = a + 1; b < group.size(); ++b) {
          const MergeWitness witness = merge_witness(group[a], group[b]);
          macro.merge_strength = std::min(macro.merge_strength, witness.strength);
          if (!witness.label.empty() &&
              std::find(macro.merge_witnesses.begin(), macro.merge_witnesses.end(),
                        witness.label) == macro.merge_witnesses.end()) {
            macro.merge_witnesses.push_back(witness.label);
          }
        }
      }
    }

    std::unordered_map<std::string, int> distance;
    std::vector<std::string> queue;
    for (const std::string& id : direct_ids) {
      distance[id] = 0;
      queue.push_back(id);
    }
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
      const std::string cur = queue[qi];
      const int depth = distance[cur];
      if (depth >= 2) {
        continue;
      }
      if (depth > 0 && macro_hub(cur)) {
        continue;
      }
      auto ait = call_adj.find(cur);
      if (ait == call_adj.end()) {
        continue;
      }
      for (const std::string& next : ait->second) {
        if (!distance.count(next)) {
          distance[next] = depth + 1;
          queue.push_back(next);
        }
      }
    }

    // A non-core query branch may join the zone only through an owner stem
    // reached independently from at least two direct roles. This admits a
    // cancellation arm without turning mere same-file proximity into a merge.
    std::unordered_map<std::string, std::unordered_set<std::string>> owner_support;
    for (const std::string& role : direct_ids) {
      std::unordered_map<std::string, int> role_distance{{role, 0}};
      std::vector<std::string> role_queue{role};
      for (std::size_t qi = 0; qi < role_queue.size(); ++qi) {
        const std::string cur = role_queue[qi];
        auto nit = by_id.find(cur);
        if (nit != by_id.end() && !nit->second->stem.empty()) {
          owner_support[nit->second->stem].insert(role);
          if (primary_stems.count(nit->second->stem)) {
            node_ids.insert(cur);
          }
        }
        const int depth = role_distance[cur];
        if (depth >= 2) {
          continue;
        }
        if (depth > 0 && macro_hub(cur)) {
          continue;
        }
        auto ait = callers.find(cur);
        if (ait == callers.end()) {
          continue;
        }
        for (const std::string& next : ait->second) {
          if (!role_distance.count(next)) {
            role_distance[next] = depth + 1;
            role_queue.push_back(next);
          }
        }
      }
    }
    std::unordered_set<std::string> owner_stems;
    for (const auto& [stem, roles] : owner_support) {
      if (roles.size() >= 2 && primary_stems.count(stem)) {
        owner_stems.insert(stem);
      }
    }

    std::unordered_set<std::string> query_nodes;
    for (const auto& node : s->nodes) {
      if (node.query_hit && !node.cold && owner_stems.count(node.stem)) {
        query_nodes.insert(node.id);
      }
    }
    std::unordered_set<std::string> grouped;
    for (const std::string& start : query_nodes) {
      if (!grouped.insert(start).second) {
        continue;
      }
      std::vector<std::string> component{start};
      for (std::size_t qi = 0; qi < component.size(); ++qi) {
        auto ait = call_adj.find(component[qi]);
        if (ait == call_adj.end()) {
          continue;
        }
        for (const std::string& next : ait->second) {
          if (query_nodes.count(next) && grouped.insert(next).second) {
            component.push_back(next);
          }
        }
      }
      bool witnessed = component.size() >= 2;
      for (const std::string& id : component) {
        witnessed = witnessed || distance.count(id) > 0;
      }
      if (!witnessed) {
        continue;
      }
      std::stable_sort(component.begin(), component.end(), [&](const std::string& a,
                                                              const std::string& b) {
        return by_id.at(a)->prior_sem > by_id.at(b)->prior_sem;
      });
      macro.anchor_groups.push_back(component);
      node_ids.insert(component.begin(), component.end());
    }

    macro.node_ids.assign(node_ids.begin(), node_ids.end());
    std::stable_sort(macro.node_ids.begin(), macro.node_ids.end());
    macro.primary_stems.assign(primary_stems.begin(), primary_stems.end());
    std::stable_sort(macro.primary_stems.begin(), macro.primary_stems.end());
    float zone_mass = 0.f;
    for (const std::string& id : macro.node_ids) {
      auto nit = by_id.find(id);
      if (nit != by_id.end()) {
        zone_mass += std::max(0.f, nit->second->mass);
      }
    }
    macro.mass_coverage = total_mass > 1e-9f ? zone_mass / total_mass : 0.f;
    int best_query_rank = std::numeric_limits<int>::max();
    int causal_branches = 0;
    for (const auto& anchors : macro.anchor_groups) {
      if (anchors.size() >= 2) {
        ++causal_branches;
      }
      for (const std::string& id : anchors) {
        auto nit = by_id.find(id);
        if (nit != by_id.end() && nit->second->query_rank >= 0) {
          best_query_rank = std::min(best_query_rank, nit->second->query_rank);
        }
      }
    }
    const float branch_bonus = std::min(0.24f, 0.08f * static_cast<float>(causal_branches));
    const float rank_bonus =
        best_query_rank == std::numeric_limits<int>::max()
            ? 0.f
            : 0.22f / (1.f + 0.35f * static_cast<float>(best_query_rank));
    const float nucleus_bonus = group.size() > 1 ? 0.02f : 0.f;
    const float causal_merge_bonus = 0.03f * macro.merge_strength;
    macro.score = std::min(1.f, base_score + nucleus_bonus + branch_bonus + rank_bonus +
                                   causal_merge_bonus +
                                   macro_facet_bonus(s, direct_ids, by_id));
    std::ostringstream why;
    why << group.size() << " nuclei, " << macro.anchor_groups.size()
        << " witnessed query branches, " << macro.node_ids.size() << " unique nodes";
    if (!macro.merge_witnesses.empty()) {
      why << ", merge " << macro.merge_witnesses.front();
    }
    macro.why = why.str();
    out.push_back(std::move(macro));
  }

  std::unordered_set<std::string> explained_query_nodes;
  for (const auto& macro : out) {
    for (const auto& group : macro.anchor_groups) {
      explained_query_nodes.insert(group.begin(), group.end());
    }
  }
  std::unordered_set<std::string> orphan_query_nodes;
  for (const auto& node : s->nodes) {
    if (node.query_hit && !node.cold && !explained_query_nodes.count(node.id)) {
      orphan_query_nodes.insert(node.id);
    }
  }
  std::unordered_set<std::string> orphan_seen;
  for (const std::string& start : orphan_query_nodes) {
    if (!orphan_seen.insert(start).second) {
      continue;
    }
    std::vector<std::string> component{start};
    for (std::size_t qi = 0; qi < component.size(); ++qi) {
      auto ait = call_adj.find(component[qi]);
      if (ait == call_adj.end()) {
        continue;
      }
      for (const std::string& next : ait->second) {
        if (orphan_query_nodes.count(next) && orphan_seen.insert(next).second) {
          component.push_back(next);
        }
      }
    }
    int best_rank = std::numeric_limits<int>::max();
    float max_sem = 0.f;
    std::unordered_map<std::string, int> stem_rank;
    float component_mass = 0.f;
    for (const std::string& id : component) {
      auto nit = by_id.find(id);
      if (nit == by_id.end()) {
        continue;
      }
      const EffectNode& node = *nit->second;
      if (node.query_rank >= 0) {
        best_rank = std::min(best_rank, node.query_rank);
        auto [sit, inserted] = stem_rank.insert({node.stem, node.query_rank});
        if (!inserted) {
          sit->second = std::min(sit->second, node.query_rank);
        }
      }
      max_sem = std::max(max_sem, node.prior_sem);
      component_mass += std::max(0.f, node.mass);
    }
    if (best_rank == std::numeric_limits<int>::max()) {
      continue;
    }
    bool strong_writer_seed = false;
    for (const std::string& id : component) {
      auto nit = by_id.find(id);
      if (nit == by_id.end()) {
        continue;
      }
      const EffectNode& node = *nit->second;
      if (node.query_rank >= 0 && node.query_rank <= 2 &&
          (node.prior_sem >= 0.6f || node.seed) && node_is_direct_writer(s, id)) {
        strong_writer_seed = true;
        break;
      }
    }
    if (component.size() < 2 && best_rank > 4 && !strong_writer_seed) {
      continue;
    }
    std::vector<std::pair<std::string, int>> ranked_stems(stem_rank.begin(), stem_rank.end());
    std::stable_sort(ranked_stems.begin(), ranked_stems.end(), [](const auto& a, const auto& b) {
      if (a.second != b.second) {
        return a.second < b.second;
      }
      return a.first < b.first;
    });

    EffectMacroConstellation macro;
    macro.node_ids = component;
    std::stable_sort(macro.node_ids.begin(), macro.node_ids.end());
    macro.anchor_groups.push_back(component);
    for (std::size_t i = 0; i < ranked_stems.size() && i < 2; ++i) {
      if (!ranked_stems[i].first.empty()) {
        macro.primary_stems.push_back(ranked_stems[i].first);
      }
    }
    macro.mass_coverage = total_mass > 1e-9f ? component_mass / total_mass : 0.f;
    const float rank_bonus = 0.22f / (1.f + 0.35f * static_cast<float>(best_rank));
    const float cohesion_bonus = component.size() >= 2 ? 0.08f : 0.f;
    macro.score = std::min(1.f, 0.55f + 0.10f * std::clamp(max_sem, 0.f, 1.f) +
                                   rank_bonus + cohesion_bonus);
    std::ostringstream why;
    why << "query causal component, " << component.size() << " anchors, best rank "
        << best_rank;
    macro.why = why.str();
    out.push_back(std::move(macro));
  }
  std::stable_sort(out.begin(), out.end(), [](const EffectMacroConstellation& a,
                                              const EffectMacroConstellation& b) {
    if (std::fabs(a.score - b.score) > 1e-6f) {
      return a.score > b.score;
    }
    return a.mass_coverage > b.mass_coverage;
  });
  if (static_cast<int>(out.size()) > k) {
    out.resize(static_cast<std::size_t>(k));
  }
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i].id = "M" + std::to_string(i + 1);
  }
  s->macro_constellations = std::move(out);
}

void project_constellation_light(EffectSlice* s) {
  if (s == nullptr || s->constellations.empty()) {
    return;
  }
  const EffectConstellation& top = s->constellations.front();
  if (top.primary_stems.empty()) {
    return;
  }
  const float runner_score =
      s->constellations.size() > 1 ? s->constellations[1].score : 0.f;
  const float confidence = std::clamp((top.score - runner_score) / 0.05f, 0.f, 1.f);
  if (confidence <= 1e-4f) {
    return;
  }

  std::vector<EffectNode*> recipients;
  float recipient_weight = 0.f;
  float total_mass = 0.f;
  for (auto& node : s->nodes) {
    total_mass += std::max(0.f, node.mass);
    if (!node.query_hit || node.cold || node.stem.empty() ||
        std::find(top.primary_stems.begin(), top.primary_stems.end(), node.stem) ==
            top.primary_stems.end()) {
      continue;
    }
    recipients.push_back(&node);
    recipient_weight += 0.25f + std::max(0.f, node.prior_sem);
  }
  if (recipients.empty() || recipient_weight <= 1e-9f || total_mass <= 1e-9f) {
    return;
  }

  const float budget = total_mass * 0.12f * confidence;
  const float keep = std::max(0.f, 1.f - budget / total_mass);
  for (auto& node : s->nodes) {
    node.mass *= keep;
  }
  for (EffectNode* node : recipients) {
    const float weight = 0.25f + std::max(0.f, node->prior_sem);
    node->mass += budget * weight / recipient_weight;
  }
}

struct BeamPath {
  std::vector<std::string> nodes;
  std::vector<std::string> facts;
  float score = 0.f;
};

void rank_threads(EffectSlice* s, int k) {
  if (s == nullptr) {
    s->threads.clear();
    return;
  }
  s->threads.clear();
  if (s->nodes.empty() || k <= 0) {
    return;
  }
  std::unordered_map<std::string, std::vector<const EffectFact*>> outg;
  for (const auto& f : s->facts) {
    outg[f.from].push_back(&f);
  }
  // Latch is a communication hub: writers are reachable from the latch (undirected
  // at M) so polar ON/OFF is a directed path.
  std::vector<EffectFact> rev;
  rev.reserve(s->facts.size());
  for (const auto& f : s->facts) {
    if (f.kind != EffectFactKind::Write) {
      continue;
    }
    EffectFact r = f;
    r.from = f.to;
    r.to = f.from;
    r.id = f.id + "_rev";
    rev.push_back(std::move(r));
  }
  for (const auto& f : rev) {
    outg[f.from].push_back(&f);
  }
  std::unordered_map<std::string, const EffectNode*> by_id;
  for (const auto& n : s->nodes) {
    by_id[n.id] = &n;
  }

  auto path_ok = [&](const BeamPath& p) -> bool {
    int fns = 0;
    int latches = 0;
    int ctrls = 0;
    std::unordered_set<std::string> latch_set;
    for (const auto& id : p.nodes) {
      auto it = by_id.find(id);
      if (it == by_id.end()) {
        continue;
      }
      if (it->second->kind == EffectNodeKind::Fn) {
        ++fns;
      } else if (it->second->kind == EffectNodeKind::Latch) {
        ++latches;
        latch_set.insert(it->second->symbol);
      } else if (it->second->kind == EffectNodeKind::Ctrl) {
        ++ctrls;
      }
    }
    return fns >= 1 && (latches >= 1 || ctrls >= 1 || fns >= 2);
  };

  constexpr float kGoodSem = 0.55f;
  auto density_score = [&](const BeamPath& p) -> float {
    float sum = 0.f;
    int n = 0;
    int n_good = 0;
    float any_sem = 0.f;
    for (const auto& id : p.nodes) {
      auto it = by_id.find(id);
      if (it == by_id.end()) {
        continue;
      }
      any_sem = std::max(any_sem, it->second->prior_sem);
      if (it->second->kind != EffectNodeKind::Fn) {
        continue;
      }
      if (glue_fn_symbol(it->second->symbol)) {
        continue;
      }
      const float c = it->second->prior_sem;
      if (c <= 0.15f) {
        continue;
      }
      sum += c;
      ++n;
      if (c >= kGoodSem) {
        ++n_good;
      }
    }
    if (n <= 0) {
      return 0.05f * any_sem;
    }
    return (sum / static_cast<float>(n)) * std::log(1.f + static_cast<float>(n_good));
  };

  std::vector<BeamPath> beam;
  std::vector<const EffectNode*> starts;
  std::unordered_set<std::string> start_ids;
  auto push_start = [&](const EffectNode* n) {
    if (n == nullptr || n->cold || !start_ids.insert(n->id).second) {
      return;
    }
    starts.push_back(n);
  };
  for (const auto& n : s->nodes) {
    if (n.seed) {
      push_start(&n);
    }
  }
  std::vector<const EffectNode*> extra;
  auto member_query_tied = [&](const std::string& mem) {
    const std::string key = latch_member_key(mem);
    return query_unlocks_member(s->query, key) || s->unlocked_members.count(key);
  };
  for (const auto& n : s->nodes) {
    if (n.cold || n.seed) {
      continue;
    }
    if (n.kind == EffectNodeKind::Fn && n.prior_sem > 0.2f) {
      extra.push_back(&n);
      continue;
    }
    if (n.kind == EffectNodeKind::Latch) {
      const std::string mem = latch_member_key(n.symbol.empty() ? n.id : n.symbol);
      if (!latch_muted(s, mem) && member_query_tied(mem)) {
        extra.push_back(&n);
      }
    }
  }
  std::stable_sort(extra.begin(), extra.end(),
                   [](const EffectNode* a, const EffectNode* b) {
                     return a->prior_sem > b->prior_sem;
                   });
  for (const auto* n : extra) {
    if (static_cast<int>(starts.size()) >= 16) {
      break;
    }
    push_start(n);
  }
  for (const auto* st : starts) {
    BeamPath p;
    p.nodes.push_back(st->id);
    p.score = density_score(p);
    beam.push_back(std::move(p));
  }

  std::vector<BeamPath> done;
  for (int depth = 0; depth < 7 && !beam.empty(); ++depth) {
    std::vector<BeamPath> next;
    for (const auto& p : beam) {
      const std::string& last = p.nodes.back();
      auto it = outg.find(last);
      if (it == outg.end() || static_cast<int>(p.nodes.size()) >= 8) {
        if (path_ok(p)) {
          done.push_back(p);
        }
        continue;
      }
      bool expanded = false;
      for (const EffectFact* f : it->second) {
        if (std::find(p.nodes.begin(), p.nodes.end(), f->to) != p.nodes.end()) {
          continue;
        }
        auto nit = by_id.find(f->to);
        if (nit == by_id.end() || nit->second->cold) {
          continue;
        }
        BeamPath np = p;
        np.nodes.push_back(f->to);
        np.facts.push_back(f->id);
        np.score = density_score(np);
        next.push_back(std::move(np));
        expanded = true;
      }
      if (!expanded && path_ok(p)) {
        done.push_back(p);
      }
    }
    std::stable_sort(next.begin(), next.end(),
                     [](const BeamPath& a, const BeamPath& b) { return a.score > b.score; });
    if (next.size() > 40) {
      next.resize(40);
    }
    beam.swap(next);
  }
  for (auto& p : beam) {
    if (path_ok(p)) {
      done.push_back(std::move(p));
    }
  }

  std::unordered_map<std::string, std::vector<std::string>> latch_writers;
  std::unordered_map<std::string, std::vector<std::string>> latch_ctrls;
  for (const auto& f : s->facts) {
    if (f.kind == EffectFactKind::Write && !f.member.empty()) {
      latch_writers[f.member].push_back(f.from);
    }
    if (f.kind == EffectFactKind::Read && !f.member.empty()) {
      latch_ctrls[f.member].push_back(f.to);
    }
  }
  auto add_explicit = [&](std::vector<std::string> nodes, float extra, const std::string& /*why*/) {
    BeamPath p;
    p.nodes = std::move(nodes);
    p.score = density_score(p) + extra;
    if (path_ok(p)) {
      done.push_back(std::move(p));
    }
  };
  for (const auto& [m, writers] : latch_writers) {
    if (writers.size() < 2) {
      continue;
    }
    if (latch_muted(s, latch_member_key(m))) {
      continue;
    }
    std::string lid;
    for (const auto& n : s->nodes) {
      if (n.kind != EffectNodeKind::Latch) {
        continue;
      }
      const std::string key = latch_member_key(n.symbol.empty() ? n.id : n.symbol);
      if (key == latch_member_key(m) || n.symbol == m || n.id == m) {
        lid = n.id;
        break;
      }
    }
    if (lid.empty()) {
      lid = latch_id(m);
    }
    std::string seed_w;
    std::string clear_w;
    for (const auto& w : writers) {
      auto it = by_id.find(w);
      if (it == by_id.end()) {
        continue;
      }
      if (it->second->seed && seed_w.empty()) {
        seed_w = w;
      }
      if ((it->second->symbol.find("clear") != std::string::npos ||
           it->second->symbol.find("end_") != std::string::npos ||
           it->second->symbol.find("reset") != std::string::npos) &&
          clear_w.empty()) {
        clear_w = w;
      }
    }
    if (seed_w.empty()) {
      seed_w = writers.front();
    }
    const bool true_poles = !clear_w.empty() && clear_w != seed_w;
    const bool ctrl_reads = !latch_ctrls[m].empty();
    if (!true_poles && !ctrl_reads) {
      continue;
    }
    const bool query_tied = member_query_tied(m);
    if (!query_tied) {
      continue;
    }
    const float boost = (ctrl_reads ? 0.2f : 0.12f);
    if (true_poles) {
      add_explicit({seed_w, lid, clear_w}, boost + 0.05f, m);
    }
    if (ctrl_reads) {
      add_explicit({seed_w, lid, latch_ctrls[m].front()}, boost, m);
    }
  }

  for (auto& p : done) {
    std::unordered_set<std::string> latches;
    int fns = 0;
    bool has_ctrl = false;
    bool has_seed = false;
    int glue_fns = 0;
    bool query_polar = false;
    for (const auto& id : p.nodes) {
      auto it = by_id.find(id);
      if (it == by_id.end()) {
        continue;
      }
      if (it->second->seed) {
        has_seed = true;
      }
      if (it->second->kind == EffectNodeKind::Latch) {
        latches.insert(it->second->symbol);
        if (member_query_tied(it->second->symbol.empty() ? it->second->id : it->second->symbol)) {
          query_polar = true;
        }
      } else if (it->second->kind == EffectNodeKind::Fn) {
        ++fns;
        if (glue_fn_symbol(it->second->symbol)) {
          ++glue_fns;
        }
      } else if (it->second->kind == EffectNodeKind::Ctrl) {
        has_ctrl = true;
      }
    }
    p.score = density_score(p);
    if (query_polar && latches.size() >= 1 && fns >= 2) {
      p.score += 0.12f;
    }
    if (query_polar && has_ctrl && !latches.empty()) {
      p.score += 0.08f;
    }
    if (has_seed) {
      p.score += 0.06f;
    }
    if (fns > 0 && glue_fns == fns) {
      p.score *= 0.4f;
    }
  }
  std::stable_sort(done.begin(), done.end(),
                   [](const BeamPath& a, const BeamPath& b) { return a.score > b.score; });

  auto path_has_seed = [&](const BeamPath& p) {
    for (const auto& id : p.nodes) {
      auto it = by_id.find(id);
      if (it != by_id.end() && it->second->seed) {
        return true;
      }
    }
    return false;
  };
  auto path_stem = [&](const BeamPath& p) -> std::string {
    for (const auto& id : p.nodes) {
      auto it = by_id.find(id);
      if (it != by_id.end() && it->second->kind == EffectNodeKind::Fn && !it->second->stem.empty()) {
        return it->second->stem;
      }
    }
    return {};
  };
  auto emit = [&](const BeamPath& p) {
    EffectThread th;
    th.id = "T" + std::to_string(static_cast<int>(s->threads.size()) + 1);
    th.node_ids = p.nodes;
    th.fact_ids = p.facts;
    th.score = p.score;
    std::unordered_set<std::string> latches;
    int fns = 0;
    bool has_ctrl = false;
    for (const auto& id : p.nodes) {
      auto it = by_id.find(id);
      if (it == by_id.end()) {
        continue;
      }
      if (it->second->kind == EffectNodeKind::Latch) {
        latches.insert(it->second->symbol);
        th.latches.push_back(it->second->symbol);
      } else if (it->second->kind == EffectNodeKind::Fn) {
        ++fns;
      } else if (it->second->kind == EffectNodeKind::Ctrl) {
        has_ctrl = true;
      }
    }
    std::ostringstream why;
    if (!th.latches.empty() && fns >= 2 &&
        member_query_tied(th.latches.front())) {
      why << "polaridad via latch " << th.latches.front();
    } else if (has_ctrl) {
      why << "ctrl importa estado";
    } else {
      why << "media cosine (" << th.node_ids.size() << " hops)";
    }
    th.why = why.str();
    s->threads.push_back(std::move(th));
  };

  std::unordered_set<std::string> seen_sig;
  std::unordered_map<std::string, int> stem_n;
  constexpr int kMaxPerStemPpr = 2;
  constexpr int kMaxPathFromSeed = 4;
  const int kAnchored = std::min(2, k);
  auto starts_at_seed = [&](const BeamPath& p) {
    if (p.nodes.empty()) {
      return false;
    }
    auto it = by_id.find(p.nodes[0]);
    return it != by_id.end() && it->second->seed;
  };
  auto try_take = [&](const BeamPath& p, bool require_seed, int cap, int max_stem) {
    if (static_cast<int>(s->threads.size()) >= cap) {
      return false;
    }
    if (require_seed && !path_has_seed(p)) {
      return false;
    }
    const std::string st = path_stem(p);
    if (!st.empty() && max_stem > 0 && stem_n[st] >= max_stem) {
      return false;
    }
    std::string sig;
    for (const auto& id : p.nodes) {
      sig += id;
      sig += "|";
    }
    if (!seen_sig.insert(sig).second) {
      return false;
    }
    if (!st.empty()) {
      ++stem_n[st];
    }
    emit(p);
    return true;
  };
  for (const auto& p : done) {
    if (starts_at_seed(p) && static_cast<int>(p.nodes.size()) <= kMaxPathFromSeed) {
      try_take(p, false, kAnchored, 1);
    }
  }
  for (const auto& p : done) {
    try_take(p, true, kAnchored, 1);
  }
  for (const auto& p : done) {
    try_take(p, false, k, kMaxPerStemPpr);
  }
}

void compute_holes(EffectSlice* s) {
  if (s == nullptr) {
    return;
  }
  s->holes.clear();
  std::unordered_map<std::string, std::unordered_set<std::string>> writers;
  for (const auto& f : s->facts) {
    if (f.kind == EffectFactKind::Write && !f.member.empty()) {
      writers[f.member].insert(f.from);
    }
  }
  int handoffs = 0;
  for (const auto& n : s->nodes) {
    if (n.kind == EffectNodeKind::Handoff) {
      ++handoffs;
    }
  }
  for (const auto& [m, fns] : writers) {
    if (fns.size() == 1) {
      s->holes.push_back("latch " + m + " una sola polaridad");
    }
  }
  if (!writers.empty() && handoffs == 0) {
    s->holes.push_back("latch tocado sin handoff en bola");
  }
}

}  // namespace

void effect_slice_rank(EffectSlice* s, int k) {
  run_ppr(s);
  rank_constellations(s, kEffectSliceMaxConstellations);
  rank_macro_constellations(s, kEffectSliceMaxConstellations);
  project_constellation_light(s);
  rank_threads(s, k > 0 ? k : kEffectSliceMaxThreads);
}

bool effect_hub_member(const std::string& member) {
  return hub_member(latch_member_key(member));
}

bool effect_query_unlocks_member(const std::string& query, const std::string& member) {
  return query_unlocks_member(query, latch_member_key(member));
}

std::string effect_latch_member_key(const std::string& symbol_or_id) {
  return latch_member_key(symbol_or_id);
}

const char* effect_node_kind_name(EffectNodeKind k) {
  switch (k) {
    case EffectNodeKind::Fn:
      return "fn";
    case EffectNodeKind::Ctrl:
      return "ctrl";
    case EffectNodeKind::Latch:
      return "latch";
    case EffectNodeKind::Handoff:
      return "handoff";
  }
  return "?";
}

const char* effect_ctrl_kind_name(EffectCtrlKind k) {
  switch (k) {
    case EffectCtrlKind::None:
      return "";
    case EffectCtrlKind::If:
      return "if";
    case EffectCtrlKind::Switch:
      return "switch";
    case EffectCtrlKind::Case:
      return "case";
    case EffectCtrlKind::Loop:
      return "loop";
    case EffectCtrlKind::Guard:
      return "guard";
  }
  return "";
}

const char* effect_fact_kind_name(EffectFactKind k) {
  switch (k) {
    case EffectFactKind::Call:
      return "call";
    case EffectFactKind::EnterCtrl:
      return "enter_ctrl";
    case EffectFactKind::Then:
      return "then";
    case EffectFactKind::Else:
      return "else";
    case EffectFactKind::Case:
      return "case";
    case EffectFactKind::Default:
      return "default";
    case EffectFactKind::Fallthrough:
      return "fallthrough";
    case EffectFactKind::Wrap:
      return "wrap";
    case EffectFactKind::Write:
      return "write";
    case EffectFactKind::Read:
      return "read";
    case EffectFactKind::Handoff:
      return "handoff";
    case EffectFactKind::Contains:
      return "contains";
  }
  return "?";
}

EffectNodeKind parse_effect_node_kind(const std::string& s) {
  if (s == "ctrl") {
    return EffectNodeKind::Ctrl;
  }
  if (s == "latch") {
    return EffectNodeKind::Latch;
  }
  if (s == "handoff") {
    return EffectNodeKind::Handoff;
  }
  return EffectNodeKind::Fn;
}

EffectCtrlKind parse_effect_ctrl_kind(const std::string& s) {
  if (s == "if") {
    return EffectCtrlKind::If;
  }
  if (s == "switch") {
    return EffectCtrlKind::Switch;
  }
  if (s == "case") {
    return EffectCtrlKind::Case;
  }
  if (s == "loop") {
    return EffectCtrlKind::Loop;
  }
  if (s == "guard") {
    return EffectCtrlKind::Guard;
  }
  return EffectCtrlKind::None;
}

EffectFactKind parse_effect_fact_kind(const std::string& s) {
  if (s == "enter_ctrl") {
    return EffectFactKind::EnterCtrl;
  }
  if (s == "then") {
    return EffectFactKind::Then;
  }
  if (s == "else") {
    return EffectFactKind::Else;
  }
  if (s == "case") {
    return EffectFactKind::Case;
  }
  if (s == "default") {
    return EffectFactKind::Default;
  }
  if (s == "fallthrough") {
    return EffectFactKind::Fallthrough;
  }
  if (s == "wrap") {
    return EffectFactKind::Wrap;
  }
  if (s == "write") {
    return EffectFactKind::Write;
  }
  if (s == "read") {
    return EffectFactKind::Read;
  }
  if (s == "handoff") {
    return EffectFactKind::Handoff;
  }
  if (s == "contains") {
    return EffectFactKind::Contains;
  }
  return EffectFactKind::Call;
}

const EffectNode* effect_slice_find_node(const EffectSlice& s, const std::string& id) {
  for (const auto& n : s.nodes) {
    if (n.id == id) {
      return &n;
    }
  }
  return nullptr;
}

int effect_slice_count_kind(const EffectSlice& s, EffectNodeKind k) {
  int n = 0;
  for (const auto& node : s.nodes) {
    if (node.kind == k) {
      ++n;
    }
  }
  return n;
}

bool effect_slice_seed(EffectSlice* s, const EffectSliceSeedIn& in, std::string* err) {
  if (s == nullptr) {
    if (err) {
      *err = "slice null";
    }
    return false;
  }
  *s = EffectSlice{};
  s->query = in.query;
  s->seeds = in.seeds;
  s->add_siblings = in.add_siblings;
  s->generation = 0;
  s->inventory_paths.insert(in.inventory_paths.begin(), in.inventory_paths.end());
  int n = 0;
  auto add_fn = [&](const EffectSliceSeedFn& fn, bool seed) {
    if (fn.file_level || fn.symbol.empty()) {
      if (!fn.path.empty()) {
        s->inventory_paths.insert(fn.path);
      }
      return;
    }
    if (n >= in.window_n) {
      return;
    }
    const std::string path = fn.path;
    EffectNode node;
    node.id = fn_id(path, fn.symbol);
    node.kind = EffectNodeKind::Fn;
    node.path = path;
    node.symbol = fn.symbol;
    node.line = fn.line;
    node.prior_sem = fn.prior_sem;
    node.seed = seed;
    node.stem = stem_of(path.empty() ? fn.symbol : path);
    if (add_node(s, node)) {
      ++n;
    }
  };
  for (const auto& fn : in.map_window) {
    add_fn(fn, true);
  }
  if (!in.extra_anchor.empty()) {
    EffectSliceSeedFn extra;
    const auto colon = in.extra_anchor.rfind(':');
    if (colon != std::string::npos && in.extra_anchor.find('/') != std::string::npos) {
      extra.path = in.extra_anchor.substr(0, colon);
      extra.symbol = in.extra_anchor.substr(colon + 1);
    } else {
      extra.symbol = in.extra_anchor;
    }
    extra.prior_sem = 1.f;
    add_fn(extra, true);
  }
  if (s->nodes.empty() && s->inventory_paths.empty()) {
    if (err) {
      *err = "seed vacío";
    }
    return false;
  }
  return true;
}

void effect_slice_fill_seed_from_map(EffectSliceSeedIn* in, const std::string& map_md, int top_n) {
  if (in == nullptr || map_md.empty()) {
    return;
  }
  if (top_n <= 0) {
    top_n = kEffectSliceMapTopDefault;
  }
  if (in->query.empty()) {
    std::istringstream qs(map_md);
    std::string line;
    int lines = 0;
    while (std::getline(qs, line) && lines < 24) {
      ++lines;
      if (line.rfind("query:", 0) == 0) {
        std::string q = line.substr(6);
        while (!q.empty() && (q.front() == ' ' || q.front() == '\t')) {
          q.erase(q.begin());
        }
        in->query = q;
        break;
      }
    }
  }
  const auto rows = a_queue_inputs_from_ranked_map_markdown(map_md, static_cast<std::size_t>(top_n));
  const int n = static_cast<int>(rows.size());
  int i = 0;
  auto stem_of_file = [](const std::string& path) {
    std::string base = path;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) {
      base = base.substr(slash + 1);
    }
    const auto dot = base.rfind('.');
    if (dot != std::string::npos && dot > 0) {
      base = base.substr(0, dot);
    }
    return base;
  };
  auto is_file_level = [&](const AQueueBuildInput& row) {
    if (row.file.empty()) {
      return false;
    }
    if (row.line > 1) {
      return false;
    }
    if (row.name.empty()) {
      return true;
    }
    return row.name == stem_of_file(row.file);
  };
  for (const auto& row : rows) {
    const float prior = n <= 1 ? 1.f : 0.2f + 0.8f * static_cast<float>(n - i) / static_cast<float>(n);
    ++i;
    if (is_file_level(row)) {
      if (!row.file.empty()) {
        in->inventory_paths.push_back(row.file);
      }
      continue;
    }
    if (row.name.empty()) {
      continue;
    }
    EffectSliceSeedFn fn;
    fn.path = row.file;
    fn.symbol = row.name;
    fn.line = row.line;
    fn.prior_sem = prior;
    in->map_window.push_back(std::move(fn));
  }
  in->window_n = std::max(in->window_n, static_cast<int>(in->map_window.size()));
}

bool effect_slice_build(EffectSlice* s, const EffectSliceDeps& deps, std::string* err) {
  if (s == nullptr) {
    if (err) {
      *err = "slice null";
    }
    return false;
  }
  if (deps.workspace_root.empty()) {
    if (err) {
      *err = "workspace_root vacío";
    }
    return false;
  }

  std::vector<std::string> inventory;
  inventory.reserve(s->inventory_paths.size() + 8);
  for (const auto& p : s->inventory_paths) {
    inventory.push_back(p);
  }
  if (s->add_siblings) {
    for (const auto& n : s->nodes) {
      if (n.kind == EffectNodeKind::Fn && n.seed && !n.path.empty()) {
        inventory.push_back(n.path);
      }
    }
  }
  std::unordered_set<std::string> seen_inv;
  for (const auto& p : inventory) {
    if (p.empty() || !seen_inv.insert(p).second) {
      continue;
    }
    add_file_inventory(s, deps, p);
    s->inventory_paths.insert(p);
  }

  // Fill missing paths via search (definition-ish: first slice-path hit).
  for (auto& n : s->nodes) {
    if (n.kind != EffectNodeKind::Fn || !n.path.empty() || !deps.search) {
      continue;
    }
    const auto hits = deps.search(n.symbol);
    for (const auto& h : hits) {
      const std::string rel = rel_of(deps.workspace_root, h.path);
      if (rel.empty() || !is_slice_path(rel)) {
        continue;
      }
      n.path = rel;
      n.line = h.line;
      n.id = fn_id(n.path, n.symbol);
      n.stem = stem_of(n.path);
      n.anchor = n.path + ":" + n.symbol;
      if (s->add_siblings) {
        add_file_inventory(s, deps, n.path);
      }
      break;
    }
  }

  std::vector<std::string> u0;
  std::vector<std::string> hop_ids;
  std::vector<std::string> fresh;
  for (const auto& n : s->nodes) {
    if (n.kind != EffectNodeKind::Fn) {
      continue;
    }
    u0.push_back(n.id);
    if (s->hop_origin.count(n.id)) {
      continue;
    }
    fresh.push_back(n.id);
    if (s->generation == 0) {
      if (n.seed) {
        hop_ids.push_back(n.id);
      }
    } else {
      hop_ids.push_back(n.id);
    }
  }
  if (hop_ids.empty()) {
    hop_ids = fresh;
  }
  for (const auto& id : hop_ids) {
    s->hop_origin.insert(id);
  }
  summarize_ids_parallel(s, deps, u0);
  hop_down_from(s, deps, hop_ids);
  hop_up_from(s, deps, hop_ids);

  std::vector<std::string> newcomers;
  for (const auto& n : s->nodes) {
    if (n.kind == EffectNodeKind::Fn && !s->summarized.count(n.id)) {
      newcomers.push_back(n.id);
    }
  }
  summarize_ids_parallel(s, deps, newcomers);

  rebuild_latches_and_ctrl(s, deps);
  run_ppr(s);
  rank_constellations(s, kEffectSliceMaxConstellations);
  rank_macro_constellations(s, kEffectSliceMaxConstellations);
  rank_threads(s, kEffectSliceMaxThreads);
  compute_holes(s);
  return true;
}

std::vector<EffectThread> effect_slice_threads(const EffectSlice& s, int k) {
  std::vector<EffectThread> out;
  const int n = std::min(k, static_cast<int>(s.threads.size()));
  for (int i = 0; i < n; ++i) {
    out.push_back(s.threads[static_cast<std::size_t>(i)]);
  }
  return out;
}

std::vector<EffectConstellation> effect_slice_constellations(const EffectSlice& s, int k) {
  std::vector<EffectConstellation> out;
  const int n = std::min(k, static_cast<int>(s.constellations.size()));
  for (int i = 0; i < n; ++i) {
    out.push_back(s.constellations[static_cast<std::size_t>(i)]);
  }
  return out;
}

std::string effect_slice_stats_markdown(const EffectSlice& s) {
  std::ostringstream o;
  std::unordered_set<std::string> files;
  std::unordered_set<std::string> stems;
  for (const auto& n : s.nodes) {
    if (n.kind != EffectNodeKind::Fn) {
      continue;
    }
    if (!n.path.empty()) {
      files.insert(n.path);
    }
    if (!n.stem.empty()) {
      stems.insert(n.stem);
    }
  }
  o << "slice g=" << s.generation << " nodes=" << s.nodes.size() << " facts=" << s.facts.size()
    << " fn=" << effect_slice_count_kind(s, EffectNodeKind::Fn)
    << " ctrl=" << effect_slice_count_kind(s, EffectNodeKind::Ctrl)
    << " latch=" << effect_slice_count_kind(s, EffectNodeKind::Latch)
    << " handoff=" << effect_slice_count_kind(s, EffectNodeKind::Handoff)
    << " files=" << files.size() << " stems=" << stems.size()
    << " constellations=" << s.constellations.size() << " threads=" << s.threads.size();
  if (s.exhausted) {
    o << " exhausted";
  }
  o << "\n";
  if (!stems.empty()) {
    o << "stems:";
    std::vector<std::string> ordered(stems.begin(), stems.end());
    std::sort(ordered.begin(), ordered.end());
    for (const auto& st : ordered) {
      o << " " << st;
    }
    o << "\n";
  }
  if (!s.holes.empty()) {
    o << "holes:";
    for (const auto& h : s.holes) {
      o << " [" << h << "]";
    }
    o << "\n";
  }
  return o.str();
}

std::string effect_slice_view_markdown(const EffectSlice& s) {
  std::ostringstream o;
  o << effect_slice_stats_markdown(s);
  o << "\n### Constelaciones (C*)\n";
  if (s.constellations.empty()) {
    o << "_(vacío)_\n";
  }
  for (const auto& c : s.constellations) {
    o << "**" << c.id << "** score=" << c.score << " center=" << c.center_id << " — "
      << c.why << "\n";
    o << "  primary:";
    for (const auto& st : c.primary_stems) {
      o << " " << st;
    }
    if (!c.peripheral_stems.empty()) {
      o << " · peripheral:";
      for (const auto& st : c.peripheral_stems) {
        o << " " << st;
      }
    }
    if (!c.context_stems.empty()) {
      o << " · context:";
      for (const auto& st : c.context_stems) {
        o << " " << st;
      }
    }
    o << "\n";
  }
  o << "\n### Macroconstelaciones (M*)\n";
  if (s.macro_constellations.empty()) {
    o << "_(vacío)_\n";
  }
  for (const auto& m : s.macro_constellations) {
    o << "**" << m.id << "** score=" << m.score << " — " << m.why << "\n";
    o << "  nuclei:";
    for (const auto& id : m.nucleus_ids) {
      o << " " << id;
    }
    o << " · primary:";
    for (const auto& stem : m.primary_stems) {
      o << " " << stem;
    }
    if (!m.merge_witnesses.empty()) {
      o << " · merge=" << m.merge_strength << ":";
      for (const auto& witness : m.merge_witnesses) {
        o << " " << witness;
      }
    }
    o << "\n";
  }
  o << "\n### Hilos (T*)\n";
  if (s.threads.empty()) {
    o << "_(vacío)_\n";
  }
  for (const auto& th : s.threads) {
    o << "**" << th.id << "** score=" << static_cast<int>(th.score) << " — " << th.why << "\n";
    o << "  ";
    for (std::size_t i = 0; i < th.node_ids.size(); ++i) {
      if (i) {
        o << " → ";
      }
      const EffectNode* n = effect_slice_find_node(s, th.node_ids[i]);
      if (n == nullptr) {
        o << th.node_ids[i];
        continue;
      }
      if (n->kind == EffectNodeKind::Fn) {
        o << n->symbol;
      } else if (n->kind == EffectNodeKind::Latch) {
        o << "latch:" << n->symbol;
      } else if (n->kind == EffectNodeKind::Ctrl) {
        o << "[" << effect_ctrl_kind_name(n->ctrl_kind);
        if (!n->cond.empty()) {
          std::string c = n->cond;
          if (c.size() > 48) {
            c = c.substr(0, 48) + "…";
          }
          for (char& ch : c) {
            if (ch == '\n' || ch == '\r') {
              ch = ' ';
            }
          }
          o << " " << c;
        }
        o << "]";
      } else {
        o << "handoff→" << n->symbol;
      }
    }
    o << "\n";
  }
  return o.str();
}

void effect_slice_fail(EffectSlice* s, const std::vector<std::string>& rejected_ids,
                       const std::string& why) {
  if (s == nullptr) {
    return;
  }
  s->rejected_thread_ids = rejected_ids;
  std::unordered_set<std::string> rej(rejected_ids.begin(), rejected_ids.end());
  std::unordered_set<std::string> in_rejected;
  std::unordered_set<std::string> in_kept;
  for (const auto& th : s->threads) {
    auto* dst = rej.count(th.id) ? &in_rejected : &in_kept;
    for (const auto& id : th.node_ids) {
      dst->insert(id);
    }
  }
  for (auto& n : s->nodes) {
    if (in_rejected.count(n.id) && !in_kept.count(n.id)) {
      n.cold = true;
    }
  }
  if (!why.empty()) {
    s->holes.push_back(why);
  }
  compute_holes(s);
}

bool effect_slice_expand(EffectSlice* s, const EffectSliceDeps& deps, std::string* err) {
  if (s == nullptr) {
    if (err) {
      *err = "slice null";
    }
    return false;
  }
  if (s->generation + 1 >= kEffectSliceMaxExpand) {
    s->exhausted = true;
    return true;
  }
  const int before = static_cast<int>(s->nodes.size());
  std::vector<std::string> hot_files;
  std::vector<EffectNode> hot_fns;
  for (const auto& n : s->nodes) {
    if (n.kind == EffectNodeKind::Fn && !n.cold && n.mass > 0.01f) {
      hot_fns.push_back(n);
      if (!n.path.empty()) {
        hot_files.push_back(n.path);
      }
    }
  }
  for (const auto& p : hot_files) {
    add_siblings(s, deps, p);
  }
  if (deps.search) {
    int latch_hits = 0;
    for (const auto& n : s->nodes) {
      if (n.kind != EffectNodeKind::Latch || latch_hits >= 3) {
        continue;
      }
      const auto hits = deps.search(n.symbol);
      ++latch_hits;
      int added = 0;
      for (const auto& h : hits) {
        if (added >= 8) {
          break;
        }
        const std::string rel = rel_of(deps.workspace_root, h.path);
        if (rel.empty() || !is_slice_path(rel)) {
          continue;
        }
        const ATrailHop hop =
            a_trail_enrich_hop(abs_of(deps.workspace_root, rel), rel, h.line, n.symbol);
        if (hop.symbol.empty()) {
          continue;
        }
        EffectNode fn;
        fn.id = fn_id(rel, hop.symbol);
        fn.kind = EffectNodeKind::Fn;
        fn.path = rel;
        fn.symbol = hop.symbol;
        fn.line = hop.call_line;
        fn.stem = stem_of(rel);
        if (add_node(s, fn)) {
          ++added;
        }
      }
    }
  }
  if (static_cast<int>(s->nodes.size()) == before) {
    s->exhausted = true;
    return true;
  }
  ++s->generation;
  return effect_slice_build(s, deps, err);
}

nlohmann::json effect_slice_to_json(const EffectSlice& s) {
  nlohmann::json j;
  j["query"] = s.query;
  j["seeds"] = s.seeds;
  j["generation"] = s.generation;
  j["exhausted"] = s.exhausted;
  j["holes"] = s.holes;
  nlohmann::json nodes = nlohmann::json::array();
  for (const auto& n : s.nodes) {
    nodes.push_back({{"id", n.id},
                     {"kind", effect_node_kind_name(n.kind)},
                     {"ctrl_kind", effect_ctrl_kind_name(n.ctrl_kind)},
                     {"path", n.path},
                     {"symbol", n.symbol},
                     {"anchor", n.anchor},
                     {"stem", n.stem},
                     {"parent_fn", n.parent_fn},
                     {"parent_switch", n.parent_switch},
                     {"cond", n.cond},
                     {"line", n.line},
                     {"prior_sem", n.prior_sem},
                     {"mass", n.mass},
                     {"seed", n.seed},
                     {"query_hit", n.query_hit},
                     {"query_rank", n.query_rank},
                     {"cold", n.cold}});
  }
  j["nodes"] = std::move(nodes);
  nlohmann::json facts = nlohmann::json::array();
  for (const auto& f : s.facts) {
    facts.push_back({{"id", f.id},
                     {"from", f.from},
                     {"to", f.to},
                     {"kind", effect_fact_kind_name(f.kind)},
                     {"member", f.member},
                     {"w_edge", f.w_edge}});
  }
  j["facts"] = std::move(facts);
  nlohmann::json constellations = nlohmann::json::array();
  for (const auto& c : s.constellations) {
    constellations.push_back({{"id", c.id},
                              {"center_id", c.center_id},
                              {"member", c.member},
                              {"nodes", c.node_ids},
                              {"facts", c.fact_ids},
                              {"writers", c.writer_ids},
                              {"readers", c.reader_ids},
                              {"controls", c.control_ids},
                              {"handoffs", c.handoff_ids},
                              {"core_stems", c.core_stems},
                              {"context_stems", c.context_stems},
                              {"primary_stems", c.primary_stems},
                              {"peripheral_stems", c.peripheral_stems},
                              {"score", c.score},
                              {"mass_coverage", c.mass_coverage},
                              {"why", c.why}});
  }
  j["constellations"] = std::move(constellations);
  nlohmann::json macro_constellations = nlohmann::json::array();
  for (const auto& m : s.macro_constellations) {
    macro_constellations.push_back({{"id", m.id},
                                    {"nuclei", m.nucleus_ids},
                                    {"nodes", m.node_ids},
                                    {"anchor_groups", m.anchor_groups},
                                    {"primary_stems", m.primary_stems},
                                    {"merge_witnesses", m.merge_witnesses},
                                    {"merge_strength", m.merge_strength},
                                    {"score", m.score},
                                    {"mass_coverage", m.mass_coverage},
                                    {"why", m.why}});
  }
  j["macro_constellations"] = std::move(macro_constellations);
  nlohmann::json threads = nlohmann::json::array();
  for (const auto& t : s.threads) {
    threads.push_back({{"id", t.id},
                       {"nodes", t.node_ids},
                       {"facts", t.fact_ids},
                       {"latches", t.latches},
                       {"score", t.score},
                       {"why", t.why}});
  }
  j["threads"] = std::move(threads);
  return j;
}

bool effect_slice_from_json(const nlohmann::json& j, EffectSlice* out, std::string* err) {
  if (out == nullptr || !j.is_object()) {
    if (err) {
      *err = "json slice inválido";
    }
    return false;
  }
  *out = EffectSlice{};
  out->query = j.value("query", "");
  if (j.contains("seeds") && j["seeds"].is_array()) {
    for (const auto& x : j["seeds"]) {
      if (x.is_string()) {
        out->seeds.push_back(x.get<std::string>());
      }
    }
  }
  out->generation = j.value("generation", 0);
  out->exhausted = j.value("exhausted", false);
  if (j.contains("holes") && j["holes"].is_array()) {
    for (const auto& x : j["holes"]) {
      if (x.is_string()) {
        out->holes.push_back(x.get<std::string>());
      }
    }
  }
  if (j.contains("nodes") && j["nodes"].is_array()) {
    for (const auto& o : j["nodes"]) {
      EffectNode n;
      n.id = o.value("id", "");
      n.kind = parse_effect_node_kind(o.value("kind", "fn"));
      n.ctrl_kind = parse_effect_ctrl_kind(o.value("ctrl_kind", ""));
      n.path = o.value("path", "");
      n.symbol = o.value("symbol", "");
      n.anchor = o.value("anchor", "");
      n.stem = o.value("stem", "");
      n.parent_fn = o.value("parent_fn", "");
      n.parent_switch = o.value("parent_switch", "");
      n.cond = o.value("cond", "");
      n.line = o.value("line", 0);
      n.prior_sem = o.value("prior_sem", 0.f);
      n.mass = o.value("mass", 0.f);
      n.seed = o.value("seed", false);
      n.query_hit = o.value("query_hit", false);
      n.query_rank = o.value("query_rank", -1);
      n.cold = o.value("cold", false);
      out->nodes.push_back(std::move(n));
    }
  }
  if (j.contains("facts") && j["facts"].is_array()) {
    for (const auto& o : j["facts"]) {
      EffectFact f;
      f.id = o.value("id", "");
      f.from = o.value("from", "");
      f.to = o.value("to", "");
      f.kind = parse_effect_fact_kind(o.value("kind", "call"));
      f.member = o.value("member", "");
      f.w_edge = o.value("w_edge", 1.f);
      out->facts.push_back(std::move(f));
    }
  }
  if (j.contains("threads") && j["threads"].is_array()) {
    for (const auto& o : j["threads"]) {
      EffectThread t;
      t.id = o.value("id", "");
      if (o.contains("nodes")) {
        t.node_ids = o["nodes"].get<std::vector<std::string>>();
      }
      if (o.contains("facts")) {
        t.fact_ids = o["facts"].get<std::vector<std::string>>();
      }
      if (o.contains("latches")) {
        t.latches = o["latches"].get<std::vector<std::string>>();
      }
      t.score = o.value("score", 0.f);
      t.why = o.value("why", "");
      out->threads.push_back(std::move(t));
    }
  }
  if (j.contains("constellations") && j["constellations"].is_array()) {
    for (const auto& o : j["constellations"]) {
      EffectConstellation c;
      c.id = o.value("id", "");
      c.center_id = o.value("center_id", "");
      c.member = o.value("member", "");
      if (o.contains("nodes")) {
        c.node_ids = o["nodes"].get<std::vector<std::string>>();
      }
      if (o.contains("facts")) {
        c.fact_ids = o["facts"].get<std::vector<std::string>>();
      }
      if (o.contains("writers")) {
        c.writer_ids = o["writers"].get<std::vector<std::string>>();
      }
      if (o.contains("readers")) {
        c.reader_ids = o["readers"].get<std::vector<std::string>>();
      }
      if (o.contains("controls")) {
        c.control_ids = o["controls"].get<std::vector<std::string>>();
      }
      if (o.contains("handoffs")) {
        c.handoff_ids = o["handoffs"].get<std::vector<std::string>>();
      }
      if (o.contains("core_stems")) {
        c.core_stems = o["core_stems"].get<std::vector<std::string>>();
      }
      if (o.contains("context_stems")) {
        c.context_stems = o["context_stems"].get<std::vector<std::string>>();
      }
      if (o.contains("primary_stems")) {
        c.primary_stems = o["primary_stems"].get<std::vector<std::string>>();
      }
      if (o.contains("peripheral_stems")) {
        c.peripheral_stems = o["peripheral_stems"].get<std::vector<std::string>>();
      }
      c.score = o.value("score", 0.f);
      c.mass_coverage = o.value("mass_coverage", 0.f);
      c.why = o.value("why", "");
      out->constellations.push_back(std::move(c));
    }
  }
  if (j.contains("macro_constellations") && j["macro_constellations"].is_array()) {
    for (const auto& o : j["macro_constellations"]) {
      EffectMacroConstellation m;
      m.id = o.value("id", "");
      if (o.contains("nuclei")) {
        m.nucleus_ids = o["nuclei"].get<std::vector<std::string>>();
      }
      if (o.contains("nodes")) {
        m.node_ids = o["nodes"].get<std::vector<std::string>>();
      }
      if (o.contains("anchor_groups")) {
        m.anchor_groups = o["anchor_groups"].get<std::vector<std::vector<std::string>>>();
      }
      if (o.contains("primary_stems")) {
        m.primary_stems = o["primary_stems"].get<std::vector<std::string>>();
      }
      if (o.contains("merge_witnesses")) {
        m.merge_witnesses = o["merge_witnesses"].get<std::vector<std::string>>();
      }
      m.merge_strength = o.value("merge_strength", 0.f);
      m.score = o.value("score", 0.f);
      m.mass_coverage = o.value("mass_coverage", 0.f);
      m.why = o.value("why", "");
      out->macro_constellations.push_back(std::move(m));
    }
  }
  return true;
}

}  // namespace tuide
