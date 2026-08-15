#include "ai/level2_session.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/ai_trace.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace {

const std::unordered_set<std::string>& l2_whitelist() {
  static const std::unordered_set<std::string> k = {
      "get_code_of",     "search",
      "repo_map",        "read_file",
      "list_files",      "workspace_symbols",
      "hover",           "diagnostics",
      "context_pack",    "list_tools",
      "file_outline",    "headers_of",
      "definition",      "references",
  };
  return k;
}

std::string json_escape(const std::string& s) {
  return ai_trace_escape(s, 8000);
}

std::string now_ms_str() {
  using clock = std::chrono::system_clock;
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
          .count();
  return std::to_string(ms);
}

std::string trim_ws(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' ||
                        s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' ||
                        s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

// path from "path:Symbol" / "path:42" / "path:A-B" / "path:Sym#tail" / bare path.
std::string path_from_plan_target(const std::string& target) {
  std::string t = trim_ws(target);
  if (t.empty()) {
    return {};
  }
  const auto hash = t.rfind('#');
  if (hash != std::string::npos && hash + 1 < t.size()) {
    std::string w = t.substr(hash + 1);
    for (char& c : w) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    if (w == "head" || w == "mid" || w == "tail") {
      t = trim_ws(t.substr(0, hash));
    }
  }
  const auto colon = t.rfind(':');
  if (colon != std::string::npos && colon > 0 && colon + 1 < t.size()) {
    const std::string left = t.substr(0, colon);
    if (left.find('/') != std::string::npos || left.find('\\') != std::string::npos ||
        left.find('.') != std::string::npos) {
      return left;
    }
  }
  return t;
}

bool replace_ranked_map_in_session(const std::string& session_path, const std::string& map_body,
                                   std::string* err) {
  std::ifstream in(session_path);
  if (!in) {
    if (err) {
      *err = "session.md ausente";
    }
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string session = ss.str();
  const std::string map_mark = "## Ranked map";
  const std::string obs_mark = "## Observations";
  const auto map_pos = session.find(map_mark);
  if (map_pos == std::string::npos) {
    if (err) {
      *err = "sin ## Ranked map";
    }
    return false;
  }
  const auto obs_pos = session.find(obs_mark, map_pos);
  std::string new_map = map_body;
  if (!new_map.empty() && new_map.back() != '\n') {
    new_map.push_back('\n');
  }
  const std::string replacement = map_mark + "\n\n" + new_map + "\n";
  if (obs_pos == std::string::npos) {
    session = session.substr(0, map_pos) + replacement;
  } else {
    session = session.substr(0, map_pos) + replacement + session.substr(obs_pos);
  }
  std::ofstream out(session_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir session.md";
    }
    return false;
  }
  out << session;
  return true;
}

std::string truncate_to_budget(std::string text, std::size_t max_chars, const std::string& hint) {
  if (text.size() <= max_chars) {
    return text;
  }
  if (max_chars < 80) {
    return text.substr(0, max_chars);
  }
  // Head + tail so Search/Replace keeps signature and closing region.
  const std::size_t head = (max_chars * 55) / 100;
  const std::size_t tail = max_chars > head + 70 ? max_chars - head - 70 : max_chars / 4;
  std::ostringstream out;
  out << text.substr(0, head);
  out << "\n…[truncated en pack";
  if (!hint.empty()) {
    out << "; refetch get_code_of `" << hint << "` (o `#tail` / `path:A-B`)";
  }
  out << "]…\n";
  out << text.substr(text.size() - tail);
  return out.str();
}

// Keep the middle of an already line-focused window (head+tail would drop the locus).
std::string truncate_center_budget(std::string text, std::size_t max_chars, const std::string& hint,
                                   const std::vector<std::string>& prefer_needles = {},
                                   bool prefer_first_hit = false) {
  if (text.size() <= max_chars) {
    return text;
  }
  if (max_chars < 80) {
    return text.substr(0, max_chars);
  }
  const std::size_t keep = max_chars > 60 ? max_chars - 60 : max_chars;
  std::size_t mid = text.size() / 2;
  // Prefer last hit of identifier-like needles (edit site often after many earlier matches).
  // When prefer_first_hit: role markers at the front of prefer_needles win (decl/array loci).
  std::string low = text;
  for (char& c : low) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  int best = -1;
  int idx = 0;
  for (const auto& n : prefer_needles) {
    ++idx;
    if (n.size() < 4) {
      continue;
    }
    bool identish = n.find('_') != std::string::npos || n.find("tab") != std::string::npos ||
                    n.find("press") != std::string::npos || n.find("render") != std::string::npos ||
                    n.find("console") != std::string::npos || n.find("button") != std::string::npos ||
                    n.find("array") != std::string::npos || n.find("target") != std::string::npos ||
                    n.find("struct") != std::string::npos || n.find("enum") != std::string::npos ||
                    n.find("string_view") != std::string::npos;
    if (!identish) {
      continue;
    }
    std::string nlow = n;
    for (char& c : nlow) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const auto p = prefer_first_hit ? low.find(nlow) : low.rfind(nlow);
    if (p == std::string::npos) {
      continue;
    }
    // Earlier prefer entries (role markers) outrank longer incidental needles.
    const int sc = static_cast<int>(n.size()) + (prefer_first_hit ? (500 - idx * 10) : 0);
    if (sc > best) {
      best = sc;
      mid = p;
    }
  }
  if (best < 0) {
    for (const char* k : {"::", "constexpr", "std::", "struct ", "enum ", "case "}) {
      const auto p = prefer_first_hit ? low.find(k) : low.rfind(k);
      if (p != std::string::npos) {
        mid = p;
        break;
      }
    }
  }
  std::size_t a = mid > keep / 2 ? mid - keep / 2 : 0;
  if (a + keep > text.size()) {
    a = text.size() - keep;
  }
  std::ostringstream out;
  if (a > 0) {
    out << "…[truncated head en pack";
    if (!hint.empty()) {
      out << "; refetch `" << hint << "`";
    }
    out << "]…\n";
  }
  out << text.substr(a, keep);
  if (a + keep < text.size()) {
    out << "\n…[truncated tail en pack]…\n";
  }
  return out.str();
}

bool text_looks_truncated(const std::string& text) {
  return text.find("[TRUNCATED]") != std::string::npos ||
         text.find("[truncated]") != std::string::npos ||
         text.find("omitted lines") != std::string::npos ||
         text.find("missing_lines:") != std::string::npos;
}

std::string extract_refetch_hint(const std::string& text, const std::string& fallback_target) {
  const std::string marker = "refetch: get_code_of `";
  const auto p = text.find(marker);
  if (p != std::string::npos) {
    const auto start = p + marker.size();
    const auto end = text.find('`', start);
    if (end != std::string::npos && end > start) {
      return text.substr(start, end - start);
    }
  }
  const auto omit = text.find("omitted lines ");
  if (omit != std::string::npos) {
    // "omitted lines A-B; refetch path:A-B ..."
    const auto refetch = text.find("refetch ", omit);
    if (refetch != std::string::npos) {
      auto s = refetch + 8;
      while (s < text.size() && (text[s] == ' ' || text[s] == '`')) {
        ++s;
      }
      auto e = s;
      while (e < text.size() && text[e] != ' ' && text[e] != '`' && text[e] != ';' &&
             text[e] != ']' && text[e] != '\n') {
        ++e;
      }
      if (e > s) {
        return text.substr(s, e - s);
      }
    }
  }
  if (!fallback_target.empty()) {
    return fallback_target + "#mid";
  }
  return {};
}

std::vector<std::string> tokenize_needles(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 3) {
      out.push_back(cur);
    }
    cur.clear();
  };
  for (unsigned char ch : text) {
    if (std::isalnum(ch) || ch == '_' || ch == ':') {
      cur.push_back(static_cast<char>(std::tolower(ch)));
    } else {
      flush();
    }
  }
  flush();
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string extract_map_query_line(const std::string& map_body) {
  const auto qpos = map_body.find("query:");
  if (qpos == std::string::npos) {
    return {};
  }
  const auto line_end = map_body.find('\n', qpos);
  std::string q = map_body.substr(qpos + 6, line_end == std::string::npos ? std::string::npos
                                                                          : line_end - (qpos + 6));
  return trim_ws(q);
}

// Jaccard-ish: shared tokens / min(|a|,|b|). Low → stale map for this Instruction.
double needle_overlap_ratio(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  if (a.empty() || b.empty()) {
    return 0.0;
  }
  std::unordered_set<std::string> sb(b.begin(), b.end());
  int shared = 0;
  for (const auto& t : a) {
    if (sb.count(t)) {
      ++shared;
    }
  }
  const double denom = static_cast<double>(std::min(a.size(), b.size()));
  return denom > 0 ? static_cast<double>(shared) / denom : 0.0;
}

bool target_has_symbol_or_range(const std::string& target) {
  std::string t = trim_ws(target);
  const auto hash = t.rfind('#');
  if (hash != std::string::npos) {
    t = trim_ws(t.substr(0, hash));
  }
  const auto colon = t.rfind(':');
  if (colon == std::string::npos || colon + 1 >= t.size()) {
    return false;
  }
  const std::string right = t.substr(colon + 1);
  if (right.find('-') != std::string::npos) {
    return true;  // A-B
  }
  bool digits = !right.empty();
  for (char c : right) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      digits = false;
      break;
    }
  }
  if (digits) {
    return true;  // line
  }
  return !right.empty();  // Symbol
}

std::string resolved_name_from_tool_text(const std::string& text) {
  // "path:1-10 (Name)" or "path:1-10 (Name) [TRUNCATED]"
  const auto nl = text.find('\n');
  const std::string head = nl == std::string::npos ? text : text.substr(0, nl);
  const auto lp = head.rfind('(');
  const auto rp = head.rfind(')');
  if (lp != std::string::npos && rp != std::string::npos && rp > lp + 1) {
    return head.substr(lp + 1, rp - lp - 1);
  }
  return {};
}

bool name_matches_needles(const std::string& name, const std::vector<std::string>& needles) {
  if (name.empty() || needles.empty()) {
    return true;  // no signal → do not warn
  }
  std::string nl = name;
  for (char& c : nl) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  for (const auto& n : needles) {
    if (n.size() < 3) {
      continue;
    }
    if (nl.find(n) != std::string::npos || n.find(nl) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Symbol / right-hand side of path:Symbol (empty for bare / path:line / path:A-B).
std::string symbol_from_plan_target(const std::string& target) {
  std::string t = trim_ws(target);
  const auto hash = t.rfind('#');
  if (hash != std::string::npos) {
    t = trim_ws(t.substr(0, hash));
  }
  const auto colon = t.rfind(':');
  if (colon == std::string::npos || colon + 1 >= t.size()) {
    return {};
  }
  const std::string left = t.substr(0, colon);
  if (left.find('/') == std::string::npos && left.find('\\') == std::string::npos &&
      left.find('.') == std::string::npos) {
    return {};
  }
  const std::string right = t.substr(colon + 1);
  if (right.find('-') != std::string::npos) {
    return {};  // A-B
  }
  bool digits = !right.empty();
  for (char c : right) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      digits = false;
      break;
    }
  }
  if (digits) {
    return {};  // line
  }
  return right;
}

int line_from_plan_target(const std::string& target) {
  std::string t = trim_ws(target);
  const auto hash = t.rfind('#');
  if (hash != std::string::npos) {
    t = trim_ws(t.substr(0, hash));
  }
  const auto colon = t.rfind(':');
  if (colon == std::string::npos || colon + 1 >= t.size()) {
    return 0;
  }
  const std::string right = t.substr(colon + 1);
  if (right.find('-') != std::string::npos) {
    return 0;
  }
  for (char c : right) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return 0;
    }
  }
  try {
    return std::stoi(right);
  } catch (...) {
    return 0;
  }
}

std::string line_window_target(const std::string& path, int line, int radius = 80) {
  if (path.empty() || line <= 0) {
    return {};
  }
  const int a = std::max(1, line - radius);
  const int b = line + radius;
  return path + ":" + std::to_string(a) + "-" + std::to_string(b);
}

// Prefer more lines after `line` (bodies/switches usually follow the match).
std::string line_window_target_biased(const std::string& path, int line, int before = 40,
                                      int after = 90) {
  if (path.empty() || line <= 0) {
    return {};
  }
  const int a = std::max(1, line - before);
  const int b = line + after;
  return path + ":" + std::to_string(a) + "-" + std::to_string(b);
}

bool name_ok_for_target(const std::string& got_name, const std::string& target,
                        const std::vector<std::string>& needles) {
  if (got_name.empty()) {
    return true;
  }
  const std::string req = symbol_from_plan_target(target);
  if (!req.empty()) {
    std::string a = got_name;
    std::string b = req;
    for (char& c : a) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (char& c : b) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (a == b || a.find(b) != std::string::npos || b.find(a) != std::string::npos) {
      return true;
    }
  }
  // path:line / path:A-B: resolved enclosing symbol is expected; do not WARN.
  if (line_from_plan_target(target) > 0 || target.find('-') != std::string::npos) {
    return true;
  }
  return name_matches_needles(got_name, needles);
}

int score_symbol_against_needles(const std::string& sym, const std::vector<std::string>& needles) {
  std::string s = sym;
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  // Reject common C++ / outline junk that substring-matches needles (const⊂control).
  static const std::unordered_set<std::string> kJunk = {
      "const", "constexpr", "int", "void", "bool", "char", "auto", "return", "true", "false",
      "null", "this", "class", "struct", "enum", "union", "namespace", "using", "public",
      "private", "protected", "static", "inline", "virtual", "template", "typename", "std",
      "string", "size", "type", "file", "line", "name", "data", "value", "state", "path",
  };
  if (kJunk.count(s) || s.size() < 4) {
    return 0;
  }
  int score = 0;
  for (const auto& n : needles) {
    if (n.size() < 4) {
      continue;
    }
    if (s == n) {
      score += 100;
    } else if (s.find(n) != std::string::npos) {
      // needle inside symbol — require a substantial needle (avoid key⊂KeyChordSpec)
      if (n.size() >= 5) {
        score += 40;
      }
    } else if (n.find(s) != std::string::npos) {
      // symbol inside needle only if almost as long (avoid const⊂control)
      if (s.size() >= 6 && s.size() * 2 >= n.size()) {
        score += 30;
      }
    }
  }
  return score;
}

bool looks_like_code_ident(const std::string& tok) {
  if (tok.size() < 5) {
    return false;
  }
  // Trailing ':' alone (e.g. fragments_ok: from plan stats) is not an ident.
  if (!tok.empty() && tok.back() == ':') {
    return false;
  }
  bool has_under = false;
  bool has_digit = false;
  for (unsigned char c : tok) {
    if (c == '_' || c == ':') {
      has_under = true;
    } else if (std::isdigit(c)) {
      has_digit = true;
    }
  }
  // snake / nested / numbered, or smashed CamelCase (ToggleLineMark → togglelinemark).
  return has_under || has_digit || tok.size() >= 10;
}

bool is_plan_stats_telemetry_token(const std::string& tok) {
  std::string t = tok;
  while (!t.empty() && t.back() == ':') {
    t.pop_back();
  }
  static const std::unordered_set<std::string> kStats = {
      "fragments_ok", "outlines_ok", "pack_chars", "truncated", "auto_refetch",
      "target_count", "fragments", "outlines",
  };
  return kStats.count(t) != 0;
}

// True for path:Symbol / path:line / path:A-B style plan targets (not "3 fragments_ok…").
bool looks_like_plan_target_token(const std::string& tok) {
  if (tok.size() < 5 || tok.find('/') == std::string::npos) {
    return false;
  }
  // Require a file-ish path before optional :anchor.
  const auto colon = tok.find(':');
  const std::string path = colon == std::string::npos ? tok : tok.substr(0, colon);
  return path.find('.') != std::string::npos || path.rfind("src/", 0) == 0;
}

std::string fnv1a_hex(const std::string& s) {
  std::uint64_t h = 14695981039346656037ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  std::ostringstream out;
  out << std::hex << h;
  return out.str();
}

std::string edit_hunks_fingerprint(const std::vector<SearchReplaceHunk>& hunks) {
  std::ostringstream raw;
  for (const auto& h : hunks) {
    raw << h.path << '\n' << h.search << "\n---\n" << h.replace << "\n===\n";
  }
  return fnv1a_hex(raw.str());
}

// Bare identifiers / path:Symbol almost always match ≥2 times or the wrong locus.
bool search_too_generic(const std::string& search) {
  std::string s = trim_ws(search);
  if (s.empty()) {
    return true;
  }
  bool only_identish = true;
  for (unsigned char c : s) {
    if (!(std::isalnum(c) != 0 || c == '_' || c == ':' || c == '/' || c == '.' || c == '-')) {
      only_identish = false;
      break;
    }
  }
  // "tick_terminal_shell" / "Foo::bar" without surrounding code → too generic.
  if (only_identish) {
    return true;
  }
  // Tiny fragments (even with punctuation) are rarely unique.
  return s.size() < 12;
}

int count_content_lines(const std::string& text) {
  int n = 0;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!trim_ws(line).empty()) {
      ++n;
    }
  }
  return n;
}

// Reject "struct Foo {" → full struct body (duplicates the rest of the type).
// Search must cover the span being replaced; tiny openers expanded into blocks are poison.
bool hunk_expands_over_opener(const SearchReplaceHunk& h) {
  const std::string search = trim_ws(h.search);
  const std::string replace = trim_ws(h.replace);
  if (search.empty() || replace.size() <= search.size()) {
    return false;
  }
  const int s_lines = count_content_lines(search);
  const int r_lines = count_content_lines(replace);
  const bool has_open = search.find('{') != std::string::npos;
  const bool has_close = search.find('}') != std::string::npos;
  const bool opener_only =
      (has_open && !has_close && s_lines <= 2) ||
      (!search.empty() && search.back() == '{' && s_lines <= 2);
  if (!opener_only) {
    return false;
  }
  // Full braced body (or many lines) replacing just the opener.
  if (r_lines >= 4 && r_lines >= s_lines * 3) {
    return true;
  }
  if (replace.find('}') != std::string::npos && replace.size() > search.size() * 4) {
    return true;
  }
  return false;
}

std::string hunk_shape_error(const SearchReplaceHunk& h) {
  if (search_too_generic(h.search)) {
    return "search demasiado genérico (ident suelto o <12 chars; usa un bloque de código único)";
  }
  if (hunk_expands_over_opener(h)) {
    return "hunk mal formado: `search` es solo el opener (`… {`) pero `replace` trae el bloque "
           "entero — el `search` debe cubrir todo el span a sustituir (p.ej. struct completo "
           "hasta `};`), no solo la línea de apertura";
  }
  return {};
}

std::vector<std::string> parse_pack_targets_header(const std::string& pack);

std::string target_to_rel_path(const std::string& target) {
  const auto colon = target.find(':');
  if (colon == std::string::npos) {
    return target;
  }
  // path:Symbol or path:12 or path:A-B — keep path (may contain drive? ignore)
  return target.substr(0, colon);
}

void add_unique_path(std::vector<std::string>* paths, const std::string& rel) {
  if (!paths || rel.empty()) {
    return;
  }
  for (const auto& p : *paths) {
    if (p == rel) {
      return;
    }
  }
  paths->push_back(rel);
}

std::string read_file_raw(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<std::string> edit_path_candidates(const std::string& workspace_root, bool has_pack,
                                              const std::vector<std::string>& watchlist) {
  std::vector<std::string> paths;
  for (const auto& t : watchlist) {
    add_unique_path(&paths, target_to_rel_path(t));
  }
  if (has_pack) {
    for (const auto& t :
         parse_pack_targets_header(read_file_raw(Level2Session::pack_path(workspace_root)))) {
      add_unique_path(&paths, target_to_rel_path(t));
    }
  }
  return paths;
}

// If search is missing in h.path but uniquely matches one pack/watchlist file, return that path.
std::string suggest_path_for_search(const std::string& workspace_root, const SearchReplaceHunk& h,
                                    const std::vector<std::string>& candidates) {
  if (h.search.empty() || candidates.empty()) {
    return {};
  }
  std::string unique;
  for (const auto& rel : candidates) {
    if (rel == h.path) {
      continue;
    }
    const fs::path abs = fs::path(workspace_root) / rel;
    if (!fs::exists(abs)) {
      continue;
    }
    const std::string text = read_file_raw(abs.string());
    std::string err;
    if (!find_unique_span(text, h.search, nullptr, &err)) {
      continue;
    }
    if (!unique.empty()) {
      return {};  // ambiguous across candidates
    }
    unique = rel;
  }
  return unique;
}

std::string pack_span_hint(const std::string& workspace_root, const std::string& failed_path) {
  const std::string pack = read_file_raw(Level2Session::pack_path(workspace_root));
  if (pack.empty()) {
    return {};
  }
  const std::string marker = "### get_code_of `";
  std::size_t pos = 0;
  std::size_t chosen = std::string::npos;
  if (!failed_path.empty()) {
    while ((pos = pack.find(marker, pos)) != std::string::npos) {
      const auto end = pack.find('`', pos + marker.size());
      if (end == std::string::npos) {
        break;
      }
      const std::string target = pack.substr(pos + marker.size(), end - (pos + marker.size()));
      const std::string tpath = target_to_rel_path(target);
      if (target.find(failed_path) != std::string::npos ||
          (!tpath.empty() && failed_path.find(tpath) != std::string::npos)) {
        chosen = pos;
        break;
      }
      pos = end + 1;
    }
  }
  if (chosen == std::string::npos) {
    chosen = pack.find(marker);
  }
  if (chosen == std::string::npos) {
    return {};
  }
  const auto fence = pack.find("```", chosen);
  if (fence == std::string::npos) {
    return {};
  }
  const auto body_start = pack.find('\n', fence);
  if (body_start == std::string::npos) {
    return {};
  }
  const auto fence_end = pack.find("```", body_start + 1);
  if (fence_end == std::string::npos) {
    return {};
  }
  std::string body = pack.substr(body_start + 1, fence_end - (body_start + 1));
  std::istringstream in(body);
  std::ostringstream out;
  std::string line;
  int n = 0;
  while (std::getline(in, line) && n < 12) {
    if (trim_ws(line).empty()) {
      continue;
    }
    out << line << '\n';
    ++n;
  }
  const std::string hint = out.str();
  return hint.size() >= 12 ? hint : std::string{};
}

void merge_needles(std::vector<std::string>* dst, const std::vector<std::string>& add) {
  if (!dst) {
    return;
  }
  std::unordered_set<std::string> seen(dst->begin(), dst->end());
  for (const auto& n : add) {
    if (n.size() < 3 || !seen.insert(n).second) {
      continue;
    }
    dst->push_back(n);
  }
}

void merge_labeled_line(const std::string& section, const char* label,
                        std::vector<std::string>* out) {
  if (!out || label == nullptr) {
    return;
  }
  const std::string lab = label;
  std::size_t pos = 0;
  while ((pos = section.find(lab, pos)) != std::string::npos) {
    const bool at_line = (pos == 0) || section[pos - 1] == '\n';
    if (!at_line) {
      pos += lab.size();
      continue;
    }
    const auto eol = section.find('\n', pos);
    const auto from = pos + lab.size();
    const auto n = (eol == std::string::npos ? section.size() : eol) - from;
    merge_needles(out, tokenize_needles(section.substr(from, n)));
    pos = eol == std::string::npos ? section.size() : eol + 1;
  }
}

// query/instruction/seeds + tool args from Observations (not the bootstrap guide paragraph).
std::vector<std::string> session_pack_needles(const std::string& session_body) {
  std::vector<std::string> out;
  const auto instr = session_body.find("## Instruction");
  const auto map = session_body.find("## Ranked map");
  if (instr != std::string::npos) {
    const auto end = (map != std::string::npos && map > instr) ? map : session_body.size();
    const std::string section = session_body.substr(instr, end - instr);
    merge_labeled_line(section, "query:", &out);
    merge_labeled_line(section, "instruction:", &out);
    merge_labeled_line(section, "seeds:", &out);
  }
  const auto obs = session_body.find("## Observations");
  if (obs != std::string::npos) {
    const std::string slice = session_body.substr(obs);
    // Tool args only. Do NOT harvest plan telemetry lines like
    // "targets: 3  fragments_ok: 3/3  pack_chars: …" — those used to poison
    // pack_instruction_gaps (false pack_incomplete → plan loop).
    static const char* markers[] = {
        "get_code_of `", "get_code_of ", "search `", "search ", "file_outline `",
        "### get_code_of `", "arg:",
    };
    for (const char* m : markers) {
      const std::string marker = m;
      std::size_t pos = 0;
      while ((pos = slice.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        std::size_t end = pos;
        while (end < slice.size() && slice[end] != '\n' && slice[end] != '`' &&
               slice[end] != ')') {
          ++end;
        }
        merge_needles(&out, tokenize_needles(slice.substr(pos, end - pos)));
      }
    }
    // Optional real target lists: only path-like tokens (src/foo.cpp:Sym).
    const std::string targets_marker = "targets:";
    std::size_t tpos = 0;
    while ((tpos = slice.find(targets_marker, tpos)) != std::string::npos) {
      tpos += targets_marker.size();
      std::size_t end = tpos;
      while (end < slice.size() && slice[end] != '\n') {
        ++end;
      }
      const std::string arg = slice.substr(tpos, end - tpos);
      // Skip plan stats: "3  fragments_ok: …" / target_count lines.
      if (arg.find("fragments_ok") != std::string::npos ||
          arg.find("pack_chars") != std::string::npos ||
          arg.find("outlines_ok") != std::string::npos) {
        continue;
      }
      for (const auto& tok : tokenize_needles(arg)) {
        if (looks_like_plan_target_token(tok) && !is_plan_stats_telemetry_token(tok)) {
          merge_needles(&out, {tok});
        }
      }
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
    const int sa = looks_like_code_ident(a) ? 0 : 1;
    const int sb = looks_like_code_ident(b) ? 0 : 1;
    if (sa != sb) {
      return sa < sb;
    }
    return a < b;
  });
  return out;
}

std::string extract_outline_symbol_name(const std::string& line) {
  // tool_registry: "fn name :line" / "  struct Foo :10-20" / legacy "`foo`" / "N:foo"
  std::string s = trim_ws(line);
  if (s.empty() || s.rfind("outline:", 0) == 0 || s.rfind("…", 0) == 0) {
    return {};
  }
  // Skip leading kind token when present.
  static const std::unordered_set<std::string> kKinds = {
      "fn", "method", "class", "struct", "enum", "var", "field", "ns", "type", "macro",
      "union", "iface", "trait", "impl", "mod", "file",
  };
  std::string tok;
  std::vector<std::string> toks;
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '-') {
      tok.push_back(c);
    } else if (!tok.empty()) {
      toks.push_back(tok);
      tok.clear();
    }
  }
  if (!tok.empty()) {
    toks.push_back(tok);
  }
  std::size_t i = 0;
  if (!toks.empty() && kKinds.count(toks[0])) {
    ++i;
  }
  // Skip pure line-number prefixes "104" from "104:struct ..."
  if (i < toks.size()) {
    bool all_digit = !toks[i].empty();
    for (char c : toks[i]) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        all_digit = false;
        break;
      }
    }
    if (all_digit) {
      ++i;
    }
  }
  if (i >= toks.size()) {
    return {};
  }
  std::string name = toks[i];
  // Drop trailing :line already glued.
  const auto colon = name.rfind(':');
  if (colon != std::string::npos && colon + 1 < name.size()) {
    bool digits = true;
    for (std::size_t j = colon + 1; j < name.size(); ++j) {
      if (!std::isdigit(static_cast<unsigned char>(name[j]))) {
        digits = false;
        break;
      }
    }
    if (digits) {
      name = name.substr(0, colon);
    }
  }
  if (name.size() < 3 || name.find('/') != std::string::npos || name.find('.') != std::string::npos) {
    return {};
  }
  if (kKinds.count(name)) {
    return {};
  }
  return name;
}

// Pick best path:Symbol from file_outline text using pack needles.
std::string best_symbol_target_from_outline(const std::string& path, const std::string& outline,
                                            const std::vector<std::string>& needles) {
  std::string best_sym;
  int best = -1;
  std::istringstream in(outline);
  std::string line;
  while (std::getline(in, line)) {
    const std::string cand = extract_outline_symbol_name(line);
    if (cand.empty()) {
      continue;
    }
    const int sc = score_symbol_against_needles(cand, needles);
    if (sc > best) {
      best = sc;
      best_sym = cand;
    }
  }
  if (best <= 0 || best_sym.empty()) {
    return {};
  }
  // Require a real needle hit (best>0 already); reject ultra-generic short names.
  if (best_sym.size() < 6) {
    return {};
  }
  return path + ":" + best_sym;
}

// Keep outline lines that hit needles; always keep header.
std::string filter_outline_for_needles(const std::string& outline,
                                       const std::vector<std::string>& needles,
                                       std::size_t max_chars) {
  if (outline.size() <= max_chars || needles.empty()) {
    if (outline.size() <= max_chars) {
      return outline;
    }
    return truncate_to_budget(outline, max_chars, {});
  }
  std::ostringstream kept;
  std::istringstream in(outline);
  std::string line;
  bool first = true;
  std::size_t used = 0;
  while (std::getline(in, line)) {
    const bool keep = first || score_symbol_against_needles(extract_outline_symbol_name(line),
                                                           needles) > 0 ||
                      name_matches_needles(line, needles);
    first = false;
    if (!keep) {
      continue;
    }
    if (used + line.size() + 1 > max_chars) {
      kept << "… (outline filtrado por needles)\n";
      break;
    }
    kept << line << '\n';
    used += line.size() + 1;
  }
  std::string out = kept.str();
  if (out.size() < 40) {
    return truncate_to_budget(outline, max_chars, {});
  }
  return out;
}

// From search tool text, pick best path:line hit inside `path`.
// Requires a strong needle hit on the line — weak path-only matches are rejected
// (avoids bare → file preamble windows like strings_es:1-80).
std::string best_line_target_from_search(const std::string& path, const std::string& search_text,
                                         const std::vector<std::string>& needles = {}) {
  // Typical: "src/ui/press_ids.hpp:42  ..." or "file:line:col"
  int best_line = 0;
  int best_score = -1;
  std::istringstream in(search_text);
  std::string line;
  const std::string needle_path = path;
  while (std::getline(in, line)) {
    const auto p = line.find(needle_path);
    if (p == std::string::npos) {
      continue;
    }
    const auto colon = line.find(':', p + needle_path.size());
    if (colon == std::string::npos || colon + 1 >= line.size()) {
      continue;
    }
    int ln = 0;
    try {
      ln = std::stoi(line.substr(colon + 1));
    } catch (...) {
      continue;
    }
    if (ln <= 0) {
      continue;
    }
    int sc = score_symbol_against_needles(line, needles);
    std::string low = line;
    for (char& c : low) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    // Prefer lines that hit strong identifier needles (no domain-hardcoded boosts).
    int strong_hits = 0;
    for (const auto& n : needles) {
      if (!looks_like_code_ident(n) || n.size() < 5) {
        continue;
      }
      if (low.find(n) != std::string::npos) {
        sc += static_cast<int>(std::min<std::size_t>(n.size(), 24));
        ++strong_hits;
      }
    }
    // Code-shaped edit loci beat prose / preamble (generic: nested names, not domain words).
    if (low.find("::") != std::string::npos) {
      sc += 25;
      ++strong_hits;
    }
    if (low.find('_') != std::string::npos) {
      sc += 8;
    }
    // Path stem alone is not enough — require a real needle/code hit.
    if (strong_hits <= 0 && sc < 40) {
      continue;
    }
    if (sc > best_score) {
      best_score = sc;
      best_line = ln;
    }
  }
  // Threshold: reject "first line in file" noise.
  if (best_line <= 0 || best_score < 20) {
    return {};
  }
  // Tighter window around a confident hit (edit locus, not ±80 of preamble).
  const int radius = best_score >= 60 ? 30 : 40;
  return line_window_target(path, best_line, radius);
}

std::vector<std::string> parse_pack_targets_header(const std::string& pack) {
  std::vector<std::string> out;
  const auto p = pack.find("targets (");
  if (p == std::string::npos) {
    return out;
  }
  const auto line_end = pack.find('\n', p);
  const std::string line =
      pack.substr(p, line_end == std::string::npos ? std::string::npos : line_end - p);
  std::size_t i = 0;
  while (true) {
    const auto a = line.find('`', i);
    if (a == std::string::npos) {
      break;
    }
    const auto b = line.find('`', a + 1);
    if (b == std::string::npos) {
      break;
    }
    out.push_back(line.substr(a + 1, b - a - 1));
    i = b + 1;
  }
  return out;
}

std::string to_lower_copy(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

bool is_source_ext(std::string ext) {
  ext = to_lower_copy(std::move(ext));
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".mm";
}

bool is_header_ext(std::string ext) {
  ext = to_lower_copy(std::move(ext));
  return ext == ".hpp" || ext == ".h" || ext == ".hh" || ext == ".hxx";
}

bool path_looks_like_header(const std::string& path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos || dot + 1 >= path.size()) {
    return false;
  }
  return is_header_ext(path.substr(dot));
}

// Same-stem .hpp/.h next to a .cpp (declaration / include surface for edits).
std::string sibling_header_rel(const std::string& workspace_root, const std::string& rel) {
  fs::path p(rel);
  if (!is_source_ext(p.extension().string())) {
    return {};
  }
  static const char* kExts[] = {".hpp", ".h", ".hh", ".hxx"};
  for (const char* hext : kExts) {
    fs::path cand = p;
    cand.replace_extension(hext);
    std::error_code ec;
    if (fs::exists(fs::path(workspace_root) / cand, ec) && !ec) {
      return cand.generic_string();
    }
  }
  return {};
}

std::vector<std::string> compile_undeclared_idents(const std::string& log) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  std::istringstream in(log);
  std::string line;
  while (std::getline(in, line) && out.size() < 8) {
    const std::string ll = to_lower_copy(line);
    if (ll.find("undeclared") == std::string::npos &&
        ll.find("not declared") == std::string::npos &&
        ll.find("has not been declared") == std::string::npos) {
      continue;
    }
    for (char q : {'\'', '`'}) {
      std::size_t a = 0;
      while ((a = line.find(q, a)) != std::string::npos) {
        const auto b = line.find(q, a + 1);
        if (b == std::string::npos || b <= a + 1) {
          break;
        }
        std::string id = line.substr(a + 1, b - a - 1);
        const auto colon = id.rfind("::");
        if (colon != std::string::npos && colon + 2 < id.size()) {
          id = id.substr(colon + 2);
        }
        if (id.size() >= 2 && id.size() <= 80 &&
            (std::isalpha(static_cast<unsigned char>(id[0])) || id[0] == '_') &&
            seen.insert(id).second) {
          out.push_back(id);
        }
        a = b + 1;
      }
    }
  }
  return out;
}

std::string path_stem_key(std::string_view path) {
  std::string p(path);
  // Drop trailing :line / :Symbol for get_code_of args.
  const auto colon = p.find(':');
  if (colon != std::string::npos) {
    // Keep Windows drive? We use unix paths in workspace.
    p = p.substr(0, colon);
  }
  const auto slash = p.find_last_of("/\\");
  std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return to_lower_copy(base);
}

void push_hot(std::vector<std::string>& hot, std::unordered_set<std::string>& seen,
              std::string key) {
  key = trim_ws(key);
  if (key.empty()) {
    return;
  }
  const std::string low = to_lower_copy(key);
  if (!seen.insert(low).second) {
    return;
  }
  hot.push_back(std::move(key));
}

bool is_ranked_entry_start(const std::string& line) {
  if (line.empty() || !std::isdigit(static_cast<unsigned char>(line[0]))) {
    return false;
  }
  std::size_t i = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  return i < line.size() && line[i] == '.' && (i + 1 >= line.size() || line[i + 1] == ' ');
}

bool entry_matches_hot(const std::string& entry, const std::vector<std::string>& hot_keys) {
  if (hot_keys.empty()) {
    return false;
  }
  const std::string low = to_lower_copy(entry);
  for (const auto& raw : hot_keys) {
    const std::string h = to_lower_copy(raw);
    if (h.empty()) {
      continue;
    }
    if (low.find(h) != std::string::npos) {
      return true;
    }
    // stem=foo in why lines, or bare stem
    if (low.find("stem=" + h) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string first_line_of(const std::string& entry) {
  const auto nl = entry.find('\n');
  std::string line = nl == std::string::npos ? entry : entry.substr(0, nl);
  return trim_ws(line);
}

// Idents from Instruction missing in pack (generic: no domain facets).
std::vector<std::string> pack_instruction_gaps(const std::string& pack,
                                               const std::vector<std::string>& needles) {
  std::vector<std::string> missing;
  int strong = 0;
  int strong_hit = 0;
  const std::string pack_low = to_lower_copy(pack);
  for (const auto& n : needles) {
    if (!looks_like_code_ident(n) || n.size() < 8) {
      continue;
    }
    // Ignore leftover path:Symbol tokens if a tool arg leaked in.
    if (n.rfind("path:", 0) == 0) {
      continue;
    }
    if (is_plan_stats_telemetry_token(n)) {
      continue;
    }
    ++strong;
    const auto low = to_lower_copy(n);
    if (pack_low.find(low) != std::string::npos) {
      ++strong_hit;
    } else {
      missing.push_back(n);
    }
  }
  std::vector<std::string> gaps;
  if (strong >= 1 && strong_hit == 0) {
    std::ostringstream tip;
    tip << "ningún ident de Instruction aparece en el pack";
    const int show = std::min(static_cast<int>(missing.size()), 6);
    if (show > 0) {
      tip << " (";
      for (int i = 0; i < show; ++i) {
        if (i) {
          tip << ", ";
        }
        tip << '`' << missing[static_cast<std::size_t>(i)] << '`';
      }
      tip << ") — re-plan anclado";
    } else {
      tip << " — re-plan anclado";
    }
    gaps.push_back(tip.str());
  }
  return gaps;
}

std::string trim_observations_section(std::string obs_section, std::size_t max_chars) {
  if (max_chars == 0 || obs_section.size() <= max_chars) {
    return obs_section;
  }
  std::vector<std::size_t> turns;
  std::size_t pos = 0;
  while (true) {
    const auto p = obs_section.find("\n### turn ", pos);
    if (p == std::string::npos) {
      break;
    }
    turns.push_back(p + 1);
    pos = p + 1;
  }
  const auto p0 = obs_section.find("### turn ");
  if (p0 != std::string::npos && (turns.empty() || turns.front() != p0)) {
    turns.insert(turns.begin(), p0);
  }
  std::size_t drop = 0;
  while (drop + 1 < turns.size() && obs_section.size() > max_chars) {
    ++drop;
    std::ostringstream rebuilt;
    rebuilt << "## Observations\n\n<!-- older observations trimmed (" << drop << ") -->\n\n";
    rebuilt << obs_section.substr(turns[drop]);
    obs_section = rebuilt.str();
  }
  // One remaining turn can still blow the budget (e.g. 200-line get_code_of).
  if (obs_section.size() > max_chars) {
    std::string keep = obs_section.substr(0, max_chars);
    const auto nl = keep.rfind('\n');
    if (nl != std::string::npos && nl + 40 < keep.size()) {
      keep.resize(nl + 1);
    }
    keep += "… (packed observations truncated)\n";
    obs_section = std::move(keep);
  }
  if (obs_section.size() > max_chars) {
    obs_section.resize(max_chars);
    obs_section += "\n<!-- observations truncated -->\n";
  }
  return obs_section;
}

}  // namespace

std::vector<std::string> Level2Session::hot_keys_from_observations(const std::string& observations) {
  std::vector<std::string> hot;
  std::unordered_set<std::string> seen;

  // ### turn N — `tool` `arg`
  for (std::size_t pos = 0; (pos = observations.find("### turn ", pos)) != std::string::npos;) {
    const auto eol = observations.find('\n', pos);
    const std::string header =
        observations.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
    pos = eol == std::string::npos ? observations.size() : eol + 1;

    // Prefer last backtick-quoted token as arg.
    std::vector<std::string> ticks;
    for (std::size_t i = 0; i < header.size();) {
      const auto a = header.find('`', i);
      if (a == std::string::npos) {
        break;
      }
      const auto b = header.find('`', a + 1);
      if (b == std::string::npos) {
        break;
      }
      ticks.push_back(header.substr(a + 1, b - a - 1));
      i = b + 1;
    }
    if (ticks.size() >= 2) {
      const std::string& arg = ticks.back();
      push_hot(hot, seen, arg);
      // path without :Symbol
      const auto colon = arg.find(':');
      if (colon != std::string::npos && colon > 0) {
        push_hot(hot, seen, arg.substr(0, colon));
        const std::string sym = arg.substr(colon + 1);
        // Skip pure line numbers as hot keys (too broad vs scores).
        const bool all_digit =
            !sym.empty() &&
            std::all_of(sym.begin(), sym.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!all_digit) {
          push_hot(hot, seen, sym);
        }
      }
      push_hot(hot, seen, path_stem_key(arg));
    } else if (ticks.size() == 1 && ticks[0].find('/') != std::string::npos) {
      push_hot(hot, seen, ticks[0]);
      push_hot(hot, seen, path_stem_key(ticks[0]));
    }
  }

  // outline: path …
  for (std::size_t pos = 0; (pos = observations.find("outline:", pos)) != std::string::npos;) {
    const auto eol = observations.find('\n', pos);
    std::string line =
        observations.substr(pos + 8, eol == std::string::npos ? std::string::npos : eol - (pos + 8));
    pos = eol == std::string::npos ? observations.size() : eol + 1;
    const auto sym = line.find("symbols=");
    if (sym != std::string::npos) {
      line = line.substr(0, sym);
    }
    line = trim_ws(line);
    if (!line.empty()) {
      push_hot(hot, seen, line);
      push_hot(hot, seen, path_stem_key(line));
    }
  }

  return hot;
}

std::string Level2Session::compact_ranked_map_markdown(const std::string& map_section,
                                                       const std::vector<std::string>& hot_keys) {
  // map_section includes leading "## Ranked map" or starts after it.
  std::string body = map_section;
  std::string header;
  const std::string mark = "## Ranked map";
  if (body.rfind(mark, 0) == 0) {
    header = mark;
    body = body.substr(mark.size());
    if (!body.empty() && body[0] == '\n') {
      body.erase(body.begin());
    }
  }

  // Drop ## Bodies (and anything after) — detail for hot stems stays in Ranked entries / Observations.
  const auto bodies = body.find("\n## Bodies");
  if (bodies != std::string::npos) {
    body = body.substr(0, bodies);
  }
  const auto bodies_at0 = body.rfind("## Bodies", 0) == 0 ? std::size_t{0} : std::string::npos;
  if (bodies_at0 == 0) {
    body.clear();
  }

  // Split into preamble (query/note/## Ranked entries) + numbered entries.
  std::vector<std::string> entries;
  std::string preamble;
  {
    std::istringstream in(body);
    std::string line;
    std::ostringstream pre;
    std::ostringstream cur;
    bool in_entry = false;
    auto flush_entry = [&]() {
      if (!in_entry) {
        return;
      }
      std::string e = cur.str();
      while (!e.empty() && (e.back() == '\n' || e.back() == '\r')) {
        e.pop_back();
      }
      if (!e.empty()) {
        entries.push_back(std::move(e));
      }
      cur.str("");
      cur.clear();
    };
    while (std::getline(in, line)) {
      if (is_ranked_entry_start(line)) {
        if (!in_entry) {
          preamble = pre.str();
        }
        flush_entry();
        in_entry = true;
        cur << line << '\n';
      } else if (in_entry) {
        cur << line << '\n';
      } else {
        pre << line << '\n';
      }
    }
    flush_entry();
    if (!in_entry) {
      preamble = pre.str();
    }
  }

  int kept_detail = 0;
  std::ostringstream out;
  if (!header.empty()) {
    out << header << "\n\n";
  }
  out << preamble;
  if (preamble.find("## Ranked entries") == std::string::npos && !entries.empty()) {
    out << "## Ranked entries\n\n";
  }
  for (const auto& e : entries) {
    if (entry_matches_hot(e, hot_keys)) {
      out << e << "\n\n";
      ++kept_detail;
    } else {
      out << first_line_of(e) << "\n";
    }
  }
  out << "\n<!-- map compacted: full detail for " << kept_detail << " hot stem(s); "
      << "other entries are name-only -->\n";
  return out.str();
}

bool Level2Session::compact_session_context(const std::string& workspace_root,
                                            std::string* err_out) {
  if (workspace_root.empty()) {
    if (err_out) {
      *err_out = "workspace_root vacío";
    }
    return false;
  }
  std::string session = read_file(session_path(workspace_root));
  if (session.empty()) {
    if (err_out) {
      *err_out = "session.md ausente";
    }
    return false;
  }

  const std::string map_mark = "## Ranked map";
  const std::string obs_mark = "## Observations";
  const auto map_pos = session.find(map_mark);
  if (map_pos == std::string::npos) {
    return true;  // nothing to compact
  }
  const auto obs_pos = session.find(obs_mark, map_pos);
  const std::string before = session.substr(0, map_pos);
  std::string map_section;
  std::string after;
  if (obs_pos != std::string::npos) {
    map_section = session.substr(map_pos, obs_pos - map_pos);
    after = session.substr(obs_pos);
  } else {
    map_section = session.substr(map_pos);
  }

  // Skip if already compacted and no new hot keys would change cold entries... still cheap to redo.
  const auto hot = hot_keys_from_observations(after);
  // If explore has no observations yet, keep full map (caller shouldn't invoke, but be safe).
  if (hot.empty() && after.find("### turn ") == std::string::npos) {
    return true;
  }

  const std::string compact_map = compact_ranked_map_markdown(map_section, hot);
  session = before + compact_map;
  if (!after.empty()) {
    if (session.empty() || session.back() != '\n') {
      session.push_back('\n');
    }
    session += after;
  }
  session = trim_session_body(std::move(session));
  if (!write_file(session_path(workspace_root), session, err_out)) {
    return false;
  }
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"map_compact\",\"hot\":" +
                                   std::to_string(hot.size()) + "}");
  ai_trace(AiTraceChannel::L2, "l2_map_compact",
           "{\"hot\":" + std::to_string(hot.size()) +
               ",\"session_chars\":" + std::to_string(session.size()) + "}");
  return true;
}

Level2Session::Level2Session(Level2SessionDeps deps) : deps_(std::move(deps)) {}

Level2Session::Level2Session(ToolRegistry* tools) : deps_{tools, {}, {}} {}

std::string Level2Session::dir_for(const std::string& workspace_root) {
  return (fs::path(workspace_root) / ".tuide" / "ai" / "l2").lexically_normal().string();
}

std::string Level2Session::session_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "session.md").string();
}

std::string Level2Session::request_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "request.json").string();
}

std::string Level2Session::response_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "response.json").string();
}

std::string Level2Session::trace_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "trace.ndjson").string();
}

std::string Level2Session::state_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "state.json").string();
}

std::string Level2Session::pending_edits_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "pending_edits.json").string();
}

std::string Level2Session::map_initial_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "map_initial.md").string();
}

std::string Level2Session::pack_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "pack.md").string();
}

bool Level2Session::tool_allowed(const std::string& name) {
  return l2_whitelist().count(name) > 0;
}

std::string Level2Session::tool_guide_markdown() {
  return R"(## Tool guide

Fases: **explore** (mapa → **plan** → pack) → **edit** ↔ **compile** → **map_review** →
**done** (solo con `action=done` explícito). Compile OK **no** finaliza: restaura el mapa
inicial (salvo `map_stale`) y pregunta si falta algo. Si explore **no** localiza el código: **clarify**.

### Explore (primera mirada = plan)
Preferir `plan` en el **primer** paso: 4–8 `path:Symbol` / `path:line` anclados a la
Instruction (evitar path bare). Máx. ~8 tools sueltos antes del primer plan (el runtime
puede emitir un soft `_nudge:_`). Tras pack cubierto: extras con `tools` batch (máx. 4);
si sigues leyendo, otro `_nudge:_` pide `done next=edit`. No repetir el mismo path con
ventanas solapadas. Si `[TRUNCATED]`, refetch tip (`path:A-B` / `#mid|#tail`), no shotgun.
Truncado ≠ bloqueo de edit si no hay gaps Instruction.
`pack_incomplete` = gaps Instruction↔pack (o cero fragmentos), no meros truncados.
Sin facetas hardcoded: un ident de `query:`/`instruction:` debe aparecer en el pack.
Si `map_stale=1` el mapa rankeado **no** se inyecta: `search` / `plan` anclado.

```json
{"action":"plan","targets":["src/a.cpp:Foo","src/b.cpp:42","src/c.hpp:Bar"],"summary":"…"}
```
Máx. 16 targets. Tras el pack → Instruction+pack (sin mapa completo).

| tool | arg | ejemplo |
|------|-----|---------|
| get_code_of | path:Symbol \| path:line \| path:A-B \| #head/#mid/#tail | `src/ai/ai_controller.cpp:wake#tail` |
| file_outline | path | `src/ui/console_panel.cpp` |
| search | needles | `on_pty_output` |
| headers_of / definition / references | ver help | — |

### Acciones JSON

Explore (con mapa):
```json
{"action":"plan","targets":["src/a.cpp:Foo","src/b.cpp:Bar"]}
{"action":"tools","calls":[{"name":"search","arg":"wake"}]}
{"action":"done","summary":"código localizado…","next":"edit"}
{"action":"done","summary":"no encontré X","next":"clarify"}
```
- Preferir `plan` en el primer paso. `next=edit` con evidencia en pack/Observations.
- Clarify prematuro: pushback hasta `ai.level2.clarify_pushback_max` (default 3).
- `done next=edit` sin pack o con `pack_incomplete` (gaps) puede rechazarse (pushback).

Edit / tras pack:
```json
{"action":"edit","hunks":[{"path":"src/foo.cpp","search":"…exact…","replace":"…"}]}
{"action":"plan","targets":["…"]}
{"action":"done","summary":"cambios listos: paths…"}
```
- Zona en Truncated → refetch antes del hunk (no inventes).
- `search` debe ser un **bloque de código único** (no un ident suelto tipo `foo_bar`).
- Hunk idéntico al último fallo → rechazado; tras varios fallos → clarify.
- Tras `edit` OK → compile. Compile OK → **mapa inicial** + «¿algo más?» (`plan` / `edit` / `done`).
- Compile fail (≤3): stderr + old/new; reemite `edit`.
)";
}

std::string Level2Session::read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool Level2Session::write_file(const std::string& path, const std::string& body,
                               std::string* err) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  if (ec) {
    if (err) {
      *err = "no se pudo crear dir: " + ec.message();
    }
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir " + path;
    }
    return false;
  }
  out << body;
  return true;
}

Level2Session::State Level2Session::load_state(const std::string& workspace_root) {
  State st;
  const std::string raw = read_file(state_path(workspace_root));
  if (raw.empty()) {
    return st;
  }
  try {
    const auto j = nlohmann::json::parse(raw);
    st.turn = j.value("turn", 0);
    st.done = j.value("done", false);
    st.last_action = j.value("last_action", "");
    st.phase = j.value("phase", "explore");
    st.edit_attempt = j.value("edit_attempt", 0);
    st.compile_attempt = j.value("compile_attempt", 0);
    st.clarify_pushback = j.value("clarify_pushback", 0);
    st.pack_incomplete_pushback = j.value("pack_incomplete_pushback", 0);
    st.explore_tool_count = j.value("explore_tool_count", 0);
    st.plan_nudge_sent = j.value("plan_nudge_sent", false);
    st.post_pack_tool_count = j.value("post_pack_tool_count", 0);
    st.edit_nudge_sent = j.value("edit_nudge_sent", false);
    st.edit_phase_tool_count = j.value("edit_phase_tool_count", 0);
    st.edit_phase_nudge_sent = j.value("edit_phase_nudge_sent", false);
    st.consecutive_complete_plans = j.value("consecutive_complete_plans", 0);
    st.edit_fail_count = j.value("edit_fail_count", 0);
    st.identical_edit_repeats = j.value("identical_edit_repeats", 0);
    st.last_failed_edit_fp = j.value("last_failed_edit_fp", "");
    st.has_pack = j.value("has_pack", false);
    st.pack_incomplete = j.value("pack_incomplete", false);
    st.map_stale = j.value("map_stale", false);
    st.map_review = j.value("map_review", false);
    st.last_op_id = j.value("last_op_id", static_cast<uint64_t>(0));
    if (j.contains("watchlist") && j["watchlist"].is_array()) {
      for (const auto& t : j["watchlist"]) {
        if (t.is_string()) {
          st.watchlist.push_back(t.get<std::string>());
        }
      }
    }
    if (j.contains("pending") && j["pending"].is_array()) {
      for (const auto& p : j["pending"]) {
        PendingHunk h;
        h.path = p.value("path", "");
        h.abs_path = p.value("abs_path", "");
        h.old_text = p.value("old_text", "");
        h.new_text = p.value("new_text", "");
        h.before = p.value("before", "");
        st.pending.push_back(std::move(h));
      }
    }
  } catch (...) {
  }
  return st;
}

bool Level2Session::save_state(const std::string& workspace_root, const State& st,
                               std::string* err) {
  nlohmann::json pending = nlohmann::json::array();
  for (const auto& p : st.pending) {
    pending.push_back({{"path", p.path},
                       {"abs_path", p.abs_path},
                       {"old_text", p.old_text},
                       {"new_text", p.new_text},
                       {"before", p.before}});
  }
  nlohmann::json j = {{"turn", st.turn},
                      {"done", st.done},
                      {"last_action", st.last_action},
                      {"phase", st.phase},
                      {"edit_attempt", st.edit_attempt},
                      {"compile_attempt", st.compile_attempt},
                      {"clarify_pushback", st.clarify_pushback},
                      {"pack_incomplete_pushback", st.pack_incomplete_pushback},
                      {"explore_tool_count", st.explore_tool_count},
                      {"plan_nudge_sent", st.plan_nudge_sent},
                      {"post_pack_tool_count", st.post_pack_tool_count},
                      {"edit_nudge_sent", st.edit_nudge_sent},
                      {"edit_phase_tool_count", st.edit_phase_tool_count},
                      {"edit_phase_nudge_sent", st.edit_phase_nudge_sent},
                      {"consecutive_complete_plans", st.consecutive_complete_plans},
                      {"edit_fail_count", st.edit_fail_count},
                      {"identical_edit_repeats", st.identical_edit_repeats},
                      {"last_failed_edit_fp", st.last_failed_edit_fp},
                      {"has_pack", st.has_pack},
                      {"pack_incomplete", st.pack_incomplete},
                      {"map_stale", st.map_stale},
                      {"map_review", st.map_review},
                      {"last_op_id", st.last_op_id},
                      {"watchlist", st.watchlist},
                      {"pending", pending}};
  return write_file(state_path(workspace_root), j.dump(2) + "\n", err);
}

std::string Level2Session::truncate_observation(const std::string& text, int max_lines,
                                                std::size_t max_chars) {
  std::string result;
  if (max_lines <= 0) {
    result = text;
  } else {
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
      out << line << '\n';
      if (++n >= max_lines) {
        out << "… (observation truncated at " << max_lines << " lines)\n";
        break;
      }
    }
    result = out.str();
  }
  if (max_chars > 0 && result.size() > max_chars) {
    result.resize(max_chars);
    const auto nl = result.rfind('\n');
    if (nl != std::string::npos && nl + 40 < result.size()) {
      result.resize(nl + 1);
    }
    result += "… (observation truncated at " + std::to_string(max_chars) + " chars)\n";
  }
  return result;
}

std::string Level2Session::truncate_observation_tail(const std::string& text, int max_lines) {
  if (max_lines <= 0 || text.empty()) {
    return text;
  }
  std::vector<std::string> lines;
  lines.reserve(static_cast<std::size_t>(max_lines) + 8);
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  if (static_cast<int>(lines.size()) <= max_lines) {
    std::ostringstream out;
    for (const auto& l : lines) {
      out << l << '\n';
    }
    return out.str();
  }
  std::ostringstream out;
  out << "… (showing last " << max_lines << " of " << lines.size() << " lines)\n";
  const std::size_t start = lines.size() - static_cast<std::size_t>(max_lines);
  for (std::size_t i = start; i < lines.size(); ++i) {
    out << lines[i] << '\n';
  }
  return out.str();
}

std::string Level2Session::trim_session_body(std::string body) {
  if (body.size() <= kCharBudget) {
    return body;
  }
  const std::string obs_mark = "## Observations";
  const auto obs_pos = body.find(obs_mark);
  if (obs_pos == std::string::npos) {
    return body.substr(0, kCharBudget) + "\n\n<!-- truncated hard -->\n";
  }
  const std::string head = body.substr(0, obs_pos);
  const std::size_t obs_budget =
      kCharBudget > head.size() ? kCharBudget - head.size() : kCharBudget / 4;
  std::string obs_section = trim_observations_section(body.substr(obs_pos), obs_budget);
  body = head + obs_section;
  if (body.size() > kCharBudget) {
    body.resize(kCharBudget);
    body += "\n<!-- session hard-truncated -->\n";
  }
  return body;
}

void Level2Session::append_trace(const std::string& workspace_root,
                                 const std::string& json_line) {
  std::error_code ec;
  fs::create_directories(dir_for(workspace_root), ec);
  std::ofstream out(trace_path(workspace_root), std::ios::app);
  if (!out) {
    return;
  }
  out << json_line;
  if (json_line.empty() || json_line.back() != '\n') {
    out << '\n';
  }
}

void Level2Session::write_response_json(const std::string& workspace_root, bool ok,
                                        const std::string& action, const std::string& name,
                                        const std::string& arg, const std::string& text,
                                        const std::string& error, int turn,
                                        const std::string& phase) {
  nlohmann::json j = {{"ok", ok},
                      {"action", action},
                      {"name", name},
                      {"arg", arg},
                      {"turn", turn},
                      {"error", error},
                      {"text", text},
                      {"phase", phase}};
  std::string err;
  write_file(response_path(workspace_root), j.dump(2) + "\n", &err);
}

std::string Level2Session::maybe_tool_nudge(State& st, int tools_added) {
  if (tools_added <= 0) {
    return {};
  }
  // phase=edit: stop tool loops after edit_feedback / pack already available.
  if (st.phase == "edit") {
    st.edit_phase_tool_count += tools_added;
    if (!st.edit_phase_nudge_sent &&
        st.edit_phase_tool_count >= kEditPhaseToolNudgeAfter) {
      st.edit_phase_nudge_sent = true;
      std::ostringstream nudge;
      nudge << "_nudge:_ phase=edit: llevas " << st.edit_phase_tool_count
            << " tools. Emite **`action=edit`** ahora con `search` = span completo del "
               "pack (p.ej. `struct Foo {` … `};`), path del fragmento. No más "
               "`get_code_of` en bucle.\n\n";
      return nudge.str();
    }
    return {};
  }
  if (st.phase != "explore") {
    return {};
  }
  if (!st.has_pack) {
    st.explore_tool_count += tools_added;
    if (!st.plan_nudge_sent && st.explore_tool_count >= kExplorePlanNudgeAfter) {
      st.plan_nudge_sent = true;
      std::ostringstream nudge;
      nudge << "_nudge:_ Llevas " << st.explore_tool_count
            << " tools sin `plan`. Emite `action=plan` con 4–8 `path:Symbol`/`path:line` "
               "anclados a la Instruction (evitar path bare).\n\n";
      return nudge.str();
    }
    return {};
  }
  // Pack already covers Instruction: extras should be refetch tips, then edit.
  if (st.pack_incomplete) {
    return {};
  }
  st.post_pack_tool_count += tools_added;
  if (!st.edit_nudge_sent && st.post_pack_tool_count >= kPostPackEditNudgeAfter) {
    st.edit_nudge_sent = true;
    std::ostringstream nudge;
    nudge << "_nudge:_ Llevas " << st.post_pack_tool_count
          << " tools tras pack (Instruction cubierta). Emite `done next=edit` o `edit` "
             "(refetch solo gaps / `## Truncated` tip; no shotgun).\n\n";
    return nudge.str();
  }
  return {};
}

bool Level2Session::append_observation(const std::string& workspace_root, const std::string& block,
                                       std::size_t* session_chars, std::string* err) {
  std::string session = read_file(session_path(workspace_root));
  if (session.empty()) {
    if (err) {
      *err = "session.md ausente";
    }
    return false;
  }
  const std::string empty_marker = "## Observations\n\n(vacío — L2 pide tools)\n";
  if (session.find(empty_marker) != std::string::npos) {
    const auto pos = session.find(empty_marker);
    session.replace(pos, empty_marker.size(), std::string("## Observations\n\n") + block);
  } else if (session.find("## Observations") == std::string::npos) {
    session += "\n## Observations\n\n";
    session += block;
  } else {
    session += block;
  }
  session = trim_session_body(std::move(session));
  if (session_chars) {
    *session_chars = session.size();
  }
  return write_file(session_path(workspace_root), session, err);
}

void Level2Session::compact_observations_after_pack(const std::string& workspace_root,
                                                    const State& st, std::size_t* session_chars) {
  if (!st.has_pack) {
    return;
  }
  std::string session = read_file(session_path(workspace_root));
  const auto obs_pos = session.find("## Observations");
  if (obs_pos == std::string::npos) {
    return;
  }
  const std::string head = session.substr(0, obs_pos);
  session = head + trim_observations_section(session.substr(obs_pos), kMaxObservationCharsPacked);
  write_file(session_path(workspace_root), session, nullptr);
  if (session_chars) {
    *session_chars = session.size();
  }
}

bool Level2Session::bootstrap(const Level2BootstrapOpts& opts, std::string* err_out) {
  if (opts.workspace_root.empty()) {
    if (err_out) {
      *err_out = "workspace_root vacío";
    }
    return false;
  }
  std::error_code ec;
  fs::create_directories(dir_for(opts.workspace_root), ec);
  if (ec) {
    if (err_out) {
      *err_out = ec.message();
    }
    return false;
  }

  std::string map_path = opts.map_path;
  if (map_path.empty()) {
    map_path =
        (fs::path(opts.workspace_root) / ".tuide" / "ai" / "map_last.md").lexically_normal().string();
  }
  std::string map_body = read_file(map_path);
  if (map_body.empty()) {
    map_body = "(sin map_last.md — L1 aún no escribió el mapa)\n";
  } else if (map_body.rfind("# ", 0) == 0) {
    const auto nl = map_body.find('\n');
    if (nl != std::string::npos) {
      map_body = map_body.substr(nl + 1);
      while (!map_body.empty() && (map_body[0] == '\n' || map_body[0] == '\r')) {
        map_body.erase(map_body.begin());
      }
    }
  }

  const std::string map_query = extract_map_query_line(map_body);
  const std::string intent = opts.query + " " + opts.instruction;
  const auto intent_needles = tokenize_needles(intent);
  const auto map_needles = tokenize_needles(map_query);
  const double overlap = needle_overlap_ratio(intent_needles, map_needles);
  // Generic stale gate: map query poorly overlaps current Instruction.
  const bool map_stale =
      !map_query.empty() && !intent_needles.empty() && overlap < 0.22;

  std::ostringstream md;
  md << "# L2 session\n\n";
  // Tool guide lives only in the L2 system prompt (avoid duplicating ~1.3k chars into n_ctx).
  md << "## Instruction\n\n";
  md << "query: " << opts.query << "\n\n";
  if (!opts.instruction.empty()) {
    md << "instruction: " << opts.instruction << "\n\n";
  }
  if (!opts.seeds.empty()) {
    md << "seeds:";
    for (const auto& s : opts.seeds) {
      md << ' ' << s;
    }
    md << "\n\n";
  }
  if (map_stale) {
    md << "**map_stale=1**: el mapa rankeado parece de otra query (`" << map_query
       << "`; overlap=" << static_cast<int>(overlap * 100)
       << "%). No se inyecta el mapa completo. Usa `search` / `plan` anclado a esta "
          "Instruction.\n\n";
  }
  md << "Fase inicial: **explore**. Preferir `action=plan` en el **primer** paso con "
        "4–8 targets `path:Symbol`/`path:line` (evitar path bare). Máx. ~8 tools sueltos "
        "antes del primer plan. Tras pack cubierto: extras con `tools` batch (máx. 4); "
        "luego `{\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"}` o `edit` directo. "
        "Truncado ≠ bloqueo de edit si no hay gaps Instruction.\n\n";
  md << "## Ranked map\n\n";
  if (map_stale) {
    md << "_(omitido — map_stale; query del mapa: `" << map_query
       << "`; overlap=" << static_cast<int>(overlap * 100)
       << "%. Usa `search` / `plan` anclado a la Instruction.)_\n";
  } else {
    md << map_body;
    if (!map_body.empty() && map_body.back() != '\n') {
      md << '\n';
    }
  }
  md << "\n## Observations\n\n";
  md << "(vacío — L2 pide plan/tools)\n";

  std::string body = trim_session_body(md.str());
  if (!write_file(session_path(opts.workspace_root), body, err_out)) {
    return false;
  }
  // Full map snapshot for post-compile "¿algo más?" review.
  if (!write_file(map_initial_path(opts.workspace_root), map_body, err_out)) {
    return false;
  }

  State st;
  st.phase = "explore";
  st.last_action = "bootstrap";
  st.has_pack = false;
  st.pack_incomplete = false;
  st.explore_tool_count = 0;
  st.plan_nudge_sent = false;
  st.post_pack_tool_count = 0;
  st.edit_nudge_sent = false;
  st.edit_phase_tool_count = 0;
  st.edit_phase_nudge_sent = false;
  st.consecutive_complete_plans = 0;
  st.edit_fail_count = 0;
  st.identical_edit_repeats = 0;
  st.last_failed_edit_fp.clear();
  st.map_stale = map_stale;
  st.map_review = false;
  st.watchlist.clear();
  // Hard reset prior pack so plan1 cannot merge stale targets (path:Symbol del prompt viejo).
  {
    std::string pack_err;
    write_file(pack_path(opts.workspace_root),
               "# L2 code pack\n\n_(vacío — bootstrap; sin plan aún)_\n", &pack_err);
  }
  if (!save_state(opts.workspace_root, st, err_out)) {
    return false;
  }

  write_file(request_path(opts.workspace_root),
             "{\n  \"action\": \"tool\",\n  \"name\": \"get_code_of\",\n  \"arg\": \"\"\n}\n",
             nullptr);
  write_response_json(opts.workspace_root, true, "bootstrap", "", "", "session ready", "", 0,
                      "explore");
  append_trace(opts.workspace_root,
               std::string("{\"ts\":") + now_ms_str() +
                   ",\"event\":\"bootstrap\",\"query\":\"" + json_escape(opts.query) +
                   "\",\"phase\":\"explore\",\"map_stale\":" + (map_stale ? "1" : "0") +
                   ",\"map_overlap\":" + std::to_string(overlap) + "}");
  ai_trace(AiTraceChannel::L2, "l2_bootstrap",
           std::string("{\"path\":\"") + json_escape(session_path(opts.workspace_root)) +
               "\",\"map_stale\":" + (map_stale ? "1" : "0") + "}");
  return true;
}

Level2TurnResult Level2Session::apply_tool(const std::string& workspace_root,
                                           const std::string& name, const std::string& arg) {
  Level2TurnResult out;
  out.action = "tool";
  out.name = name;
  out.arg = arg;
  State st = load_state(workspace_root);
  out.phase = st.phase;

  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (st.done || st.phase == "done") {
    out.error = "sesión done; reinicia con bootstrap";
    return out;
  }
  if (st.phase != "explore" && st.phase != "edit") {
    out.error = "tools solo en phase explore|edit (ahora=" + st.phase + ")";
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }
  if (!tool_allowed(name)) {
    out.error = "tool no permitido: " + name;
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }
  if (deps_.tools == nullptr || !deps_.tools->has(name)) {
    out.error = "tool no registrado: " + name;
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }
  if (st.phase == "edit" && st.edit_phase_tool_count >= kEditPhaseToolPushbackAfter) {
    ++st.turn;
    st.last_action = "edit_phase_tool_pushback";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — edit_phase_tool_pushback\n\n";
    block << "Rechazado: " << st.edit_phase_tool_count << " tools en phase=edit (máx "
          << kEditPhaseToolPushbackAfter << "). Emite **`action=edit`** con search = span "
             "completo del pack; no más tools.\n\n";
    const std::string hint = pack_span_hint(workspace_root, "");
    if (!hint.empty()) {
      block << "## span sugerido (pack)\n\n```\n" << hint << "```\n\n";
    }
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "tool", name, arg, block.str(),
                        "edit_phase_tool_pushback", st.turn, "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"edit_phase_tool_pushback\",\"turn\":" +
                                     std::to_string(st.turn) + "}");
    out.ok = true;  // turn accepted; model must pivot to edit
    out.phase = "edit";
    out.summary = "edit_phase_tool_pushback";
    out.error = "edit_phase_tool_pushback";
    return out;
  }
  if (st.phase == "explore" && st.has_pack && !st.pack_incomplete &&
      st.post_pack_tool_count >= kPostPackEditPushbackAfter) {
    ++st.turn;
    st.last_action = "post_pack_tool_pushback";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — post_pack_tool_pushback\n\n";
    block << "Rechazado: " << st.post_pack_tool_count
          << " tools extras tras pack completo (máx " << kPostPackEditPushbackAfter
          << "). Emite `{\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"}` o "
             "**`action=edit`** — no más `get_code_of` en bucle.\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "tool", name, arg, block.str(),
                        "post_pack_tool_pushback", st.turn, st.phase);
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"post_pack_tool_pushback\",\"turn\":" +
                                     std::to_string(st.turn) + "}");
    out.ok = true;
    out.phase = st.phase;
    out.summary = "post_pack_tool_pushback";
    out.error = "post_pack_tool_pushback";
    return out;
  }

  const auto t0 = std::chrono::steady_clock::now();
  const AiToolResult tr = deps_.tools->invoke(name, arg);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  ++st.turn;
  st.last_action = "tool:" + name;
  out.turn = st.turn;

  const int max_lines = st.has_pack ? kMaxObservationLinesPacked : kMaxObservationLines;
  const std::size_t max_chars = st.has_pack ? kMaxObservationCharsPerTurnPacked : 0;
  const std::string obs_text = truncate_observation(tr.text, max_lines, max_chars);
  std::ostringstream block;
  block << "### turn " << st.turn << " — `" << name << "`";
  if (!arg.empty()) {
    block << " `" << arg << "`";
  }
  block << "\n\n```\n" << obs_text;
  if (!obs_text.empty() && obs_text.back() != '\n') {
    block << '\n';
  }
  if (tr.text.find("[TRUNCATED]") != std::string::npos ||
      tr.text.find("[truncated]") != std::string::npos ||
      obs_text.size() + 20 < tr.text.size()) {
    block << "note: [TRUNCATED] — usa `refetch` del resultado (`path:A-B` / "
             "`path:Symbol#mid|#tail`); no inventes el cuerpo.\n";
  }
  block << "```\n\n";

  std::string err;
  if (!append_observation(workspace_root, block.str(), &out.session_chars, &err)) {
    out.error = err;
    return out;
  }

  // Soft nudges: plan early, or stop extras after a covering pack.
  if (const std::string nudge = maybe_tool_nudge(st, 1); !nudge.empty()) {
    if (!append_observation(workspace_root, nudge, &out.session_chars, &err)) {
      out.error = err;
      return out;
    }
  }

  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, tr.ok, "tool", name, arg, obs_text,
                      tr.ok ? "" : "tool failed", st.turn, st.phase);
  std::ostringstream tj;
  tj << "{\"ts\":" << now_ms_str() << ",\"event\":\"tool\",\"turn\":" << st.turn
     << ",\"name\":\"" << json_escape(name) << "\",\"ok\":" << (tr.ok ? "true" : "false")
     << ",\"ms\":" << ms << ",\"phase\":\"" << st.phase << "\"}";
  append_trace(workspace_root, tj.str());
  ai_trace(AiTraceChannel::L2, "l2_tool",
           "{\"turn\":" + std::to_string(st.turn) + ",\"name\":\"" + ai_trace_escape(name) +
               "\",\"ok\":" + (tr.ok ? "1" : "0") + ",\"duration_ms\":" + std::to_string(ms) +
               ",\"phase\":\"" + st.phase + "\",\"arg\":\"" + ai_trace_escape(arg, 120) + "\"}");
  // Shrink ranked map in explore; always compact Observations once a pack exists.
  if (st.phase == "explore") {
    compact_session_context(workspace_root, nullptr);
  }
  if (st.has_pack) {
    compact_observations_after_pack(workspace_root, st, &out.session_chars);
  }
  out.session_chars = read_file(session_path(workspace_root)).size();
  out.ok = true;
  out.summary = "tool turn=" + std::to_string(st.turn);
  return out;
}

Level2TurnResult Level2Session::apply_tools(const std::string& workspace_root,
                                            const std::vector<L2ToolCall>& calls) {
  Level2TurnResult out;
  out.action = "tools";
  State st = load_state(workspace_root);
  out.phase = st.phase;

  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (calls.empty()) {
    out.error = "tools.calls vacío";
    return out;
  }
  if (st.done || st.phase == "done") {
    out.error = "sesión done; reinicia con bootstrap";
    return out;
  }
  if (st.phase != "explore" && st.phase != "edit") {
    out.error = "tools solo en phase explore|edit (ahora=" + st.phase + ")";
    return out;
  }
  if (deps_.tools == nullptr) {
    out.error = "tools no registrados";
    return out;
  }
  if (st.phase == "edit" && st.edit_phase_tool_count >= kEditPhaseToolPushbackAfter) {
    ++st.turn;
    st.last_action = "edit_phase_tool_pushback";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — edit_phase_tool_pushback\n\n";
    block << "Rechazado: batch tools en phase=edit tras " << st.edit_phase_tool_count
          << " tools (máx " << kEditPhaseToolPushbackAfter
          << "). Emite **`action=edit`** ahora.\n\n";
    const std::string hint = pack_span_hint(workspace_root, "");
    if (!hint.empty()) {
      block << "## span sugerido (pack)\n\n```\n" << hint << "```\n\n";
    }
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "tools", "", "", block.str(),
                        "edit_phase_tool_pushback", st.turn, "edit");
    out.ok = true;
    out.phase = "edit";
    out.summary = "edit_phase_tool_pushback";
    out.error = "edit_phase_tool_pushback";
    return out;
  }
  if (st.phase == "explore" && st.has_pack && !st.pack_incomplete &&
      st.post_pack_tool_count >= kPostPackEditPushbackAfter) {
    ++st.turn;
    st.last_action = "post_pack_tool_pushback";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — post_pack_tool_pushback\n\n";
    block << "Rechazado: batch tools extras tras pack completo (" << st.post_pack_tool_count
          << "/" << kPostPackEditPushbackAfter
          << "). Emite `done next=edit` o **`action=edit`**.\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "tools", "", "", block.str(),
                        "post_pack_tool_pushback", st.turn, st.phase);
    out.ok = true;
    out.phase = st.phase;
    out.summary = "post_pack_tool_pushback";
    out.error = "post_pack_tool_pushback";
    return out;
  }

  const int n = std::min(static_cast<int>(calls.size()), kL2MaxToolBatch);
  std::ostringstream batch_block;
  int ok_n = 0;
  int fail_n = 0;
  const auto batch_t0 = std::chrono::steady_clock::now();

  for (int i = 0; i < n; ++i) {
    const auto& call = calls[static_cast<std::size_t>(i)];
    if (!tool_allowed(call.name)) {
      ++st.turn;
      batch_block << "### turn " << st.turn << " — `" << call.name << "`";
      if (!call.arg.empty()) {
        batch_block << " `" << call.arg << "`";
      }
      batch_block << "\n\n```\nerror: tool no permitido: " << call.name << "\n```\n\n";
      ++fail_n;
      continue;
    }
    if (!deps_.tools->has(call.name)) {
      ++st.turn;
      batch_block << "### turn " << st.turn << " — `" << call.name << "`";
      if (!call.arg.empty()) {
        batch_block << " `" << call.arg << "`";
      }
      batch_block << "\n\n```\nerror: tool no registrado: " << call.name << "\n```\n\n";
      ++fail_n;
      continue;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const AiToolResult tr = deps_.tools->invoke(call.name, call.arg);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    ++st.turn;
    st.last_action = "tool:" + call.name;
    out.turn = st.turn;
    out.name = call.name;
    out.arg = call.arg;

    std::string obs_text = truncate_observation(
        tr.text, st.has_pack ? kMaxObservationLinesPacked : kMaxObservationLinesBatch,
        st.has_pack ? kMaxObservationCharsPerTurnPacked : 0);
    const bool was_truncated =
        tr.text.find("[TRUNCATED]") != std::string::npos ||
        tr.text.find("[truncated]") != std::string::npos || obs_text.size() < tr.text.size();
    if (was_truncated) {
      obs_text +=
          "\nnote: [TRUNCATED] — usa `refetch` (`path:A-B` / `path:Symbol#mid|#tail`); "
          "no inventes código.\n";
    }

    batch_block << "### turn " << st.turn << " — `" << call.name << "`";
    if (!call.arg.empty()) {
      batch_block << " `" << call.arg << "`";
    }
    batch_block << "\n\n```\n" << obs_text;
    if (!obs_text.empty() && obs_text.back() != '\n') {
      batch_block << '\n';
    }
    batch_block << "```\n\n";

    if (tr.ok) {
      ++ok_n;
    } else {
      ++fail_n;
    }

    std::ostringstream tj;
    tj << "{\"ts\":" << now_ms_str() << ",\"event\":\"tool\",\"turn\":" << st.turn
       << ",\"name\":\"" << json_escape(call.name) << "\",\"ok\":" << (tr.ok ? "true" : "false")
       << ",\"ms\":" << ms << ",\"phase\":\"" << st.phase << "\",\"batch\":1}";
    append_trace(workspace_root, tj.str());
    ai_trace(AiTraceChannel::L2, "l2_tool",
             "{\"turn\":" + std::to_string(st.turn) + ",\"name\":\"" + ai_trace_escape(call.name) +
                 "\",\"ok\":" + (tr.ok ? "1" : "0") + ",\"duration_ms\":" + std::to_string(ms) +
                 ",\"phase\":\"" + st.phase + "\",\"batch\":1,\"arg\":\"" +
                 ai_trace_escape(call.arg, 120) + "\"}");
  }

  if (static_cast<int>(calls.size()) > kL2MaxToolBatch) {
    batch_block << "_nota: se ejecutaron " << n << "/" << calls.size()
                << " calls (máx. " << kL2MaxToolBatch << ")._\n\n";
  }

  std::string err;
  if (!append_observation(workspace_root, batch_block.str(), &out.session_chars, &err)) {
    out.error = err;
    return out;
  }

  if (const std::string nudge = maybe_tool_nudge(st, n); !nudge.empty()) {
    if (!append_observation(workspace_root, nudge, &out.session_chars, &err)) {
      out.error = err;
      return out;
    }
  }

  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, fail_n == 0, "tools", out.name, out.arg, batch_block.str(),
                      fail_n == 0 ? "" : "algunas tools fallaron", st.turn, st.phase);

  const auto batch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - batch_t0)
                            .count();
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"tools_batch\",\"n\":" + std::to_string(n) +
                                   ",\"ok\":" + std::to_string(ok_n) +
                                   ",\"fail\":" + std::to_string(fail_n) +
                                   ",\"ms\":" + std::to_string(batch_ms) + "}");

  if (st.phase == "explore") {
    compact_session_context(workspace_root, nullptr);
  }
  if (st.has_pack) {
    compact_observations_after_pack(workspace_root, st, &out.session_chars);
  }
  out.session_chars = read_file(session_path(workspace_root)).size();

  out.ok = true;  // batch accepted; individual fails are in Observations
  out.phase = st.phase;
  out.summary = "tools batch n=" + std::to_string(n) + " ok=" + std::to_string(ok_n) +
                " fail=" + std::to_string(fail_n);
  return out;
}

Level2TurnResult Level2Session::apply_plan(const std::string& workspace_root,
                                           const std::vector<std::string>& targets,
                                           const std::string& summary) {
  Level2TurnResult out;
  out.action = "plan";
  State st = load_state(workspace_root);
  out.phase = st.phase;

  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (targets.empty() && st.watchlist.empty()) {
    out.error = "plan.targets vacío";
    return out;
  }
  if (st.done || st.phase == "done") {
    out.error = "sesión done; reinicia con bootstrap";
    return out;
  }
  if (st.phase != "explore" && st.phase != "edit") {
    out.error = "plan solo en phase explore|edit (ahora=" + st.phase + ")";
    write_response_json(workspace_root, false, "error", "plan", "", "", out.error, st.turn,
                        st.phase);
    return out;
  }
  if (deps_.tools == nullptr) {
    out.error = "tools no registrados";
    return out;
  }

  const std::string session_body = read_file(session_path(workspace_root));
  const auto needles = session_pack_needles(session_body);

  // Merge with previous watchlist / pack header (plan2 does not wipe plan1).
  // Only reuse pack.md targets when this session actually wrote a pack (has_pack).
  // Otherwise a stale pack from a prior bootstrap pollutes the new plan.
  std::vector<std::string> merged = st.watchlist;
  if (merged.empty() && st.has_pack) {
    merged = parse_pack_targets_header(read_file(pack_path(workspace_root)));
  }
  std::unordered_set<std::string> seen_t(merged.begin(), merged.end());
  for (const auto& raw : targets) {
    std::string t = trim_ws(raw);
    if (t.empty() || !seen_t.insert(t).second) {
      continue;
    }
    merged.push_back(std::move(t));
  }
  if (static_cast<int>(merged.size()) > kL2MaxPlanTargets) {
    merged.resize(static_cast<std::size_t>(kL2MaxPlanTargets));
  }

  // Normalize bare paths → path:Symbol / path:A-B via outline, then search-in-file.
  std::vector<std::string> uniq_targets;
  uniq_targets.reserve(merged.size());
  std::vector<std::string> normalize_notes;
  std::unordered_set<std::string> skip_fetch;  // bare junk: warn only, no fragment body
  for (const auto& raw : merged) {
    std::string t = raw;
    if (!target_has_symbol_or_range(t)) {
      const std::string path = path_from_plan_target(t);
      std::string resolved;
      if (!path.empty() && deps_.tools->has("file_outline")) {
        const AiToolResult ol = deps_.tools->invoke("file_outline", path);
        if (ol.ok) {
          resolved = best_symbol_target_from_outline(path, ol.text, needles);
        }
      }
      std::string from_search;
      if (!path.empty() && deps_.tools->has("search")) {
        // Needles first (edit idents); path stem last — stem-alone matches pollute ranking.
        std::ostringstream q;
        int nq = 0;
        for (const auto& n : needles) {
          if (!looks_like_code_ident(n) || n.size() < 5) {
            continue;
          }
          if (nq > 0) {
            q << '|';
          }
          q << n;
          if (++nq >= 8) {
            break;
          }
        }
        const std::string stem = path_stem_key(path);
        if (nq > 0 && !stem.empty() && stem.size() >= 4) {
          q << '|' << stem;
          ++nq;
        } else if (nq == 0 && !stem.empty()) {
          q << stem;
          ++nq;
        }
        if (nq > 0) {
          const AiToolResult sr = deps_.tools->invoke("search", q.str());
          if (sr.ok) {
            from_search = best_line_target_from_search(path, sr.text, needles);
          }
        }
      }
      // Prefer search-in-file only when the hit is strong; else outline; else omit.
      if (!from_search.empty()) {
        resolved = from_search;
        normalize_notes.push_back("- bare `" + raw + "` → `" + resolved + "` (search-in-file)");
        t = resolved;
      } else if (!resolved.empty()) {
        normalize_notes.push_back("- bare `" + raw + "` → `" + resolved + "` (needles/outline)");
        t = resolved;
      } else {
        normalize_notes.push_back(
            "- bare `" + raw +
            "` sin hit fuerte — omitido; preferir `path:Symbol` / `path:line`");
        skip_fetch.insert(raw);
      }
    }
    if (std::find(uniq_targets.begin(), uniq_targets.end(), t) == uniq_targets.end()) {
      uniq_targets.push_back(t);
    }
  }
  if (uniq_targets.empty()) {
    out.error = "plan.targets vacío tras normalizar";
    return out;
  }

  std::vector<std::string> uniq_paths;
  std::unordered_set<std::string> seen_paths;
  for (const auto& t : uniq_targets) {
    if (skip_fetch.count(t)) {
      continue;
    }
    const std::string p = path_from_plan_target(t);
    if (!p.empty() && seen_paths.insert(p).second) {
      uniq_paths.push_back(p);
    }
  }

  // Sibling .hpp/.h next to a .cpp: declaration / #include surface for edits that compile.
  {
    const std::vector<std::string> paths_copy = uniq_paths;
    for (const auto& p : paths_copy) {
      const std::string sib = sibling_header_rel(workspace_root, p);
      if (sib.empty()) {
        continue;
      }
      if (seen_paths.insert(sib).second) {
        uniq_paths.push_back(sib);
      }
      const std::string t = sib + ":1";
      if (std::find(uniq_targets.begin(), uniq_targets.end(), t) == uniq_targets.end() &&
          std::find(uniq_targets.begin(), uniq_targets.end(), sib) == uniq_targets.end()) {
        uniq_targets.push_back(t);
        normalize_notes.push_back("- sibling header `" + p + "` → `" + t +
                                  "` (decl / #include)");
      }
    }
  }

  struct Frag {
    std::string target;
    std::string text;
    bool ok = false;
    bool truncated = false;
    bool junk = false;  // wrong_symbol from bare / no overlap → drop body
    bool explicit_locus = false;  // from this plan's targets (path:line/Symbol)
    int plan_line = 0;            // original path:line before window expand
    std::string refetch;
    std::size_t rank_size = 0;
    int rank_boost = 0;  // higher = earlier (small + relevant)
  };
  struct Outline {
    std::string path;
    std::string text;
    bool ok = false;
  };

  const auto plan_t0 = std::chrono::steady_clock::now();
  std::unordered_set<std::string> explicit_targets;
  std::unordered_set<std::string> explicit_paths;
  for (const auto& raw : targets) {
    const std::string t = trim_ws(raw);
    if (!t.empty()) {
      explicit_targets.insert(t);
      const std::string ep = path_from_plan_target(t);
      if (!ep.empty()) {
        explicit_paths.insert(ep);
      }
    }
  }
  // Helper: path overlaps any pack needle (stem or filename).
  auto path_touches_needles = [&](const std::string& path) -> bool {
    if (path.empty()) {
      return false;
    }
    const std::string stem = path_stem_key(path);
    const std::string low = to_lower_copy(path);
    for (const auto& n : needles) {
      if (n.size() < 4) {
        continue;
      }
      if ((!stem.empty() && (stem.find(n) != std::string::npos || n.find(stem) != std::string::npos)) ||
          low.find(n) != std::string::npos) {
        return true;
      }
    }
    return false;
  };

  std::vector<Frag> frags;
  frags.reserve(uniq_targets.size() + 8);
  std::unordered_set<std::string> fetched;
  for (const auto& t : uniq_targets) {
    Frag f;
    f.target = t;
    if (skip_fetch.count(t)) {
      f.ok = false;
      f.junk = true;
      f.text = "omitido: bare path sin resolución (`" + t + "`)";
      f.rank_size = 999999;
      f.rank_boost = -100;
      frags.push_back(std::move(f));
      continue;
    }
    if (!deps_.tools->has("get_code_of")) {
      f.text = "error: get_code_of no registrado";
    } else {
      std::string fetch_arg = t;
      const int line = line_from_plan_target(t);
      const std::string path = path_from_plan_target(t);
      f.plan_line = line;
      f.explicit_locus = explicit_targets.count(t) > 0 ||
                         (line > 0 && explicit_paths.count(path) > 0);
      // path:line → fetch a downward-biased range (tighter for explicit edit loci).
      if (line > 0 && !path.empty()) {
        fetch_arg = f.explicit_locus ? line_window_target_biased(path, line, 25, 55)
                                     : line_window_target_biased(path, line);
      }
      const AiToolResult tr = deps_.tools->invoke("get_code_of", fetch_arg);
      f.ok = tr.ok;
      f.text = tr.text.empty() ? (tr.ok ? "(vacío)" : "error get_code_of") : tr.text;
      f.truncated = text_looks_truncated(f.text);
      // Keep explicit path:line label when possible (don't rewrite to a huge A-B only).
      if (f.explicit_locus && line > 0) {
        f.target = path + ":" + std::to_string(line);
        f.refetch = fetch_arg;
      } else if (fetch_arg != t) {
        f.target = fetch_arg;
      }
      if (f.truncated && f.refetch.empty()) {
        if (line > 0) {
          f.refetch = line_window_target_biased(path, line + 60, 10, 70);
        } else {
          f.refetch = extract_refetch_hint(f.text, t);
        }
      }
      const std::string got_name = resolved_name_from_tool_text(f.text);
      const bool ok_name = name_ok_for_target(got_name, t, needles);
      const int needle_sc = score_symbol_against_needles(got_name, needles) +
                            score_symbol_against_needles(f.text.substr(0, 400), needles);
      if (f.ok && !ok_name) {
        const bool was_bare_like = symbol_from_plan_target(t).empty() && line <= 0;
        const bool sibling_preamble =
            !f.explicit_locus && line == 1 && path_looks_like_header(path);
        if ((was_bare_like || needle_sc <= 0) && !sibling_preamble) {
          f.junk = true;
          f.text = "WARN wrong_symbol: resuelto `" + got_name +
                   "` no solapa needles/target — fragmento omitido. "
                   "Pide `path:Symbol` / `path:A-B` concreto.\n";
          f.ok = false;
          f.truncated = false;
          f.rank_boost = -50;
        } else {
          f.text +=
              "\nWARN wrong_symbol: resuelto `" + got_name +
              "` no solapa Instruction — revisa outline / pide path:Symbol concreto.\n";
          if (!f.explicit_locus && line == 1 && path_looks_like_header(path)) {
            f.rank_boost = 75;
          }
        }
      } else if (f.ok) {
        f.rank_boost = 10 + needle_sc / 5;
        if (line > 0) {
          f.rank_boost += 80;  // edit locus — pack before merge noise
        }
        if (f.explicit_locus) {
          f.rank_boost += 50;
        }
        // Merged watchlist noise (not in this plan's explicit targets) with weak needles → demote.
        if (!f.explicit_locus && needle_sc <= 0 && line <= 0) {
          f.rank_boost -= 60;
        }
        // Path unrelated to Instruction needles and not explicit → hard demote / noise.
        if (!f.explicit_locus && !path_touches_needles(path) && needle_sc <= 0) {
          f.rank_boost -= 80;
        }
        // Injected sibling.hpp:1 is decl surface, not an edit locus — keep but don't steal Control.
        if (!f.explicit_locus && line == 1 && path_looks_like_header(path)) {
          f.rank_boost = 75 + std::max(0, needle_sc) / 5;
        }
      }
      fetched.insert(t);
      fetched.insert(fetch_arg);
      if (f.explicit_locus && line > 0) {
        fetched.insert(path + ":" + std::to_string(line));
      }
    }
    f.rank_size = f.junk ? 999999 : f.text.size();
    frags.push_back(std::move(f));
  }

  // Auto-refetch truncated gaps (explore-fill): prefer line windows + needle-overlapping hints.
  std::vector<Frag> extras;
  auto already = [&](const std::string& t) {
    return fetched.count(t) ||
           std::find(uniq_targets.begin(), uniq_targets.end(), t) != uniq_targets.end();
  };
  for (const auto& f : frags) {
    if (!f.truncated || f.junk || !deps_.tools->has("get_code_of")) {
      continue;
    }
    std::vector<std::string> try_targets;
    const int line = line_from_plan_target(f.target);
    const std::string path = path_from_plan_target(f.target);
    if (line > 0 && !path.empty()) {
      try_targets.push_back(line_window_target_biased(path, line));
      try_targets.push_back(line_window_target_biased(path, line + 100, 20, 100));
      try_targets.push_back(line_window_target_biased(path, std::max(1, line - 100), 20, 100));
    }
    if (!f.refetch.empty()) {
      try_targets.push_back(f.refetch);
    }
    int added = 0;
    for (const auto& rt : try_targets) {
      if (rt.empty() || already(rt)) {
        continue;
      }
      const AiToolResult tr = deps_.tools->invoke("get_code_of", rt);
      Frag e;
      e.target = rt;
      e.ok = tr.ok;
      e.text = tr.text.empty() ? "(vacío)" : tr.text;
      e.truncated = text_looks_truncated(e.text);
      e.refetch = extract_refetch_hint(e.text, rt);
      e.rank_size = e.text.size();
      const int hit = score_symbol_against_needles(e.text.substr(0, 1200), needles);
      // Skip empty/noise auto-fills that don't touch pack needles.
      if (hit <= 0 && line <= 0) {
        continue;
      }
      if (hit <= 0 && line > 0 && added > 0) {
        continue;
      }
      e.rank_boost = 50 + (line > 0 ? 40 : 0) + hit / 3;
      // Primary biased window around the requested line gets top priority.
      if (line > 0 && !path.empty() && rt == line_window_target_biased(path, line)) {
        e.rank_boost = 130 + hit / 2;
      }
      fetched.insert(rt);
      extras.push_back(std::move(e));
      if (++added >= 2) {
        break;
      }
    }
  }
  for (auto& e : extras) {
    frags.push_back(std::move(e));
  }

  // Relevance first, then small; junk last.
  std::stable_sort(frags.begin(), frags.end(), [](const Frag& a, const Frag& b) {
    if (a.junk != b.junk) {
      return !a.junk;
    }
    if (a.rank_boost != b.rank_boost) {
      return a.rank_boost > b.rank_boost;
    }
    return a.rank_size < b.rank_size;
  });

  // --- Pack diversity: classify fragments into generic roles and reserve slots. ---
  enum class FragRole {
    Decl = 0,       // struct/enum/constexpr type surface
    IdConst = 1,    // string/press/tab id constants
    Layout = 2,     // boxes / click targets / arrays
    Control = 3,    // selected_/render_/switch body
    ApiFn = 4,      // named helpers (path:Symbol)
    Other = 5,
    Noise = 6,
  };
  auto role_name = [](FragRole r) -> const char* {
    switch (r) {
      case FragRole::Decl:
        return "decl";
      case FragRole::IdConst:
        return "id_const";
      case FragRole::Layout:
        return "layout";
      case FragRole::Control:
        return "control";
      case FragRole::ApiFn:
        return "api_fn";
      case FragRole::Other:
        return "other";
      case FragRole::Noise:
        return "noise";
    }
    return "other";
  };
  auto classify_frag = [&](const Frag& f) -> FragRole {
    if (f.junk || !f.ok) {
      return FragRole::Noise;
    }
    std::string low = f.text;
    for (char& c : low) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const std::string tgt = to_lower_copy(f.target);
    const int needle_hit = score_symbol_against_needles(f.text.substr(0, 900), needles);
    auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
    // Merged weak noise (pty/repaint/wake) without needle support.
    if ((has("on_pty") || has("repaint") || has("scrollback") || has("request_terminal")) &&
        needle_hit <= 0) {
      return FragRole::Noise;
    }
    if (f.rank_boost < 15 && needle_hit <= 0) {
      return FragRole::Noise;
    }
    // Unrelated merge path with no needle support → noise (drops terminal noise on shortcut tasks).
    if (!f.explicit_locus && needle_hit <= 0 &&
        !path_touches_needles(path_from_plan_target(f.target))) {
      return FragRole::Noise;
    }
    // Decl before control: type surfaces often mention selected_tab fields.
    if (has("struct ") || has("enum ") || has("class ") ||
        (has("static constexpr int") && (has("\nk") || has(" k")))) {
      return FragRole::Decl;
    }
    // Id constants: press/tab ids OR shortcut/i18n string tables / action meta.
    if (tgt.find("press_id") != std::string::npos || has("press_id::") || has("console.tab") ||
        has("constexpr std::string_view k") || (has("constexpr") && has(".tab.")) ||
        (has("string_view k") && (has("console.") || has("press"))) ||
        has("shortcuts.") || has("{\"shortcuts.") || tgt.find("strings_") != std::string::npos ||
        has("kactionmeta") || has("key_action_label") ||
        (has("toggle_") && (has("shortcut") || has("keyaction")))) {
      return FragRole::IdConst;
    }
    // Layout vs control: declaration/array beat incidental &state->tab_boxes in render.
    const bool layout_decl = has("std::array<box") || has("std::array< box") ||
                             has("tab_boxes;") || has("targets = {") ||
                             (has("std::array") && has("tab_boxes") && !has("else if (selected"));
    const bool layout_click =
        has("targets = {") || (has("clickable") && has("tab_boxes") && has("press_id::"));
    const bool controlish = has("render_") || has("else if (selected") || has("body =") ||
                            has("switch (") || (has("selected_tab ==") && !has("struct "));
    if (layout_decl || (layout_click && !controlish)) {
      return FragRole::Layout;
    }
    if (controlish) {
      return FragRole::Control;
    }
    if (has("tab_boxes") && !controlish) {
      return FragRole::Layout;
    }
    if ((!symbol_from_plan_target(f.target).empty() || tgt.find("make_") != std::string::npos) &&
        needle_hit > 0) {
      return FragRole::ApiFn;
    }
    if (needle_hit <= 0 && f.rank_boost < 40) {
      return FragRole::Noise;
    }
    return FragRole::Other;
  };

  std::vector<FragRole> frag_roles(frags.size(), FragRole::Other);
  for (std::size_t i = 0; i < frags.size(); ++i) {
    frag_roles[i] = classify_frag(frags[i]);
  }
  // Post-pass: strongest markers win (huge windows often match several roles).
  for (std::size_t i = 0; i < frags.size(); ++i) {
    if (frags[i].junk || !frags[i].ok) {
      continue;
    }
    std::string low = frags[i].text;
    for (char& c : low) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const std::string tgt = to_lower_copy(frags[i].target);
    const bool controlish = low.find("render_") != std::string::npos ||
                            low.find("else if (selected_tab") != std::string::npos ||
                            low.find("body =") != std::string::npos;
    const bool layout_decl =
        low.find("std::array<box") != std::string::npos ||
        low.find("std::array< box") != std::string::npos ||
        low.find("tab_boxes;") != std::string::npos || low.find("targets = {") != std::string::npos ||
        (low.find("std::array") != std::string::npos && low.find("tab_boxes") != std::string::npos &&
         !controlish);
    // Incidental &state->tab_boxes inside a render window is Control, not Layout.
    if (layout_decl && !(controlish && frags[i].rank_size > 2500 &&
                         low.find("std::array") == std::string::npos &&
                         low.find("targets = {") == std::string::npos)) {
      frag_roles[i] = FragRole::Layout;
    } else if ((low.find("struct ") != std::string::npos || low.find("enum ") != std::string::npos) &&
               low.find("static constexpr int") != std::string::npos) {
      frag_roles[i] = FragRole::Decl;
    } else if (controlish) {
      frag_roles[i] = FragRole::Control;
    } else if (tgt.find("press_id") != std::string::npos ||
               low.find("press_id::k") != std::string::npos ||
               low.find("console.tab.") != std::string::npos ||
               low.find("shortcuts.") != std::string::npos ||
               tgt.find("strings_") != std::string::npos) {
      frag_roles[i] = FragRole::IdConst;
    } else if (frag_roles[i] == FragRole::IdConst && low.find("press_id") == std::string::npos &&
               low.find("console.tab") == std::string::npos &&
               low.find("shortcuts.") == std::string::npos &&
               low.find("constexpr std::string_view k") == std::string::npos) {
      frag_roles[i] = FragRole::Other;  // drop false string_view helpers
    }
    const std::string pth = to_lower_copy(path_from_plan_target(frags[i].target));
    if ((frag_roles[i] == FragRole::Other || frag_roles[i] == FragRole::ApiFn ||
         frag_roles[i] == FragRole::Noise) &&
        path_looks_like_header(pth)) {
      frag_roles[i] = FragRole::Decl;
    }
    // Symbol-only helpers require a code-like needle hit on the symbol itself.
    if (frag_roles[i] == FragRole::ApiFn || frag_roles[i] == FragRole::Other) {
      const std::string sym = to_lower_copy(symbol_from_plan_target(frags[i].target));
      if (tgt.find("request_terminal") != std::string::npos || tgt.find("on_pty") != std::string::npos ||
          tgt.find("repaint") != std::string::npos || tgt.find("scrollback") != std::string::npos) {
        frag_roles[i] = FragRole::Noise;
      } else if (frag_roles[i] == FragRole::ApiFn) {
        bool strong = false;
        for (const auto& n : needles) {
          if (!looks_like_code_ident(n) || n.size() < 5 || sym.empty()) {
            continue;
          }
          if (sym.find(n) != std::string::npos || n.find(sym) != std::string::npos) {
            strong = true;
            break;
          }
        }
        if (!strong) {
          frag_roles[i] = FragRole::Noise;
        }
      }
    }
  }

  // Build pack order: reserve core roles first (Other is filler — pack after extras).
  static const FragRole kRoleOrder[] = {FragRole::Decl, FragRole::IdConst, FragRole::Layout,
                                        FragRole::Control, FragRole::ApiFn};
  static const FragRole kRoleOrderAll[] = {FragRole::Decl,    FragRole::IdConst, FragRole::Layout,
                                           FragRole::Control, FragRole::ApiFn,   FragRole::Other};
  std::vector<std::size_t> pack_order;
  pack_order.reserve(frags.size());
  std::vector<char> picked(frags.size(), 0);
  auto pick_best = [&](FragRole want) -> int {
    int best = -1;
    int best_score = -1;
    for (std::size_t i = 0; i < frags.size(); ++i) {
      if (picked[i] || frags[i].junk || frag_roles[i] != want) {
        continue;
      }
      // Prefer compact exemplars: huge mixed windows dilute the role signal.
      int sc = frags[i].rank_boost * 10 - static_cast<int>(frags[i].rank_size / 80);
      std::string low = frags[i].text.substr(0, 1600);
      for (char& c : low) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      const std::string tgt = to_lower_copy(frags[i].target);
      if (want == FragRole::Decl && low.find("struct ") != std::string::npos) {
        sc += 80;
      }
      if (want == FragRole::Layout) {
        if (low.find("std::array") != std::string::npos && low.find("tab_boxes") != std::string::npos) {
          sc += 120;
        } else if (low.find("targets = {") != std::string::npos) {
          sc += 110;
        } else if (low.find("tab_boxes") != std::string::npos) {
          sc += 60;
        }
        if (low.find("else if (selected") != std::string::npos) {
          sc -= 100;  // render body, not layout surface
        }
        if (frags[i].rank_size < 1200) {
          sc += 40;
        }
      }
      if (want == FragRole::Control &&
          (low.find("render_") != std::string::npos || low.find("else if (selected") != std::string::npos)) {
        sc += 80;
      }
      if (want == FragRole::IdConst) {
        if (tgt.find("press_id") != std::string::npos || tgt.find("strings_") != std::string::npos) {
          sc += 140;
        }
        if (low.find("shortcuts.") != std::string::npos) {
          sc += 120;
        }
        if (low.find("console.tab") != std::string::npos || low.find("press_id::k") != std::string::npos) {
          sc += 100;
        } else if (low.find("press_id") != std::string::npos) {
          sc += 60;
        }
        if (tgt.find("console_tab_press") != std::string::npos) {
          sc += 80;
        }
        if (frags[i].explicit_locus) {
          sc += 60;
        }
      }
      if (want == FragRole::ApiFn && frags[i].target.find("make_") != std::string::npos) {
        sc += 40;
      }
      if (frags[i].explicit_locus) {
        sc += 40;
      }
      if (sc > best_score) {
        best_score = sc;
        best = static_cast<int>(i);
      }
    }
    return best;
  };
  // Reserve one slot per role when a candidate exists (diversity).
  // Layout gets a second early slot (click-target arrays) before control/api burn budget.
  for (FragRole r : kRoleOrder) {
    const int idx = pick_best(r);
    if (idx >= 0) {
      picked[static_cast<std::size_t>(idx)] = 1;
      pack_order.push_back(static_cast<std::size_t>(idx));
    }
    if (r == FragRole::Layout) {
      const int idx2 = pick_best(FragRole::Layout);
      if (idx2 >= 0) {
        picked[static_cast<std::size_t>(idx2)] = 1;
        pack_order.push_back(static_cast<std::size_t>(idx2));
      }
    }
  }
  // Diversity by file: one best remaining fragment per distinct path (scattered edits).
  {
    std::unordered_set<std::string> paths_seen;
    for (std::size_t oi = 0; oi < pack_order.size(); ++oi) {
      const std::string p = path_from_plan_target(frags[pack_order[oi]].target);
      if (!p.empty()) {
        paths_seen.insert(p);
      }
    }
    std::vector<std::pair<int, std::size_t>> by_path;  // score, index
    for (std::size_t i = 0; i < frags.size(); ++i) {
      if (picked[i] || frags[i].junk || frag_roles[i] == FragRole::Noise) {
        continue;
      }
      const std::string p = path_from_plan_target(frags[i].target);
      if (p.empty() || paths_seen.count(p)) {
        continue;
      }
      int sc = frags[i].rank_boost * 10 - static_cast<int>(frags[i].rank_size / 100);
      if (frags[i].explicit_locus) {
        sc += 80;
      }
      by_path.push_back({sc, i});
    }
    std::sort(by_path.begin(), by_path.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& pr : by_path) {
      const std::size_t i = pr.second;
      if (picked[i]) {
        continue;
      }
      const std::string p = path_from_plan_target(frags[i].target);
      if (p.empty() || !paths_seen.insert(p).second) {
        continue;
      }
      picked[i] = 1;
      pack_order.push_back(i);
      if (paths_seen.size() >= 8) {
        break;
      }
    }
  }
  // Second pass: best remaining exemplar per role (not raw sort order).
  int role_extra[7] = {};
  role_extra[static_cast<int>(FragRole::Layout)] = 1;  // already took layout extra above
  for (FragRole r : kRoleOrder) {
    if (role_extra[static_cast<int>(r)] >= 1) {
      continue;
    }
    const int idx = pick_best(r);
    if (idx < 0) {
      continue;
    }
    picked[static_cast<std::size_t>(idx)] = 1;
    pack_order.push_back(static_cast<std::size_t>(idx));
    ++role_extra[static_cast<int>(r)];
  }
  // Other only after core role extras (avoid starving layout/id slots).
  {
    const int idx = pick_best(FragRole::Other);
    if (idx >= 0) {
      picked[static_cast<std::size_t>(idx)] = 1;
      pack_order.push_back(static_cast<std::size_t>(idx));
    }
  }
  // Noise only if budget leftover later (append at end, packing may skip).
  for (std::size_t i = 0; i < frags.size(); ++i) {
    if (!picked[i] && !frags[i].junk && frag_roles[i] == FragRole::Noise) {
      pack_order.push_back(i);
      picked[i] = 1;
    }
  }
  for (std::size_t i = 0; i < frags.size(); ++i) {
    if (!picked[i]) {
      pack_order.push_back(i);
    }
  }

  std::ostringstream diversity_note;
  {
    int counts[7] = {};
    for (std::size_t i = 0; i < frags.size(); ++i) {
      if (!frags[i].junk && frags[i].ok) {
        ++counts[static_cast<int>(frag_roles[i])];
      }
    }
    diversity_note << "pack_roles:";
    for (FragRole r : kRoleOrderAll) {
      const int c = counts[static_cast<int>(r)];
      if (c > 0) {
        diversity_note << ' ' << role_name(r) << '=' << c;
      }
    }
    if (counts[static_cast<int>(FragRole::Noise)] > 0) {
      diversity_note << " noise=" << counts[static_cast<int>(FragRole::Noise)] << "(deferred)";
    }
    diversity_note << '\n';
  }

  std::vector<Outline> outlines;
  outlines.reserve(uniq_paths.size());
  for (const auto& p : uniq_paths) {
    Outline o;
    o.path = p;
    if (!deps_.tools->has("file_outline")) {
      o.text = "error: file_outline no registrado";
    } else {
      const AiToolResult tr = deps_.tools->invoke("file_outline", p);
      o.ok = tr.ok;
      o.text = tr.text.empty() ? (tr.ok ? "(vacío)" : "error file_outline") : tr.text;
    }
    outlines.push_back(std::move(o));
  }

  std::vector<Outline> headers;
  if (deps_.tools->has("headers_of")) {
    for (const auto& p : uniq_paths) {
      const AiToolResult tr = deps_.tools->invoke("headers_of", p);
      if (!tr.ok || tr.text.empty()) {
        continue;
      }
      Outline h;
      h.path = p;
      h.ok = true;
      h.text = tr.text;
      headers.push_back(std::move(h));
    }
  }

  const std::size_t budget = kMaxPackChars;
  const std::size_t outline_reserve =
      std::min<std::size_t>(budget / 3, 2500 + uniq_paths.size() * 200);
  std::size_t frag_budget = budget > outline_reserve + 200 ? budget - outline_reserve : budget / 2;

  std::ostringstream pack;
  pack << "# L2 code pack\n\n";
  if (!summary.empty()) {
    pack << "plan_summary: " << summary << "\n\n";
  }
  if (!normalize_notes.empty()) {
    pack << "## Target normalize\n\n";
    for (const auto& n : normalize_notes) {
      pack << n << "\n";
    }
    pack << "\n";
  }
  pack << diversity_note.str() << "\n";
  pack << "targets (" << uniq_targets.size() << "):";
  for (const auto& t : uniq_targets) {
    pack << " `" << t << "`";
  }
  pack << "\n\n## Fragments\n\n";

  // Reserved diversity slots get a fair share so one locus cannot starve other roles.
  const std::size_t reserved_n = std::min<std::size_t>(pack_order.size(), 5);
  const std::size_t role_share =
      reserved_n > 0 ? std::max<std::size_t>(400, frag_budget / (reserved_n + 1))
                     : kMaxFragShareChars;

  std::size_t used_frags = 0;
  int frag_ok = 0;
  std::vector<std::string> trunc_index;
  std::unordered_set<int> roles_packed;
  for (std::size_t oi = 0; oi < pack_order.size(); ++oi) {
    const std::size_t i = pack_order[oi];
    const auto& f = frags[i];
    const FragRole role = frag_roles[i];
    if (f.junk) {
      std::ostringstream sec;
      sec << "### get_code_of `" << f.target << "` (omitido)\n\n" << f.text << "\n\n";
      if (used_frags + sec.str().size() < frag_budget) {
        pack << sec.str();
        used_frags += sec.str().size();
      }
      continue;
    }
    const std::size_t remaining =
        frag_budget > used_frags ? frag_budget - used_frags : 0;
    if (remaining < 80) {
      pack << "_pack: fragmentos restantes omitidos por presupuesto ("
           << (pack_order.size() - oi) << ")._\n\n";
      for (std::size_t oj = oi; oj < pack_order.size(); ++oj) {
        const auto& fj = frags[pack_order[oj]];
        if (fj.junk) {
          continue;
        }
        trunc_index.push_back("- `" + fj.target +
                              "` — omitido por presupuesto pack; refetch get_code_of `" +
                              fj.target + "`");
      }
      break;
    }
    // Drop noise unless we still have spare budget after diversity slots.
    if (role == FragRole::Noise && used_frags > frag_budget / 2) {
      continue;
    }
    if (f.rank_boost < 0 && remaining < frag_budget / 3) {
      continue;
    }
    if (f.rank_boost < 15 && role == FragRole::Noise) {
      continue;
    }
    if (f.ok) {
      ++frag_ok;
    }

    const bool first_of_role = roles_packed.insert(static_cast<int>(role)).second;
    std::size_t per = std::min({remaining, kMaxFragShareChars, role_share});
    // First control/layout/decl slot: allow up to role_share (not half budget).
    if (!first_of_role) {
      per = std::min(per, role_share * 3 / 4);
    }
    if (f.rank_boost >= 80 && first_of_role && role == FragRole::Control) {
      per = std::min(remaining, std::max(per, role_share));
    }
    // IdConst/Layout exemplars are small surfaces — give them enough room to keep markers.
    if (first_of_role &&
        (role == FragRole::IdConst || role == FragRole::Layout || role == FragRole::Decl)) {
      per = std::min(remaining, std::max(per, std::min<std::size_t>(role_share + 200, 1400)));
    }

    const bool pack_trunc = f.text.size() > per;
    std::string tip = !f.refetch.empty() ? f.refetch : f.target;
    const int line = f.plan_line > 0 ? f.plan_line : line_from_plan_target(f.target);
    if (line > 0) {
      tip = f.explicit_locus ? line_window_target_biased(path_from_plan_target(f.target), line, 25, 55)
                             : line_window_target_biased(path_from_plan_target(f.target), line);
    }
    std::vector<std::string> prefer = needles;
    if (role == FragRole::Layout) {
      prefer.insert(prefer.begin(),
                    {"tab_boxes[ConsolePanelTabs::kAi]", "targets = {", "tab_boxes", "std::array"});
    } else if (role == FragRole::Control) {
      prefer.insert(prefer.begin(),
                    {"keybind_matches", "ToggleBreakpoint", "render_", "else if (selected",
                     "selected_tab =="});
    } else if (role == FragRole::IdConst) {
      prefer.insert(prefer.begin(),
                    {"shortcuts.", "toggle_breakpoint", "toggle_line_mark", "kConsoleTabAi",
                     "console.tab.ai", "press_id::k", "console.tab", "string_view"});
    } else if (role == FragRole::Decl) {
      prefer.insert(prefer.begin(),
                    {"ToggleBreakpoint", "KeyAction", "struct ", "static constexpr int", "enum "});
    } else if (role == FragRole::ApiFn) {
      prefer.insert(prefer.begin(), {"toggle_breakpoint", "KeyAction::", "make_"});
    }
    const bool center = line > 0 || f.rank_boost >= 50 || role == FragRole::Control ||
                        role == FragRole::Layout || role == FragRole::Decl ||
                        role == FragRole::IdConst;
    // Layout/Decl: first marker. IdConst with shortcuts.: first hit. Switches of press ids: last.
    const bool first_hit = role == FragRole::Layout || role == FragRole::Decl ||
                           (role == FragRole::IdConst && f.text.find("shortcuts.") != std::string::npos);
    std::string body = center ? truncate_center_budget(f.text, per, tip, prefer, first_hit)
                              : truncate_to_budget(f.text, per, tip);
    const bool is_trunc = f.truncated || pack_trunc;
    if (is_trunc) {
      trunc_index.push_back("- `" + f.target + "` — truncated; refetch `get_code_of " + tip +
                            "` (también `#tail` / `path:A-B`)");
    }
    std::ostringstream sec;
    sec << "### get_code_of `" << f.target << "`";
    if (!f.ok) {
      sec << " (fail)";
    } else if (is_trunc) {
      sec << " [TRUNCATED]";
    }
    sec << "  <!-- role:" << role_name(role) << " -->";
    sec << "\n\n```\n" << body;
    if (!body.empty() && body.back() != '\n') {
      sec << '\n';
    }
    sec << "```\n\n";
    pack << sec.str();
    used_frags += sec.str().size();
  }

  // Headers before outlines so overflow drops outlines first (decl/#include stay).
  const std::size_t header_budget_cap = std::min<std::size_t>(1000, budget / 8);
  std::size_t after_frags = pack.str().size();
  std::size_t header_budget =
      budget > after_frags + 400
          ? std::min(header_budget_cap, budget - after_frags - 400)
          : 0;
  if (!headers.empty() && header_budget > 80) {
    pack << "## Headers\n\n";
    for (const auto& h : headers) {
      if (header_budget < 60) {
        break;
      }
      std::string body =
          truncate_to_budget(h.text, std::min<std::size_t>(header_budget, 400), {});
      std::ostringstream sec;
      sec << "### headers_of `" << h.path << "`\n\n```\n" << body;
      if (!body.empty() && body.back() != '\n') {
        sec << '\n';
      }
      sec << "```\n\n";
      const std::string s = sec.str();
      if (s.size() > header_budget) {
        break;
      }
      pack << s;
      header_budget -= s.size();
    }
  }

  pack << "## Outlines\n\n";
  std::size_t outline_budget =
      budget > pack.str().size() ? budget - pack.str().size() : 0;
  int outline_ok = 0;
  for (std::size_t i = 0; i < outlines.size(); ++i) {
    const auto& o = outlines[i];
    if (o.ok) {
      ++outline_ok;
    }
    if (outline_budget < 60) {
      pack << "_pack: outlines restantes omitidos por presupuesto._\n\n";
      break;
    }
    const std::size_t per =
        std::max<std::size_t>(80, outline_budget / std::max<std::size_t>(1, outlines.size() - i));
    std::string body = filter_outline_for_needles(o.text, needles, per);
    std::ostringstream sec;
    sec << "### file_outline `" << o.path << "`" << (o.ok ? "" : " (fail)") << "\n\n```\n"
        << body;
    if (!body.empty() && body.back() != '\n') {
      sec << '\n';
    }
    sec << "```\n\n";
    const std::string s = sec.str();
    if (s.size() > outline_budget) {
      pack << truncate_to_budget(s, outline_budget, {}) << "\n";
      outline_budget = 0;
      break;
    }
    pack << s;
    outline_budget -= s.size();
  }

  std::ostringstream trunc_sec;
  if (!trunc_index.empty()) {
    trunc_sec << "## Truncated (refetch before editing these)\n\n";
    trunc_sec << "Cuerpos incompletos (" << trunc_index.size() << "). No inventes código. "
                 "Pide el hueco con `get_code_of path:A-B` o `path:Symbol#mid|#tail`.\n\n";
    for (const auto& line : trunc_index) {
      trunc_sec << line << "\n";
    }
    trunc_sec << "\n";
  }

  std::string pack_body = pack.str();
  // Never head+tail truncate the whole pack (that deletes middle fragments). Prefer
  // dropping Outlines then Truncated; keep Headers (decl / #include) with Fragments.
  auto strip_from = [&](const char* marker) {
    const auto p = pack_body.rfind(marker);
    if (p != std::string::npos) {
      pack_body.erase(p);
    }
  };
  if (pack_body.size() + trunc_sec.str().size() > budget) {
    strip_from("## Outlines\n");
  }
  if (pack_body.size() + trunc_sec.str().size() > budget) {
    strip_from("## Headers\n");
  }
  const std::string trunc = trunc_sec.str();
  if (pack_body.size() + trunc.size() <= budget) {
    pack_body += trunc;
  } else if (pack_body.size() < budget) {
    pack_body.append(trunc, 0, budget - pack_body.size());
  }
  if (pack_body.size() > budget) {
    pack_body.resize(budget);
  }
  std::string err;
  if (!write_file(pack_path(workspace_root), pack_body, &err)) {
    out.error = err.empty() ? "no se pudo escribir pack.md" : err;
    return out;
  }

  ++st.turn;
  st.last_action = "plan";
  st.has_pack = true;
  const auto gaps = pack_instruction_gaps(pack_body, needles);
  st.pack_incomplete = (frag_ok == 0) || !gaps.empty();
  st.explore_tool_count = 0;
  st.plan_nudge_sent = false;
  st.post_pack_tool_count = 0;
  st.edit_nudge_sent = false;
  st.map_review = false;
  // Watchlist keeps resolved targets (not skipped bare junk).
  st.watchlist.clear();
  for (const auto& t : uniq_targets) {
    if (!skip_fetch.count(t)) {
      st.watchlist.push_back(t);
    }
  }
  out.turn = st.turn;

  std::ostringstream block;
  block << "### turn " << st.turn << " — plan\n\n";
  if (!summary.empty()) {
    block << summary << "\n\n";
  }
  // Use target_count: (not targets:) so session_pack_needles never treats this
  // telemetry line as Instruction idents.
  block << "target_count: " << uniq_targets.size() << "  fragments_ok: " << frag_ok << "/"
        << frags.size() << "  outlines_ok: " << outline_ok << "/" << uniq_paths.size()
        << "  truncated: " << trunc_index.size() << "  pack_chars: " << pack_body.size() << "/"
        << budget << "  auto_refetch: " << extras.size() << "\n";
  block << "Archivo: `.tuide/ai/l2/pack.md`. El siguiente prompt usa Instruction+pack "
           "(mapa fuera). ";
  if (st.pack_incomplete) {
    block << "**pack_incomplete=1:** gaps Instruction↔pack"
          << (frag_ok == 0 ? " (cero fragmentos OK)" : "")
          << " — amplía `plan` antes de `done next=edit` (pushback activo). ";
    if (!gaps.empty()) {
      block << "\nGaps:\n";
      for (const auto& g : gaps) {
        block << "- " << g << "\n";
      }
    }
  } else if (!trunc_index.empty()) {
    block << "Hay truncados en `## Truncated` (refetch tip); no bloquean edit si Instruction "
             "está cubierta. ";
  }
  if (st.pack_incomplete) {
    st.consecutive_complete_plans = 0;
  } else {
    ++st.consecutive_complete_plans;
  }
  block << "Emite `done next=edit`, `edit`, o amplía con otro `plan`/`tools`.\n\n";
  if (!st.pack_incomplete &&
      st.consecutive_complete_plans >= Level2Session::kRepeatedPlanEditNudgeAfter &&
      !st.edit_nudge_sent) {
    st.edit_nudge_sent = true;
    block << "_nudge:_ Llevas " << st.consecutive_complete_plans
          << " `plan` seguidos con pack cubierto. Emite `done next=edit` o `edit` "
             "(no más `plan` sin tools/`get_code_of` nuevos).\n\n";
  }
  if (!append_observation(workspace_root, block.str(), &out.session_chars, &err)) {
    out.error = err;
    return out;
  }

  compact_session_context(workspace_root, nullptr);
  compact_observations_after_pack(workspace_root, st, &out.session_chars);
  out.session_chars = read_file(session_path(workspace_root)).size();

  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, frag_ok > 0, "plan", "", "",
                      "pack " + std::to_string(pack_body.size()) + " chars",
                      frag_ok > 0 ? "" : "ningún fragmento OK", st.turn, st.phase);

  const auto plan_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - plan_t0)
                           .count();
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"plan\",\"targets\":" +
                                   std::to_string(uniq_targets.size()) + ",\"frag_ok\":" +
                                   std::to_string(frag_ok) + ",\"outlines\":" +
                                   std::to_string(outline_ok) + ",\"pack_chars\":" +
                                   std::to_string(pack_body.size()) +
                                   ",\"pack_incomplete\":" + (st.pack_incomplete ? "1" : "0") +
                                   ",\"ms\":" + std::to_string(plan_ms) + "}");
  ai_trace(AiTraceChannel::L2, "l2_plan",
           "{\"turn\":" + std::to_string(st.turn) + ",\"targets\":" +
               std::to_string(uniq_targets.size()) + ",\"pack_chars\":" +
               std::to_string(pack_body.size()) + ",\"pack_incomplete\":" +
               (st.pack_incomplete ? "1" : "0") +
               ",\"duration_ms\":" + std::to_string(plan_ms) + "}");

  out.ok = true;
  out.phase = st.phase;
  out.summary = "plan pack=" + std::to_string(pack_body.size()) + " targets=" +
                std::to_string(uniq_targets.size()) +
                (st.pack_incomplete ? " incomplete=1" : "");
  return out;
}

Level2TurnResult Level2Session::apply_edit(const std::string& workspace_root,
                                           const std::vector<SearchReplaceHunk>& hunks) {
  Level2TurnResult out;
  out.action = "edit";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  const auto edit_t0 = std::chrono::steady_clock::now();
  auto edit_ms = [&]() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 edit_t0)
        .count();
  };
  if (st.phase != "edit") {
    out.error = "action=edit solo en phase=edit (ahora=" + st.phase + ")";
    write_response_json(workspace_root, false, "error", "edit", "", "", out.error, st.turn, st.phase);
    return out;
  }
  st.map_review = false;

  auto force_clarify = [&](const std::string& reason) {
    // Drop any on-disk edits from a prior apply that never finished cleanly (e.g. compile
    // fail keeps pending until max attempts — clarify must not leave the tree dirty).
    for (const auto& p : st.pending) {
      if (!p.before.empty() && !p.abs_path.empty()) {
        write_text_file(p.abs_path, p.before, nullptr);
      }
    }
    st.pending.clear();
    ++st.turn;
    st.done = true;
    st.phase = "clarify";
    st.last_action = "need_clarification";
    out.turn = st.turn;
    out.ok = true;
    out.phase = "clarify";
    out.summary = reason;
    out.error.clear();
    std::ostringstream block;
    block << "### turn " << st.turn << " — need_clarification\n\n";
    block << reason << "\n\n";
    block << "Sesión cerrada en **clarify** (edits pendientes revertidos). Reformula el "
             "cambio (path + símbolo) y relanza L2.\n\n";
    std::string obs_err;
    append_observation(workspace_root, block.str(), &out.session_chars, &obs_err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "done", "clarify", "", reason, "", st.turn,
                        "clarify");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"need_clarification\",\"turn\":" +
                                     std::to_string(st.turn) + ",\"reason\":\"edit_loop\"}");
    ai_trace(AiTraceChannel::L2, "l2_edit_clarify",
             "{\"turn\":" + std::to_string(st.turn) + ",\"edit_fail_count\":" +
                 std::to_string(st.edit_fail_count) + ",\"identical_repeats\":" +
                 std::to_string(st.identical_edit_repeats) + "}");
  };

  auto record_edit_failure = [&](const std::string& err_msg, const std::string& path,
                                 const std::string& search, const std::string& replace,
                                 const std::string& fp, bool identical_repeat) {
    const auto ms = edit_ms();
    ++st.turn;
    ++st.edit_fail_count;
    if (identical_repeat) {
      ++st.identical_edit_repeats;
    } else {
      st.identical_edit_repeats = 0;
      st.last_failed_edit_fp = fp;
    }
    st.last_action = identical_repeat ? "edit_repeat_pushback" : "edit_feedback";
    out.turn = st.turn;
    out.error = err_msg;
    out.summary = err_msg;
    out.ok = false;
    out.phase = "edit";

    std::ostringstream block;
    block << "### turn " << st.turn << " — "
          << (identical_repeat ? "edit_repeat_pushback" : "edit_feedback") << "\n\n";
    block << "error: " << err_msg << "\n\n";
    block << "edit_fail_count=" << st.edit_fail_count << "/" << kMaxEditApplyFails;
    if (identical_repeat) {
      block << "  identical_repeats=" << st.identical_edit_repeats << "/"
            << kMaxIdenticalEditRepeats;
    }
    block << "\n\n";
    if (!path.empty()) {
      block << "path: `" << path << "`\n\n";
    }
    if (!search.empty()) {
      block << "## search (failed)\n\n```\n"
            << truncate_observation(search, 24) << "```\n\n";
    }
    if (!replace.empty()) {
      block << "## replace (intended)\n\n```\n"
            << truncate_observation(replace, 24) << "```\n\n";
    }
    if (identical_repeat) {
      block << "**Hunk idéntico rechazado.** No reemitas el mismo `search`/`replace`. "
               "Obligatorio: `get_code_of` del locus (p.ej. `path:Symbol` o `path:A-B`) y un "
               "`search` con bloque de código **único** (no un ident suelto).\n\n";
    } else {
      block << "Reemite `action=edit` con `search` exacto y único (bloque de código, no un "
               "ident suelto; preferir multilínea). Si cambias un `struct`/`class`, el `search` "
               "debe cubrir **todo** el span hasta `};`, no solo `struct X {`. Usa "
               "`get_code_of`. **No repitas el mismo hunk fallido.** Si introduces un símbolo "
               "nuevo, incluye también el hunk de declaración en el `.hpp`/`.h` hermano.\n\n";
      block << "**Siguiente acción: `action=edit`** (evita bucle de tools).\n\n";
      const std::string hint = pack_span_hint(workspace_root, path);
      if (!hint.empty()) {
        block << "## span sugerido desde pack (úsalo como base del search)\n\n```\n"
              << hint << "```\n\n";
      }
    }
    std::string obs_err;
    append_observation(workspace_root, block.str(), &out.session_chars, &obs_err);
    if (st.has_pack) {
      compact_observations_after_pack(workspace_root, st, &out.session_chars);
    }
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "edit", "", path, block.str(), err_msg, st.turn,
                        "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"" +
                                     (identical_repeat ? "edit_repeat" : "edit_fail") +
                                     "\",\"turn\":" + std::to_string(st.turn) +
                                     ",\"ms\":" + std::to_string(ms) + ",\"fail_count\":" +
                                     std::to_string(st.edit_fail_count) + ",\"error\":\"" +
                                     json_escape(err_msg) + "\"}");
    ai_trace(AiTraceChannel::L2, "l2_edit",
             "{\"turn\":" + std::to_string(st.turn) + ",\"ok\":0,\"hunks\":" +
                 std::to_string(hunks.size()) + ",\"duration_ms\":" + std::to_string(ms) +
                 ",\"identical\":" + (identical_repeat ? "1" : "0") + ",\"fail_count\":" +
                 std::to_string(st.edit_fail_count) + ",\"error\":\"" +
                 ai_trace_escape(err_msg) + "\"}");

    if (st.identical_edit_repeats >= kMaxIdenticalEditRepeats ||
        st.edit_fail_count >= kMaxEditApplyFails) {
      force_clarify(
          identical_repeat
              ? "edit loop: mismo hunk fallido repetido; ¿puedes concretar path:símbolo y el "
                "bloque exacto a cambiar?"
              : "edit loop: demasiados fallos de Search/Replace; ¿puedes concretar path:símbolo "
                "y el bloque exacto a cambiar?");
    }
  };

  if (hunks.empty()) {
    record_edit_failure("hunks vacío", "", "", "", "empty", false);
    return out;
  }

  std::vector<SearchReplaceHunk> work = hunks;
  for (auto& h : work) {
    normalize_hunk_escape_noise(&h);
  }

  const std::string fp = edit_hunks_fingerprint(work);
  if (!st.last_failed_edit_fp.empty() && fp == st.last_failed_edit_fp) {
    const auto& h0 = work.front();
    record_edit_failure("hunk idéntico al último fallo (rechazado)", h0.path, h0.search,
                        h0.replace, fp, true);
    return out;
  }

  for (const auto& h : work) {
    if (const std::string shape = hunk_shape_error(h); !shape.empty()) {
      record_edit_failure(shape, h.path, h.search, h.replace, fp, false);
      return out;
    }
  }

  const auto path_cands = edit_path_candidates(workspace_root, st.has_pack, st.watchlist);
  std::vector<std::string> path_corrections;

  std::vector<ApplyHunkResult> applied;
  applied.reserve(work.size());
  for (auto& h : work) {
    ApplyHunkResult r = apply_hunk_to_workspace_file(workspace_root, h, /*write=*/true);
    if (!r.ok && r.error.find("0 matches") != std::string::npos) {
      const std::string alt = suggest_path_for_search(workspace_root, h, path_cands);
      if (!alt.empty()) {
        SearchReplaceHunk corrected = h;
        corrected.path = alt;
        ApplyHunkResult r2 =
            apply_hunk_to_workspace_file(workspace_root, corrected, /*write=*/true);
        if (r2.ok) {
          path_corrections.push_back("`" + h.path + "` → `" + alt + "`");
          h.path = alt;
          r = std::move(r2);
        } else {
          r.error = r.error + " — el search sí está en `" + alt +
                    "` (pack/watchlist); reemite con ese path";
        }
      }
    }
    if (!r.ok) {
      // Rollback any already written hunks in this batch.
      for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
        write_text_file(it->abs_path, it->before, nullptr);
      }
      std::string err_msg = "hunk falló (" + h.path + "): " + r.error;
      if (r.error.find("0 matches") != std::string::npos &&
          r.error.find("pack/watchlist") == std::string::npos && !path_cands.empty()) {
        err_msg +=
            " — revisa path (candidates pack/watchlist: " +
            [&]() {
              std::ostringstream os;
              for (std::size_t i = 0; i < path_cands.size() && i < 6; ++i) {
                if (i) {
                  os << ", ";
                }
                os << "`" << path_cands[i] << "`";
              }
              return os.str();
            }() +
            "); usa newlines reales (no `\\s*`/`\\n` literales)";
      }
      record_edit_failure(err_msg, h.path, h.search, h.replace, fp, false);
      return out;
    }
    if (deps_.sync_edit) {
      deps_.sync_edit(r);
    }
    applied.push_back(std::move(r));
  }

  ++st.turn;
  ++st.edit_attempt;
  st.edit_fail_count = 0;
  st.identical_edit_repeats = 0;
  st.last_failed_edit_fp.clear();
  st.edit_phase_tool_count = 0;
  st.edit_phase_nudge_sent = false;
  st.last_action = "edit";
  st.last_op_id = static_cast<uint64_t>(st.turn);
  st.pending.clear();
  for (std::size_t i = 0; i < applied.size(); ++i) {
    PendingHunk p;
    p.path = work[i].path;
    p.abs_path = applied[i].abs_path;
    p.old_text = applied[i].old_text;
    p.new_text = applied[i].new_text;
    p.before = applied[i].before;
    st.pending.push_back(std::move(p));
  }
  out.turn = st.turn;

  std::ostringstream block;
  block << "### turn " << st.turn << " — edit\n\n";
  block << "hunks=" << applied.size() << " edit_attempt=" << st.edit_attempt << "\n\n";
  if (!path_corrections.empty()) {
    block << "path auto-corregido (match único en pack/watchlist): ";
    for (std::size_t i = 0; i < path_corrections.size(); ++i) {
      if (i) {
        block << "; ";
      }
      block << path_corrections[i];
    }
    block << "\n\n";
  }
  for (std::size_t i = 0; i < applied.size(); ++i) {
    block << "#### hunk " << (i + 1) << " `" << work[i].path << "` @"
          << applied[i].span.start_line << "\n\n";
    block << "```diff\n- " << truncate_observation(applied[i].old_text, 40);
    block << "+ " << truncate_observation(applied[i].new_text, 40) << "```\n\n";
  }
  std::string err;
  if (!append_observation(workspace_root, block.str(), &out.session_chars, &err)) {
    out.error = err;
    return out;
  }
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, true, "edit", "", "", block.str(), "", st.turn, "edit");
  const auto apply_ms = edit_ms();
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"edit\",\"turn\":" + std::to_string(st.turn) +
                                   ",\"hunks\":" + std::to_string(applied.size()) +
                                   ",\"ms\":" + std::to_string(apply_ms) + "}");
  ai_trace(AiTraceChannel::L2, "l2_edit",
           "{\"turn\":" + std::to_string(st.turn) + ",\"ok\":1,\"hunks\":" +
               std::to_string(applied.size()) + ",\"duration_ms\":" + std::to_string(apply_ms) +
               "}");
  out.ok = true;
  out.summary = "edit applied; compiling…";
  return after_successful_edit(workspace_root, st);
}

Level2TurnResult Level2Session::after_successful_edit(const std::string& workspace_root,
                                                      State st) {
  st.phase = "compile";
  save_state(workspace_root, st, nullptr);
  return run_compile(workspace_root);
}

Level2TurnResult Level2Session::run_compile(const std::string& workspace_root) {
  Level2TurnResult out;
  out.action = "compile";
  State st = load_state(workspace_root);
  out.phase = st.phase;

  ++st.compile_attempt;
  ++st.turn;
  st.last_action = "compile";
  out.turn = st.turn;

  const auto compile_t0 = std::chrono::steady_clock::now();
  int exit_code = -1;
  std::string output;
  if (deps_.run_compile) {
    exit_code = deps_.run_compile(&output);
  } else {
    const std::string log = (fs::path(workspace_root) / ".tuide" / "ai" / "l2" / "compile.log")
                                .lexically_normal()
                                .string();
    const std::string cmd = "cd " + workspace_root + " && ./tools/compile.sh -y >" + log +
                            " 2>&1; echo $?";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      char buf[64] = {};
      if (fgets(buf, sizeof(buf), pipe)) {
        exit_code = std::atoi(buf);
      }
      pclose(pipe);
    }
    output = read_file(log);
  }
  const auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - compile_t0)
                              .count();
  ai_trace(AiTraceChannel::L2, "l2_compile",
           "{\"turn\":" + std::to_string(st.turn) + ",\"attempt\":" +
               std::to_string(st.compile_attempt) + ",\"exit\":" + std::to_string(exit_code) +
               ",\"ok\":" + (exit_code == 0 ? "1" : "0") +
               ",\"duration_ms\":" + std::to_string(compile_ms) + "}");

  const std::string trunc = truncate_observation_tail(output, kMaxCompileLogLines);
  std::ostringstream block;
  if (exit_code == 0) {
    block << "### turn " << st.turn << " — compile_ok\n\n";
    block << "attempt: " << st.compile_attempt << "/" << kMaxCompileAttempts
          << "  exit_code: " << exit_code << "\n\n";
    const std::string ok_log = truncate_observation_tail(output, 12);
    block << "```\n" << ok_log;
    if (!ok_log.empty() && ok_log.back() != '\n') {
      block << '\n';
    }
    block << "```\n\n";
    block << "Compile OK **no** cierra la tarea.\n"
             "¿Algo más?\n"
             "- Más sitios → `action=plan` con nuevos targets (arma otro pack).\n"
             "- Más cambios → `get_code_of` del locus **antes** de `edit` (el disco ya "
             "cambió; no reuses el `search` del pack viejo).\n"
             "- Instruction cubierta → "
             "`{\"action\":\"done\",\"summary\":\"…qué cambiaste…\"}` (sin next).\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);

    // Restore full initial map for "¿algo más?" — skip if it was a stale (wrong-query) map.
    if (!st.map_stale) {
      const std::string initial_map = read_file(map_initial_path(workspace_root));
      if (!initial_map.empty()) {
        std::string map_err;
        if (replace_ranked_map_in_session(session_path(workspace_root), initial_map, &map_err)) {
          out.session_chars = read_file(session_path(workspace_root)).size();
        }
      }
    }

    // Build gate only: return to edit so the model can continue or explicitly done.
    st.phase = "edit";
    st.done = false;
    st.last_action = "compile_ok";
    st.map_review = true;
    st.pending.clear();
    st.compile_attempt = 0;  // fresh fail budget for the next edit batch
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "compile", "", "",
                        "compile ok; map restored — ¿algo más?", "", st.turn, "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"compile_ok\",\"exit\":0,\"ms\":" +
                                     std::to_string(compile_ms) +
                                     ",\"resume\":\"edit\",\"map_review\":1}");
    out.ok = true;
    out.action = "compile";
    out.summary = "OK compile; map_review (¿algo más?)";
    out.phase = "edit";
    out.turn = st.turn;
    return out;
  }

  // Fail: single observation (no duplicate stderr) — tail only + old/new hunks.
  block << "### turn " << st.turn << " — compile_feedback\n\n";
  block << "attempt: " << st.compile_attempt << "/" << kMaxCompileAttempts << "\n";
  block << "exit_code: " << exit_code << "\n\n";
  block << "```stderr (tail)\n" << trunc;
  if (!trunc.empty() && trunc.back() != '\n') {
    block << '\n';
  }
  block << "```\n\n";
  const auto undecl = compile_undeclared_idents(trunc);
  if (!undecl.empty()) {
    block << "Símbolos no declarados:";
    for (const auto& id : undecl) {
      block << " `" << id << "`";
    }
    block << "\nDeclara en el header hermano (mismo stem `.hpp`/`.h`) y/o añade `#include`; "
             "un hunk solo en el `.cpp` no basta.\n\n";
  }
  for (std::size_t i = 0; i < st.pending.size(); ++i) {
    const auto& p = st.pending[i];
    block << "## old (hunk " << (i + 1) << " `" << p.path << "`)\n\n```\n"
          << truncate_observation(p.old_text, 24) << "```\n\n";
    block << "## new (hunk " << (i + 1) << " `" << p.path << "`)\n\n```\n"
          << truncate_observation(p.new_text, 24) << "```\n\n";
  }
  block << "Reemite `action=edit` corrigiendo el error. Si el fallo es redeclaración/"
           "duplicados: el `search` debe cubrir el span completo a sustituir (p.ej. struct "
           "entero), no solo la línea de apertura.\n\n";

  // Restore disk to pre-batch immediately. Leaving the broken tree on disk made the
  // next edit's pending.before = already-corrupt content; max-attempt "rollback"
  // then could not recover the last compile_ok (or original) baseline.
  for (const auto& p : st.pending) {
    if (!p.before.empty() && !p.abs_path.empty()) {
      write_text_file(p.abs_path, p.before, nullptr);
    }
  }
  block << "Archivos restaurados al baseline pre-hunk de este intento"
           " — reemite `edit` sobre ese estado (no sobre el árbol roto).\n\n";

  std::string err;
  append_observation(workspace_root, block.str(), &out.session_chars, &err);
  if (st.has_pack) {
    compact_observations_after_pack(workspace_root, st, &out.session_chars);
  }

  if (st.compile_attempt >= kMaxCompileAttempts) {
    st.pending.clear();
    st.last_action = "compile_fail_rollback";
    // Bypass clarify_pushback: el runtime ya agotó reintentos de compile.
    if (deps_.clarify_pushback_max > 0) {
      st.clarify_pushback = deps_.clarify_pushback_max;
    }
    save_state(workspace_root, st, nullptr);
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"compile_fail\",\"exit\":" +
                                     std::to_string(exit_code) + ",\"attempt\":" +
                                     std::to_string(st.compile_attempt) + ",\"ms\":" +
                                     std::to_string(compile_ms) + ",\"rollback\":1,\"clarify\":1}");
    std::ostringstream summary;
    summary << "FAIL compile x" << st.compile_attempt
            << "; rollback al baseline pre-hunk. No pude dejar el árbol compilando — "
               "¿reformulas o acotas el cambio?";
    // Failure, not success: close as clarify so harness/UI don't treat this as done OK.
    Level2TurnResult fin = mark_done(workspace_root, summary.str(), "clarify");
    fin.ok = false;
    fin.error = summary.str();
    if (fin.summary.empty()) {
      fin.summary = summary.str();
    }
    return fin;
  }

  st.pending.clear();
  st.phase = "edit";
  st.last_action = "compile_feedback";
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, false, "compile", "", "", trunc,
                      "compile failed; re-edit", st.turn, "edit");
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"compile_fail\",\"exit\":" +
                                   std::to_string(exit_code) + ",\"attempt\":" +
                                   std::to_string(st.compile_attempt) + ",\"ms\":" +
                                   std::to_string(compile_ms) + ",\"rollback\":1}");
  out.ok = false;
  out.summary = "compile failed; phase=edit again";
  out.phase = "edit";
  out.error = "compile exit_code=" + std::to_string(exit_code);
  return out;
}

Level2TurnResult Level2Session::rollback_pending(const std::string& workspace_root) {
  Level2TurnResult out;
  out.action = "rollback";
  State st = load_state(workspace_root);
  for (const auto& p : st.pending) {
    if (!p.before.empty() && !p.abs_path.empty()) {
      write_text_file(p.abs_path, p.before, nullptr);
    }
  }
  st.pending.clear();
  st.last_action = "rollback";
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = "rollback ok";
  out.phase = st.phase;
  return out;
}

Level2TurnResult Level2Session::mark_done(const std::string& workspace_root,
                                          const std::string& summary, const std::string& next_in) {
  Level2TurnResult out;
  out.action = "done";
  out.summary = summary;
  State st = load_state(workspace_root);
  out.phase = st.phase;

  std::string next = next_in;
  // After compile_ok (phase=edit / map_review) the model often emits done next=edit
  // meaning "finished" — treat as final done instead of error-looping until max_steps.
  if (next == "edit" && st.phase == "edit") {
    next.clear();
  }

  if (next == "edit") {
    if (st.phase != "explore") {
      out.error = "next=edit solo desde explore";
      return out;
    }
    const int max_pack_push = deps_.pack_incomplete_pushback_max;
    const bool need_pack_push =
        (!st.has_pack || st.pack_incomplete) && max_pack_push > 0 &&
        st.pack_incomplete_pushback < max_pack_push;
    if (need_pack_push) {
      ++st.pack_incomplete_pushback;
      ++st.turn;
      st.last_action = "pack_incomplete_pushback";
      out.turn = st.turn;
      std::ostringstream block;
      block << "### turn " << st.turn << " — pack_incomplete_pushback ("
            << st.pack_incomplete_pushback << "/" << max_pack_push << ")\n\n";
      if (!st.has_pack) {
        block << "Sin pack. Motivo del modelo: " << summary << "\n\n";
        block << "Antes de `done next=edit`, emite `action=plan` con 4–8 "
                 "`path:Symbol`/`path:line` anclados a la Instruction.\n\n";
      } else {
        block << "Pack incompleto (gaps Instruction↔pack). Motivo del modelo: " << summary
              << "\n\n";
        block << "Antes de `done next=edit`, amplía con `action=plan` (anclado). Truncados "
                 "en `## Truncated` se refetchan con tip; no bastan solos para este pushback.\n";
        const std::string pack = read_file(pack_path(workspace_root));
        const std::string sess = read_file(session_path(workspace_root));
        const auto needles = session_pack_needles(sess);
        const auto hints = pack_instruction_gaps(pack, needles);
        if (!hints.empty()) {
          block << "\nSugerencias de plan (gaps Instruction vs pack):\n";
          for (const auto& h : hints) {
            block << "- " << h << "\n";
          }
          block << "\n";
        } else {
          block << "\n";
        }
      }
      std::string err;
      append_observation(workspace_root, block.str(), &out.session_chars, &err);
      save_state(workspace_root, st, nullptr);
      write_response_json(workspace_root, false, "done", "pack_incomplete_pushback", "",
                          block.str(),
                          st.has_pack ? "pack incomplete; cover Instruction gaps"
                                      : "sin pack; emite plan",
                          st.turn, st.phase);
      append_trace(workspace_root,
                   std::string("{\"ts\":") + now_ms_str() +
                       ",\"event\":\"pack_incomplete_pushback\",\"n\":" +
                       std::to_string(st.pack_incomplete_pushback) + ",\"max\":" +
                       std::to_string(max_pack_push) +
                       ",\"has_pack\":" + (st.has_pack ? "1" : "0") + "}");
      out.ok = true;
      out.phase = st.phase;
      out.summary = "pack_incomplete_pushback " + std::to_string(st.pack_incomplete_pushback) +
                    "/" + std::to_string(max_pack_push);
      return out;
    }
    // Drop full map bodies before edit phase prompts (keep detail only for hot stems).
    compact_session_context(workspace_root, nullptr);
    ++st.turn;
    st.phase = "edit";
    st.last_action = "ready_to_edit";
    st.pack_incomplete = false;
    st.edit_fail_count = 0;
    st.identical_edit_repeats = 0;
    st.last_failed_edit_fp.clear();
    st.edit_phase_tool_count = 0;
    st.edit_phase_nudge_sent = false;
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — ready_to_edit\n\n";
    block << summary << "\n\n";
    block << "Fase **edit**: emite `action=edit` con hunks Search/Replace. "
             "Máx. ~" << kEditPhaseToolNudgeAfter
          << " tools de refetch; luego edit obligatorio. "
             "Si el pack marcó [TRUNCATED] en la zona a editar, refetch antes.\n\n";
    const std::string hint = pack_span_hint(workspace_root, "");
    if (!hint.empty()) {
      block << "## span sugerido (pack)\n\n```\n" << hint << "```\n\n";
    }
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "done", "", "", summary, "", st.turn, "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"phase_edit\",\"turn\":" +
                                     std::to_string(st.turn) + "}");
    out.ok = true;
    out.phase = "edit";
    out.summary = "phase=edit";
    return out;
  }

  if (next == "clarify" || next == "abort" || next == "need_info") {
    if (st.phase != "explore" && st.phase != "edit") {
      out.error = "next=clarify solo desde explore|edit";
      return out;
    }
    const int max_push = deps_.clarify_pushback_max;
    if (max_push > 0 && st.clarify_pushback < max_push) {
      ++st.clarify_pushback;
      ++st.turn;
      st.last_action = "clarify_pushback";
      out.turn = st.turn;
      std::ostringstream block;
      block << "### turn " << st.turn << " — clarify_pushback (" << st.clarify_pushback << "/"
            << max_push << ")\n\n";
      block << "Clarify prematuro rechazado. Motivo del modelo: " << summary << "\n\n";
      block << "No cierres aún. Emite `action=plan` (más targets) o `action=tools`/`tool` para "
               "pedir **más código** (otros stems del ## Ranked map: `get_code_of path:Symbol`, "
               "`file_outline`, `search`). Solo tras explorar más puedes usar "
               "`done next=clarify` de nuevo"
            << (st.clarify_pushback >= max_push
                    ? ""
                    : (" (quedan " + std::to_string(max_push - st.clarify_pushback) +
                       " pushbacks)."))
            << "\n\n";
      std::string err;
      append_observation(workspace_root, block.str(), &out.session_chars, &err);
      save_state(workspace_root, st, nullptr);
      write_response_json(workspace_root, false, "done", "clarify_pushback", "", block.str(),
                          "clarify prematuro; pide más código", st.turn, st.phase);
      append_trace(workspace_root,
                   std::string("{\"ts\":") + now_ms_str() +
                       ",\"event\":\"clarify_pushback\",\"n\":" +
                       std::to_string(st.clarify_pushback) + ",\"max\":" +
                       std::to_string(max_push) + "}");
      ai_trace(AiTraceChannel::L2, "l2_clarify_pushback",
               "{\"n\":" + std::to_string(st.clarify_pushback) +
                   ",\"max\":" + std::to_string(max_push) + "}");
      out.ok = true;
      out.phase = st.phase;
      out.summary = "clarify_pushback " + std::to_string(st.clarify_pushback) + "/" +
                    std::to_string(max_push);
      return out;
    }
    ++st.turn;
    st.done = true;
    st.phase = "clarify";
    st.last_action = "need_clarification";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — clarify (arreglo cancelado)\n\n";
    block << summary << "\n\n";
    block << "_No se pasa a edit/compile. El usuario debe dar más detalle y relanzar._\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "done", "clarify", "", summary, "", st.turn,
                        "clarify");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"need_clarification\",\"turn\":" +
                                     std::to_string(st.turn) + "}");
    out.ok = true;
    out.phase = "clarify";
    out.summary = summary.empty() ? "need clarification" : summary;
    return out;
  }

  ++st.turn;
  st.done = true;
  st.phase = "done";
  st.last_action = "done";
  out.turn = st.turn;
  std::ostringstream block;
  block << "### turn " << st.turn << " — done\n\n" << summary << "\n\n";
  std::string err;
  append_observation(workspace_root, block.str(), &out.session_chars, &err);
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, true, "done", "", "", summary, "", st.turn, "done");
  out.ok = true;
  out.phase = "done";
  return out;
}

Level2TurnResult Level2Session::process_request_file(const std::string& workspace_root) {
  Level2TurnResult out;
  const std::string raw = read_file(request_path(workspace_root));
  if (raw.empty()) {
    out.error = "request.json vacío o ausente";
    write_response_json(workspace_root, false, "error", "", "", "", out.error, 0);
    return out;
  }
  try {
    const auto j = nlohmann::json::parse(raw);
    const std::string action = j.value("action", "");
    const State st = load_state(workspace_root);
    out.phase = st.phase;

    if (action == "tool") {
      if (j.contains("calls") && j["calls"].is_array()) {
        std::vector<L2ToolCall> calls;
        for (const auto& c : j["calls"]) {
          if (!c.is_object()) {
            continue;
          }
          L2ToolCall call;
          call.name = c.value("name", "");
          call.arg = c.value("arg", "");
          if (!call.name.empty()) {
            calls.push_back(std::move(call));
          }
          if (static_cast<int>(calls.size()) >= kL2MaxToolBatch) {
            break;
          }
        }
        return apply_tools(workspace_root, calls);
      }
      return apply_tool(workspace_root, j.value("name", ""), j.value("arg", ""));
    }
    if (action == "tools") {
      std::vector<L2ToolCall> calls;
      if (j.contains("calls") && j["calls"].is_array()) {
        for (const auto& c : j["calls"]) {
          if (!c.is_object()) {
            continue;
          }
          L2ToolCall call;
          call.name = c.value("name", "");
          call.arg = c.value("arg", "");
          if (!call.name.empty()) {
            calls.push_back(std::move(call));
          }
          if (static_cast<int>(calls.size()) >= kL2MaxToolBatch) {
            break;
          }
        }
      }
      return apply_tools(workspace_root, calls);
    }
    if (action == "plan" || action == "watchlist") {
      std::vector<std::string> targets;
      if (j.contains("targets") && j["targets"].is_array()) {
        for (const auto& t : j["targets"]) {
          if (t.is_string()) {
            targets.push_back(t.get<std::string>());
          } else if (t.is_object()) {
            const std::string path = t.value("path", "");
            const std::string sym = t.value("symbol", t.value("name", ""));
            const int line = t.value("line", 0);
            if (!path.empty() && !sym.empty()) {
              targets.push_back(path + ":" + sym);
            } else if (!path.empty() && line > 0) {
              targets.push_back(path + ":" + std::to_string(line));
            } else if (!path.empty()) {
              targets.push_back(path);
            }
          }
          if (static_cast<int>(targets.size()) >= kL2MaxPlanTargets) {
            break;
          }
        }
      }
      return apply_plan(workspace_root, targets, j.value("summary", ""));
    }
    if (action == "done") {
      return mark_done(workspace_root, j.value("summary", ""), j.value("next", ""));
    }
    if (action == "edit") {
      std::string err;
      auto hunks = parse_search_replace_json(j, &err);
      if (hunks.empty()) {
        out.error = err.empty() ? "edit sin hunks" : err;
        write_response_json(workspace_root, false, "error", "edit", "", "", out.error, st.turn,
                            st.phase);
        return out;
      }
      return apply_edit(workspace_root, hunks);
    }
    if (action == "compile") {
      // Runtime-owned; allow manual trigger only if phase=compile or edit (force).
      return run_compile(workspace_root);
    }
    out.error = "action desconocida: " + action;
    write_response_json(workspace_root, false, "error", "", "", "", out.error, 0, st.phase);
    return out;
  } catch (const std::exception& ex) {
    out.error = std::string("JSON inválido: ") + ex.what();
    write_response_json(workspace_root, false, "error", "", "", "", out.error, 0);
    return out;
  }
}

std::string Level2Session::status_text(const std::string& workspace_root) const {
  std::ostringstream out;
  out << "=== L2 harness session ===\n";
  out << "dir: " << dir_for(workspace_root) << '\n';
  const State st = load_state(workspace_root);
  out << "phase: " << st.phase << "  turn: " << st.turn << "  done: " << (st.done ? "yes" : "no")
      << "\n";
  out << "edit_attempt=" << st.edit_attempt << " compile_attempt=" << st.compile_attempt
      << " pending_hunks=" << st.pending.size() << "\n";
  out << "has_pack: " << (st.has_pack ? "yes" : "no")
      << "  pack_incomplete: " << (st.pack_incomplete ? "yes" : "no")
      << "  map_stale: " << (st.map_stale ? "yes" : "no")
      << "  map_review: " << (st.map_review ? "yes" : "no") << "\n";
  out << "last: " << (st.last_action.empty() ? "-" : st.last_action) << '\n';
  const std::string session = read_file(session_path(workspace_root));
  out << "session.md: "
      << (session.empty() ? "missing" : (std::to_string(session.size()) + " chars")) << '\n';
  return out.str();
}

std::string Level2Session::status_flags(const std::string& workspace_root) const {
  const State st = load_state(workspace_root);
  std::ostringstream out;
  out << "phase=" << st.phase << " turn=" << st.turn
      << " has_pack=" << (st.has_pack ? "yes" : "no")
      << " pack_incomplete=" << (st.pack_incomplete ? "yes" : "no")
      << " map_stale=" << (st.map_stale ? "yes" : "no");
  return out.str();
}

}  // namespace tuide
