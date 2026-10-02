#include "ai/l2_explore_a.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace tuide {
namespace {

namespace fs = std::filesystem;

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_srcish_rel(const std::string& rel) {
  return rel.rfind("src/", 0) == 0 || rel.rfind("include/", 0) == 0 || rel.rfind("lib/", 0) == 0;
}

std::string trim_copy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

// Strip // and rough /* */ noise for classification (not a full lexer).
std::string strip_line_comment(std::string line) {
  const auto q = line.find("//");
  if (q != std::string::npos) {
    // Keep if // is inside quotes — cheap check: odd number of " before //
    int quotes = 0;
    for (std::size_t i = 0; i < q; ++i) {
      if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
        ++quotes;
      }
    }
    if (quotes % 2 == 0) {
      line = line.substr(0, q);
    }
  }
  return line;
}

bool word_boundary_at(const std::string& s, std::size_t pos, std::size_t len) {
  if (pos > 0 && is_ident_char(s[pos - 1])) {
    return false;
  }
  if (pos + len < s.size() && is_ident_char(s[pos + len])) {
    return false;
  }
  return true;
}

// Find first word-boundary occurrence of name in line; npos if none.
std::size_t find_name_wb(const std::string& line, const std::string& name) {
  if (name.empty()) {
    return std::string::npos;
  }
  std::size_t pos = 0;
  while (pos < line.size()) {
    const auto f = line.find(name, pos);
    if (f == std::string::npos) {
      return std::string::npos;
    }
    if (word_boundary_at(line, f, name.size())) {
      return f;
    }
    pos = f + 1;
  }
  return std::string::npos;
}

std::string skip_ws(const std::string& s, std::size_t* i) {
  while (*i < s.size() && std::isspace(static_cast<unsigned char>(s[*i]))) {
    ++(*i);
  }
  return {};
}

bool looks_like_type_token(const std::string& tok) {
  if (tok.empty()) {
    return false;
  }
  static const char* kTypes[] = {"bool",   "int",    "char",   "void",   "auto",  "float",
                                 "double", "size_t", "int32_t", "int64_t", "uint32_t",
                                 "uint64_t", "string", "atomic", "optional", "unique_ptr",
                                 "shared_ptr", "vector", "mutex", "condition_variable"};
  for (const char* t : kTypes) {
    if (tok == t) {
      return true;
    }
  }
  // CamelCase / trailing _t
  if (tok.size() > 2 && tok.back() == 't' && tok[tok.size() - 2] == '_') {
    return true;
  }
  if (std::isupper(static_cast<unsigned char>(tok[0])) != 0) {
    return true;
  }
  return false;
}

std::string token_before(const std::string& line, std::size_t name_pos) {
  if (name_pos == 0) {
    return {};
  }
  std::size_t end = name_pos;
  while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
    --end;
  }
  if (end == 0) {
    return {};
  }
  // Skip :: qualifier chain → take last segment before name
  std::size_t start = end;
  while (start > 0) {
    const char c = line[start - 1];
    if (is_ident_char(c) || c == ':') {
      --start;
      continue;
    }
    break;
  }
  std::string tok = line.substr(start, end - start);
  // Drop leading ::
  while (tok.size() >= 2 && tok[0] == ':' && tok[1] == ':') {
    tok = tok.substr(2);
  }
  const auto colons = tok.rfind("::");
  if (colons != std::string::npos) {
    tok = tok.substr(colons + 2);
  }
  return tok;
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

std::string snippet_around(const std::string& workspace_root, const std::string& rel, int line,
                           int pad) {
  fs::path abs = fs::path(workspace_root) / rel;
  const std::string src = read_abs(abs.string());
  if (src.empty() || line <= 0) {
    return {};
  }
  const auto lines = split_lines(src);
  const int a = std::max(1, line - pad);
  const int b = std::min(static_cast<int>(lines.size()), line + pad);
  std::ostringstream out;
  for (int i = a; i <= b; ++i) {
    out << lines[static_cast<std::size_t>(i - 1)] << '\n';
  }
  return out.str();
}

}  // namespace

ADataFlowKind a_dataflow_classify_line(const std::string& raw_line, const std::string& name) {
  if (name.empty()) {
    return ADataFlowKind::Unknown;
  }
  const std::string line = strip_line_comment(raw_line);
  const auto pos = find_name_wb(line, name);
  if (pos == std::string::npos) {
    return ADataFlowKind::Unknown;
  }

  // Atomic / member call: name.load / .store / .exchange / .compare_exchange
  std::size_t after = pos + name.size();
  skip_ws(line, &after);
  if (after < line.size() && line[after] == '.') {
    ++after;
    skip_ws(line, &after);
    auto method_end = after;
    while (method_end < line.size() && is_ident_char(line[method_end])) {
      ++method_end;
    }
    const std::string method = line.substr(after, method_end - after);
    if (method == "store" || method == "exchange" || method == "compare_exchange_weak" ||
        method == "compare_exchange_strong" || method == "fetch_add" || method == "fetch_sub" ||
        method == "fetch_or" || method == "fetch_and" || method == "fetch_xor") {
      return ADataFlowKind::Write;
    }
    if (method == "load" || method == "wait") {
      return ADataFlowKind::Read;
    }
  }

  // Pre-inc/dec: ++name / --name
  if (pos >= 2) {
    const std::string pre = trim_copy(line.substr(0, pos));
    if (pre.size() >= 2 &&
        ((pre[pre.size() - 2] == '+' && pre.back() == '+') ||
         (pre[pre.size() - 2] == '-' && pre.back() == '-'))) {
      return ADataFlowKind::Write;
    }
  }

  // Post-inc/dec / assignment after name
  after = pos + name.size();
  skip_ws(line, &after);
  if (after + 1 < line.size() &&
      ((line[after] == '+' && line[after + 1] == '+') ||
       (line[after] == '-' && line[after + 1] == '-'))) {
    return ADataFlowKind::Write;
  }
  if (after < line.size()) {
    const char c0 = line[after];
    const char c1 = after + 1 < line.size() ? line[after + 1] : '\0';
    // = but not ==, != already handled by first char
    if (c0 == '=' && c1 != '=') {
      return ADataFlowKind::Write;
    }
    if ((c0 == '+' || c0 == '-' || c0 == '*' || c0 == '/' || c0 == '%' || c0 == '|' || c0 == '&' ||
         c0 == '^') &&
        c1 == '=') {
      return ADataFlowKind::Write;
    }
  }

  // Decl: Type name … ; or Type name = / name{…}
  const std::string before = token_before(line, pos);
  auto line_has_typeish_before = [&]() {
    if (!before.empty() && looks_like_type_token(before)) {
      return true;
    }
    // template / qualifier soup: std::atomic<bool> name_{…};
    const std::string head = line.substr(0, pos);
    return head.find("atomic") != std::string::npos || head.find("bool") != std::string::npos ||
           head.find("mutex") != std::string::npos || head.find('<') != std::string::npos;
  };
  after = pos + name.size();
  skip_ws(line, &after);
  const bool brace_or_semi_or_eq =
      (after < line.size() && (line[after] == '{' || line[after] == ';' ||
                               (line[after] == '=' && after + 1 < line.size() && line[after + 1] != '=')));
  if (line_has_typeish_before() && brace_or_semi_or_eq) {
    return ADataFlowKind::Decl;
  }
  if (!before.empty() && looks_like_type_token(before)) {
    return ADataFlowKind::Decl;
  }
  // Member decl in class: name_; at end of line-ish with type earlier
  if (!before.empty() && (line.find(';') != std::string::npos) &&
      (name.back() == '_' || before.find("atomic") != std::string::npos)) {
    if (looks_like_type_token(before) || before == "mutable" || before == "static") {
      return ADataFlowKind::Decl;
    }
  }

  return ADataFlowKind::Read;
}

ADataFlowReport a_dataflow_build(const std::string& workspace_root, const std::string& name,
                                 const std::string& path_hint,
                                 const std::vector<ATrailSearchHit>& hits, int max_writes,
                                 int max_reads, int max_decls) {
  ADataFlowReport r;
  r.name = name;
  r.path_hint = path_hint;
  r.raw_hits = static_cast<int>(hits.size());

  std::unordered_set<std::string> seen;
  auto key_of = [](const std::string& p, int line) {
    return p + ":" + std::to_string(line);
  };

  // Prefer path_hint matches first
  std::vector<ATrailSearchHit> ordered = hits;
  if (!path_hint.empty()) {
    std::stable_partition(ordered.begin(), ordered.end(), [&](const ATrailSearchHit& h) {
      return h.path.find(path_hint) != std::string::npos || path_hint.find(h.path) != std::string::npos;
    });
  }

  for (const auto& h : ordered) {
    if (h.path.empty() || h.line <= 0) {
      ++r.dropped;
      continue;
    }
    if (!is_srcish_rel(h.path)) {
      ++r.dropped;
      continue;
    }
    const std::string preview = h.preview.empty() ? "" : h.preview;
    if (find_name_wb(strip_line_comment(preview), name) == std::string::npos &&
        find_name_wb(preview, name) == std::string::npos) {
      // rg may match substrings; skip non-wb
      ++r.dropped;
      continue;
    }
    const std::string k = key_of(h.path, h.line);
    if (seen.count(k)) {
      continue;
    }
    seen.insert(k);

    ADataFlowKind kind = a_dataflow_classify_line(preview, name);
    if (kind == ADataFlowKind::Unknown) {
      // Try reading the real line from disk (preview may be truncated)
      fs::path abs = fs::path(workspace_root) / h.path;
      const auto lines = split_lines(read_abs(abs.string()));
      if (h.line >= 1 && h.line <= static_cast<int>(lines.size())) {
        kind = a_dataflow_classify_line(lines[static_cast<std::size_t>(h.line - 1)], name);
      }
    }
    if (kind == ADataFlowKind::Unknown) {
      kind = ADataFlowKind::Read;
    }

    ADataFlowSite site;
    site.path = h.path;
    site.line = h.line;
    site.kind = kind;
    site.preview = trim_copy(preview);
    if (site.preview.size() > 120) {
      site.preview = site.preview.substr(0, 117) + "…";
    }
    site.snippet = snippet_around(workspace_root, h.path, h.line, kADataFlowSnippetPad);

    auto* bucket = &r.reads;
    int* cap = &max_reads;
    if (kind == ADataFlowKind::Write) {
      bucket = &r.writes;
      cap = &max_writes;
    } else if (kind == ADataFlowKind::Decl) {
      bucket = &r.decls;
      cap = &max_decls;
    }
    if (static_cast<int>(bucket->size()) >= *cap) {
      ++r.dropped;
      continue;
    }
    bucket->push_back(std::move(site));
  }
  return r;
}

ADataFlowReport a_dataflow_build_with_search(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    int max_writes, int max_reads, int max_decls) {
  std::vector<ATrailSearchHit> hits;
  if (search) {
    hits = search(name);
  }
  return a_dataflow_build(workspace_root, name, path_hint, hits, max_writes, max_reads, max_decls);
}

std::string a_dataflow_markdown(const ADataFlowReport& r) {
  std::ostringstream out;
  out << "## Data-flow (rg, sin LSP) — `" << r.name << "`\n";
  if (!r.path_hint.empty()) {
    out << "hint: `" << r.path_hint << "`\n";
  }
  out << "raw_hits=" << r.raw_hits << " decls=" << r.decls.size() << " writes=" << r.writes.size()
      << " reads=" << r.reads.size() << " dropped=" << r.dropped << "\n\n";

  auto dump = [&](const char* title, const std::vector<ADataFlowSite>& xs, bool with_snip) {
    out << "### " << title << " (" << xs.size() << ")\n";
    if (xs.empty()) {
      out << "_(ninguno)_\n\n";
      return;
    }
    for (const auto& s : xs) {
      out << "- `" << s.path << ":" << s.line << "` — " << s.preview << "\n";
      if (with_snip && !s.snippet.empty()) {
        out << "```\n" << s.snippet;
        if (s.snippet.back() != '\n') {
          out << '\n';
        }
        out << "```\n";
      }
    }
    out << "\n";
  };

  dump("decls", r.decls, true);
  dump("writes", r.writes, true);
  dump("reads", r.reads, false);  // reads: one-liners only (budget)
  out << "_Heurística rg: no es SSA. Útil para reservar suspect_vars hacia Phase B._\n";
  return out.str();
}

nlohmann::json a_dataflow_to_json(const ADataFlowReport& r) {
  auto sites = [](const std::vector<ADataFlowSite>& xs) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : xs) {
      arr.push_back({{"path", s.path},
                     {"line", s.line},
                     {"kind", a_dataflow_kind_name(s.kind)},
                     {"preview", s.preview},
                     {"snippet", s.snippet}});
    }
    return arr;
  };
  return nlohmann::json{{"name", r.name},
                        {"path_hint", r.path_hint},
                        {"raw_hits", r.raw_hits},
                        {"dropped", r.dropped},
                        {"decls", sites(r.decls)},
                        {"writes", sites(r.writes)},
                        {"reads", sites(r.reads)}};
}

}  // namespace tuide
