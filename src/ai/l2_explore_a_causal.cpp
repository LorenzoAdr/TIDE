#include "ai/l2_explore_a.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tuide {
namespace {

namespace fs = std::filesystem;

std::string shorten_detail(std::string detail) {
  constexpr std::size_t kMax = 80;
  if (detail.size() <= kMax) {
    return detail;
  }
  detail.resize(kMax - 3);
  detail.append("...");
  return detail;
}

std::string hop_label(const ATrailHop& hop) {
  if (hop.scope_chain.find("::") != std::string::npos) {
    return hop.scope_chain;
  }
  if (!hop.symbol.empty()) {
    return hop.symbol;
  }
  return hop.anchor;
}

bool skip_causal_path(const std::string& rel) {
  if (rel.empty()) {
    return true;
  }
  return rel.find("third_party/") != std::string::npos || rel.rfind("build/", 0) == 0 ||
         rel.find("/build/") != std::string::npos;
}

ACausalFlowNode site_leaf(ACausalNodeKind kind, const std::string& name, const ADataFlowSite& site,
                          const std::string& detail) {
  ACausalFlowNode node;
  node.kind = kind;
  node.name = name;
  node.path = site.path;
  node.line = site.line;
  node.detail = shorten_detail(detail);
  node.preview = site.preview;
  return node;
}

struct CallerSite {
  std::string search_name;
  std::string name;
  std::string path;
  int line = 0;
  int control_line = 0;
  bool in_else = false;
  std::string detail;
};

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool keyword_call(const std::string& name) {
  return name == "if" || name == "for" || name == "while" || name == "switch" || name == "catch" ||
         name == "return" || name == "sizeof" || name == "decltype" || name == "static_assert" ||
         name == "co_await" || name == "co_yield";
}

std::vector<std::string> callees_in_snippet(const std::string& snippet, const std::string& self) {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;
  for (std::size_t i = 0; i < snippet.size(); ++i) {
    if (!is_ident_char(snippet[i]) || (i > 0 && is_ident_char(snippet[i - 1]))) {
      continue;
    }
    std::size_t j = i;
    while (j < snippet.size() && is_ident_char(snippet[j])) {
      ++j;
    }
    const std::string name = snippet.substr(i, j - i);
    std::size_t k = j;
    while (k < snippet.size() && std::isspace(static_cast<unsigned char>(snippet[k]))) {
      ++k;
    }
    if (k < snippet.size() && snippet[k] == '(' && !name.empty() && name != self &&
        !keyword_call(name) && seen.insert(name).second) {
      names.push_back(name);
      if (static_cast<int>(names.size()) >= kACausalCalleesPerHop) {
        break;
      }
    }
    i = j - 1;
  }
  return names;
}

std::vector<CallerSite> direct_callers(
    const std::string& workspace_root, const std::string& fn,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& search,
    ATrailParseCache* cache) {
  std::vector<CallerSite> out;
  if (fn.empty()) {
    return out;
  }
  const std::vector<ATrailSearchHit> hits = search(fn);
  std::unordered_set<std::string> seen;
  int looked = 0;
  for (const ATrailSearchHit& hit : hits) {
    if (looked >= 60 || static_cast<int>(out.size()) >= kACausalCallersPerFn) {
      break;
    }
    if (hit.line <= 0 || skip_causal_path(hit.path)) {
      continue;
    }
    ++looked;
    const std::string key = hit.path + ":" + std::to_string(hit.line);
    if (!seen.insert(key).second) {
      continue;
    }
    fs::path abs = hit.path;
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / hit.path;
    }
    const ATrailHop hop =
        a_trail_enrich_hop(abs.lexically_normal().string(), hit.path, hit.line, fn, cache);
    if (!hop.is_call_site || hop.symbol.empty() || hop.symbol == fn) {
      continue;
    }
    CallerSite site;
    site.search_name = hop.symbol;
    site.name = hop_label(hop);
    if (site.name.empty()) {
      site.name = hop.symbol;
    }
    site.path = hop.path.empty() ? hit.path : hop.path;
    site.line = hop.call_line > 0 ? hop.call_line : hit.line;
    site.control_line = hop.control_line;
    site.in_else = hop.control_in_else;
    site.detail = hop.control_cond;
    out.push_back(std::move(site));
  }
  return out;
}

ACausalFlowNode with_scope_condition(
    ACausalFlowNode continuation, const CallerSite& site, const std::string& workspace_root,
    const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int cond_depth, int* budget, ATrailParseCache* cache);

void chains_ending_at(
    const std::string& workspace_root, ACausalFlowNode leaf, const std::string& focus_symbol,
    int depth, std::unordered_set<std::string> seen,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& search, int* budget,
    std::vector<ACausalFlowNode>* out, ATrailParseCache* cache, int cond_depth) {
  if (out == nullptr || budget == nullptr || *budget <= 0) {
    return;
  }
  const std::vector<CallerSite> callers =
      (depth <= 0 || focus_symbol.empty())
          ? std::vector<CallerSite>{}
          : direct_callers(workspace_root, focus_symbol, search, cache);
  std::vector<CallerSite> fresh;
  for (const CallerSite& caller : callers) {
    if (!caller.search_name.empty() && !seen.count(caller.search_name)) {
      fresh.push_back(caller);
    }
  }
  if (fresh.empty()) {
    out->push_back(std::move(leaf));
    --(*budget);
    return;
  }
  for (const CallerSite& caller : fresh) {
    if (*budget <= 0) {
      break;
    }
    ACausalFlowNode node;
    node.kind = ACausalNodeKind::Caller;
    node.name = caller.name;
    node.path = caller.path;
    node.line = caller.line;
    ACausalFlowNode inner =
        with_scope_condition(leaf, caller, workspace_root, caller.path, search,
                             kACausalFlowMaxWrites, std::max(depth, 1), cond_depth, budget, cache);
    const bool opened = inner.kind != leaf.kind || inner.name != leaf.name || inner.line != leaf.line;
    if (!opened) {
      node.detail = shorten_detail(caller.detail);
      node.children.push_back(leaf);
    } else {
      node.children.push_back(std::move(inner));
    }
    std::unordered_set<std::string> next = seen;
    next.insert(caller.search_name);
    chains_ending_at(workspace_root, std::move(node), caller.search_name, depth - 1, std::move(next),
                     search, budget, out, cache, opened ? cond_depth - 1 : cond_depth);
  }
}

ATrailHop definition_hop(
    const std::string& workspace_root, const std::string& name,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& search,
    ATrailParseCache* cache) {
  const std::vector<ATrailSearchHit> hits = search(name);
  int looked = 0;
  for (const ATrailSearchHit& hit : hits) {
    if (looked >= 8) {
      break;
    }
    if (hit.line <= 0 || skip_causal_path(hit.path)) {
      continue;
    }
    ++looked;
    fs::path abs = hit.path;
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / hit.path;
    }
    ATrailHop hop =
        a_trail_enrich_hop(abs.lexically_normal().string(), hit.path, hit.line, name, cache);
    if (hop.symbol == name && !hop.is_call_site) {
      return hop;
    }
  }
  return {};
}

struct CauseOperand {
  std::string name;
  bool call = false;
};

bool skip_cause_word(const std::string& word) {
  return word == "if" || word == "else" || word == "for" || word == "while" || word == "switch" ||
         word == "case" || word == "return" || word == "true" || word == "false" ||
         word == "nullptr" || word == "sizeof" || word == "const" || word == "void" ||
         word == "bool" || word == "int" || word == "auto" || word == "this" || word == "operator";
}

void skip_cause_ws(const std::string& text, std::size_t* i) {
  while (*i < text.size() && std::isspace(static_cast<unsigned char>(text[*i]))) {
    ++(*i);
  }
}

std::vector<CauseOperand> scan_causes(const std::string& text, const std::string& skip_name,
                                      bool keep_calls) {
  std::vector<CauseOperand> out;
  std::unordered_set<std::string> seen;
  std::size_t i = 0;
  while (i < text.size() && static_cast<int>(out.size()) < 8) {
    if (!is_ident_char(text[i])) {
      ++i;
      continue;
    }
    const std::size_t start = i;
    while (i < text.size() && is_ident_char(text[i])) {
      ++i;
    }
    std::string chain = text.substr(start, i - start);
    bool dotted = false;
    while (true) {
      skip_cause_ws(text, &i);
      const bool arrow = i + 1 < text.size() && text[i] == '-' && text[i + 1] == '>';
      const bool dot = i < text.size() && text[i] == '.';
      if (!arrow && !dot) {
        break;
      }
      i += arrow ? 2 : 1;
      skip_cause_ws(text, &i);
      if (i >= text.size() || !is_ident_char(text[i])) {
        break;
      }
      const std::size_t name_at = i;
      while (i < text.size() && is_ident_char(text[i])) {
        ++i;
      }
      chain += arrow ? "->" : ".";
      chain += text.substr(name_at, i - name_at);
      dotted = true;
    }
    skip_cause_ws(text, &i);
    bool call = i < text.size() && text[i] == '(';
    std::string name = chain;
    if (call && dotted) {
      const auto dot = name.rfind('.');
      const auto arrow = name.rfind("->");
      std::size_t cut = std::string::npos;
      if (dot != std::string::npos) {
        cut = dot;
      }
      if (arrow != std::string::npos && (cut == std::string::npos || arrow > cut)) {
        cut = arrow;
      }
      if (cut != std::string::npos) {
        name = name.substr(0, cut);
      }
      call = false;
    }
    if (name == skip_name || skip_cause_word(name) || name.size() < 2 || !seen.insert(name).second) {
      continue;
    }
    if (call && !keep_calls) {
      continue;
    }
    if (!call && !dotted && !keep_calls) {
      continue;
    }
    CauseOperand operand;
    operand.name = std::move(name);
    operand.call = call;
    out.push_back(std::move(operand));
  }
  return out;
}

bool field_cause_name(const std::string& name) {
  return name.find('.') != std::string::npos || name.find("->") != std::string::npos ||
         (!name.empty() && name.back() == '_');
}

int member_dots(const std::string& name) {
  int dots = 0;
  for (char c : name) {
    if (c == '.') {
      ++dots;
    }
  }
  return dots;
}

std::string last_member(const std::string& name) {
  const auto dot = name.rfind('.');
  const auto arrow = name.rfind("->");
  std::size_t cut = std::string::npos;
  if (dot != std::string::npos) {
    cut = dot + 1;
  }
  if (arrow != std::string::npos && (cut == std::string::npos || arrow + 2 > cut)) {
    cut = arrow + 2;
  }
  if (cut == std::string::npos || cut >= name.size()) {
    return name;
  }
  return name.substr(cut);
}

// a.b.c and previous.a.b are the same field reached through a parameter.
// config_.mode stays intact so a two-part member still searches that text.
std::string causal_field_key(const std::string& name) {
  if (member_dots(name) < 2 && name.find("->") == std::string::npos) {
    return name;
  }
  const std::string tail = last_member(name);
  if (tail.empty() || tail == name) {
    return name;
  }
  // mode is too common; keep the qualifier so compile_commands.mode does not
  // collect every .mode in the tree. Long members are the field itself.
  if (tail.size() < 8 && tail.find('_') == std::string::npos && member_dots(name) >= 2) {
    const auto dot = name.rfind('.');
    const auto prev = dot > 0 ? name.rfind('.', dot - 1) : std::string::npos;
    return name.substr(prev == std::string::npos ? 0 : prev + 1);
  }
  return "." + tail;
}

bool member_access_line(const std::string& line, const std::string& member) {
  if (member.empty()) {
    return false;
  }
  const std::string dot = "." + member;
  const std::string arrow = "->" + member;
  std::size_t pos = 0;
  while (pos < line.size()) {
    const auto d = line.find(dot, pos);
    const auto a = line.find(arrow, pos);
    std::size_t at = std::string::npos;
    std::size_t len = 0;
    if (d != std::string::npos && (a == std::string::npos || d <= a)) {
      at = d + 1;
      len = member.size();
    } else if (a != std::string::npos) {
      at = a + 2;
      len = member.size();
    }
    if (at == std::string::npos) {
      return false;
    }
    const bool right_ok = at + len >= line.size() || !is_ident_char(line[at + len]);
    if (right_ok) {
      return true;
    }
    pos = at + len;
  }
  return false;
}

std::string strip_cause_comment(std::string line) {
  const auto cut = line.find("//");
  if (cut != std::string::npos) {
    line.resize(cut);
  }
  return line;
}

std::vector<std::string> read_cause_lines(const std::string& abs_path) {
  std::ifstream in(abs_path);
  std::vector<std::string> lines;
  if (!in) {
    return lines;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

bool line_binds_name(const std::string& raw, const std::string& name) {
  const std::string line = strip_cause_comment(raw);
  std::size_t pos = 0;
  while (pos < line.size()) {
    const auto found = line.find(name, pos);
    if (found == std::string::npos) {
      return false;
    }
    const bool left_ok = found == 0 || !is_ident_char(line[found - 1]);
    const bool right_ok =
        found + name.size() >= line.size() || !is_ident_char(line[found + name.size()]);
    if (left_ok && right_ok) {
      std::size_t after = found + name.size();
      while (after < line.size() && std::isspace(static_cast<unsigned char>(line[after]))) {
        ++after;
      }
      if (after < line.size() && line[after] == '=' &&
          (after + 1 >= line.size() || line[after + 1] != '=')) {
        return true;
      }
    }
    pos = found + name.size();
  }
  return false;
}

std::string function_on_line(const std::string& raw) {
  const std::string line = strip_cause_comment(raw);
  if (line.find("](") != std::string::npos || line.find("] (") != std::string::npos) {
    return {};
  }
  if (line.find('{') == std::string::npos || line.find('(') == std::string::npos) {
    return {};
  }
  const auto paren = line.find('(');
  const std::string head = line.substr(0, paren);
  if (head.find("if") != std::string::npos || head.find("for") != std::string::npos ||
      head.find("while") != std::string::npos || head.find("switch") != std::string::npos ||
      head.find("catch") != std::string::npos) {
    const auto kw = head.find_last_of(" \t");
    const std::string last = kw == std::string::npos ? head : head.substr(kw + 1);
    if (last == "if" || last == "for" || last == "while" || last == "switch" || last == "catch") {
      return {};
    }
  }
  std::size_t end = paren;
  while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
    --end;
  }
  std::size_t start = end;
  while (start > 0 && is_ident_char(line[start - 1])) {
    --start;
  }
  if (start == end) {
    return {};
  }
  return line.substr(start, end - start);
}

struct LocalBinding {
  int line = 0;
  std::string text;
  std::string statement;
  std::string function;
  int function_line = 0;
};

std::string statement_from(const std::vector<std::string>& lines, int line_no) {
  std::string text;
  if (line_no <= 0) {
    return text;
  }
  for (int n = 0; n < 8 && line_no + n <= static_cast<int>(lines.size()); ++n) {
    std::string piece = strip_cause_comment(lines[static_cast<std::size_t>(line_no + n - 1)]);
    while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.front()))) {
      piece.erase(piece.begin());
    }
    while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.back()))) {
      piece.pop_back();
    }
    if (piece.empty()) {
      continue;
    }
    if (!text.empty()) {
      text.push_back(' ');
    }
    text += piece;
    if (piece.find(';') != std::string::npos) {
      break;
    }
  }
  return text;
}

LocalBinding find_local_binding(const std::vector<std::string>& lines, int use_line,
                                const std::string& name) {
  LocalBinding binding;
  if (name.empty() || use_line <= 1 || lines.empty()) {
    return binding;
  }
  const int begin = std::max(1, use_line - 160);
  for (int line_no = use_line - 1; line_no >= begin; --line_no) {
    const std::string& raw = lines[static_cast<std::size_t>(line_no - 1)];
    if (!line_binds_name(raw, name)) {
      continue;
    }
    binding.line = line_no;
    binding.statement = statement_from(lines, line_no);
    binding.text = shorten_detail(binding.statement.empty() ? strip_cause_comment(raw)
                                                            : binding.statement);
    while (!binding.text.empty() &&
           std::isspace(static_cast<unsigned char>(binding.text.front()))) {
      binding.text.erase(binding.text.begin());
    }
    for (int fn_line = line_no; fn_line >= begin; --fn_line) {
      const std::string fn = function_on_line(lines[static_cast<std::size_t>(fn_line - 1)]);
      if (!fn.empty() && fn != name) {
        binding.function = fn;
        binding.function_line = fn_line;
        break;
      }
    }
    break;
  }
  return binding;
}

void append_write_chains(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int* budget, std::vector<ACausalFlowNode>* out, ATrailParseCache* parse_cache,
    int cond_depth) {
  if (out == nullptr || budget == nullptr || *budget <= 0 || name.empty()) {
    return;
  }
  std::string needle = name;
  bool member_only = false;
  if (needle.size() > 1 && needle.front() == '.') {
    needle = needle.substr(1);
    member_only = true;
  }
  std::vector<ATrailSearchHit> hits = cached(needle);
  if (member_only) {
    std::vector<ATrailSearchHit> kept;
    for (const ATrailSearchHit& hit : hits) {
      if (member_access_line(hit.preview, needle)) {
        kept.push_back(hit);
      }
    }
    hits = std::move(kept);
  }
  ADataFlowReport report =
      a_dataflow_build(workspace_root, needle, path_hint, hits, max_writes, 0, 0);
  if (report.writes.empty() && member_dots(name) >= 2 && !member_only) {
    const std::string tail = last_member(name);
    const bool broad = tail.size() < 8 && tail.find('_') == std::string::npos;
    const auto mid_dot = name.rfind('.');
    std::string mid;
    if (mid_dot != std::string::npos && mid_dot > 0) {
      const auto prev = name.rfind('.', mid_dot - 1);
      mid = name.substr(prev == std::string::npos ? 0 : prev + 1, mid_dot - (prev == std::string::npos ? 0 : prev + 1));
    }
    std::vector<ATrailSearchHit> kept;
    for (const ATrailSearchHit& hit : cached(tail)) {
      if (!member_access_line(hit.preview, tail)) {
        continue;
      }
      if (broad && !mid.empty() && hit.preview.find(mid) == std::string::npos) {
        continue;
      }
      kept.push_back(hit);
    }
    needle = tail;
    report = a_dataflow_build(workspace_root, needle, path_hint, kept, max_writes, 0, 0);
  }
  for (const ADataFlowSite& site : report.writes) {
    if (*budget <= 0) {
      break;
    }
    fs::path abs = site.path;
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / site.path;
    }
    const ATrailHop hop =
        a_trail_enrich_hop(abs.lexically_normal().string(), site.path, site.line, "", parse_cache);
    const std::string fn = hop.symbol;
    const std::string write_name = fn.empty() ? std::string("write") : hop_label(hop);
    CallerSite scope;
    scope.search_name = fn;
    scope.path = site.path;
    scope.line = site.line;
    scope.control_line = hop.control_line;
    scope.in_else = hop.control_in_else;
    scope.detail = hop.control_cond;
    ACausalFlowNode leaf = site_leaf(ACausalNodeKind::Write, write_name, site, {});
    leaf = with_scope_condition(std::move(leaf), scope, workspace_root, site.path, cached, max_writes,
                                max_depth, cond_depth, budget, parse_cache);
    std::unordered_set<std::string> seen;
    if (!fn.empty()) {
      seen.insert(fn);
    }
    chains_ending_at(workspace_root, std::move(leaf), fn, max_depth, std::move(seen), cached, budget,
                     out, parse_cache, cond_depth);
  }
}

ACausalFlowNode cause_operand_node(
    const std::string& workspace_root, const CauseOperand& operand, const std::string& path_hint,
    const ATrailHop& call_hop,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int* budget, ATrailParseCache* parse_cache, int cond_depth) {
  ACausalFlowNode node;
  node.kind = ACausalNodeKind::Guard;
  node.name = operand.name.size() > 1 && operand.name.front() == '.' ? operand.name.substr(1)
                                                                     : operand.name;
  node.path = call_hop.path;
  node.line = call_hop.call_line;
  if (!operand.call && !field_cause_name(operand.name)) {
    fs::path abs = call_hop.path.empty() ? fs::path(path_hint) : fs::path(call_hop.path);
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / abs;
    }
    const LocalBinding binding =
        find_local_binding(read_cause_lines(abs.lexically_normal().string()), call_hop.call_line,
                           operand.name);
    if (binding.line > 0) {
      node.name = binding.text;
      node.line = binding.line;
      ATrailHop binding_hop = call_hop;
      binding_hop.call_line = binding.line;
      std::unordered_set<std::string> field_seen;
      for (const CauseOperand& field : scan_causes(binding.statement, operand.name, false)) {
        if (!field_cause_name(field.name)) {
          continue;
        }
        CauseOperand keyed = field;
        keyed.name = causal_field_key(field.name);
        if (!field_seen.insert(keyed.name).second) {
          continue;
        }
        int field_budget = 4;
        node.children.push_back(cause_operand_node(workspace_root, keyed, path_hint, binding_hop,
                                                   cached, max_writes, max_depth, &field_budget,
                                                   parse_cache, cond_depth));
      }
      if (node.children.empty() && !binding.function.empty()) {
        ACausalFlowNode owner;
        owner.kind = ACausalNodeKind::Caller;
        owner.name = binding.function;
        owner.path = node.path;
        owner.line = binding.function_line;
        node.children.push_back(std::move(owner));
      }
    }
    return node;
  }
  if (!operand.call) {
    append_write_chains(workspace_root, operand.name, path_hint, cached, max_writes, max_depth,
                        budget, &node.children, parse_cache, cond_depth);
    return node;
  }
  const ATrailHop def = definition_hop(workspace_root, operand.name, cached, parse_cache);
  if (!def.path.empty()) {
    node.path = def.path;
    node.line = def.call_line;
  }
  for (const CauseOperand& read : scan_causes(def.snippet, operand.name, false)) {
    if (budget != nullptr && *budget <= 0) {
      break;
    }
    ACausalFlowNode step;
    step.kind = ACausalNodeKind::Guard;
    step.name = read.name;
    step.path = def.path.empty() ? call_hop.path : def.path;
    step.line = def.call_line > 0 ? def.call_line : call_hop.call_line;
    append_write_chains(workspace_root, read.name, path_hint, cached, max_writes, max_depth, budget,
                        &step.children, parse_cache, cond_depth);
    node.children.push_back(std::move(step));
  }
  return node;
}

bool attach_call_cause(
    ACausalFlowTree* tree, const std::string& workspace_root, const std::string& name,
    const std::string& path_hint, int anchor_line,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int* budget, ATrailParseCache* parse_cache) {
  if (tree == nullptr || anchor_line <= 0 || path_hint.empty() || name.empty()) {
    return false;
  }
  fs::path abs = path_hint;
  if (!abs.is_absolute()) {
    abs = fs::path(workspace_root) / path_hint;
  }
  const ATrailHop hop =
      a_trail_enrich_hop(abs.lexically_normal().string(), path_hint, anchor_line, name, parse_cache);
  if (!hop.is_call_site) {
    return false;
  }
  std::string condition = hop.control_cond;
  const auto cut = condition.find(" · ");
  if (cut != std::string::npos) {
    condition = condition.substr(0, cut);
  }
  const std::vector<CauseOperand> operands = scan_causes(condition, name, true);
  if (condition.empty() || operands.empty()) {
    return false;
  }
  ACausalFlowNode guard;
  guard.kind = ACausalNodeKind::Guard;
  guard.name = shorten_detail(hop.control_in_else ? "else · " + condition : condition);
  guard.detail = guard.name;
  guard.path = hop.path.empty() ? path_hint : hop.path;
  guard.line = hop.control_line > 0 ? hop.control_line
                                    : (hop.call_line > 0 ? hop.call_line : anchor_line);
  for (const CauseOperand& operand : operands) {
    if (budget != nullptr && *budget <= 0) {
      break;
    }
    guard.children.push_back(cause_operand_node(workspace_root, operand, path_hint, hop, cached,
                                                max_writes, max_depth, budget, parse_cache, 2));
  }
  tree->root.children.push_back(std::move(guard));
  return true;
}

ACausalFlowNode with_scope_condition(
    ACausalFlowNode continuation, const CallerSite& site, const std::string& workspace_root,
    const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int cond_depth, int* budget, ATrailParseCache* cache) {
  if (cond_depth <= 0 || site.detail.empty()) {
    if (continuation.detail.empty()) {
      continuation.detail = shorten_detail(site.detail);
    }
    return continuation;
  }
  std::string condition = site.detail;
  const auto cut = condition.find(" · ");
  if (cut != std::string::npos) {
    condition = condition.substr(0, cut);
  }
  const std::vector<CauseOperand> operands = scan_causes(condition, site.search_name, true);
  if (operands.empty()) {
    if (continuation.detail.empty()) {
      continuation.detail = shorten_detail(condition);
    }
    return continuation;
  }
  ACausalFlowNode guard;
  guard.kind = ACausalNodeKind::Guard;
  guard.name = shorten_detail(site.in_else ? "else · " + condition : condition);
  guard.detail = guard.name;
  guard.path = site.path;
  guard.line = site.control_line > 0 ? site.control_line : site.line;
  ATrailHop hop;
  hop.path = site.path;
  hop.call_line = site.line > 0 ? site.line : guard.line;
  int reserved = 8;
  for (const CauseOperand& operand : operands) {
    guard.children.push_back(cause_operand_node(workspace_root, operand, path_hint, hop, cached,
                                                max_writes, max_depth, &reserved, cache,
                                                cond_depth - 1));
  }
  (void)budget;
  guard.children.push_back(std::move(continuation));
  return guard;
}

void add_downstream(
    const std::string& workspace_root, ACausalFlowNode* node, const std::string& snippet,
    const std::string& self, int hops,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& search,
    std::unordered_set<std::string>* seen, ATrailParseCache* cache) {
  if (node == nullptr || hops <= 0 || seen == nullptr) {
    return;
  }
  const std::vector<std::string> callees = callees_in_snippet(snippet, self);
  for (const std::string& name : callees) {
    if (seen->count(name)) {
      continue;
    }
    seen->insert(name);
    ACausalFlowNode child;
    child.kind = ACausalNodeKind::Callee;
    child.name = name;
    const ATrailHop def = definition_hop(workspace_root, name, search, cache);
    child.path = def.path;
    child.line = def.call_line;
    child.detail = shorten_detail(def.control_cond);
    if (hops > 1 && !def.snippet.empty()) {
      add_downstream(workspace_root, &child, def.snippet, name, hops - 1, search, seen, cache);
    }
    node->children.push_back(std::move(child));
    seen->erase(name);
  }
}

std::string clicked_line_text(const std::string& abs_path, int line_no) {
  const std::vector<std::string> lines = read_cause_lines(abs_path);
  if (line_no <= 0 || line_no > static_cast<int>(lines.size())) {
    return {};
  }
  std::string text = strip_cause_comment(lines[static_cast<std::size_t>(line_no - 1)]);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  return text;
}

int enclosing_function_line(const std::vector<std::string>& lines, int line_no,
                            const std::string& fn) {
  if (fn.empty() || line_no <= 0) {
    return 0;
  }
  const int begin = std::max(1, line_no - 400);
  for (int n = line_no; n >= begin; --n) {
    if (n > static_cast<int>(lines.size())) {
      continue;
    }
    if (function_on_line(lines[static_cast<std::size_t>(n - 1)]) == fn) {
      return n;
    }
  }
  return 0;
}

// Tree-sitter at the click. Condition operands are climbed; `name` itself is not searched.
ACausalFlowTree build_causal_at_point(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int max_stacks, int anchor_line, ATrailParseCache* parse_cache, bool* is_call) {
  ACausalFlowTree tree;
  tree.name = name;
  tree.root.kind = ACausalNodeKind::Root;
  tree.root.name = name;
  if (is_call != nullptr) {
    *is_call = false;
  }
  if (anchor_line <= 0 || path_hint.empty() || name.empty()) {
    return tree;
  }
  fs::path abs = path_hint;
  if (!abs.is_absolute()) {
    abs = fs::path(workspace_root) / path_hint;
  }
  const std::string abs_path = abs.lexically_normal().string();
  const ATrailHop hop = a_trail_enrich_hop(abs_path, path_hint, anchor_line, name, parse_cache);
  const std::string text = clicked_line_text(abs_path, anchor_line);
  const bool call = hop.is_call_site ||
                    (text.find(name + "(") != std::string::npos ||
                     text.find(name + " (") != std::string::npos);
  if (is_call != nullptr) {
    *is_call = call;
  }
  ACausalFlowNode clicked;
  clicked.kind = ACausalNodeKind::Write;
  clicked.name = text.empty() ? name : text;
  clicked.path = path_hint;
  clicked.line = anchor_line;
  int budget = std::max(max_stacks, 8);
  ACausalFlowNode body = clicked;
  if (!hop.control_cond.empty()) {
    CallerSite scope;
    scope.search_name = name;
    scope.path = path_hint;
    scope.line = anchor_line;
    scope.control_line = hop.control_line;
    scope.in_else = hop.control_in_else;
    scope.detail = hop.control_cond;
    body = with_scope_condition(std::move(clicked), scope, workspace_root, path_hint, cached,
                                max_writes, max_depth, 2, &budget, parse_cache);
  }
  const std::vector<std::string> lines = read_cause_lines(abs_path);
  std::string fn = hop.symbol;
  if (fn.empty() || fn == name) {
    const int begin = std::max(1, anchor_line - 400);
    for (int n = anchor_line; n >= begin; --n) {
      if (n > static_cast<int>(lines.size())) {
        continue;
      }
      const std::string found = function_on_line(lines[static_cast<std::size_t>(n - 1)]);
      if (!found.empty() && found != name) {
        fn = found;
        break;
      }
    }
  }
  if (!fn.empty() && fn != name) {
    ACausalFlowNode owner;
    owner.kind = ACausalNodeKind::Caller;
    owner.name = fn;
    owner.path = path_hint;
    owner.line = enclosing_function_line(lines, anchor_line, fn);
    owner.children.push_back(std::move(body));
    tree.root.children.push_back(std::move(owner));
  } else if (!body.name.empty()) {
    tree.root.children.push_back(std::move(body));
  }
  return tree;
}

}  // namespace

ACausalFlowTree a_causal_flow_build(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    int max_writes, int max_stacks, int max_depth, int anchor_line, ACausalFlowScope scope) {
  ACausalFlowTree tree;
  tree.name = name;
  tree.root.kind = ACausalNodeKind::Root;
  tree.root.name = name;
  if (name.empty() || !search) {
    return tree;
  }
  if (max_writes <= 0) {
    max_writes = kACausalFlowMaxWrites;
  }
  if (max_stacks <= 0) {
    max_stacks = kACausalMaxChains;
  }
  if (max_depth <= 0) {
    max_depth = kACausalUpstreamDepth;
  }

  std::unordered_map<std::string, std::vector<ATrailSearchHit>> cache;
  auto cached = [&](const std::string& symbol) -> std::vector<ATrailSearchHit> {
    const auto it = cache.find(symbol);
    if (it != cache.end()) {
      return it->second;
    }
    std::vector<ATrailSearchHit> hits = search(symbol);
    cache.emplace(symbol, hits);
    return hits;
  };

  ATrailParseCache parse_cache;
  int budget = max_stacks;
  bool anchor_call = false;
  if (anchor_line > 0) {
    ACausalFlowTree site = build_causal_at_point(workspace_root, name, path_hint, cached, max_writes,
                                                 max_depth, max_stacks, anchor_line, &parse_cache,
                                                 &anchor_call);
    if (scope == ACausalFlowScope::Site) {
      return site;
    }
    if (anchor_call) {
      tree.root.children = std::move(site.root.children);
      for (const CallerSite& caller : direct_callers(workspace_root, name, cached, &parse_cache)) {
        if (caller.line == anchor_line &&
            (caller.path == path_hint || caller.path == site.root.path)) {
          continue;
        }
        fs::path abs = caller.path;
        if (!abs.is_absolute()) {
          abs = fs::path(workspace_root) / caller.path;
        }
        const std::string call_text = clicked_line_text(abs.lexically_normal().string(), caller.line);
        ACausalFlowNode call;
        call.kind = ACausalNodeKind::Write;
        call.name = call_text.empty() ? name : call_text;
        call.path = caller.path;
        call.line = caller.line;
        call = with_scope_condition(std::move(call), caller, workspace_root, caller.path, cached,
                                    max_writes, max_depth, 2, &budget, &parse_cache);
        ACausalFlowNode owner;
        owner.kind = ACausalNodeKind::Caller;
        owner.name = caller.name.empty() ? caller.search_name : caller.name;
        owner.path = caller.path;
        owner.line = caller.line;
        owner.children.push_back(std::move(call));
        tree.root.children.push_back(std::move(owner));
      }
      return tree;
    }
  } else if (scope == ACausalFlowScope::Site) {
    return tree;
  }

  const std::vector<ATrailSearchHit> hits = cached(name);
  const ADataFlowReport report =
      a_dataflow_build(workspace_root, name, path_hint, hits, max_writes, kADataFlowMaxReads,
                       kADataFlowMaxDecls);
  for (const ADataFlowSite& site : report.writes) {
    if (budget <= 0) {
      break;
    }
    fs::path abs = site.path;
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / site.path;
    }
    const ATrailHop hop =
        a_trail_enrich_hop(abs.lexically_normal().string(), site.path, site.line, "", &parse_cache);
    const std::string fn = hop.symbol;
    const std::string write_name = fn.empty() ? std::string("write") : hop_label(hop);
    CallerSite scope;
    scope.search_name = fn;
    scope.path = site.path;
    scope.line = site.line;
    scope.control_line = hop.control_line;
    scope.in_else = hop.control_in_else;
    scope.detail = hop.control_cond;
    ACausalFlowNode leaf = site_leaf(ACausalNodeKind::Write, write_name, site, {});
    leaf = with_scope_condition(std::move(leaf), scope, workspace_root, site.path, cached,
                                max_writes, max_depth, 2, &budget, &parse_cache);
    std::unordered_set<std::string> seen;
    if (!fn.empty()) {
      seen.insert(fn);
    }
    chains_ending_at(workspace_root, std::move(leaf), fn, max_depth, std::move(seen), cached,
                     &budget, &tree.root.children, &parse_cache, 2);
  }

  for (const ADataFlowSite& site : report.decls) {
    tree.root.children.push_back(site_leaf(ACausalNodeKind::Decl, "decl", site, {}));
  }
  for (const ADataFlowSite& site : report.reads) {
    fs::path abs = site.path;
    if (!abs.is_absolute()) {
      abs = fs::path(workspace_root) / site.path;
    }
    const ATrailHop hop =
        a_trail_enrich_hop(abs.lexically_normal().string(), site.path, site.line, "", &parse_cache);
    const std::string fn = hop.symbol;
    const std::string read_name = fn.empty() ? std::string("read") : hop_label(hop);
    ACausalFlowNode node = site_leaf(ACausalNodeKind::Read, read_name, site, hop.control_cond);
    if (!fn.empty()) {
      std::unordered_set<std::string> seen;
      seen.insert(fn);
      add_downstream(workspace_root, &node, hop.snippet, fn, kACausalDownstreamHops, cached, &seen,
                     &parse_cache);
    }
    tree.root.children.push_back(std::move(node));
  }
  return tree;
}

}  // namespace tuide
