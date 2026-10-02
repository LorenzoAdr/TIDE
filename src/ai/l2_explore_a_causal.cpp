#include "ai/l2_explore_a.hpp"

#include <cctype>
#include <filesystem>
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
    site.detail = hop.control_cond;
    out.push_back(std::move(site));
  }
  return out;
}

void chains_ending_at(
    const std::string& workspace_root, ACausalFlowNode leaf, const std::string& focus_symbol,
    int depth, std::unordered_set<std::string> seen,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& search, int* budget,
    std::vector<ACausalFlowNode>* out, ATrailParseCache* cache) {
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
    node.detail = shorten_detail(caller.detail);
    node.children.push_back(leaf);
    std::unordered_set<std::string> next = seen;
    next.insert(caller.search_name);
    chains_ending_at(workspace_root, std::move(node), caller.search_name, depth - 1, std::move(next),
                     search, budget, out, cache);
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

void append_write_chains(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int* budget, std::vector<ACausalFlowNode>* out, ATrailParseCache* parse_cache) {
  if (out == nullptr || budget == nullptr || *budget <= 0 || name.empty()) {
    return;
  }
  const ADataFlowReport report = a_dataflow_build(workspace_root, name, path_hint, cached(name),
                                                   max_writes, 0, 0);
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
    ACausalFlowNode leaf = site_leaf(ACausalNodeKind::Write, write_name, site, hop.control_cond);
    std::unordered_set<std::string> seen;
    if (!fn.empty()) {
      seen.insert(fn);
    }
    chains_ending_at(workspace_root, std::move(leaf), fn, max_depth, std::move(seen), cached, budget,
                     out, parse_cache);
  }
}

ACausalFlowNode cause_operand_node(
    const std::string& workspace_root, const CauseOperand& operand, const std::string& path_hint,
    const ATrailHop& call_hop,
    const std::function<std::vector<ATrailSearchHit>(const std::string&)>& cached, int max_writes,
    int max_depth, int* budget, ATrailParseCache* parse_cache) {
  ACausalFlowNode node;
  node.kind = ACausalNodeKind::Guard;
  node.name = operand.name;
  node.path = call_hop.path;
  node.line = call_hop.call_line;
  if (!operand.call) {
    append_write_chains(workspace_root, operand.name, path_hint, cached, max_writes, max_depth,
                        budget, &node.children, parse_cache);
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
                        &step.children, parse_cache);
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
  guard.name = shorten_detail(condition);
  guard.path = hop.path.empty() ? path_hint : hop.path;
  guard.line = hop.call_line > 0 ? hop.call_line : anchor_line;
  for (const CauseOperand& operand : operands) {
    if (budget != nullptr && *budget <= 0) {
      break;
    }
    guard.children.push_back(cause_operand_node(workspace_root, operand, path_hint, hop, cached,
                                                max_writes, max_depth, budget, parse_cache));
  }
  tree->root.children.push_back(std::move(guard));
  return true;
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

}  // namespace

ACausalFlowTree a_causal_flow_build(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    int max_writes, int max_stacks, int max_depth, int anchor_line) {
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
  if (attach_call_cause(&tree, workspace_root, name, path_hint, anchor_line, cached, max_writes,
                        max_depth, &budget, &parse_cache)) {
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
    ACausalFlowNode leaf = site_leaf(ACausalNodeKind::Write, write_name, site, hop.control_cond);
    std::unordered_set<std::string> seen;
    if (!fn.empty()) {
      seen.insert(fn);
    }
    chains_ending_at(workspace_root, std::move(leaf), fn, max_depth, std::move(seen), cached,
                     &budget, &tree.root.children, &parse_cache);
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
