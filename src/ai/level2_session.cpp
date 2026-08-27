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
#include "ai/ai_types.hpp"
#include "ai/ai_path_scope.hpp"
#include "ai/l2_explore_a.hpp"
#include "ai/l2_effect_summary.hpp"
#include "ai/l2_feat.hpp"
#include "ai/l2_problem_frame.hpp"
#include "ai/l2_pack_review.hpp"

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
      "sibling_of",
  };
  return k;
}

// Phase A/B locate hot path: Tree-sitter / FS / index only (no LSP server).
const std::unordered_set<std::string>& l2_local_locate_tools() {
  static const std::unordered_set<std::string> k = {
      "get_code_of", "search",     "repo_map",     "read_file", "list_files",
      "file_outline", "headers_of", "sibling_of",   "list_tools",
  };
  return k;
}

bool is_lsp_locate_tool(const std::string& name) {
  static const std::unordered_set<std::string> k = {
      "workspace_symbols", "hover", "diagnostics", "definition", "references", "context_pack",
  };
  return k.count(name) > 0;
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

std::string tool_call_key(const std::string& name, const std::string& arg) {
  return name + "\t" + trim_ws(arg);
}

bool seen_tool_key(const std::vector<std::string>& keys, const std::string& key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

void remember_tool_key(std::vector<std::string>* keys, const std::string& key) {
  if (keys == nullptr || key.empty() || seen_tool_key(*keys, key)) {
    return;
  }
  keys->push_back(key);
  if (keys->size() > 32) {
    keys->erase(keys->begin());
  }
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
  // snake / nested / numbered.
  // A token must have _ or : (snake/nested) or a digit (versioned/numbered) to be
  // treated as a code identifier.  Pure NL words in any language, even long ones
  // (e.g. "proporciones", "razonables", "configuracion"), do NOT qualify, which
  // prevents the pack_instruction_gaps gate from firing on abstract NL queries.
  return has_under || has_digit;
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

std::string first_content_line(const std::string& text) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const std::string t = trim_ws(line);
    if (!t.empty()) {
      return t;
    }
  }
  return {};
}

bool replace_has_search_prefix(const std::string& search, const std::string& replace,
                               std::string* added) {
  if (search.empty() || replace.size() <= search.size()) {
    return false;
  }
  if (replace.compare(0, search.size(), search) == 0) {
    if (added) {
      *added = replace.substr(search.size());
    }
    return true;
  }
  const std::string s = trim_ws(search);
  const std::string r = trim_ws(replace);
  if (s.empty() || r.size() <= s.size()) {
    return false;
  }
  if (r.compare(0, s.size(), s) == 0) {
    if (added) {
      *added = r.substr(s.size());
    }
    return true;
  }
  return false;
}

bool looks_like_type_opener(const std::string& search) {
  const std::string line = first_content_line(search);
  return line.rfind("struct ", 0) == 0 || line.rfind("class ", 0) == 0 ||
         line.rfind("enum ", 0) == 0 || line.rfind("union ", 0) == 0;
}

bool looks_like_namespace_opener(const std::string& search) {
  const std::string line = first_content_line(search);
  return line.rfind("namespace ", 0) == 0 || line == "namespace {";
}

bool looks_like_new_api_block(const std::string& added) {
  const std::string t = trim_ws(added);
  if (t.size() < 8) {
    return false;
  }
  // Function definition or declaration in the inserted tail.
  if (t.find(") {") != std::string::npos || t.find("){") != std::string::npos) {
    return true;
  }
  if (t.find(") \n{") != std::string::npos) {
    return true;
  }
  // `bool foo();` / `void bar();`
  std::istringstream in(added);
  std::string line;
  while (std::getline(in, line)) {
    const std::string s = trim_ws(line);
    if (s.size() < 6) {
      continue;
    }
    if (s.back() == ';' && s.find('(') != std::string::npos && s.find('=') == std::string::npos) {
      return true;
    }
  }
  return false;
}

bool opener_only_search(const std::string& search) {
  const std::string s = trim_ws(search);
  if (s.empty()) {
    return false;
  }
  const int s_lines = count_content_lines(search);
  const bool has_open = s.find('{') != std::string::npos;
  const bool has_close = s.find('}') != std::string::npos;
  return (has_open && !has_close && s_lines <= 2) ||
         (!s.empty() && s.back() == '{' && s_lines <= 2);
}

// Reject "struct Foo {" → full struct body (duplicates the rest of the type).
// Search must cover the span being replaced; tiny openers expanded into blocks are poison.
// Prefix-inserts of *new* functions/decls are allowed (namespace / function openers).
bool hunk_expands_over_opener(const SearchReplaceHunk& h) {
  const std::string search = trim_ws(h.search);
  const std::string replace = trim_ws(h.replace);
  if (search.empty() || replace.size() <= search.size()) {
    return false;
  }
  std::string added;
  const bool prefix = replace_has_search_prefix(h.search, h.replace, &added);
  if (prefix && looks_like_new_api_block(added)) {
    if (looks_like_type_opener(h.search)) {
      return true;
    }
    return false;
  }
  if (!opener_only_search(h.search)) {
    return false;
  }
  const int s_lines = count_content_lines(search);
  const int r_lines = count_content_lines(replace);
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

void split_added_decl_def(const std::string& added, std::string* decls, std::string* defs) {
  if (decls) {
    decls->clear();
  }
  if (defs) {
    defs->clear();
  }
  std::string acc;
  int depth = 0;
  for (std::size_t i = 0; i < added.size(); ++i) {
    acc.push_back(added[i]);
    if (added[i] == '{') {
      ++depth;
    } else if (added[i] == '}') {
      if (depth > 0) {
        --depth;
      }
      if (depth == 0) {
        if (defs) {
          *defs += acc;
        }
        acc.clear();
      }
    } else if (added[i] == ';' && depth == 0) {
      if (decls) {
        *decls += acc;
      }
      acc.clear();
    }
  }
  if (!acc.empty() && defs) {
    // Trailing whitespace / incomplete — keep with defs only if it had a brace.
    if (acc.find('{') != std::string::npos) {
      *defs += acc;
    } else if (decls) {
      *decls += acc;
    }
  }
}

std::string read_file_raw(const std::string& path);
bool path_looks_like_header(const std::string& path);
std::string sibling_header_rel(const std::string& workspace_root, const std::string& rel);
std::string sibling_source_rel(const std::string& workspace_root, const std::string& rel);

bool rewrite_function_opener_insert(const std::string& workspace_root, SearchReplaceHunk* h) {
  if (!h) {
    return false;
  }
  std::string added;
  if (!replace_has_search_prefix(h->search, h->replace, &added) ||
      !looks_like_new_api_block(added) || !opener_only_search(h->search) ||
      looks_like_namespace_opener(h->search) || looks_like_type_opener(h->search)) {
    return false;
  }
  fs::path abs = h->path;
  if (!abs.is_absolute()) {
    abs = fs::path(workspace_root) / h->path;
  }
  abs = abs.lexically_normal();
  const std::string text = read_file_raw(abs.string());
  if (text.empty()) {
    return false;
  }
  SearchReplaceSpan sp;
  std::string err;
  if (!find_unique_span_allow_flex(text, h->search, &sp, &err)) {
    return false;
  }
  if (!extend_span_to_matching_brace(text, &sp, &err)) {
    return false;
  }
  const std::string full = text.substr(sp.byte_begin, sp.byte_end - sp.byte_begin);
  if (full.size() < h->search.size()) {
    return false;
  }
  h->search = full;
  if (!added.empty() && added.front() != '\n' && (full.empty() || full.back() != '\n')) {
    h->replace = full + "\n" + added;
  } else {
    h->replace = full + added;
  }
  return true;
}

std::string sibling_anchor_search(const std::string& file_text) {
  const std::string ns = "namespace tuide {";
  if (file_text.find(ns) != std::string::npos) {
    const auto first = file_text.find(ns);
    const auto second = file_text.find(ns, first + ns.size());
    if (second == std::string::npos) {
      return ns;
    }
  }
  return {};
}

std::string unique_disk_anchor(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  const std::string ns = sibling_anchor_search(text);
  if (!ns.empty()) {
    SearchReplaceSpan sp;
    std::string err;
    if (find_unique_span_allow_flex(text, ns, &sp, &err)) {
      return ns;
    }
  }
  std::string tail = text;
  constexpr std::size_t kMax = 280;
  if (tail.size() > kMax) {
    tail = tail.substr(tail.size() - kMax);
    const auto nl = tail.find('\n');
    if (nl != std::string::npos && nl + 1 < tail.size()) {
      tail = tail.substr(nl + 1);
    }
  }
  SearchReplaceSpan sp;
  std::string err;
  if (!tail.empty() && find_unique_span_allow_flex(text, tail, &sp, &err)) {
    return tail;
  }
  return {};
}

bool extract_gap_insert_body(const SearchReplaceHunk& h, bool header, std::string* added) {
  std::string raw;
  if (!replace_has_search_prefix(h.search, h.replace, &raw)) {
    raw = h.replace;
  }
  if (!looks_like_new_api_block(raw)) {
    return false;
  }
  std::string decls;
  std::string defs;
  split_added_decl_def(raw, &decls, &defs);
  std::string body = header ? decls : defs;
  if (!header && trim_ws(body).empty() && raw.find('{') != std::string::npos) {
    body = raw;
  }
  if (trim_ws(body).empty()) {
    return false;
  }
  if (added) {
    *added = body;
  }
  return true;
}

// SEARCH miss on an Instruction path that still has a gap: re-anchor to unique disk text
// and keep the new decl/def from REPLACE (7B often copies a stale pack span).
bool rewrite_missing_path_search(const std::string& workspace_root,
                                 const std::vector<std::string>& missing_paths,
                                 SearchReplaceHunk* h) {
  if (h == nullptr || h->path.empty()) {
    return false;
  }
  if (std::find(missing_paths.begin(), missing_paths.end(), h->path) == missing_paths.end()) {
    return false;
  }
  fs::path abs = h->path;
  if (!abs.is_absolute()) {
    abs = fs::path(workspace_root) / h->path;
  }
  abs = abs.lexically_normal();
  const std::string text = read_file_raw(abs.string());
  if (text.empty()) {
    return false;
  }
  SearchReplaceSpan already;
  std::string already_err;
  if (find_unique_span_allow_flex(text, h->search, &already, &already_err)) {
    return false;
  }
  std::string added;
  if (!extract_gap_insert_body(*h, path_looks_like_header(h->path), &added)) {
    return false;
  }
  const std::string marker = first_content_line(added);
  if (!marker.empty() && text.find(marker) != std::string::npos) {
    return false;
  }
  const std::string anchor = unique_disk_anchor(text);
  if (anchor.empty()) {
    return false;
  }
  std::string body = added;
  if (!body.empty() && body.front() != '\n' && (anchor.empty() || anchor.back() != '\n')) {
    body.insert(body.begin(), '\n');
  }
  h->search = anchor;
  h->replace = anchor + body;
  return true;
}

std::vector<SearchReplaceHunk> split_mixed_sibling_hunks(
    const std::string& workspace_root, std::vector<SearchReplaceHunk> hunks,
    const std::vector<std::string>& instruction_paths) {
  bool want_both = false;
  for (const auto& p : instruction_paths) {
    if (path_looks_like_header(p)) {
      want_both = true;
    }
  }
  if (!want_both) {
    for (const auto& h : hunks) {
      const std::string twin = path_looks_like_header(h.path)
                                   ? sibling_source_rel(workspace_root, h.path)
                                   : sibling_header_rel(workspace_root, h.path);
      if (!twin.empty()) {
        want_both = true;
        break;
      }
    }
  }
  if (!want_both) {
    return hunks;
  }

  std::vector<SearchReplaceHunk> out;
  out.reserve(hunks.size() + 1);
  for (auto& h : hunks) {
    std::string added;
    if (!replace_has_search_prefix(h.search, h.replace, &added)) {
      out.push_back(std::move(h));
      continue;
    }
    std::string decls;
    std::string defs;
    split_added_decl_def(added, &decls, &defs);
    if (trim_ws(decls).empty() || trim_ws(defs).empty()) {
      out.push_back(std::move(h));
      continue;
    }
    const bool on_header = path_looks_like_header(h.path);
    const std::string twin = on_header ? sibling_source_rel(workspace_root, h.path)
                                       : sibling_header_rel(workspace_root, h.path);
    if (twin.empty()) {
      out.push_back(std::move(h));
      continue;
    }
    SearchReplaceHunk here = h;
    SearchReplaceHunk there;
    there.path = twin;
    if (on_header) {
      here.replace = h.search + decls;
      const std::string twin_text =
          read_file_raw((fs::path(workspace_root) / twin).lexically_normal().string());
      std::string anchor = sibling_anchor_search(twin_text);
      if (anchor.empty()) {
        anchor = h.search;
      }
      there.search = anchor;
      there.replace = anchor + defs;
    } else {
      here.replace = h.search + defs;
      const std::string twin_text =
          read_file_raw((fs::path(workspace_root) / twin).lexically_normal().string());
      std::string anchor = sibling_anchor_search(twin_text);
      if (anchor.empty()) {
        anchor = h.search;
      }
      there.search = anchor;
      there.replace = anchor + decls;
    }
    out.push_back(std::move(here));
    out.push_back(std::move(there));
  }
  return out;
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
    if (!find_unique_span_allow_flex(text, h.search, nullptr, &err)) {
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
  // Keep blank lines — stripping them taught models collapsed searches that fail exact match.
  std::istringstream in(body);
  std::ostringstream out;
  std::string line;
  int n = 0;
  while (std::getline(in, line) && n < 16) {
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

// Paths like src/foo/bar.cpp mentioned in Instruction (generic, no domain keywords).
std::string instruction_section_text(const std::string& session_md) {
  const auto instr = session_md.find("## Instruction");
  if (instr == std::string::npos) {
    return {};
  }
  auto end = session_md.size();
  for (const char* next : {"## Ranked map", "## Code pack", "## Observations"}) {
    const auto p = session_md.find(next, instr + 1);
    if (p != std::string::npos && p < end) {
      end = p;
    }
  }
  return session_md.substr(instr, end - instr);
}

std::vector<std::string> instruction_src_paths(const std::string& workspace_root,
                                               const std::string& session_md) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  // Ranked map / pack / observations mention many src/ paths; only Instruction counts.
  const std::string section = instruction_section_text(session_md);
  const std::string& text = section.empty() ? session_md : section;
  for (std::size_t i = 0; i + 4 < text.size(); ++i) {
    if (text.compare(i, 4, "src/") != 0) {
      continue;
    }
    if (i > 0) {
      const char prev = text[i - 1];
      if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_' || prev == '/' ||
          prev == '.') {
        continue;
      }
    }
    std::size_t j = i;
    while (j < text.size()) {
      const char c = text[j];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '.' ||
          c == '-') {
        ++j;
        continue;
      }
      break;
    }
    std::string path = text.substr(i, j - i);
    while (!path.empty() && (path.back() == '.' || path.back() == '/')) {
      path.pop_back();
    }
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) {
      continue;
    }
    const std::string ext = path.substr(dot);
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".cc" && ext != ".cxx") {
      continue;
    }
    if (!seen.insert(path).second) {
      continue;
    }
    if (!workspace_root.empty()) {
      std::error_code ec;
      if (!fs::exists(fs::path(workspace_root) / path, ec)) {
        continue;
      }
    }
    out.push_back(path);
  }
  return out;
}

void remember_edited_path(std::vector<std::string>& edited_paths, const std::string& path) {
  if (path.empty()) {
    return;
  }
  for (const auto& p : edited_paths) {
    if (p == path) {
      return;
    }
  }
  edited_paths.push_back(path);
}

std::vector<std::string> missing_instruction_paths(const std::string& workspace_root,
                                                   const std::vector<std::string>& edited_paths) {
  std::string sess;
  {
    std::ifstream in(Level2Session::session_path(workspace_root));
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      sess = ss.str();
    }
  }
  const auto want = instruction_src_paths(workspace_root, sess);
  std::vector<std::string> missing;
  for (const auto& p : want) {
    bool hit = false;
    for (const auto& e : edited_paths) {
      if (e == p) {
        hit = true;
        break;
      }
    }
    if (!hit) {
      missing.push_back(p);
    }
  }
  return missing;
}

constexpr int kMaxCoverageGatePushbacks = 3;
constexpr std::size_t kMaxAppliedBlobChars = 48000;
constexpr int kMaxCoverageBodyLines = 48;
constexpr std::size_t kMaxCoverageBodyChars = 2800;
constexpr int kMaxCoverageSignatureLines = 16;
constexpr std::size_t kMaxCoverageSignatureChars = 800;

bool coverage_marker_interesting(const std::string& raw) {
  if (raw.size() < 3 || raw.size() > 64) {
    return false;
  }
  std::string lower = to_lower_copy(raw);
  static const char* kStop[] = {
      "src",  "util", "void", "bool", "true", "false", "return", "static", "constexpr",
      "int",  "auto", "string", "namespace", "include", "pragma", "once", "class", "struct",
      "const", "using", "public", "private", "protected", "template", "typename", "nullptr",
      "size", "path", "file", "edit", "done", "plan", "tool", "both", "ambos", "archivos",
      "obligatorios", "añade", "encima", "junto", "también", "acepta", "tras", "cambia",
      "default", "selected", "query", "instruction", "shell", "command", "exists", "quote",
      "nullptr", "stdio", "cstdlib", "iostream", "vector", "map",
  };
  for (const char* s : kStop) {
    if (lower == s) {
      return false;
    }
  }
  // Prefer distinctive tokens: L2_*, snake_case, kCamel, CamelCase-ish.
  if (lower.rfind("l2_", 0) == 0) {
    return true;
  }
  if (raw.find('_') != std::string::npos) {
    return true;
  }
  if (raw.size() >= 2 && raw[0] == 'k' && std::isupper(static_cast<unsigned char>(raw[1]))) {
    return true;
  }
  bool has_upper = false;
  bool has_lower = false;
  for (unsigned char c : raw) {
    if (std::isupper(c)) {
      has_upper = true;
    }
    if (std::islower(c)) {
      has_lower = true;
    }
  }
  if (has_upper && has_lower && raw.size() >= 6) {
    return true;
  }
  return false;
}

// Distinctive idents / L2_* markers from Instruction (preserve case for display).
std::vector<std::string> instruction_coverage_markers(const std::string& session_md) {
  const std::string section = instruction_section_text(session_md);
  std::vector<std::string> out;
  std::unordered_set<std::string> seen_lower;
  auto consider = [&](const std::string& tok) {
    if (!coverage_marker_interesting(tok)) {
      return;
    }
    const std::string key = to_lower_copy(tok);
    if (!seen_lower.insert(key).second) {
      return;
    }
    out.push_back(tok);
  };
  // Explicit L2_* (any case) from raw text.
  for (std::size_t i = 0; i + 3 < section.size(); ++i) {
    if ((section[i] == 'L' || section[i] == 'l') &&
        (section[i + 1] == '2') && section[i + 2] == '_') {
      std::size_t j = i;
      while (j < section.size() &&
             (std::isalnum(static_cast<unsigned char>(section[j])) || section[j] == '_')) {
        ++j;
      }
      consider(section.substr(i, j - i));
      i = j;
    }
  }
  // Ident tokens from query: line (preserve case).
  std::size_t qpos = 0;
  while ((qpos = section.find("query:", qpos)) != std::string::npos) {
    const bool at_line = (qpos == 0) || section[qpos - 1] == '\n';
    if (!at_line) {
      qpos += 6;
      continue;
    }
    const auto eol = section.find('\n', qpos);
    const std::string line =
        section.substr(qpos + 6, (eol == std::string::npos ? section.size() : eol) - (qpos + 6));
    std::string cur;
    auto flush = [&]() {
      if (!cur.empty()) {
        consider(cur);
      }
      cur.clear();
    };
    for (unsigned char ch : line) {
      if (std::isalnum(ch) || ch == '_') {
        cur.push_back(static_cast<char>(ch));
      } else {
        flush();
      }
    }
    flush();
    qpos = eol == std::string::npos ? section.size() : eol + 1;
  }
  // Cap to avoid huge gates on long queries.
  if (out.size() > 12) {
    out.resize(12);
  }
  return out;
}

bool blob_contains_ci(const std::string& hay, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  if (hay.size() < needle.size()) {
    return false;
  }
  const std::string h = to_lower_copy(hay);
  const std::string n = to_lower_copy(needle);
  return h.find(n) != std::string::npos;
}

std::vector<std::string> missing_instruction_markers(const std::string& workspace_root,
                                                     const std::string& session_md,
                                                     const std::string& applied_blob) {
  std::unordered_set<std::string> path_stems;
  for (const auto& p : instruction_src_paths(workspace_root, session_md)) {
    const std::string stem = path_stem_key(p);
    if (!stem.empty()) {
      path_stems.insert(stem);
    }
  }
  std::vector<std::string> missing;
  for (const auto& m : instruction_coverage_markers(session_md)) {
    if (path_stems.count(to_lower_copy(m))) {
      continue;  // filename stem is not a code marker
    }
    if (!blob_contains_ci(applied_blob, m)) {
      missing.push_back(m);
    }
  }
  return missing;
}

struct InstructionCoverageGaps {
  std::vector<std::string> missing_paths;
  std::vector<std::string> missing_markers;
  bool empty() const { return missing_paths.empty() && missing_markers.empty(); }
};

InstructionCoverageGaps collect_instruction_coverage_gaps(
    const std::string& workspace_root, const std::vector<std::string>& edited_paths,
    const std::string& applied_blob) {
  InstructionCoverageGaps g;
  std::string sess;
  {
    std::ifstream in(Level2Session::session_path(workspace_root));
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      sess = ss.str();
    }
  }
  g.missing_paths = missing_instruction_paths(workspace_root, edited_paths);
  g.missing_markers = missing_instruction_markers(workspace_root, sess, applied_blob);
  return g;
}

bool coverage_gate_should_block(const std::string& workspace_root,
                                const InstructionCoverageGaps& gaps) {
  if (gaps.empty()) {
    return false;
  }
  // Paths missing always block when Instruction named any src path we track.
  if (!gaps.missing_paths.empty()) {
    return true;
  }
  // Markers: block when ≥2 distinctive markers and some missing (multi-locus).
  if (gaps.missing_markers.empty()) {
    return false;
  }
  std::string sess;
  {
    std::ifstream in(Level2Session::session_path(workspace_root));
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      sess = ss.str();
    }
  }
  const auto all_markers = instruction_coverage_markers(sess);
  if (all_markers.size() >= 2) {
    return true;
  }
  return false;
}

void append_applied_blob(std::string* blob, const std::string& new_text) {
  if (blob == nullptr || new_text.empty()) {
    return;
  }
  if (!blob->empty()) {
    blob->push_back('\n');
  }
  *blob += new_text;
  if (blob->size() > kMaxAppliedBlobChars) {
    blob->erase(0, blob->size() - kMaxAppliedBlobChars);
  }
}

std::string sibling_source_rel(const std::string& workspace_root, const std::string& rel) {
  fs::path p(rel);
  if (!is_header_ext(p.extension().string())) {
    return {};
  }
  static const char* kExts[] = {".cpp", ".cc", ".cxx", ".c"};
  for (const char* sext : kExts) {
    fs::path cand = p;
    cand.replace_extension(sext);
    std::error_code ec;
    if (fs::exists(fs::path(workspace_root) / cand, ec) && !ec) {
      return cand.generic_string();
    }
  }
  return {};
}

// Best-effort fresh on-disk snippet for coverage gaps (uses get_code_of / file read).
std::string fetch_coverage_bodies(Level2SessionDeps& deps, const std::string& workspace_root,
                                  const InstructionCoverageGaps& gaps,
                                  const std::vector<std::string>& edited_paths) {
  std::ostringstream out;
  int bodies = 0;
  auto add_body = [&](const std::string& label, const std::string& text) {
    if (bodies >= 2 || text.empty()) {
      return;
    }
    ++bodies;
    out << "### fresh `" << label << "`\n\n```\n";
    // Tail: new decls/defs usually sit at the end; head-truncate hid command_exists.
    std::string clipped = Level2Session::truncate_observation_tail(text, kMaxCoverageBodyLines);
    if (clipped.size() > kMaxCoverageBodyChars) {
      clipped = clipped.substr(clipped.size() - kMaxCoverageBodyChars);
      const auto nl = clipped.find('\n');
      if (nl != std::string::npos && nl + 1 < clipped.size()) {
        clipped = clipped.substr(nl + 1);
      }
      clipped = "… (coverage tail)\n" + clipped;
    }
    out << clipped;
    if (!text.empty() && text.back() != '\n') {
      out << '\n';
    }
    out << "```\n\n";
  };

  auto add_signature = [&](const std::string& label, const std::string& text) {
    if (text.empty()) {
      return;
    }
    std::string clipped =
        Level2Session::truncate_observation_tail(text, kMaxCoverageSignatureLines);
    if (clipped.size() > kMaxCoverageSignatureChars) {
      clipped = clipped.substr(clipped.size() - kMaxCoverageSignatureChars);
      const auto nl = clipped.find('\n');
      if (nl != std::string::npos && nl + 1 < clipped.size()) {
        clipped = clipped.substr(nl + 1);
      }
      clipped = "… (firma)\n" + clipped;
    }
    out << "### firma (referencia, NO edites) `" << label << "`\n\n```\n";
    out << clipped;
    if (!text.empty() && text.back() != '\n') {
      out << '\n';
    }
    out << "```\n\n";
  };

  auto already_edited = [&](const std::string& p) {
    return std::find(edited_paths.begin(), edited_paths.end(), p) != edited_paths.end();
  };

  std::vector<std::string> targets;
  for (const auto& p : gaps.missing_paths) {
    targets.push_back(p);
  }
  // Marker gaps on already-edited paths: re-show those paths + sibling twin.
  if (targets.empty() && !gaps.missing_markers.empty()) {
    for (const auto& p : edited_paths) {
      targets.push_back(p);
    }
  }
  for (const auto& p : gaps.missing_paths) {
    const std::string twin = sibling_header_rel(workspace_root, p);
    if (!twin.empty() && !already_edited(twin)) {
      targets.push_back(twin);
    }
    const std::string src = sibling_source_rel(workspace_root, p);
    if (!src.empty() && !already_edited(src)) {
      targets.push_back(src);
    }
  }
  // Dedupe preserve order.
  std::unordered_set<std::string> seen;
  std::vector<std::string> uniq;
  for (const auto& t : targets) {
    if (seen.insert(t).second) {
      uniq.push_back(t);
    }
  }

  for (const auto& path : uniq) {
    if (bodies >= 2) {
      break;
    }
    std::string text;
    std::string arg = path;
    const bool missing_file =
        std::find(gaps.missing_paths.begin(), gaps.missing_paths.end(), path) !=
        gaps.missing_paths.end();
    // Missing Instruction files: disk as SEARCH source (get_code_of may snap to the first
    // symbol and hide the last function). Marker-only gaps still prefer path:Symbol.
    if (missing_file) {
      text = read_file_raw((fs::path(workspace_root) / path).string());
    }
    if (text.empty()) {
      if (!gaps.missing_markers.empty() && !missing_file) {
        arg = path + ":" + gaps.missing_markers.front();
      }
      if (deps.tools != nullptr && deps.tools->has("get_code_of")) {
        const AiToolResult tr = deps.tools->invoke("get_code_of", arg);
        if (tr.ok && !tr.text.empty() && tr.text.find("error") != 0) {
          text = tr.text;
        } else if (arg != path) {
          const AiToolResult tr2 = deps.tools->invoke("get_code_of", path);
          if (tr2.ok) {
            text = tr2.text;
          }
        }
      }
    }
    if (text.empty()) {
      text = read_file_raw((fs::path(workspace_root) / path).string());
    }
    add_body(arg, text);
  }
  // Edited Instruction twins: short signature AFTER the missing file (SEARCH source).
  if (!gaps.missing_paths.empty()) {
    std::unordered_set<std::string> signed_once;
    for (const auto& p : gaps.missing_paths) {
      const std::string twins[] = {sibling_header_rel(workspace_root, p),
                                   sibling_source_rel(workspace_root, p)};
      for (const auto& twin : twins) {
        if (twin.empty() || !already_edited(twin) || !signed_once.insert(twin).second) {
          continue;
        }
        add_signature(twin, read_file_raw((fs::path(workspace_root) / twin).string()));
      }
    }
  }
  return out.str();
}

std::string format_coverage_observation(int turn, const char* kind,
                                        const InstructionCoverageGaps& gaps,
                                        const std::string& bodies) {
  std::ostringstream block;
  block << "### turn " << turn << " — " << kind << "\n\n";
  block << "Instruction coverage incompleta. No emitas `done` aún.\n\n";
  if (!gaps.missing_paths.empty()) {
    block << "Paths de Instruction sin editar:\n";
    for (const auto& p : gaps.missing_paths) {
      block << "- `" << p << "`\n";
    }
    block << '\n';
  }
  if (!gaps.missing_markers.empty()) {
    block << "Marcadores / símbolos de Instruction aún no presentes en tus edits:\n";
    for (const auto& m : gaps.missing_markers) {
      block << "- `" << m << "`\n";
    }
    block << '\n';
  }
  if (l2_feat::enabled("EDIT_LEAN_PROMPT")) {
    block << "Siguiente acción: un hunk Aider del primer path de arriba.\n"
             "Empieza con ese path y `<<<<<<< SEARCH` (texto exacto del **fresh**, no de la "
             "firma).\n"
             "La firma del path ya cubierto es solo referencia. PROHIBIDO JSON / plan / tool / "
             "done.\n\n";
  } else {
    block << "Siguiente acción obligatoria: `{\"action\":\"edit\",\"hunks\":[…]}` "
             "cubriendo lo que falta (multi-hunk / sibling hpp+cpp OK).\n\n";
  }
  if (!bodies.empty()) {
    block << "## Código fresco (disco actual)\n\n" << bodies;
  }
  return block.str();
}

bool post_edit_coverage_enabled() {
  return l2_feat::enabled("POST_EDIT_COVERAGE");
}

// Idents from Instruction missing in pack (generic: no domain facets).
std::vector<std::string> pack_instruction_gaps(const std::string& pack,
                                               const std::vector<std::string>& needles) {
  std::vector<std::string> missing;
  int strong = 0;
  int strong_hit = 0;
  const std::string pack_low = to_lower_copy(pack);
  for (const auto& n : needles) {
    // Only tokens that look like real code identifiers (snake_case / versioned) are
    // meaningful coverage markers.  After the A1 fix looks_like_code_ident already
    // requires _ or digit, so the size<8 guard is kept for extra safety.
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

void Level2Session::set_context_budget(L2ContextBudget budget) {
  budget_ = std::move(budget);
}

bool Level2Session::maybe_trim_pack_to_budget(const std::string& workspace_root,
                                              std::string* summary) {
  const std::string path = pack_path(workspace_root);
  std::string pack = read_file(path);
  if (pack.empty() || pack.find("_(vacío") != std::string::npos) {
    return false;
  }
  const std::size_t limit = budget_.pack_chars;
  if (limit == 0 || pack.size() <= static_cast<std::size_t>(limit * 12 / 10)) {
    return false;
  }
  const std::size_t before = pack.size();
  // Prefer dropping ## Outlines first (code lives in Fragments); then head+tail.
  const auto outlines = pack.find("\n## Outlines");
  if (outlines != std::string::npos && outlines > limit / 2) {
    pack = pack.substr(0, outlines) + "\n\n_… outlines omitidos (budget local) …_\n";
  }
  if (pack.size() > limit) {
    pack = truncate_to_budget(std::move(pack), limit, "budget local");
  }
  if (!write_file(path, pack, nullptr)) {
    return false;
  }
  if (summary) {
    *summary = "pack recortado al budget " + budget_.backend + " (" + std::to_string(before) +
               "→" + std::to_string(pack.size()) + " chars)";
  }
  return true;
}

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

std::string Level2Session::a_state_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "a_state.json").string();
}

std::string Level2Session::a_notes_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "a_notes.md").string();
}

std::string Level2Session::answer_path(const std::string& workspace_root) {
  return (fs::path(dir_for(workspace_root)) / "answer.md").lexically_normal().string();
}

bool Level2Session::is_continuable(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return false;
  }
  const State st = load_state(workspace_root);
  if (!st.continuable) {
    return false;
  }
  return st.done || st.phase == "done" || st.phase == "clarify";
}

bool Level2Session::mark_continuable(const std::string& workspace_root, std::string* err_out) {
  if (workspace_root.empty()) {
    if (err_out) {
      *err_out = "workspace_root vacío";
    }
    return false;
  }
  State st = load_state(workspace_root);
  st.continuable = true;
  st.resume = false;
  return save_state(workspace_root, st, err_out);
}

bool Level2Session::clear_session(const std::string& workspace_root, std::string* err_out) {
  if (workspace_root.empty()) {
    if (err_out) {
      *err_out = "workspace_root vacío";
    }
    return false;
  }
  const std::string dir = dir_for(workspace_root);
  std::error_code ec;
  if (fs::exists(dir, ec)) {
    fs::remove_all(dir, ec);
    if (ec) {
      if (err_out) {
        *err_out = "no se pudo borrar sesión L2: " + ec.message();
      }
      return false;
    }
  }
  return true;
}

namespace {

bool upsert_followups_section(const std::string& session_file, int followup_n,
                              const std::string& user_text, std::string* err) {
  std::ifstream in(session_file);
  if (!in) {
    if (err) {
      *err = "session.md ausente";
    }
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string session = ss.str();

  std::ostringstream entry;
  entry << followup_n << ". " << user_text << "\n";

  const std::string fu_mark = "## Follow-ups";
  const std::string map_mark = "## Ranked map";
  const std::string git_mark = "## Git context";
  const std::string obs_mark = "## Observations";

  auto insert_before = [&](std::size_t pos) {
    std::string block = fu_mark + "\n\n" + entry.str() + "\n";
    session = session.substr(0, pos) + block + session.substr(pos);
  };

  const auto fu_pos = session.find(fu_mark);
  if (fu_pos != std::string::npos) {
    // Append before the next major section after Follow-ups.
    std::size_t next = session.find("\n## ", fu_pos + fu_mark.size());
    if (next == std::string::npos) {
      if (!session.empty() && session.back() != '\n') {
        session.push_back('\n');
      }
      session += entry.str();
    } else {
      session = session.substr(0, next + 1) + entry.str() + session.substr(next + 1);
    }
  } else {
    std::size_t pos = session.find(map_mark);
    if (pos == std::string::npos) {
      pos = session.find(git_mark);
    }
    if (pos == std::string::npos) {
      pos = session.find(obs_mark);
    }
    if (pos == std::string::npos) {
      if (!session.empty() && session.back() != '\n') {
        session.push_back('\n');
      }
      session += "\n" + fu_mark + "\n\n" + entry.str();
    } else {
      insert_before(pos);
    }
  }

  // Keep workflow: line in Instruction in sync if present (caller may rewrite separately).
  std::ofstream out(session_file, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir session.md";
    }
    return false;
  }
  out << session;
  return true;
}

bool rewrite_instruction_workflow_line(const std::string& session_file,
                                       const std::string& workflow_name, std::string* err) {
  std::ifstream in(session_file);
  if (!in) {
    if (err) {
      *err = "session.md ausente";
    }
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string session = ss.str();
  const std::string key = "workflow: ";
  const auto instr = session.find("## Instruction");
  if (instr == std::string::npos) {
    return true;
  }
  const auto end_instr = session.find("\n## ", instr + 1);
  const std::size_t search_end = end_instr == std::string::npos ? session.size() : end_instr;
  const auto wf = session.find(key, instr);
  if (wf != std::string::npos && wf < search_end) {
    const auto line_end = session.find('\n', wf);
    const std::string replacement = key + workflow_name;
    if (line_end == std::string::npos) {
      session = session.substr(0, wf) + replacement;
    } else {
      session = session.substr(0, wf) + replacement + session.substr(line_end);
    }
  }
  std::ofstream out(session_file, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir session.md";
    }
    return false;
  }
  out << session;
  return true;
}

}  // namespace

bool Level2Session::reopen_for_followup(const std::string& workspace_root,
                                        const std::string& user_text,
                                        const std::string& workflow_opt, std::string* err_out) {
  if (workspace_root.empty()) {
    if (err_out) {
      *err_out = "workspace_root vacío";
    }
    return false;
  }
  if (user_text.empty()) {
    if (err_out) {
      *err_out = "follow-up vacío";
    }
    return false;
  }
  if (!is_continuable(workspace_root)) {
    if (err_out) {
      *err_out = "sesión no continuable; usa bootstrap o Reset";
    }
    return false;
  }

  State st = load_state(workspace_root);
  const std::string prev_workflow = st.workflow.empty() ? "agent" : st.workflow;
  if (!workflow_opt.empty()) {
    st.workflow = ai_workflow_kind_name(parse_ai_workflow_kind(workflow_opt));
  }
  const std::string workflow_name = st.workflow.empty() ? "agent" : st.workflow;
  const AiWorkflowKind wf = parse_ai_workflow_kind(workflow_name);

  ++st.followup_count;
  st.done = false;
  st.continuable = true;
  st.resume = true;
  st.map_review = false;
  st.edit_attempt = 0;
  st.compile_attempt = 0;
  st.clarify_pushback = 0;
  st.pack_incomplete_pushback = 0;
  st.explore_tool_count = 0;
  st.plan_nudge_sent = false;
  st.post_pack_tool_count = 0;
  st.edit_nudge_sent = false;
  st.pending.clear();
  st.last_action = "followup";

  // Prefer short-circuit into edit when agent + pack already present.
  if (!ai_workflow_is_readonly(wf) && st.has_pack) {
    st.phase = "edit";
  } else {
    st.phase = "explore";
  }

  std::string err;
  if (!upsert_followups_section(session_path(workspace_root), st.followup_count, user_text, &err)) {
    if (err_out) {
      *err_out = err.empty() ? "no se pudo actualizar Follow-ups" : err;
    }
    return false;
  }
  if (workflow_name != prev_workflow) {
    rewrite_instruction_workflow_line(session_path(workspace_root), workflow_name, nullptr);
  }

  {
    Level2SessionDeps deps;
    Level2Session writer(deps);
    std::ostringstream block;
    block << "### turn follow-up " << st.followup_count << " — user\n\n";
    block << user_text << "\n\n";
    if (workflow_name != prev_workflow) {
      block << "_workflow: " << prev_workflow << " → " << workflow_name << "_\n\n";
    }
    block << "Resume: si ## Follow-ups + pack/answer bastan → "
          << (ai_workflow_is_readonly(wf) ? "`synthesize`" : "`edit`")
          << " (o explore/plan si falta contexto).\n\n";
    std::size_t chars = 0;
    if (!writer.append_observation(workspace_root, block.str(), &chars, &err)) {
      if (err_out) {
        *err_out = err.empty() ? "no se pudo escribir observation de follow-up" : err;
      }
      return false;
    }
  }

  if (!save_state(workspace_root, st, err_out)) {
    return false;
  }
  write_response_json(workspace_root, true, "followup", "", "", user_text, "", st.turn, st.phase);
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"followup\",\"n\":" +
                                   std::to_string(st.followup_count) + ",\"phase\":\"" + st.phase +
                                   "\",\"workflow\":\"" + workflow_name + "\",\"has_pack\":" +
                                   (st.has_pack ? "1" : "0") + "}");
  ai_trace(AiTraceChannel::L2, "l2_followup",
           "{\"n\":" + std::to_string(st.followup_count) + ",\"phase\":\"" + st.phase +
               "\",\"workflow\":\"" + workflow_name + "\"}");
  return true;
}

std::string Level2Session::resume_context_markdown(const std::string& workspace_root,
                                                   std::size_t max_chars) {
  std::ostringstream out;
  const std::string answer = read_file(answer_path(workspace_root));
  if (!answer.empty()) {
    out << "## Prior answer\n\n";
    constexpr std::size_t kAnswerCap = 3500;
    if (answer.size() > kAnswerCap) {
      out << answer.substr(0, kAnswerCap) << "\n…[prior answer truncado]…\n\n";
    } else {
      out << answer;
      if (answer.back() != '\n') {
        out << '\n';
      }
      out << '\n';
    }
  }

  const State st = load_state(workspace_root);
  if (!st.watchlist.empty()) {
    out << "## Prior targets\n\n";
    for (const auto& t : st.watchlist) {
      out << "- `" << t << "`\n";
    }
    out << '\n';
  }

  const std::string pack = read_file(pack_path(workspace_root));
  if (!pack.empty() && pack.find("_(vacío") == std::string::npos) {
    out << "## Prior changes / pack\n\n";
    constexpr std::size_t kPackCap = 4000;
    if (pack.size() > kPackCap) {
      out << pack.substr(0, kPackCap) << "\n…[prior pack truncado]…\n";
    } else {
      out << pack;
      if (pack.back() != '\n') {
        out << '\n';
      }
    }
  }

  std::string s = out.str();
  if (max_chars > 0 && s.size() > max_chars) {
    s = s.substr(0, max_chars) + "\n…[resume context truncado]…\n";
  }
  return s;
}

bool Level2Session::tool_allowed(const std::string& name) {
  return l2_whitelist().count(name) > 0;
}

bool Level2Session::tool_allowed_in_phase(const std::string& name, const std::string& phase) {
  if (!tool_allowed(name)) {
    return false;
  }
  // P5: explore_a/explore_b never need clangd for locate/pack when Phase A is on.
  if (l2_feat::enabled("L2_EXPLORE_PHASE_A") &&
      (phase == "explore_a" || phase == "explore_b")) {
    return l2_local_locate_tools().count(name) > 0;
  }
  return true;
}

std::string Level2Session::tool_guide_markdown() {
  return R"(## Tool guide

Fases: **explore** (mapa → **plan** → pack) → **edit** ↔ **compile** → **map_review** →
**done** (solo con `action=done` explícito). Compile OK **no** finaliza: restaura el mapa
inicial (salvo `map_stale`) y pregunta si falta algo. Si explore **no** localiza el código: **clarify**.

### Explore (primera mirada = plan)
Preferir `plan` en el **primer** paso: 4–8 `path:Symbol` / `path:line` anclados a la
Instruction (evitar path bare). Orden de `targets` = importancia (primero = must en el pack;
el runtime omite por la cola si no cabe presupuesto). Máx. ~8 tools sueltos antes del primer plan (el runtime
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
- Si el hunk cae en Truncated → refetch tip de esa ventana (no inventes); Truncated no fuerza ampliar pack.
- `search` debe ser un **bloque de código único** (no un ident suelto tipo `foo_bar`).
- Hunk idéntico al último fallo → rechazado; tras varios fallos → clarify.
- Tras `edit` OK → compile. Compile OK → **mapa inicial** + «¿algo más?» (`plan` / `edit` / `done`).
- Compile fail (≤3): stderr + old/new; reemite `edit`.
)";
}

std::string Level2Session::tool_guide_edit_markdown() {
  return R"(## Tool guide (edit)
Hunks en texto plano (Aider). NO JSON con search/replace.
src/foo.cpp
<<<<<<< SEARCH
bloque único del pack
=======
nuevo
>>>>>>> REPLACE
{"action":"done","summary":"cambios listos"}
PROHIBIDO action=plan. Tras compile_fail: un hunk corto, SEARCH = span del pack.
)";
}

std::string Level2Session::tool_guide_explore_markdown() {
  return R"(## Tool guide (explore)
JSON only. Primer paso:
{"action":"plan","targets":["src/a.cpp:Foo","src/b.hpp:Bar"]}
Tras pack: {"action":"done","summary":"localizado","next":"edit"} o action=edit.
No repitas el mismo get_code_of. Refetch solo con path:A-B distinto.
)";
}

std::string Level2Session::tool_guide_explore_a_markdown() {
  return R"(## Tool guide (explore_a — edit site + trail)
JSON only. PROHIBIDO plan/tool/edit/pack.
useful = hipótesis (estado/busy/flag/API del síntoma); el trail profundiza — no hace falta
el edit site exacto en el peek. Getters OK. UI cosmético / cancel LSP genérico → reject.
Máx 1 useful/vuelta → el runtime abre call-stacks (trail); no corones primary aún.
Trail: {"action":"a_trail_judge","verdicts":[{"target":"S2","verdict":"interesting","why":"…"},
{"target":"S1","verdict":"reject","why":"…"}]}
(verdict = exactamente interesting o reject; nunca el literal "interesting|reject")
Si todos reject → L0 invalidado. Interesting → runtime profundiza pilas completas (TS scopes).
Juzga peeks: {"action":"a_judge","verdicts":[…],"done":false}
Cierra: {"action":"a_done","loci":[{"stem":"…","anchor":"path:Symbol","role":"primary","why":"…"}],
"summary":"…"}
)";
}

std::string Level2Session::tool_guide_explore_b_markdown() {
  return R"(## Tool guide (explore_b — pack desde loci)
JSON only. Pack desde loci de a_done (plan vacío o targets de loci).
{"action":"plan","targets":[]} o {"action":"plan","targets":["path:Symbol",…]}
Tras pack: {"action":"done","summary":"…","next":"edit"} o action=edit.
PROHIBIDO caza libre multi-stem fuera de loci / micro-A allowlist.
)";
}

std::string Level2Session::strip_unrelated_on_disk_excerpts(const std::string& obs) {
  // Keep "## on disk `path`" only when this block is compile/edit feedback for that
  // same path; drop sibling dumps that bloat the edit prompt.
  std::string failed_path;
  const auto path_mark = obs.find("path: `");
  if (path_mark != std::string::npos) {
    const auto start = path_mark + 7;
    const auto end = obs.find('`', start);
    if (end != std::string::npos) {
      failed_path = obs.substr(start, end - start);
    }
  }
  if (failed_path.empty()) {
    const auto hunk_mark = obs.find("## old (hunk");
    if (hunk_mark != std::string::npos) {
      const auto tick = obs.find('`', hunk_mark);
      if (tick != std::string::npos) {
        const auto tick2 = obs.find('`', tick + 1);
        if (tick2 != std::string::npos) {
          failed_path = obs.substr(tick + 1, tick2 - tick - 1);
        }
      }
    }
  }

  std::string out;
  std::size_t pos = 0;
  while (pos < obs.size()) {
    const auto disk = obs.find("## on disk `", pos);
    if (disk == std::string::npos) {
      out.append(obs, pos, std::string::npos);
      break;
    }
    out.append(obs, pos, disk - pos);
    const auto path_b = disk + 12;
    const auto path_e = obs.find('`', path_b);
    std::string disk_path;
    if (path_e != std::string::npos) {
      disk_path = obs.substr(path_b, path_e - path_b);
    }
    auto next = obs.find("\n## ", path_e == std::string::npos ? disk + 1 : path_e + 1);
    if (next == std::string::npos) {
      next = obs.size();
    }
    const bool keep =
        !failed_path.empty() && (disk_path == failed_path ||
                                 disk_path.find(failed_path) != std::string::npos ||
                                 failed_path.find(disk_path) != std::string::npos);
    if (keep) {
      out.append(obs, disk, next - disk);
    }
    pos = next;
  }
  return out;
}

std::string Level2Session::last_edit_relevant_observation(const std::string& session_md,
                                                          std::size_t max_chars) {
  const auto obs_pos = session_md.find("## Observations");
  if (obs_pos == std::string::npos) {
    return {};
  }
  const std::string obs = session_md.substr(obs_pos);
  const char* kinds[] = {"compile_feedback", "edit_feedback", "edit_repeat_pushback",
                         "json_invalid", "post_edit_coverage", "edit_covered_path",
                         "covered_path_limit"};
  std::size_t best = std::string::npos;
  for (const char* k : kinds) {
    const std::string needle = std::string(" — ") + k;
    const auto p = obs.rfind(needle);
    if (p == std::string::npos) {
      continue;
    }
    const auto header = obs.rfind("### turn ", p);
    if (header == std::string::npos || header > p) {
      continue;
    }
    if (best == std::string::npos || header > best) {
      best = header;
    }
  }
  if (best == std::string::npos) {
    return {};
  }
  auto end = obs.find("\n### turn ", best + 1);
  if (end == std::string::npos) {
    end = obs.size();
  }
  std::string block = obs.substr(best, end - best);
  block = strip_unrelated_on_disk_excerpts(block);
  if (max_chars > 0 && block.size() > max_chars) {
    // Keep the header + head of the block (stderr/error), not a mid-string tail.
    const auto nl = block.find('\n');
    std::string head = nl == std::string::npos ? block : block.substr(0, nl + 1);
    const std::size_t keep = max_chars > head.size() + 40 ? max_chars - 40 : max_chars / 2;
    block = head + block.substr(nl == std::string::npos ? 0 : nl + 1, keep) +
            "\n…[observation acotada; no copies este corte]\n";
  }
  return "## Observations\n\n" + block;
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
    st.workflow = j.value("workflow", "agent");
    st.edit_attempt = j.value("edit_attempt", 0);
    st.compile_attempt = j.value("compile_attempt", 0);
    st.clarify_pushback = j.value("clarify_pushback", 0);
    st.pack_incomplete_pushback = j.value("pack_incomplete_pushback", 0);
    st.explore_tool_count = j.value("explore_tool_count", 0);
    st.plan_nudge_sent = j.value("plan_nudge_sent", false);
    st.post_pack_tool_count = j.value("post_pack_tool_count", 0);
    st.edit_nudge_sent = j.value("edit_nudge_sent", false);
    if (j.contains("seen_tool_keys") && j["seen_tool_keys"].is_array()) {
      for (const auto& t : j["seen_tool_keys"]) {
        if (t.is_string()) {
          st.seen_tool_keys.push_back(t.get<std::string>());
        }
      }
    }
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
    st.continuable = j.value("continuable", false);
    st.resume = j.value("resume", false);
    st.followup_count = j.value("followup_count", 0);
    st.last_op_id = j.value("last_op_id", static_cast<uint64_t>(0));
    st.applied_blob = j.value("applied_blob", "");
    st.coverage_gate_pushback = j.value("coverage_gate_pushback", 0);
    st.covered_path_rejects = j.value("covered_path_rejects", 0);
    st.pack_review_ok = j.value("pack_review_ok", false);
    st.pack_review_cycles = j.value("pack_review_cycles", 0);
    if (j.contains("review_search_terms") && j["review_search_terms"].is_array()) {
      for (const auto& t : j["review_search_terms"]) {
        if (t.is_string()) {
          st.review_search_terms.push_back(t.get<std::string>());
        }
      }
    }
    if (j.contains("rejected_targets") && j["rejected_targets"].is_array()) {
      for (const auto& t : j["rejected_targets"]) {
        if (t.is_string()) {
          st.rejected_targets.push_back(t.get<std::string>());
        }
      }
    }
    if (j.contains("watchlist") && j["watchlist"].is_array()) {
      for (const auto& t : j["watchlist"]) {
        if (t.is_string()) {
          st.watchlist.push_back(t.get<std::string>());
        }
      }
    }
    if (j.contains("edited_paths") && j["edited_paths"].is_array()) {
      for (const auto& t : j["edited_paths"]) {
        if (t.is_string()) {
          st.edited_paths.push_back(t.get<std::string>());
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
                      {"workflow", st.workflow},
                      {"edit_attempt", st.edit_attempt},
                      {"compile_attempt", st.compile_attempt},
                      {"clarify_pushback", st.clarify_pushback},
                      {"pack_incomplete_pushback", st.pack_incomplete_pushback},
                      {"explore_tool_count", st.explore_tool_count},
                      {"plan_nudge_sent", st.plan_nudge_sent},
                      {"post_pack_tool_count", st.post_pack_tool_count},
                      {"edit_nudge_sent", st.edit_nudge_sent},
                      {"seen_tool_keys", st.seen_tool_keys},
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
                      {"continuable", st.continuable},
                      {"resume", st.resume},
                      {"followup_count", st.followup_count},
                      {"last_op_id", st.last_op_id},
                      {"watchlist", st.watchlist},
                      {"edited_paths", st.edited_paths},
                      {"applied_blob", st.applied_blob},
                      {"coverage_gate_pushback", st.coverage_gate_pushback},
                      {"covered_path_rejects", st.covered_path_rejects},
                      {"pack_review_ok", st.pack_review_ok},
                      {"pack_review_cycles", st.pack_review_cycles},
                      {"review_search_terms", st.review_search_terms},
                      {"rejected_targets", st.rejected_targets},
                      {"pending", pending}};
  return write_file(state_path(workspace_root),
                    j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n", err);
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
  write_file(response_path(workspace_root),
             j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) + "\n", &err);
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
  // Phase A: no early-plan nudge (mixed explore). explore_a has no tools.
  if (st.phase == "explore_a") {
    return {};
  }
  if (st.phase != "explore" && st.phase != "explore_b") {
    return {};
  }
  if (!st.has_pack) {
    // explore_b pre-pack: auto-plan / loci plan — never soft-nudge classic early plan.
    if (st.phase == "explore_b") {
      return {};
    }
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
  // Pack already covers Instruction: extras should be refetch tips, then next phase.
  if (st.pack_incomplete) {
    return {};
  }
  st.post_pack_tool_count += tools_added;
  if (!st.edit_nudge_sent && st.post_pack_tool_count >= kPostPackEditNudgeAfter) {
    st.edit_nudge_sent = true;
    std::ostringstream nudge;
    nudge << "_nudge:_ Llevas " << st.post_pack_tool_count
          << " tools tras pack (Instruction cubierta). ";
    const AiWorkflowKind wf = parse_ai_workflow_kind(st.workflow);
    if (ai_workflow_is_readonly(wf)) {
      if (wf == AiWorkflowKind::Plan) {
        nudge << "Emite `action=synthesize` con el plan (pasos, archivos, riesgos).\n\n";
      } else {
        nudge << "Emite `action=synthesize` con la explicación/respuesta al usuario.\n\n";
      }
    } else {
      nudge << "Emite `done next=edit` o `edit` "
               "(refetch solo gaps / `## Truncated` tip; no shotgun).\n\n";
    }
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
  session = head + trim_observations_section(session.substr(obs_pos), budget_.obs_packed);
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

  const AiWorkflowKind workflow = parse_ai_workflow_kind(opts.workflow);
  const std::string workflow_name = ai_workflow_kind_name(workflow);

  std::optional<ProblemFrame> bootstrap_pf;
  if (a_explore_anchor_causal_enabled()) {
    ProblemFrame pf;
    std::string pferr;
    if (!opts.problem_frame_json.empty() &&
        problem_frame_from_json_string(opts.problem_frame_json, &pf, &pferr)) {
      pf.provenance = "manual";
    } else if (!opts.distilled_intent_json.empty() &&
               problem_frame_from_json_string(opts.distilled_intent_json, &pf, &pferr)) {
      pf.provenance = "l1_distill";
    } else {
      pf = problem_frame_fallback_from_query(opts.query);
    }
    if (pf.instruction.empty()) {
      pf.instruction = opts.query;
    }
    problem_frame_refine_from_query(&pf, opts.query);
    std::string pfsave;
    save_problem_frame(opts.workspace_root, pf, &pfsave);
    bootstrap_pf = std::move(pf);
  }

  std::ostringstream md;
  md << "# L2 session\n\n";
  // Tool guide lives only in the L2 system prompt (avoid duplicating ~1.3k chars into n_ctx).
  md << "## Instruction\n\n";
  md << "workflow: " << workflow_name << "\n\n";
  md << "query: " << opts.query << "\n\n";
  if (!opts.instruction.empty()) {
    md << "instruction: " << opts.instruction << "\n\n";
  }
  {
    const std::string scope_line = ai_path_scope_prompt_line(opts.path_scope);
    if (!scope_line.empty()) {
      md << scope_line << "\n\n";
    }
  }
  if (!opts.seeds.empty()) {
    md << "seeds:";
    for (const auto& s : opts.seeds) {
      md << ' ' << s;
    }
    md << "\n\n";
  }
  if (!opts.distilled_intent_json.empty()) {
    md << "## Distilled intent\n\n```json\n" << trim_ws(opts.distilled_intent_json) << "\n```\n\n";
  }
  if (bootstrap_pf) {
    md << "## Problem frame\n\n```json\n" << problem_frame_to_json(*bootstrap_pf).dump(2)
       << "\n```\n\n";
  }
  if (map_stale) {
    md << "**map_stale=1**: el mapa rankeado parece de otra query (`" << map_query
       << "`; overlap=" << static_cast<int>(overlap * 100)
       << "%). No se inyecta el mapa completo. Usa `search` / `plan` anclado a esta "
          "Instruction.\n\n";
    if (l2_feat::enabled("MAP_STALE_NUDGE")) {
      md << "_nudge:_ map_stale activo — primera acción debe ser `action=plan` con "
            "`path:Symbol` tomados de la Instruction (paths `src/...` explícitos), no "
            "`done next=clarify`.\n\n";
    }
  }
  if (workflow == AiWorkflowKind::Ask) {
    md << "Fase inicial: **explore** (workflow=ask). Preferir `action=plan` → pack, luego "
          "`action=synthesize` con la explicación. **PROHIBIDO** edit/compile.\n\n";
  } else if (workflow == AiWorkflowKind::Plan) {
    md << "Fase inicial: **explore** (workflow=plan). Preferir `action=plan` → pack, luego "
          "`action=synthesize` con un plan de cambios (archivos, pasos, riesgos). "
          "**PROHIBIDO** edit/compile.\n\n";
  } else if (workflow == AiWorkflowKind::Git) {
    md << "Fase inicial: **explore** (workflow=git). Tienes ## Git context. Puedes "
          "`action=plan`/tools si necesitas código actual, o `action=synthesize` directo "
          "para resumir qué cambió. **PROHIBIDO** edit/compile.\n\n";
  } else if (l2_feat::enabled("L2_EXPLORE_PHASE_A")) {
    if (a_explore_anchor_causal_enabled()) {
      md << "Fase inicial: **explore_a / F1 anchor hunt**. PROHIBIDO `plan`/trail/dataflow. "
            "Juzga fichas (A0) → peek (A1) → `f1_done` (1 primary) o `anchor_miss_v1`.\n\n";
    } else {
      md << "Fase inicial: **explore_a** (localización). PROHIBIDO `plan`/tools/pack. "
            "Juzga peeks con `a_judge` → cierra con `a_done` (`loci[]`) → **explore_b** "
            "materializa pack desde loci. Sin caza libre multi-stem.\n\n";
    }
  } else {
    md << "Fase inicial: **explore**. Preferir `action=plan` en el **primer** paso con "
          "4–8 targets `path:Symbol`/`path:line` (evitar path bare). Máx. ~8 tools sueltos "
          "antes del primer plan. Tras pack cubierto: extras con `tools` batch (máx. 4); "
          "luego `{\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"}` o `edit` directo. "
          "Truncado ≠ bloqueo de edit si no hay gaps Instruction.\n\n";
  }
  if (!opts.seed_pack_markdown.empty()) {
    md << "**insert_pack=1**: `pack.md` ya incluye el cuerpo de la función ancla y su "
          "header. Puedes explorar el ## Ranked map / tools si necesitas más contexto; "
          "luego edita en el locus de la Instruction.\n\n";
  }
  if (workflow == AiWorkflowKind::Git && !opts.git_context.empty()) {
    md << "## Git context\n\n";
    md << opts.git_context;
    if (!opts.git_context.empty() && opts.git_context.back() != '\n') {
      md << '\n';
    }
    md << '\n';
  }
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
  const bool phase_a = l2_feat::enabled("L2_EXPLORE_PHASE_A");
  st.phase = phase_a ? "explore_a" : "explore";
  st.workflow = workflow_name;
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
  st.pack_review_ok = false;
  st.pack_review_cycles = 0;
  st.review_search_terms.clear();
  st.rejected_targets.clear();
  st.edit_fail_count = 0;
  st.identical_edit_repeats = 0;
  st.last_failed_edit_fp.clear();
  st.map_stale = map_stale;
  st.map_review = false;
  st.continuable = false;
  st.resume = false;
  st.followup_count = 0;
  st.watchlist.clear();
  // Hard reset prior pack so plan1 cannot merge stale targets (path:Symbol del prompt viejo).
  {
    std::string pack_err;
    if (!opts.seed_pack_markdown.empty()) {
      if (!write_file(pack_path(opts.workspace_root), opts.seed_pack_markdown, &pack_err)) {
        if (err_out && err_out->empty()) {
          *err_out = pack_err.empty() ? "no se pudo escribir pack.md (seed)" : pack_err;
        }
        return false;
      }
      st.has_pack = true;
      st.watchlist = opts.seeds;
    } else if (!phase_a) {
      write_file(pack_path(opts.workspace_root),
                 "# L2 code pack\n\n_(vacío — bootstrap; sin plan aún)_\n", &pack_err);
    } else {
      // Phase A: no pack.md until explore_b / plan.
      std::error_code pec;
      fs::remove(pack_path(opts.workspace_root), pec);
    }
  }
  if (!save_state(opts.workspace_root, st, err_out)) {
    return false;
  }

  if (phase_a) {
    AState ast;
    if (!map_stale) {
      tuide::AQueueMapFilterOpts fopts;
      fopts.want_n = 80;
      fopts.orphans = opts.seeds;
      const auto inputs =
          tuide::a_queue_inputs_from_ranked_map_filtered(map_body, fopts, opts.workspace_root);
      if (!inputs.empty()) {
        a_state_seed_queue(&ast, inputs, {});
      }
    }
    if (a_effect_summary_enabled()) {
      ast.a_subphase = "a0_sniff";
      ast.seeds = opts.seeds;
      if (ast.seeds.empty() && !opts.query.empty()) {
        ast.seeds.push_back(opts.query);
      }
    }
    if (a_explore_anchor_causal_enabled() && bootstrap_pf) {
      const auto anchor_seeds = problem_frame_anchor_seeds(*bootstrap_pf);
      if (!anchor_seeds.empty()) {
        ast.seeds = anchor_seeds;
      }
      ast.explore_mode = "f1_anchor";
      ast.a_subphase = "a0_sniff";
      a_apply_f1_anchor_queue_filter(&ast, *bootstrap_pf);
    }
    std::string aerr;
    if (!save_a_state(opts.workspace_root, ast, &aerr)) {
      if (err_out && err_out->empty()) {
        *err_out = aerr.empty() ? "no se pudo escribir a_state.json" : aerr;
      }
      return false;
    }
    write_file(a_notes_path(opts.workspace_root), a_notes_markdown(ast), nullptr);
  }

  write_file(request_path(opts.workspace_root),
             "{\n  \"action\": \"tool\",\n  \"name\": \"get_code_of\",\n  \"arg\": \"\"\n}\n",
             nullptr);
  write_response_json(opts.workspace_root, true, "bootstrap", "", "", "session ready", "", 0,
                      st.phase);
  append_trace(opts.workspace_root,
               std::string("{\"ts\":") + now_ms_str() +
                   ",\"event\":\"bootstrap\",\"query\":\"" + json_escape(opts.query) +
                   "\",\"phase\":\"" + st.phase + "\",\"workflow\":\"" + workflow_name +
                   "\",\"map_stale\":" + (map_stale ? "1" : "0") +
                   ",\"map_overlap\":" + std::to_string(overlap) + "}");
  ai_trace(AiTraceChannel::L2, "l2_bootstrap",
           std::string("{\"path\":\"") + json_escape(session_path(opts.workspace_root)) +
               "\",\"workflow\":\"" + workflow_name + "\",\"map_stale\":" +
               (map_stale ? "1" : "0") + ",\"phase\":\"" + st.phase + "\"}");
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
  if (st.phase == "explore_a") {
    out.error = "explore_a: peeks son runtime; usa a_judge/a_done (no tools)";
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }
  if (st.phase != "explore" && st.phase != "explore_b" && st.phase != "edit") {
    out.error = "tools solo en phase explore|explore_b|edit (ahora=" + st.phase + ")";
    write_response_json(workspace_root, false, "error", name, arg, "", out.error, st.turn, st.phase);
    return out;
  }
  if (!tool_allowed_in_phase(name, st.phase)) {
    out.error = std::string("tool no permitido en ") + st.phase + ": " + name +
                (is_lsp_locate_tool(name) ? " (LSP fuera del hot path locate)" : "");
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

  const std::string call_key = tool_call_key(name, arg);
  const bool is_code_fetch = name == "get_code_of" || name == "read_file";
  if (is_code_fetch && seen_tool_key(st.seen_tool_keys, call_key)) {
    ++st.turn;
    const bool pack_covered = st.has_pack && !st.pack_incomplete;
    st.last_action = pack_covered ? "post_pack_tool_pushback" : "repeated_tool";
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — repeated_tool `" << name << "` `" << arg << "`\n\n";
    block << "Rechazado: ya leíste `" << arg
          << "`. No repitas el mismo get_code_of. ";
    if (pack_covered) {
      block << "Pack cubierto: emite `{\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"}` "
               "o **`action=edit`**. Si falta un hueco, usa un arg DISTINTO (`path:A-B`).\n\n";
    } else {
      block << "Usa un arg distinto (`path:A-B` / otro símbolo) o `action=plan`.\n\n";
    }
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);
    if (pack_covered) {
      st.post_pack_tool_count += 1;
      st.edit_nudge_sent = true;
    } else if (const std::string nudge = maybe_tool_nudge(st, 1); !nudge.empty()) {
      append_observation(workspace_root, nudge, &out.session_chars, &err);
    }
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "tool", name, arg, block.str(), st.last_action,
                        st.turn, st.phase);
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"repeated_tool\",\"turn\":" +
                                     std::to_string(st.turn) + "}");
    out.ok = true;
    out.phase = st.phase;
    out.summary = st.last_action;
    out.error = st.last_action;
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
  const std::size_t max_chars = st.has_pack ? budget_.obs_per_turn : 0;
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
             "`path:Symbol#mid|#tail`); no inventes el cuerpo. No repitas el mismo arg.\n";
  }
  block << "```\n\n";
  remember_tool_key(&st.seen_tool_keys, call_key);

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
  if (st.phase == "explore_a") {
    out.error = "explore_a: peeks son runtime; usa a_judge/a_done (no tools)";
    return out;
  }
  if (st.phase != "explore" && st.phase != "explore_b" && st.phase != "edit") {
    out.error = "tools solo en phase explore|explore_b|edit (ahora=" + st.phase + ")";
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
    if (!tool_allowed_in_phase(call.name, st.phase)) {
      ++fail_n;
      batch_block << "### tools[" << i << "] `" << call.name << "` — denied\n\n";
      batch_block << "tool no permitido en " << st.phase << ": " << call.name;
      if (is_lsp_locate_tool(call.name)) {
        batch_block << " (LSP fuera del hot path locate)";
      }
      batch_block << "\n\n";
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

    const std::string call_key = tool_call_key(call.name, call.arg);
    const bool is_code_fetch = call.name == "get_code_of" || call.name == "read_file";
    if (is_code_fetch && seen_tool_key(st.seen_tool_keys, call_key)) {
      ++st.turn;
      batch_block << "### turn " << st.turn << " — repeated_tool `" << call.name << "` `"
                  << call.arg << "`\n\n```\nya leíste este arg; no lo repitas. "
                     "Emite edit / done next=edit o un path:A-B distinto.\n```\n\n";
      ++fail_n;
      if (st.has_pack && !st.pack_incomplete) {
        st.last_action = "post_pack_tool_pushback";
        out.error = "post_pack_tool_pushback";
      } else {
        st.last_action = "repeated_tool";
        if (out.error.empty()) {
          out.error = "repeated_tool";
        }
      }
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
        st.has_pack ? budget_.obs_per_turn : 0);
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
      remember_tool_key(&st.seen_tool_keys, call_key);
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

AState Level2Session::load_a_state(const std::string& workspace_root) {
  AState st;
  const std::string raw = read_file(a_state_path(workspace_root));
  if (raw.empty()) {
    return st;
  }
  try {
    const auto j = nlohmann::json::parse(raw);
    std::string err;
    if (!a_state_from_json(j, &st, &err)) {
      return AState{};
    }
  } catch (...) {
    return AState{};
  }
  return st;
}

bool Level2Session::save_a_state(const std::string& workspace_root, const AState& st,
                                 std::string* err) {
  try {
    const std::string body = a_state_to_json(st).dump(2);
    if (!write_file(a_state_path(workspace_root), body, err)) {
      return false;
    }
    return write_file(a_notes_path(workspace_root), a_notes_markdown(st), err);
  } catch (const std::exception& ex) {
    if (err) {
      *err = ex.what();
    }
    return false;
  }
}

Level2TurnResult Level2Session::seed_a_queue(const std::string& workspace_root,
                                             const std::vector<AQueueBuildInput>& ranked,
                                             const AQueueBuildOpts& opts) {
  Level2TurnResult out;
  out.action = "a_seed";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  AState ast = load_a_state(workspace_root);
  a_state_seed_queue(&ast, ranked, opts);
  std::string err;
  if (!save_a_state(workspace_root, ast, &err)) {
    out.error = err.empty() ? "no se pudo guardar a_state" : err;
    return out;
  }
  if (st.phase == "explore" || st.phase.empty()) {
    st.phase = "explore_a";
  }
  st.last_action = "a_seed";
  if (!save_state(workspace_root, st, &err)) {
    out.error = err.empty() ? "no se pudo guardar state" : err;
    return out;
  }
  out.ok = true;
  out.phase = st.phase;
  out.summary = "a_queue n=" + std::to_string(ast.queue.size());
  return out;
}

Level2TurnResult Level2Session::apply_a_judge(const std::string& workspace_root,
                                              const std::vector<AVerdict>& verdicts,
                                              bool turn_done_hint) {
  Level2TurnResult out;
  out.action = "a_judge";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (st.phase != "explore_a" && st.phase != "explore") {
    out.error = "a_judge solo en explore_a";
    return out;
  }
  if (verdicts.empty()) {
    AState ast_early = load_a_state(workspace_root);
    // a1_suspect_vars allows verdicts:[] (= ninguna var clara).
    if (a_effect_summary_enabled() && ast_early.a_subphase == "a1_suspect_vars") {
      // fall through with empty
    } else if (a_effect_summary_enabled() && ast_early.a1_active_set &&
               ast_early.a_subphase.rfind("a1_", 0) == 0) {
      // Bare/empty a_judge in A1 confirm → soft-reject active job and advance.
      AVerdict rej;
      rej.target = ast_early.a1_active.target;
      rej.anchor = ast_early.a1_active.target;
      rej.verdict = AVerdictKind::Reject;
      rej.why = "a_judge vacío — avanza A1";
      a_normalize_verdict(&rej);
      std::vector<AVerdict> syn = {rej};
      return apply_a_judge(workspace_root, syn, turn_done_hint);
    } else {
      out.error = "a_judge.verdicts vacío";
      return out;
    }
  }

  AState ast = load_a_state(workspace_root);

  // --- A0 Effect Summary sniff (fichas, not body peeks) ---
  if (a_in_a0_sniff(ast)) {
    int batch_expand = 0;
    int batch_reject = 0;
    std::vector<AVerdict> normalized;
    normalized.reserve(verdicts.size());
    for (AVerdict v : verdicts) {
      a_normalize_verdict(&v);
      if (v.verdict == AVerdictKind::Useful) {
        v.verdict = AVerdictKind::Expand;
        if (v.expand_with == AExpandModality::None) {
          v.expand_with = AExpandModality::Peek;
        }
      }
      if (v.verdict == AVerdictKind::Expand) {
        ++batch_expand;
        if (v.expand_with == AExpandModality::None) {
          v.expand_with = AExpandModality::Peek;
        }
        v.expand_with = a_coerce_a0_expand_modality(v.target, v.expand_with, nullptr);
        if (a_in_f1_anchor_mode(ast)) {
          v.expand_with = a_f1_coerce_expand_modality(v.expand_with);
        }
      } else if (v.verdict == AVerdictKind::Reject) {
        ++batch_reject;
      }
      normalized.push_back(v);
    }
    if (batch_expand == 0 && batch_reject == 0) {
      out.error = "a0: marca reject en glue o expand si hot/writes/calls cuadra con seeds";
      std::ostringstream obs;
      obs << "### a_judge A0 rechazado — " << out.error << "\n";
      append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
      write_response_json(workspace_root, false, "a_judge", "", "", "", out.error, st.turn,
                          st.phase);
      return out;
    }
    std::string a0err;
    if (!a_apply_a0_verdicts(&ast, normalized, &a0err, &workspace_root)) {
      out.error = a0err.empty() ? "a0_judge inválido" : a0err;
      append_observation(workspace_root, std::string("### a_judge A0 — ") + out.error + "\n",
                         &out.session_chars, nullptr);
      write_response_json(workspace_root, false, "a_judge", "", "", "", out.error, st.turn,
                          st.phase);
      return out;
    }
    const std::string session_body = read_file(session_path(workspace_root));
    const auto needles = session_pack_needles(session_body);
    ast.orphans = a_compute_orphans(ast, needles);
    if (ast.cursor >= static_cast<int>(ast.queue.size()) && ast.a1_queue.empty()) {
      maybe_expand_a_queue(&ast, ast.orphans);
    }
    std::string err;
    if (!save_a_state(workspace_root, ast, &err)) {
      out.error = err.empty() ? "no se pudo guardar a_state" : err;
      return out;
    }
    write_file(a_notes_path(workspace_root), a_notes_markdown_compact(ast), nullptr);
    st.phase = "explore_a";
    st.last_action = "a_judge";
    ++st.turn;
    if (!save_state(workspace_root, st, &err)) {
      out.error = err.empty() ? "no se pudo guardar state" : err;
      return out;
    }
    std::ostringstream obs;
    obs << "### a_judge A0 turn=" << ast.a0_turns << " cards_used=" << ast.cards_used
        << " cursor=" << ast.cursor << "/" << ast.queue.size();
    if (ast.a1_active_set) {
      obs << " → A1 " << a_expand_modality_name(ast.a1_active.modality) << " `"
          << ast.a1_active.target << "`";
    }
    obs << "\n";
    append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
    append_trace(workspace_root,
                 std::string("{\"event\":\"a0_judge\",\"cards_used\":") +
                     std::to_string(ast.cards_used) + ",\"a0_turns\":" +
                     std::to_string(ast.a0_turns) + ",\"expand_queue\":" +
                     std::to_string(ast.a1_queue.size()) + "}");
    out.ok = true;
    out.phase = st.phase;
    out.summary = "a0 expand=" + std::to_string(batch_expand) + " reject=" +
                  std::to_string(batch_reject);
    return out;
  }

  // --- A1 suspect vars (post-trail → dataflow queue) ---
  if (a_effect_summary_enabled() && ast.a_subphase == "a1_suspect_vars") {
    // Empty verdicts = "ninguna var clara" (prompt allows it) → advance without dataflow.
    std::string serr;
    if (!a_apply_a1_suspect_verdicts(&ast, verdicts, &serr)) {
      out.error = serr.empty() ? "a1_suspect inválido" : serr;
      append_observation(workspace_root, std::string("### a_judge A1 suspect — ") + out.error + "\n",
                         &out.session_chars, nullptr);
      write_response_json(workspace_root, false, "a_judge", "", "", "", out.error, st.turn,
                          st.phase);
      return out;
    }
    std::string err;
    if (!save_a_state(workspace_root, ast, &err)) {
      out.error = err.empty() ? "no se pudo guardar a_state" : err;
      return out;
    }
    write_file(a_notes_path(workspace_root), a_notes_markdown_compact(ast), nullptr);
    st.phase = "explore_a";
    st.last_action = "a_judge";
    ++st.turn;
    if (!save_state(workspace_root, st, &err)) {
      out.error = err.empty() ? "no se pudo guardar state" : err;
      return out;
    }
    std::ostringstream obs;
    obs << "### a_judge A1 suspect turn=" << ast.turns;
    if (ast.a1_active_set) {
      obs << " → dataflow `" << ast.a1_active.suspect_var << "` @ `" << ast.a1_active.target
          << "`";
    } else {
      obs << " → sin vars (vuelve A0)";
    }
    obs << "\n";
    append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
    out.ok = true;
    out.phase = st.phase;
    out.summary = ast.a1_active_set ? "a1_suspect→dataflow" : "a1_suspect→a0";
    return out;
  }

  // --- A1 confirm after expand (classic useful allowed) ---
  if (a_effect_summary_enabled() && ast.a1_active_set &&
      ast.a_subphase.rfind("a1_", 0) == 0) {
    // Fall through to classic batch gate below, then clear a1_active at end.
  } else if (a_effect_summary_enabled() && !ast.a_subphase.empty() &&
             ast.a_subphase.rfind("a1_", 0) == 0) {
    ast.a_subphase = "a0_sniff";
  }

  // Strict batch gate: 7B loves "everything useful". Soft-cap to 1 useful.
  // Peek "interesting" ≡ useful (hypothesis → trail); trail stacks use a_trail_judge.
  std::vector<AVerdict> batch_verdicts;
  batch_verdicts.reserve(verdicts.size());
  {
    int batch_useful = 0;
    int batch_reject = 0;
    for (AVerdict v : verdicts) {
      a_normalize_verdict(&v);
      if (v.verdict == AVerdictKind::Interesting) {
        v.verdict = AVerdictKind::Useful;
      }
      batch_verdicts.push_back(v);
      if (v.verdict == AVerdictKind::Useful) {
        ++batch_useful;
      } else if (v.verdict == AVerdictKind::Reject) {
        ++batch_reject;
      }
    }
    const bool multi = static_cast<int>(batch_verdicts.size()) >= 2;
    if (batch_useful > 1) {
      std::vector<std::size_t> useful_idx;
      for (std::size_t i = 0; i < batch_verdicts.size(); ++i) {
        if (batch_verdicts[i].verdict == AVerdictKind::Useful) {
          useful_idx.push_back(i);
        }
      }
      std::stable_sort(useful_idx.begin(), useful_idx.end(), [&](std::size_t a, std::size_t b) {
        return a_queue_item_score(ast, batch_verdicts[a].target) >
               a_queue_item_score(ast, batch_verdicts[b].target);
      });
      for (std::size_t k = 1; k < useful_idx.size(); ++k) {
        batch_verdicts[useful_idx[k]].verdict = AVerdictKind::Reject;
        ++batch_reject;
      }
      batch_useful = 1;
    } else if (multi && batch_useful == 0 && batch_reject == 0) {
      out.error =
          "a_judge: todos uncertain no avanza — marca reject en traps claros "
          "o 1 useful si hay estado/API del síntoma (getter/flag OK).";
    }
    if (!out.error.empty()) {
      std::ostringstream obs;
      obs << "### a_judge rechazado — " << out.error << "\n"
          << "_nudge:_ Misma tranche; reemite a_judge. useful abre trail, no corona edit site.\n";
      append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
      write_response_json(workspace_root, false, "a_judge", "", "", "", out.error, st.turn,
                          st.phase);
      return out;
    }
  }

  const bool a1_confirm = a_effect_summary_enabled() && ast.a1_active_set;
  const int tranche =
      std::min(kAMaxPeeksPerTurn, std::max(0, static_cast<int>(ast.queue.size()) - ast.cursor));
  int primary_n = 0;
  for (const auto& loc : ast.loci_draft) {
    if (loc.role == ALocusRole::Primary) {
      ++primary_n;
    }
  }
  for (AVerdict v : batch_verdicts) {
    a_normalize_verdict(&v);
    if (v.verdict == AVerdictKind::Interesting) {
      v.verdict = AVerdictKind::Useful;
    }
    ast.notes.push_back(v);
    if (v.verdict == AVerdictKind::Reject && !v.stem.empty()) {
      if (std::find(ast.rejected_stems.begin(), ast.rejected_stems.end(), v.stem) ==
          ast.rejected_stems.end()) {
        ast.rejected_stems.push_back(v.stem);
      }
    }
    if (v.verdict == AVerdictKind::Useful) {
      ALocus loc;
      loc.stem = v.stem;
      loc.anchor = v.anchor.empty() ? v.target : v.anchor;
      loc.role = ALocusRole::Suspect;  // hypothesis until trail confirms
      loc.why = v.why;
      a_normalize_locus(&loc);
      if (!a_anchor_resolvable(loc.anchor)) {
        continue;
      }
      if (a_in_f1_anchor_mode(ast)) {
        // F1: peek useful confirms anchor candidate — no call-stack trail.
        loc.role = ALocusRole::Suspect;
        bool dup = false;
        for (auto& existing : ast.loci_draft) {
          if (existing.anchor == loc.anchor ||
              (!loc.stem.empty() && existing.stem == loc.stem)) {
            dup = true;
            break;
          }
        }
        if (!dup) {
          ast.loci_draft.push_back(std::move(loc));
        }
        continue;
      }
      // Start (or replace) call-hierarchy trail — do not crown primary yet.
      if (!ast.trail.active) {
        a_trail_begin(&ast, v);
        std::string terr;
        refresh_a_trail_stacks(workspace_root, &ast, &terr);
        if (!terr.empty()) {
          append_trace(workspace_root,
                       std::string("{\"event\":\"a_trail_refresh\",\"ok\":0,\"err\":\"") +
                           json_escape(terr) + "\"}");
        } else {
          append_trace(workspace_root,
                       std::string("{\"event\":\"a_trail_begin\",\"root\":\"") +
                           json_escape(ast.trail.root_anchor) + "\",\"stacks\":" +
                           std::to_string(ast.trail.pending_stacks.size()) + "}");
        }
      }
      bool dup = false;
      for (auto& existing : ast.loci_draft) {
        if (existing.anchor == loc.anchor ||
            (!loc.stem.empty() && existing.stem == loc.stem)) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        ast.loci_draft.push_back(std::move(loc));
      }
    }
  }
  a_cap_locus_roles(&ast.loci_draft);
  ast.peeks_used += std::max(tranche, static_cast<int>(verdicts.size()));
  ast.cursor = std::min(static_cast<int>(ast.queue.size()), ast.cursor + std::max(tranche, 1));
  ++ast.turns;

  // Early-stop hint: enough useful with contrast, or budgets exhausted.
  int useful = 0;
  int reject = 0;
  for (const auto& n : ast.notes) {
    if (n.verdict == AVerdictKind::Useful) {
      ++useful;
    } else if (n.verdict == AVerdictKind::Reject) {
      ++reject;
    }
  }

  // P3: orphans + expand when queue exhausted / weak A.
  const std::string session_body = read_file(session_path(workspace_root));
  const auto needles = session_pack_needles(session_body);
  ast.orphans = a_compute_orphans(ast, needles);
  AExpandResult exp;
  const bool queue_done = ast.cursor >= static_cast<int>(ast.queue.size());
  if ((queue_done || (useful == 0 && ast.turns >= 3)) && useful < 2) {
    exp = maybe_expand_a_queue(&ast, ast.orphans);
    if (exp.expanded) {
      append_trace(workspace_root,
                   std::string("{\"event\":\"a_expand\",\"layer\":") + std::to_string(exp.layer) +
                       ",\"added\":" + std::to_string(exp.added) + ",\"expansions\":" +
                       std::to_string(ast.expansions) + ",\"reason\":\"" +
                       json_escape(exp.reason) + "\"}");
    }
  }

  const bool budget_hit = a_budget_relaxed(ast);
  const bool stable = useful >= 1 && reject >= 1 && primary_n >= 1 && primary_n <= kAMaxPrimaryLoci;
  if ((turn_done_hint && useful >= 1 && reject >= 1) || stable || (budget_hit && useful >= 1)) {
    // Soft: model should emit a_done next. Do not auto-promote without loci.
  }

  std::string err;
  if (!save_a_state(workspace_root, ast, &err)) {
    out.error = err.empty() ? "no se pudo guardar a_state" : err;
    return out;
  }
  st.phase = "explore_a";
  st.last_action = "a_judge";
  ++st.turn;
  if (!save_state(workspace_root, st, &err)) {
    out.error = err.empty() ? "no se pudo guardar state" : err;
    return out;
  }

  // Observation: notes only (no peek bodies).
  std::ostringstream obs;
  obs << "### a_judge turn=" << ast.turns << " peeks_used=" << ast.peeks_used
      << " cursor=" << ast.cursor << "/" << ast.queue.size() << "\n";
  for (const auto& v : batch_verdicts) {
    AVerdict nv = v;
    a_normalize_verdict(&nv);
    obs << "- [" << a_verdict_kind_name(nv.verdict) << "] `" << nv.target << "`";
    if (!nv.why.empty()) {
      obs << " — " << nv.why;
    }
    obs << "\n";
  }
  if (exp.expanded) {
    obs << "_nudge:_ expansión capa " << exp.layer << " +" << exp.added
        << " candidatos (" << exp.reason << "). Sigue con a_judge sobre los peeks nuevos.\n";
  } else if (a_in_f1_anchor_mode(ast) && useful >= 1) {
    for (const auto& v : batch_verdicts) {
      AVerdict nv = v;
      a_normalize_verdict(&nv);
      if (nv.verdict == AVerdictKind::Useful) {
        obs << "_nudge F1:_ peek useful en `" << nv.target
            << "`. Emite {\"action\":\"f1_done\",\"loci\":[{\"stem\":\"…\",\"anchor\":\""
            << nv.target << "\",\"role\":\"primary\",\"why\":\"…\"}]}. PROHIBIDO trail/a_done.\n";
        break;
      }
    }
  } else if (ast.expand_exhausted && useful == 0) {
    obs << "_nudge:_ expansión agotada sin useful. Emite "
           "{\"action\":\"done\",\"summary\":\"A sin locus; orphans="
        << ast.orphans.size() << "\",\"next\":\"clarify\"} o a_done si hay suspect débil.\n";
  } else if (useful >= 2 && reject == 0) {
    obs << "_nudge:_ varios useful sin reject — el trail falsifica; sigue con a_trail_judge "
           "o a_done con ≤"
        << kAMaxPrimaryLoci << " primary cuando un hop sea el edit site.\n";
  } else if (stable) {
    obs << "_nudge:_ contraste OK (useful+reject). ";
    if (ast.trail.active) {
      obs << "Trail activa — emite `a_trail_judge` (interesting|reject) sobre ramas "
             "`ON`|`CXL`|`OFF`|`LINK` y/o pilas S*, "
             "no a_done todavía.\n";
    } else {
      obs << "Emite `a_done` con ≤" << kAMaxPrimaryLoci
          << " primary cuando el trail/hop diga dónde editar. Phase B trae el barrio.\n";
    }
  } else if (ast.trail.active) {
    obs << "_nudge:_ useful = hipótesis. Revisa call-stacks con `a_trail_judge` "
           "(interesting|reject). Si todos reject → L0 se invalida.\n";
  }
  append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);

  if (a1_confirm) {
    bool df_backtrack = false;
    if (ast.a_subphase == "a1_dataflow" && ast.trail.active && !ast.a1_trail_recap.empty()) {
      for (const auto& v : batch_verdicts) {
        AVerdict nv = v;
        a_normalize_verdict(&nv);
        if (nv.verdict == AVerdictKind::Reject) {
          df_backtrack = true;
          break;
        }
      }
    }
    if (df_backtrack) {
      a_a1_backtrack_to_trail(&ast);
      save_a_state(workspace_root, ast, nullptr);
      append_observation(workspace_root,
                         "_nudge:_ dataflow reject — trail reabierta; profundiza pila "
                         "interesting o elige otra var.\n",
                         &out.session_chars, nullptr);
      out.summary += " a1_df_reject→trail";
    } else {
      ast.a1_active_set = false;
      ast.a1_active = {};
      if (!ast.a1_queue.empty()) {
        a_a1_begin_job(&ast, ast.a1_queue.front());
        ast.a1_queue.erase(ast.a1_queue.begin());
      } else {
        a_a1_clear_trail_frame(&ast);
        ast.a_subphase = "a0_sniff";
      }
      save_a_state(workspace_root, ast, nullptr);
    }
  }

  out.ok = true;
  out.phase = st.phase;
  out.summary = "a_judge useful=" + std::to_string(useful) + " reject=" + std::to_string(reject) +
                " loci_draft=" + std::to_string(ast.loci_draft.size());
  if (exp.expanded) {
    out.summary += " expand_L" + std::to_string(exp.layer) + "=+" + std::to_string(exp.added);
  }
  return out;
}

Level2TurnResult Level2Session::apply_a_done(const std::string& workspace_root,
                                             const std::vector<ALocus>& loci,
                                             const std::string& summary) {
  Level2TurnResult out;
  out.action = "a_done";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (loci.empty()) {
    out.error = "a_done.loci vacío";
    return out;
  }

  AState ast = load_a_state(workspace_root);
  std::vector<ALocus> ordered;
  ordered.reserve(loci.size());
  for (ALocus loc : loci) {
    a_normalize_locus(&loc);
    if (!a_anchor_resolvable(loc.anchor)) {
      continue;
    }
    // Prefer draft locus if model sent swapped fields but draft is clean.
    for (const auto& draft : ast.loci_draft) {
      if (draft.stem == loc.stem && a_anchor_resolvable(draft.anchor) &&
          !a_anchor_resolvable(loc.anchor)) {
        loc.anchor = draft.anchor;
      }
      if ((draft.anchor == loc.anchor || draft.stem == loc.stem) && loc.why.empty()) {
        loc.why = draft.why;
      }
    }
    ordered.push_back(std::move(loc));
  }
  // Dedupe by stem keeping first (must-ordered later).
  {
    std::vector<ALocus> dedup;
    std::unordered_set<std::string> seen_stem;
    for (auto& loc : ordered) {
      if (!loc.stem.empty() && !seen_stem.insert(loc.stem).second) {
        continue;
      }
      dedup.push_back(std::move(loc));
    }
    ordered = std::move(dedup);
  }
  ordered = a_loci_must_ordered(std::move(ordered));

  std::string gate_err;
  if (!a_validate_a_done(ast, ordered, &gate_err)) {
    // Soft demote excess primary once and re-validate (model often marks all primary).
    a_cap_locus_roles(&ordered);
    gate_err.clear();
    if (!a_validate_a_done(ast, ordered, &gate_err)) {
      out.error = gate_err;
      std::ostringstream obs;
      obs << "### a_done rechazado — " << gate_err << "\n"
          << "_nudge:_ useful = “editaría aquí para el bug de Instruction”. "
             "Glue/UI no causal / keyword irrelevante → reject. Máx "
          << kAMaxPrimaryLoci << " primary. Phase B trae complementarios.\n";
      append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
      write_response_json(workspace_root, false, "a_done", "", "", "", out.error, st.turn,
                          st.phase);
      return out;
    }
  }

  ast.loci_draft = ordered;
  ast.done = true;
  std::string err;
  if (!save_a_state(workspace_root, ast, &err)) {
    out.error = err.empty() ? "no se pudo guardar a_state" : err;
    return out;
  }

  // Seed watchlist from loci anchors (must-tier: primary first). No pack.md yet.
  st.watchlist.clear();
  for (const auto& loc : ordered) {
    if (!loc.anchor.empty() &&
        (loc.role == ALocusRole::Primary || loc.role == ALocusRole::Secondary)) {
      st.watchlist.push_back(loc.anchor);
    }
  }
  st.phase = "explore_b";
  st.last_action = "a_done";
  ++st.turn;
  if (!save_state(workspace_root, st, &err)) {
    out.error = err.empty() ? "no se pudo guardar state" : err;
    return out;
  }

  std::ostringstream obs;
  obs << "### a_done → explore_b loci=" << ordered.size() << "\n";
  if (!summary.empty()) {
    obs << summary << "\n";
  }
  for (const auto& loc : ordered) {
    obs << "- [" << a_locus_role_name(loc.role) << "] `" << loc.anchor << "`";
    if (!loc.stem.empty()) {
      obs << " stem=" << loc.stem;
    }
    if (!loc.why.empty()) {
      obs << " — " << loc.why;
    }
    obs << "\n";
  }
  obs << "_nudge:_ Phase B — emite action=plan con targets de loci (must-tier) o el runtime "
         "auto-planeará desde watchlist. PROHIBIDO planear stems fuera de loci sin miss.\n";
  append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);

  out.ok = true;
  out.phase = "explore_b";
  out.summary = summary.empty() ? ("loci=" + std::to_string(ordered.size())) : summary;
  return out;
}

Level2TurnResult Level2Session::apply_f1_done(const std::string& workspace_root,
                                              const std::vector<ALocus>& loci,
                                              const std::string& summary) {
  Level2TurnResult out;
  out.action = "f1_done";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (loci.empty()) {
    out.error = "f1_done.loci vacío";
    return out;
  }

  AState ast = load_a_state(workspace_root);
  std::vector<ALocus> ordered;
  ordered.reserve(loci.size());
  for (ALocus loc : loci) {
    a_normalize_locus(&loc);
    if (!a_anchor_resolvable(loc.anchor)) {
      continue;
    }
    if (loc.role == ALocusRole::Unknown) {
      loc.role = ALocusRole::Primary;
    }
    ordered.push_back(std::move(loc));
  }
  a_cap_locus_roles(&ordered);

  std::string gate_err;
  if (!a_validate_f1_anchor_done(ast, ordered, &gate_err)) {
    out.error = gate_err;
    std::ostringstream obs;
    obs << "### f1_done rechazado — " << gate_err << "\n"
        << "_nudge:_ confirma ancla primaria con peek useful; 1 primary; ≥1 reject en "
           "competidores.\n";
    append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
    write_response_json(workspace_root, false, "f1_done", "", "", "", out.error, st.turn,
                        st.phase);
    return out;
  }

  ast.loci_draft = ordered;
  for (const auto& loc : ordered) {
    if (loc.role == ALocusRole::Primary) {
      ast.anchor_confirmed = loc.anchor;
      break;
    }
  }
  ast.anchor_understanding = summary;
  ast.done = true;
  std::string err;
  if (!save_a_state(workspace_root, ast, &err)) {
    out.error = err.empty() ? "no se pudo guardar a_state" : err;
    return out;
  }

  st.watchlist.clear();
  for (const auto& loc : ordered) {
    if (!loc.anchor.empty()) {
      st.watchlist.push_back(loc.anchor);
    }
  }
  st.phase = "explore_f1_ok";
  st.last_action = "f1_done";
  ++st.turn;
  if (!save_state(workspace_root, st, &err)) {
    out.error = err.empty() ? "no se pudo guardar state" : err;
    return out;
  }

  std::ostringstream obs;
  obs << "### f1_done anchor=`" << ast.anchor_confirmed << "`\n";
  if (!summary.empty()) {
    obs << summary << "\n";
  }
  append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
  out.ok = true;
  out.phase = st.phase;
  out.summary = summary.empty() ? ast.anchor_confirmed : summary;
  return out;
}

Level2TurnResult Level2Session::apply_anchor_miss(const std::string& workspace_root,
                                                  const std::string& reason,
                                                  const std::vector<std::string>& candidates,
                                                  bool retrieval_needed,
                                                  const std::string& summary) {
  Level2TurnResult out;
  out.action = "anchor_miss_v1";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  AState ast = load_a_state(workspace_root);
  ast.f1_failure_reason = reason.empty() ? "anchor_miss" : reason;
  ast.done = true;
  std::string err;
  if (!save_a_state(workspace_root, ast, &err)) {
    out.error = err.empty() ? "no se pudo guardar a_state" : err;
    return out;
  }
  st.phase = retrieval_needed ? "explore_f1_retrieval" : "explore_f1_miss";
  st.last_action = "anchor_miss_v1";
  ++st.turn;
  if (!save_state(workspace_root, st, &err)) {
    out.error = err.empty() ? "no se pudo guardar state" : err;
    return out;
  }
  std::ostringstream obs;
  obs << "### anchor_miss_v1 reason=" << ast.f1_failure_reason;
  if (retrieval_needed) {
    obs << " retrieval_needed=true";
  }
  obs << "\n";
  if (!summary.empty()) {
    obs << summary << "\n";
  }
  for (const auto& c : candidates) {
    obs << "- candidate: `" << c << "`\n";
  }
  append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
  out.ok = true;
  out.phase = st.phase;
  out.summary = summary.empty() ? ast.f1_failure_reason : summary;
  return out;
}

Level2TurnResult Level2Session::allow_micro_a_paths(const std::string& workspace_root,
                                                    const std::vector<std::string>& paths) {
  Level2TurnResult out;
  out.action = "micro_a_allow";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  AState ast = load_a_state(workspace_root);
  int added = 0;
  for (const auto& p : paths) {
    if (p.empty()) {
      continue;
    }
    if (std::find(ast.b_allow_paths.begin(), ast.b_allow_paths.end(), p) !=
        ast.b_allow_paths.end()) {
      continue;
    }
    ast.b_allow_paths.push_back(p);
    ++added;
    if (static_cast<int>(ast.b_allow_paths.size()) >= 12) {
      break;
    }
  }
  std::string err;
  if (!save_a_state(workspace_root, ast, &err)) {
    out.error = err.empty() ? "no se pudo guardar a_state" : err;
    return out;
  }
  // Optionally reopen a light locate if still in explore_b with miss.
  if (st.phase == "explore_b" && added > 0) {
    st.last_action = "micro_a_allow";
    save_state(workspace_root, st, nullptr);
  }
  out.ok = true;
  out.phase = st.phase;
  out.summary = "micro_a allow +" + std::to_string(added) +
                " total=" + std::to_string(ast.b_allow_paths.size());
  return out;
}

namespace {

std::vector<ATrailSearchHit> parse_search_tool_hits(const std::string& body,
                                                    const std::string& workspace_root) {
  std::vector<ATrailSearchHit> hits;
  std::istringstream in(body);
  std::string line;
  while (std::getline(in, line)) {
    // Formats: "path:line:preview" or "/abs/path:line:preview"
    if (line.size() < 5) {
      continue;
    }
    // Find first :digits:
    std::size_t colon1 = line.find(':');
    if (colon1 == std::string::npos) {
      continue;
    }
    std::size_t colon2 = line.find(':', colon1 + 1);
    if (colon2 == std::string::npos) {
      continue;
    }
    std::string path = line.substr(0, colon1);
    std::string line_s = line.substr(colon1 + 1, colon2 - colon1 - 1);
    bool digits = !line_s.empty();
    for (char c : line_s) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        digits = false;
        break;
      }
    }
    if (!digits) {
      continue;
    }
    ATrailSearchHit h;
    h.path = path;
    h.line = std::atoi(line_s.c_str());
    h.preview = line.substr(colon2 + 1);
    if (!workspace_root.empty() && h.path.size() > workspace_root.size() &&
        h.path.compare(0, workspace_root.size(), workspace_root) == 0 &&
        (h.path[workspace_root.size()] == '/' || h.path[workspace_root.size()] == '\\')) {
      h.path = h.path.substr(workspace_root.size() + 1);
    }
    // Strip leading "./"
    if (h.path.rfind("./", 0) == 0) {
      h.path = h.path.substr(2);
    }
    hits.push_back(std::move(h));
  }
  return hits;
}

}  // namespace

bool Level2Session::refresh_a_trail_stacks(const std::string& workspace_root, AState* ast,
                                           std::string* err) {
  if (ast == nullptr || !ast->trail.active) {
    if (err) {
      *err = "trail inactiva";
    }
    return false;
  }
  std::string focus_sym = ast->trail.focus_symbol;
  std::string focus_path;
  {
    const auto colon = ast->trail.focus_anchor.rfind(':');
    if (colon != std::string::npos) {
      focus_path = ast->trail.focus_anchor.substr(0, colon);
    }
  }
  if (focus_sym.empty()) {
    if (err) {
      *err = "focus_symbol vacío";
    }
    return false;
  }

  auto search_fn = [&](const std::string& symbol) -> std::vector<ATrailSearchHit> {
    if (symbol.empty() || deps_.tools == nullptr || !deps_.tools->has("search")) {
      return {};
    }
    auto hits_from = [&](const AiToolResult& tr) -> std::vector<ATrailSearchHit> {
      if (!tr.ok || tr.text.find("(sin hits)") != std::string::npos) {
        return {};
      }
      return parse_search_tool_hits(tr.text, workspace_root);
    };
    // Prefer src/; treat empty/(sin hits) as miss and fall back (tool always returns ok).
    auto hits = hits_from(deps_.tools->invoke("search", symbol + " path:src/"));
    if (hits.empty()) {
      hits = hits_from(deps_.tools->invoke("search", symbol));
    }
    return hits;
  };

  ast->trail.pending_stacks =
      a_trail_build_full_stacks(workspace_root, focus_sym, focus_path, search_fn,
                                kATrailMaxStacks, kATrailMaxDepth);
  ast->trail.cond_branches =
      a_trail_build_cond_branches(workspace_root, focus_sym, focus_path, ast->seeds, search_fn,
                                  ast->trail.pending_stacks);
  ast->trail.awaiting_judge = true;
  append_trace(workspace_root,
               std::string("{\"event\":\"a_trail_refresh\",\"sym\":\"") +
                   json_escape(focus_sym) + "\",\"stacks\":" +
                   std::to_string(ast->trail.pending_stacks.size()) + ",\"cond\":" +
                   std::to_string(ast->trail.cond_branches.size()) + "}");
  return true;
}

Level2TurnResult Level2Session::apply_a_trail_judge(const std::string& workspace_root,
                                                    const std::vector<AVerdict>& verdicts) {
  Level2TurnResult out;
  out.action = "a_trail_judge";
  AState ast = load_a_state(workspace_root);
  if (a_in_f1_anchor_mode(ast)) {
    out.error = "a_trail_judge prohibido en F1 anchor hunt";
    return out;
  }
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (st.phase != "explore_a" && st.phase != "explore") {
    out.error = "a_trail_judge solo en explore_a";
    return out;
  }
  std::string err;
  const std::string prev_subphase = ast.a_subphase;
  if (!a_trail_apply_judge(&ast, verdicts, &err)) {
    out.error = err.empty() ? "a_trail_judge inválido" : err;
    std::ostringstream obs;
    obs << "### a_trail_judge rechazado — " << out.error << "\n";
    append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);
    write_response_json(workspace_root, false, "a_trail_judge", "", "", "", out.error, st.turn,
                        st.phase);
    return out;
  }

  // A1 trail → suspect vars before dataflow (one pass).
  // Only matched Interesting on stacks/cond branches counts (not raw symbol-name garbage).
  bool defer_trail_deepen = false;
  if (a_effect_summary_enabled() && prev_subphase == "a1_trail" && !ast.a1_suspect_done) {
    int matched_interesting = 0;
    for (const auto& b : ast.trail.cond_branches) {
      if (b.verdict == AVerdictKind::Interesting) {
        ++matched_interesting;
      }
    }
    for (const auto& s : ast.trail.pending_stacks) {
      if (s.verdict == AVerdictKind::Interesting) {
        ++matched_interesting;
      }
    }
    if (matched_interesting > 0) {
      a_fill_a1_trail_frame(&ast, verdicts);
      ast.a_subphase = "a1_suspect_vars";
      ast.a1_suspect_done = true;
      defer_trail_deepen = true;
    }
  }

  // If trail still active with force_queue: deepen first interesting stack or cond branch
  if (!defer_trail_deepen && ast.trail.active && !ast.trail.force_queue.empty()) {
    const std::string sid = ast.trail.force_queue.front();
    ast.trail.force_queue.erase(ast.trail.force_queue.begin());
    const ATrailStack* chosen = nullptr;
    for (const auto& s : ast.trail.pending_stacks) {
      if (s.id == sid) {
        chosen = &s;
        break;
      }
    }
    const ATrailCondBranch* cbranch = nullptr;
    if (chosen == nullptr) {
      for (const auto& b : ast.trail.cond_branches) {
        if (b.id == sid) {
          cbranch = &b;
          break;
        }
      }
    }
    if (cbranch != nullptr) {
      ATrailHop parent;
      parent.path = cbranch->path;
      parent.symbol = cbranch->symbol;
      parent.anchor = cbranch->anchor.empty()
                          ? (cbranch->path + ":" + (cbranch->symbol.empty() ? cbranch->id
                                                                              : cbranch->symbol))
                          : cbranch->anchor;
      if (parent.symbol.empty() && cbranch->id == "LINK") {
        for (const auto& b : ast.trail.cond_branches) {
          if (b.id == "CXL" && !b.symbol.empty()) {
            parent.symbol = b.symbol;
            if (parent.anchor.find(':') == std::string::npos && !b.path.empty()) {
              parent.anchor = b.path + ":" + b.symbol;
            }
            break;
          }
        }
      }
      parent.summary =
          cbranch->then_text.empty() ? cbranch->id : (cbranch->id + " — " + cbranch->then_text);
      bool dup = false;
      for (const auto& h : ast.trail.trail) {
        if (h.anchor == parent.anchor) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        ast.trail.trail.push_back(parent);
      }
      ast.trail.focus_anchor = parent.anchor;
      ast.trail.focus_symbol =
          parent.symbol.empty() ? ast.trail.focus_symbol : parent.symbol;
      ++ast.trail.depth;
      if (ast.trail.depth >= kATrailMaxDepth) {
        ALocus loc;
        loc.anchor = ast.trail.focus_anchor;
        loc.role = ALocusRole::Primary;
        loc.why = "trail depth cap — rama condicional candidata";
        a_normalize_locus(&loc);
        bool have = false;
        for (auto& d : ast.loci_draft) {
          if (d.anchor == loc.anchor || d.stem == loc.stem) {
            d.role = ALocusRole::Primary;
            have = true;
            break;
          }
        }
        if (!have) {
          ast.loci_draft.push_back(std::move(loc));
        }
        a_cap_locus_roles(&ast.loci_draft);
        ast.trail.awaiting_judge = false;
        ast.trail.force_queue.clear();
        ast.trail.pending_stacks.clear();
      } else {
        refresh_a_trail_stacks(workspace_root, &ast, nullptr);
      }
    } else if (chosen != nullptr && chosen->hops.size() >= 2) {
      // Hop just above L0 (second-to-last) becomes new focus — or frontmost interesting caller
      const ATrailHop& next = chosen->hops.front();
      // Compact previous focus into trail summaries
      if (!ast.trail.trail.empty()) {
        auto& last = ast.trail.trail.back();
        if (last.summary.empty()) {
          last.summary = last.symbol;
        }
      }
      ATrailHop parent = next;
      parent.snippet.clear();  // keep summary only for parents
      if (parent.summary.empty()) {
        parent.summary = parent.symbol + (parent.control_kind.empty()
                                              ? ""
                                              : (" @" + parent.control_kind));
      }
      // Avoid dup
      bool dup = false;
      for (const auto& h : ast.trail.trail) {
        if (h.anchor == parent.anchor) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        ast.trail.trail.push_back(parent);
      }
      ast.trail.focus_anchor = parent.anchor;
      ast.trail.focus_symbol = parent.symbol;
      ++ast.trail.depth;
      if (ast.trail.depth >= kATrailMaxDepth) {
        // Promote focus as primary candidate
        ALocus loc;
        loc.anchor = ast.trail.focus_anchor;
        loc.role = ALocusRole::Primary;
        loc.why = "trail depth cap — edit site candidato";
        a_normalize_locus(&loc);
        bool have = false;
        for (auto& d : ast.loci_draft) {
          if (d.anchor == loc.anchor || d.stem == loc.stem) {
            d.role = ALocusRole::Primary;
            have = true;
            break;
          }
        }
        if (!have) {
          ast.loci_draft.push_back(std::move(loc));
        }
        a_cap_locus_roles(&ast.loci_draft);
        ast.trail.awaiting_judge = false;
        // Keep trail for a_done context but stop forcing stacks
        ast.trail.force_queue.clear();
        ast.trail.pending_stacks.clear();
      } else {
        refresh_a_trail_stacks(workspace_root, &ast, nullptr);
      }
    }
  }

  ++ast.turns;
  if (!save_a_state(workspace_root, ast, &err)) {
    out.error = err.empty() ? "no se pudo guardar a_state" : err;
    return out;
  }
  st.phase = "explore_a";
  st.last_action = "a_trail_judge";
  ++st.turn;
  if (!save_state(workspace_root, st, &err)) {
    out.error = err.empty() ? "no se pudo guardar state" : err;
    return out;
  }

  std::ostringstream obs;
  obs << "### a_trail_judge turn=" << ast.turns << " trail_active=" << (ast.trail.active ? 1 : 0)
      << " depth=" << ast.trail.depth << "\n";
  for (const auto& v : verdicts) {
    obs << "- [" << a_verdict_kind_name(v.verdict) << "] `" << v.target << "`";
    if (!v.why.empty()) {
      obs << " — " << v.why;
    }
    obs << "\n";
  }
  if (!ast.trail.active) {
    if (ast.a_subphase == "a1_suspect_vars") {
      obs << "_nudge:_ trail cerrada → A1 suspect vars. Emite a_judge phase=a1_suspect_vars "
             "(expand+dataflow) o verdicts:[] si ninguna var clara.\n";
    } else if (!ast.a1_queue.empty() || ast.a1_active_set) {
      obs << "_nudge:_ trail cerrada; continúa A1 (cola=" << ast.a1_queue.size()
          << "). Emite a_judge / a_trail_judge según el prompt.\n";
    } else {
      obs << "_nudge:_ L0 invalidado o trail cerrada. Sigue `a_judge` sobre peeks de la cola.\n";
    }
  } else if (ast.trail.awaiting_judge) {
    obs << "_nudge:_ stacks refrescados (depth=" << ast.trail.depth
        << "). Emite otro `a_trail_judge` o `a_done` si ya ves el edit site.\n";
  } else {
    obs << "_nudge:_ trail lista para `a_done` (primary = hop del trail donde editarías).\n";
  }
  append_observation(workspace_root, obs.str(), &out.session_chars, nullptr);

  out.ok = true;
  out.phase = st.phase;
  out.summary = std::string("a_trail_judge active=") + (ast.trail.active ? "1" : "0") +
                " depth=" + std::to_string(ast.trail.depth) +
                " stacks=" + std::to_string(ast.trail.pending_stacks.size());
  return out;
}

std::string Level2Session::build_a_peek_tranche_markdown(const std::string& workspace_root,
                                                         int max_peeks) {
  AState ast = load_a_state(workspace_root);

  // A1 expansion: one modality per turn (mutex hard).
  if (a_effect_summary_enabled() && ast.a_subphase == "a1_suspect_vars") {
    std::ostringstream out;
    out << "## A1 suspect vars (post-trail)\n";
    out << "L0 `" << ast.a1_job_root << "`";
    if (!ast.a1_df_caller_anchor.empty()) {
      out << " · caller interesting `" << ast.a1_df_caller_anchor << "`";
    }
    out << "\n\n";
    if (!ast.a1_trail_recap.empty()) {
      out << ast.a1_trail_recap;
    } else if (!ast.a1_suspect_context.empty()) {
      out << ast.a1_suspect_context;
    } else {
      out << "_(sin contexto de pilas)_\n";
    }
    out << "\n¿Qué variable/campo de estado controla el síntoma **en esa rama**?\n";
    out << "Solo vars plausibles en el snippet; máx 2. No copies ejemplos: usa el nombre C++ real.\n";
    out << "Responde {\"action\":\"a_judge\",\"phase\":\"a1_suspect_vars\",\"verdicts\":["
           "{\"target\":\"path:Symbol\",\"verdict\":\"expand\","
           "\"expand_with\":\"dataflow\",\"suspect_var\":\"campo_\","
           "\"why\":\"estado en el snippet\"}],\"done\":false}\n";
    out << "Si ninguna clara → verdicts:[].\n";
    return out.str();
  }

  if (a_effect_summary_enabled() && ast.a1_active_set) {
    const AExpansionItem& item = ast.a1_active;
    std::ostringstream out;
    out << "## A1 confirmación (" << a_expand_modality_name(item.modality) << ")\n";
    out << "target `" << item.target << "` — responde a_judge con useful|reject|uncertain "
           "(useful solo tras esta evidencia).\n\n";

    if (item.modality == AExpandModality::Dataflow) {
      std::string var = item.suspect_var;
      if (var.empty() && !ast.seeds.empty()) {
        var = ast.seeds.front();
      }
      const std::string path_hint = a_a1_dataflow_path_hint(ast, item);
      if (!ast.a1_trail_recap.empty()) {
        out << "## Trail (hipótesis — dataflow debe cuadrar con esta rama)\n";
        out << ast.a1_trail_recap;
        if (!ast.a1_df_caller_anchor.empty()) {
          out << "caller scope: `" << ast.a1_df_caller_anchor << "`\n\n";
        }
      }
      out << "## Dataflow `" << var << "` scoped `" << path_hint << "`\n\n";
      auto search_fn = [&](const std::string& symbol) -> std::vector<ATrailSearchHit> {
        if (symbol.empty() || deps_.tools == nullptr || !deps_.tools->has("search")) {
          return {};
        }
        auto hits_from = [&](const AiToolResult& tr) -> std::vector<ATrailSearchHit> {
          if (!tr.ok || tr.text.find("(sin hits)") != std::string::npos) {
            return {};
          }
          return parse_search_tool_hits(tr.text, workspace_root);
        };
        std::string q = symbol;
        if (!path_hint.empty()) {
          q += " path:" + path_hint;
        } else {
          q += " path:src/";
        }
        auto hits = hits_from(deps_.tools->invoke("search", q));
        if (hits.empty()) {
          hits = hits_from(deps_.tools->invoke("search", symbol));
        }
        return hits;
      };
      const auto report =
          a_dataflow_build_with_search(workspace_root, var, path_hint, search_fn);
      out << a_dataflow_markdown(report);
      out << "\nResponde `a_judge`: useful solo si hits explican el síntoma **en esta rama** "
             "(coherente con trail).\n";
      out << "reject si la var no encaja o hits fuera del caller interesting → runtime "
             "reabre trail.\n";
      return out.str();
    }

    if (item.modality == AExpandModality::Trail) {
      // Ensure diagram (stacks + cond branches) before asking a_trail_judge. Empty → skip L0.
      for (int skip = 0; skip < 8; ++skip) {
        if (!ast.a1_active_set || ast.a1_active.modality != AExpandModality::Trail) {
          break;
        }
        if (!ast.trail.active) {
          AVerdict seed;
          seed.target = ast.a1_active.target;
          seed.verdict = AVerdictKind::Useful;
          seed.anchor = ast.a1_active.target;
          const auto hash = seed.anchor.find('#');
          if (hash != std::string::npos) {
            seed.anchor = seed.anchor.substr(0, hash);
          }
          a_trail_begin(&ast, seed);
          refresh_a_trail_stacks(workspace_root, &ast, nullptr);
          save_a_state(workspace_root, ast, nullptr);
        } else if (ast.trail.pending_stacks.empty() && ast.trail.cond_branches.empty()) {
          refresh_a_trail_stacks(workspace_root, &ast, nullptr);
          save_a_state(workspace_root, ast, nullptr);
        }
        const int n_items = static_cast<int>(ast.trail.pending_stacks.size()) +
                            static_cast<int>(ast.trail.cond_branches.size());
        if (n_items > 0) {
          break;
        }
        // No diagram — do not ask the model to judge; advance A1 queue.
        const std::string skipped = ast.a1_active.target;
        ast.trail = ATrail{};
        ast.a1_active_set = false;
        ast.a1_active = {};
        if (!ast.a1_queue.empty()) {
          a_a1_begin_job(&ast, ast.a1_queue.front());
          ast.a1_queue.erase(ast.a1_queue.begin());
        } else {
          a_a1_clear_trail_frame(&ast);
          ast.a_subphase = "a0_sniff";
        }
        save_a_state(workspace_root, ast, nullptr);
        append_observation(workspace_root,
                           "### A1 trail skip `" + skipped +
                               "` — sin stacks ni ramas; L0 saltado\n",
                           nullptr, nullptr);
        if (!ast.a1_active_set) {
          std::ostringstream skip_out;
          skip_out << "### trail skip `" << skipped << "`\n";
          skip_out << "_(sin stacks ni ramas condicionales — L0 no juzgable; cola A1)_\n";
          skip_out << "Emite `a_judge` / `a_trail_judge` según el prompt de la siguiente "
                      "modalidad.\n";
          return skip_out.str();
        }
        // Reload item for next modality in loop.
      }
      if (!ast.a1_active_set || ast.a1_active.modality != AExpandModality::Trail ||
          !ast.trail.active) {
        // Fell through to another modality — rebuild prompt for current state.
        return build_a_peek_tranche_markdown(workspace_root, max_peeks);
      }
      {
        const int n_items = static_cast<int>(ast.trail.pending_stacks.size()) +
                            static_cast<int>(ast.trail.cond_branches.size());
        if (n_items == 0) {
          std::ostringstream skip_out;
          skip_out << "### trail skip `" << ast.a1_active.target << "`\n";
          skip_out << "_(sin stacks ni ramas tras reintentos — no juzgar)_\n";
          skip_out << "Emite `a_judge` o `a_done` según el resto de la cola.\n";
          return skip_out.str();
        }
      }
      std::ostringstream trail_out;
      trail_out << a_trail_stacks_markdown(ast.trail);
      trail_out << "\n### Targets válidos (copia literal en a_trail_judge)\n";
      if (a_trail_judge_show_stacks(ast.trail)) {
        for (const auto& s : ast.trail.pending_stacks) {
          trail_out << "- `" << s.id << "`\n";
        }
      } else {
        for (const auto& b : ast.trail.cond_branches) {
          trail_out << "- `" << b.id << "`\n";
        }
      }
      trail_out << "\nResponde `a_trail_judge` con interesting|reject **solo** sobre esos "
                   "targets. Un juego: o S* o ON|CXL|OFF|LINK, no ambos. "
                   "PROHIBIDO usar nombres de símbolo A0 como target.\n";
      return trail_out.str();
    }

    // Default: peek
    out << "### peek `" << item.target << "`\n\n";
    std::string body;
    if (deps_.tools != nullptr && deps_.tools->has("get_code_of")) {
      const AiToolResult tr = deps_.tools->invoke("get_code_of", item.target);
      body = tr.ok ? tr.text : ("(get_code_of fail: " + tr.text + ")");
    } else {
      body = "(sin get_code_of)\n";
    }
    if (body.size() > 2400) {
      body = body.substr(0, 2400) + "\n…[peek A1 truncado ~60 líneas]…\n";
    }
    out << "```\n" << body;
    if (!body.empty() && body.back() != '\n') {
      out << '\n';
    }
    out << "```\n\nResponde `a_judge`.\n";
    return out.str();
  }

  // A0 sniff: Effect Summary cards (no bodies).
  if (a_in_a0_sniff(ast)) {
    if (ast.queue.empty() || ast.cursor >= static_cast<int>(ast.queue.size())) {
      return "_(cola A0 vacía o agotada)_\n";
    }
    const int n = std::min(max_peeks > 0 ? max_peeks : kA0MaxCardsPerTurn,
                           static_cast<int>(ast.queue.size()) - ast.cursor);
    A0TrancheBuildOpts tr_opts;
    std::shared_ptr<const SymbolIndexSnapshot> symbol_snap_keep;
    if (deps_.symbol_snapshot_fn) {
      symbol_snap_keep = deps_.symbol_snapshot_fn();
      tr_opts.symbol_snapshot = symbol_snap_keep.get();
    }
    const A0TrancheShown shown =
        a_build_a0_tranche_shown(workspace_root, ast, n, &tr_opts);
    const std::size_t n_cards = shown.items.size();
    std::ostringstream out;
    out << "## Effect Summary (A0 — olfateo; NO cuerpos)\n";
    out << "cola " << (ast.cursor + 1) << "–" << (ast.cursor + n) << " / " << ast.queue.size()
        << " · cards_used=" << ast.cards_used << " · a0_turn=" << (ast.a0_turns + 1) << "\n";
    out << "Juzga por seeds/nudge/hot/writes/calls; stem/map/kind/path_fam dan contexto L1.\n";
    out << "nudge = sugerencia determinista (expand:*|likely_*|weak_seed), no veredicto.\n";
    out << "Veredictos: expand|reject|uncertain (PROHIBIDO useful).\n\n";
    out << "## Checklist A0 (OBLIGATORIO — N=" << n_cards << ")\n";
    out << "- verdicts[]: EXACTAMENTE " << n_cards
        << " objetos; copia cada target literal de la lista.\n";
    out << "- expand si nudge/hot/seeds cuadra; likely_* / weak_seed / no_signal → reject "
           "salvo seeds claros.\n";
    out << "- Targets (uno por verdict):\n";
    for (std::size_t i = 0; i < n_cards; ++i) {
      out << "  " << (i + 1) << ". `" << shown.items[i].target << "`\n";
    }
    out << "\n";
    EffectSummaryOpts es_opts;
    es_opts.seeds = ast.seeds;
    es_opts.orphans = ast.orphans;
    if (es_opts.orphans.empty()) {
      es_opts.orphans = ast.seeds;
    }
    if (tr_opts.symbol_snapshot != nullptr) {
      es_opts.symbol_snapshot = tr_opts.symbol_snapshot;
    }
    int card_i = 0;
    for (const auto& item : shown.items) {
      es_opts.map_score = static_cast<int>(item.score);
      es_opts.stem = item.stem;
      es_opts.map_related = item.map_related;
      es_opts.refs_in = item.refs_in;
      es_opts.body_sem_permille = item.body_sem_permille;
      es_opts.file_rank = item.file_rank;
      es_opts.file_count = item.file_count;
      es_opts.dup_stem = item.dup_stem;
      EffectSummary es = effect_summary_for_queue_item(workspace_root, item, es_opts);
      out << "### card " << (++card_i) << " `" << item.target << "`\n\n";
      out << "```\n" << es.card_text << "```\n\n";
    }
    if (shown.char_truncated) {
      out << "_(tranche truncada por budget " << kA0MaxCharsPerTurn << " chars; "
          << shown.slice_n << " en slice, " << shown.items.size() << " mostradas)_\n\n";
    }
    out << "Responde {\"action\":\"a_judge\",\"phase\":\"a0_sniff\",\"verdicts\":[ … "
        << n_cards << " objetos ],\"done\":false}\n";
    return out.str();
  }

  if (ast.trail.active && (ast.trail.awaiting_judge || !ast.trail.pending_stacks.empty())) {
    if (ast.trail.pending_stacks.empty()) {
      refresh_a_trail_stacks(workspace_root, &ast, nullptr);
      save_a_state(workspace_root, ast, nullptr);
    }
    return a_trail_stacks_markdown(ast.trail);
  }
  if (ast.queue.empty() || ast.cursor >= static_cast<int>(ast.queue.size())) {
    return "_(cola A vacía o agotada)_\n";
  }
  const int n = std::min(max_peeks > 0 ? max_peeks : kAMaxPeeksPerTurn,
                         static_cast<int>(ast.queue.size()) - ast.cursor);
  std::ostringstream out;
  out << "## Peeks (fase A — efímeros; no acumular)\n";
  out << "cola " << (ast.cursor + 1) << "–" << (ast.cursor + n) << " / " << ast.queue.size()
      << " · peeks_used=" << ast.peeks_used << "\n\n";
  for (int i = 0; i < n; ++i) {
    const auto& item = ast.queue[static_cast<std::size_t>(ast.cursor + i)];
    out << "### peek " << (i + 1) << " `" << item.target << "` stem=" << item.stem << "\n\n";
    std::string body;
    if (deps_.tools != nullptr && deps_.tools->has("get_code_of")) {
      // Prefer ~60-line peeks for 7B judgment.
      const AiToolResult tr = deps_.tools->invoke("get_code_of", item.target);
      if (tr.ok) {
        body = tr.text;
      } else {
        body = "(get_code_of fail: " + tr.text + ")";
      }
    } else {
      body = "(sin tool get_code_of; juzga por target/stem)\n";
    }
    if (body.size() > 3500) {
      body = body.substr(0, 3500) + "\n…[peek truncado]…\n";
    }
    out << "```\n" << body;
    if (!body.empty() && body.back() != '\n') {
      out << '\n';
    }
    out << "```\n\n";
  }
  out << "Responde con `a_judge` (verdicts para estos peeks) o `a_done` si loci estables.\n";
  return out.str();
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
  if (st.phase == "explore_a") {
    out.error = "explore_a: usa a_judge/a_done (no plan/pack aún)";
    return out;
  }
  // P4: in explore_b, restrict plan targets to loci (+ micro-A allowlist).
  std::vector<std::string> filtered_targets = targets;
  if (st.phase == "explore_b" && l2_feat::enabled("L2_EXPLORE_PHASE_A") && !targets.empty()) {
    const AState ast = load_a_state(workspace_root);
    std::vector<std::string> kept;
    std::vector<std::string> dropped;
    for (const auto& t : targets) {
      if (a_plan_target_allowed(ast, t)) {
        kept.push_back(t);
      } else {
        dropped.push_back(t);
      }
    }
    if (kept.empty() && !ast.loci_draft.empty()) {
      out.error = "explore_b: plan fuera de loci (usa anclas de a_done o micro-A allowlist)";
      ++st.turn;
      st.last_action = "plan_outside_loci";
      std::ostringstream block;
      block << "### turn " << st.turn << " — plan_outside_loci\n\n";
      block << "Rechazado: targets fuera de loci[]. Emite plan solo con anclas de a_done "
               "(watchlist) o paths en micro-A allowlist tras pack_review miss.\n";
      for (const auto& d : dropped) {
        block << "- drop `" << d << "`\n";
      }
      append_observation(workspace_root, block.str(), &out.session_chars, nullptr);
      save_state(workspace_root, st, nullptr);
      out.ok = true;
      out.phase = st.phase;
      out.summary = "plan_outside_loci";
      out.error = "plan_outside_loci";
      return out;
    }
    if (!dropped.empty()) {
      filtered_targets = kept;
    }
  }
  if (filtered_targets.empty() && st.watchlist.empty()) {
    out.error = "plan.targets vacío";
    return out;
  }
  if (st.done || st.phase == "done") {
    out.error = "sesión done; reinicia con bootstrap";
    return out;
  }
  if (st.phase != "explore" && st.phase != "explore_b" && st.phase != "edit") {
    out.error = "plan solo en phase explore|explore_b|edit (ahora=" + st.phase + ")";
    write_response_json(workspace_root, false, "error", "plan", "", "", out.error, st.turn,
                        st.phase);
    return out;
  }

  // Anti-loop: reject plan whose file paths are all already on the watchlist
  // while pack review is still open (PACK_REVIEW miss → must add NEW paths).
  if (l2_feat::enabled("PACK_REVIEW") && st.has_pack && !st.pack_review_ok &&
      st.pack_review_cycles > 0 && !filtered_targets.empty() && !st.watchlist.empty()) {
    if (all_plan_target_paths_in_watchlist(filtered_targets, st.watchlist)) {
      ++st.turn;
      st.last_action = "repeated_plan_targets_pushback";
      out.turn = st.turn;
      std::ostringstream block;
      block << "### turn " << st.turn << " — repeated_plan_targets_pushback\n\n";
      block << "Rechazado: todos los paths del plan ya están en watchlist/pack. "
               "Emite `action=plan` con paths NUEVOS de MAP/SEARCH HITS (p. ej. "
               "`src/ai/ai_controller.cpp:Symbol`). No repitas archivos ya empaquetados.\n\n";
      std::string err;
      append_observation(workspace_root, block.str(), &out.session_chars, &err);
      save_state(workspace_root, st, nullptr);
      write_response_json(workspace_root, false, "plan", "", "", block.str(),
                          "repeated_plan_targets_pushback", st.turn, st.phase);
      out.ok = true;
      out.phase = st.phase;
      out.summary = "repeated_plan_targets_pushback";
      out.error = "repeated_plan_targets_pushback";
      return out;
    }
  }

  if (deps_.tools == nullptr) {
    out.error = "tools no registrados";
    return out;
  }

  const std::string session_body = read_file(session_path(workspace_root));
  const auto needles = session_pack_needles(session_body);
  const auto instr_paths = instruction_src_paths(workspace_root, session_body);
  std::unordered_set<std::string> instr_path_set(instr_paths.begin(), instr_paths.end());
  std::unordered_set<std::string> explicit_plan_paths;
  const std::vector<std::string> prev_watchlist = [&]() {
    std::vector<std::string> out;
    for (const auto& t : st.watchlist) {
      if (!target_in_rejected_normalized(t, st.rejected_targets)) {
        out.push_back(t);
      }
    }
    return out;
  }();
  const std::string existing_pack =
      st.has_pack ? read_file(pack_path(workspace_root)) : std::string{};
  const bool delta_fetch = st.has_pack && !prev_watchlist.empty() && !existing_pack.empty();
  for (const auto& raw : filtered_targets) {
    const std::string t = trim_ws(raw);
    if (t.empty()) {
      continue;
    }
    const std::string ep = path_from_plan_target(t);
    if (!ep.empty()) {
      explicit_plan_paths.insert(ep);
    }
  }
  std::unordered_set<std::string> keep_file_window;

  // Merge: NEW plan targets first (7B lists most-important first = pack priority).
  // Prior watchlist follows; kL2MaxPlanTargets truncates the tail (least important).
  std::vector<std::string> merged;
  merged.reserve(st.watchlist.size() + filtered_targets.size());
  std::unordered_set<std::string> seen_t;
  for (const auto& raw : filtered_targets) {
    std::string t = trim_ws(raw);
    if (t.empty() || target_in_rejected_normalized(t, st.rejected_targets) ||
        !seen_t.insert(t).second) {
      continue;
    }
    merged.push_back(std::move(t));
  }
  for (const auto& t : st.watchlist) {
    if (target_in_rejected_normalized(t, st.rejected_targets) || !seen_t.insert(t).second) {
      continue;
    }
    merged.push_back(t);
  }
  if (merged.empty() && st.has_pack) {
    for (const auto& t : parse_pack_targets_header(read_file(pack_path(workspace_root)))) {
      if (target_in_rejected_normalized(t, st.rejected_targets) || !seen_t.insert(t).second) {
        continue;
      }
      merged.push_back(t);
    }
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
        // Only Instruction-named files get a file window; other bare plans stay
        // omitted so outline junk (wrong AST symbol) does not leak into the pack.
        const bool keep_path = !path.empty() && instr_path_set.count(path);
        if (keep_path) {
          t = path + ":1";
          keep_file_window.insert(t);
          keep_file_window.insert(path);
          normalize_notes.push_back("- bare `" + raw + "` → `" + t +
                                    "` (file window; sin símbolo fuerte)");
        } else {
          normalize_notes.push_back(
              "- bare `" + raw +
              "` sin hit fuerte — omitido; preferir `path:Symbol` / `path:line`");
          skip_fetch.insert(raw);
        }
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

  // API siblings: generic lifecycle complements and same-file map neighbors.
  {
    const std::string map_last =
        read_file((fs::path(workspace_root) / ".tuide" / "ai" / "map_last.md").string());
    const auto siblings =
        expand_anchor_api_siblings(uniq_targets, map_last, 8, workspace_root);
    if (!siblings.empty()) {
      std::vector<std::string> merged_sib;
      merged_sib.reserve(uniq_targets.size() + siblings.size());
      // Keep plan order for must-tier head; inject lifecycle complements into must head.
      const std::size_t head_n =
          std::min(uniq_targets.size(), static_cast<std::size_t>(kL2MustPlanTargets));
      for (std::size_t i = 0; i < head_n; ++i) {
        merged_sib.push_back(uniq_targets[i]);
      }
      std::unordered_set<std::string> seen_m(merged_sib.begin(), merged_sib.end());
      auto try_add = [&](const std::string& s, bool prefer_front) {
        if (target_in_rejected_normalized(s, st.rejected_targets) || !seen_m.insert(s).second) {
          return;
        }
        bool dup = false;
        for (const auto& m : merged_sib) {
          if (to_lower_copy(m) == to_lower_copy(s)) {
            dup = true;
            break;
          }
        }
        if (dup) {
          return;
        }
        if (prefer_front) {
          const std::size_t insert_at =
              std::min<std::size_t>(1, merged_sib.size());  // keep #1 plan pick, then clears
          merged_sib.insert(merged_sib.begin() + static_cast<std::ptrdiff_t>(insert_at), s);
        } else {
          merged_sib.push_back(s);
        }
        normalize_notes.push_back("- api sibling → `" + s + "`");
      };
      for (const auto& s : siblings) {
        try_add(s, target_is_lifecycle_clear(s));
      }
      // Lifecycle clears on .hpp are decls — ensure twin .cpp:Symbol is must-front.
      {
        std::vector<std::string> cpp_defs;
        for (const auto& t : merged_sib) {
          if (!target_is_lifecycle_clear(t)) {
            continue;
          }
          const std::string path = path_from_plan_target(t);
          const std::string sym = symbol_from_plan_target(t);
          if (path.empty() || sym.empty() || !path_looks_like_header(path)) {
            continue;
          }
          const std::string src = sibling_source_rel(workspace_root, path);
          if (src.empty()) {
            continue;
          }
          cpp_defs.push_back(src + ":" + sym);
        }
        for (const auto& d : cpp_defs) {
          try_add(d, true);
        }
      }
      for (std::size_t i = head_n; i < uniq_targets.size(); ++i) {
        if (seen_m.insert(uniq_targets[i]).second) {
          merged_sib.push_back(uniq_targets[i]);
        }
      }
      if (static_cast<int>(merged_sib.size()) > kL2MaxPlanTargets + 6) {
        merged_sib.resize(static_cast<std::size_t>(kL2MaxPlanTargets + 6));
      }
      uniq_targets = std::move(merged_sib);
    }
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

  // Sibling .cpp next to a packed .hpp when Instruction (or this plan) names the twin.
  {
    const std::vector<std::string> paths_copy = uniq_paths;
    for (const auto& p : paths_copy) {
      const std::string sib = sibling_source_rel(workspace_root, p);
      if (sib.empty()) {
        continue;
      }
      if (!instr_path_set.count(sib) && !explicit_plan_paths.count(sib)) {
        continue;
      }
      if (seen_paths.insert(sib).second) {
        uniq_paths.push_back(sib);
      }
      const std::string t = sib + ":1";
      if (std::find(uniq_targets.begin(), uniq_targets.end(), t) == uniq_targets.end() &&
          std::find(uniq_targets.begin(), uniq_targets.end(), sib) == uniq_targets.end()) {
        uniq_targets.push_back(t);
        keep_file_window.insert(t);
        keep_file_window.insert(sib);
        normalize_notes.push_back("- sibling source `" + p + "` → `" + t + "` (definition)");
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
    bool must_keep = false;       // top of 7B plan order — pack even under budget pressure
    int plan_pri = 999;           // lower = higher priority (index in plan/watchlist order)
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
  for (std::size_t ti = 0; ti < uniq_targets.size(); ++ti) {
    const auto& t = uniq_targets[ti];
    Frag f;
    f.target = t;
    f.plan_pri = static_cast<int>(ti);
    f.must_keep = static_cast<int>(ti) < kL2MustPlanTargets || target_is_lifecycle_clear(t);
    if (skip_fetch.count(t)) {
      f.ok = false;
      f.junk = true;
      f.must_keep = false;
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
        if (keep_file_window.count(t) || keep_file_window.count(path)) {
          // path:1 file window: take ~140 lines so the last function is in-pack.
          fetch_arg = line_window_target_biased(path, line, 0, 140);
        } else {
          fetch_arg = f.explicit_locus ? line_window_target_biased(path, line, 25, 55)
                                       : line_window_target_biased(path, line);
        }
      }
      bool reused_delta = false;
      if (delta_fetch && target_in_watchlist_normalized(t, prev_watchlist)) {
        std::string reused = load_pack_fragment_body(existing_pack, t);
        if (reused.empty() && fetch_arg != t) {
          reused = load_pack_fragment_body(existing_pack, fetch_arg);
        }
        if (reused.empty() && !path.empty()) {
          reused = load_pack_fragment_body(existing_pack, path + ":1");
        }
        if (!reused.empty()) {
          f.ok = true;
          f.text = reused;
          f.truncated = text_looks_truncated(f.text);
          reused_delta = true;
          if (f.explicit_locus && line > 0) {
            f.target = path + ":" + std::to_string(line);
            f.refetch = fetch_arg;
          } else if (fetch_arg != t) {
            f.target = fetch_arg;
          }
        }
      }
      if (!reused_delta) {
      const AiToolResult tr = deps_.tools->invoke("get_code_of", fetch_arg);
      f.ok = tr.ok;
      f.text = tr.text.empty() ? (tr.ok ? "(vacío)" : "error get_code_of") : tr.text;
      f.truncated = text_looks_truncated(f.text);
      // Huge enclosing symbol without a line locus → treat as truncated (force window refill).
      if (f.ok && line <= 0) {
        const auto ssp = f.text.find("symbol_span:");
        if (ssp != std::string::npos) {
          int span_a = 0;
          int span_b = 0;
          if (std::sscanf(f.text.c_str() + ssp, "symbol_span: %d-%d", &span_a, &span_b) == 2 &&
              span_b > span_a && (span_b - span_a) > 400) {
            f.truncated = true;
            if (f.refetch.empty()) {
              f.refetch = extract_refetch_hint(f.text, t);
            }
            f.rank_boost -= 40;
          }
        }
      }
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
        const bool keep_window =
            keep_file_window.count(t) || keep_file_window.count(path) ||
            (line == 1 && instr_path_set.count(path));
        if ((was_bare_like || needle_sc <= 0) && !sibling_preamble && !keep_window) {
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
        if (!f.explicit_locus && line == 1 && keep_file_window.count(path)) {
          f.rank_boost = 75 + std::max(0, needle_sc) / 5;
        }
      }
      fetched.insert(t);
      fetched.insert(fetch_arg);
      if (f.explicit_locus && line > 0) {
        fetched.insert(path + ":" + std::to_string(line));
      }
      } else {
        const int needle_sc =
            score_symbol_against_needles(f.text.substr(0, 400), needles);
        f.rank_boost = 5 + needle_sc / 5;
        if (f.explicit_locus) {
          f.rank_boost += 50;
        }
        fetched.insert(t);
        fetched.insert(fetch_arg);
      }
    }
    f.rank_size = f.junk ? 999999 : f.text.size();
    if (f.must_keep) {
      f.rank_boost =
          std::max(f.rank_boost, 120 + (kL2MustPlanTargets - f.plan_pri) * 15);
    } else if (f.plan_pri < 900) {
      f.rank_boost += std::max(0, 40 - f.plan_pri);
    }
    // .cpp clear/cancel definitions beat header decls under pack budget.
    if (target_is_lifecycle_clear(f.target)) {
      const std::string path = path_from_plan_target(f.target);
      if (!path.empty() && !path_looks_like_header(path)) {
        f.rank_boost = std::max(f.rank_boost, 170);
      }
    }
    frags.push_back(std::move(f));
  }

  // Auto-refetch truncated gaps (explore-fill): prefer line windows + needle-overlapping hints.
  // For must_keep truncations, replace the primary body when a cleaner window is found.
  std::vector<Frag> extras;
  auto already = [&](const std::string& t) {
    return fetched.count(t) ||
           std::find(uniq_targets.begin(), uniq_targets.end(), t) != uniq_targets.end();
  };
  for (std::size_t fi = 0; fi < frags.size(); ++fi) {
    auto& f = frags[fi];
    if (!f.truncated || f.junk || !deps_.tools->has("get_code_of")) {
      continue;
    }
    std::vector<std::string> try_targets;
    const int line = f.plan_line > 0 ? f.plan_line : line_from_plan_target(f.target);
    const std::string path = path_from_plan_target(f.target);
    if (line > 0 && !path.empty()) {
      try_targets.push_back(line_window_target_biased(path, line, 15, 70));
      try_targets.push_back(line_window_target_biased(path, line));
      try_targets.push_back(line_window_target_biased(path, line + 100, 20, 100));
      try_targets.push_back(line_window_target_biased(path, std::max(1, line - 100), 20, 100));
    }
    if (!f.refetch.empty()) {
      try_targets.push_back(f.refetch);
    }
    // Must path:Symbol without line: still try refetch_hint / mid window from metadata.
    if (f.must_keep && line <= 0 && !path.empty()) {
      const std::string hint = extract_refetch_hint(f.text, f.target);
      if (!hint.empty()) {
        try_targets.push_back(hint);
      }
    }
    int added = 0;
    Frag best_replace;
    bool have_replace = false;
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
      e.must_keep = f.must_keep;
      e.plan_pri = f.plan_pri;
      e.explicit_locus = f.explicit_locus;
      e.plan_line = line_from_plan_target(rt) > 0 ? line_from_plan_target(rt) : line;
      const int hit = score_symbol_against_needles(e.text.substr(0, 1200), needles);
      // Skip empty/noise auto-fills that don't touch pack needles.
      if (hit <= 0 && line <= 0 && !f.must_keep) {
        continue;
      }
      if (hit <= 0 && line > 0 && added > 0 && !f.must_keep) {
        continue;
      }
      e.rank_boost = 50 + (line > 0 ? 40 : 0) + hit / 3;
      // Primary biased window around the requested line gets top priority.
      if (line > 0 && !path.empty() &&
          (rt == line_window_target_biased(path, line, 15, 70) ||
           rt == line_window_target_biased(path, line))) {
        e.rank_boost = 130 + hit / 2;
      }
      fetched.insert(rt);
      // Must: collect candidates; replace primary below (don't double-pack windows).
      if (f.must_keep && e.ok) {
        const std::string sym = symbol_from_plan_target(f.target);
        const bool has_sym =
            sym.size() < 4 ||
            to_lower_copy(e.text).find(to_lower_copy(sym)) != std::string::npos;
        if (has_sym && (!have_replace || (!e.truncated && best_replace.truncated) ||
                        e.rank_boost > best_replace.rank_boost)) {
          best_replace = e;
          have_replace = true;
        }
      } else {
        extras.push_back(std::move(e));
      }
      if (++added >= (f.must_keep ? 3 : 2)) {
        break;
      }
    }
    if (have_replace) {
      // Keep path:line label when we have a locus; otherwise keep original target name.
      if (best_replace.plan_line > 0 && !path.empty()) {
        f.target = path + ":" + std::to_string(best_replace.plan_line);
      }
      f.text = std::move(best_replace.text);
      f.truncated = best_replace.truncated;
      f.refetch = best_replace.refetch;
      f.ok = best_replace.ok;
      f.rank_size = f.text.size();
      f.rank_boost = std::max(f.rank_boost, best_replace.rank_boost + 20);
      if (f.plan_line <= 0 && best_replace.plan_line > 0) {
        f.plan_line = best_replace.plan_line;
      }
    } else if (f.must_keep && added > 0) {
      // No clean replace — still keep one extra window for pack diversity.
      // (already skipped pushing during must loop)
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
    // Lifecycle clear/cancel targets are exempt: NL needles rarely contain the
    // exact complementary identifier.
    if ((frag_roles[i] == FragRole::ApiFn || frag_roles[i] == FragRole::Other) &&
        !target_is_lifecycle_clear(frags[i].target)) {
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
    if (target_is_lifecycle_clear(frags[i].target) && frags[i].ok && !frags[i].junk) {
      const std::string path = path_from_plan_target(frags[i].target);
      if (!path.empty() && !path_looks_like_header(path)) {
        frag_roles[i] = FragRole::ApiFn;
        frags[i].rank_boost = std::max(frags[i].rank_boost, 170);
      } else if (frag_roles[i] != FragRole::Decl) {
        frag_roles[i] = FragRole::Decl;
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

  // Plan order wins, but lifecycle .cpp definitions beat large activation windows.
  std::stable_sort(pack_order.begin(), pack_order.end(), [&](std::size_t a, std::size_t b) {
    const auto& fa = frags[a];
    const auto& fb = frags[b];
    if (fa.must_keep != fb.must_keep) {
      return fa.must_keep && !fb.must_keep;
    }
    auto life_pri = [&](const Frag& f) {
      const bool clear = target_is_lifecycle_clear(f.target);
      const bool set = target_is_lifecycle_set(f.target);
      if (!clear && !set) {
        return 0;
      }
      const std::string path = path_from_plan_target(f.target);
      const bool hdr = !path.empty() && path_looks_like_header(path);
      if (clear && !hdr) {
        return 3;
      }
      if (set && !hdr) {
        return 2;
      }
      if (clear || set) {
        return 1;  // header decls
      }
      return 0;
    };
    const int la = life_pri(fa);
    const int lb = life_pri(fb);
    if (la != lb) {
      return la > lb;
    }
    if (fa.plan_pri != fb.plan_pri) {
      return fa.plan_pri < fb.plan_pri;
    }
    return false;
  });

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

  const std::size_t budget = budget_.pack_chars > 0 ? budget_.pack_chars : kMaxPackChars;
  const std::size_t frag_share_cap =
      budget_.frag_share > 0 ? budget_.frag_share : kMaxFragShareChars;
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
                     : frag_share_cap;

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
    if (remaining < 80 && !f.must_keep) {
      pack << "_pack: fragmentos restantes omitidos por presupuesto ("
           << (pack_order.size() - oi) << "; prioridad 7B cola atrás)._\n\n";
      for (std::size_t oj = oi; oj < pack_order.size(); ++oj) {
        const auto& fj = frags[pack_order[oj]];
        if (fj.junk || fj.must_keep) {
          continue;
        }
        trunc_index.push_back("- `" + fj.target +
                              "` — omitido por presupuesto pack; refetch get_code_of `" +
                              fj.target + "`");
      }
      // Still force-pack any remaining must_keep with a tight slice.
      for (std::size_t oj = oi; oj < pack_order.size(); ++oj) {
        const std::size_t ji = pack_order[oj];
        const auto& fj = frags[ji];
        if (fj.junk || !fj.must_keep) {
          continue;
        }
        const std::size_t rem2 = frag_budget > used_frags ? frag_budget - used_frags : 0;
        if (rem2 < 60) {
          trunc_index.push_back("- `" + fj.target +
                                "` — must truncado sin espacio; refetch get_code_of `" +
                                fj.target + "`");
          continue;
        }
        const std::size_t per_must = std::min({rem2, frag_share_cap, role_share + 200});
        std::string tip = !fj.refetch.empty() ? fj.refetch : fj.target;
        std::string body =
            truncate_center_budget(fj.text, per_must, tip, needles, true);
        std::ostringstream sec;
        sec << "### get_code_of `" << fj.target << "`";
        if (body.size() < fj.text.size()) {
          sec << " [TRUNCATED]";
        }
        sec << "  <!-- role:must -->\n\n```\n" << body << "\n```\n\n";
        pack << sec.str();
        used_frags += sec.str().size();
        if (fj.ok) {
          ++frag_ok;
        }
      }
      break;
    }
    // Drop noise unless we still have spare budget after diversity slots.
    if (role == FragRole::Noise && used_frags > frag_budget / 2 && !f.must_keep) {
      continue;
    }
    if (f.rank_boost < 0 && remaining < frag_budget / 3 && !f.must_keep) {
      continue;
    }
    if (f.rank_boost < 15 && role == FragRole::Noise && !f.must_keep) {
      continue;
    }
    if (f.ok) {
      ++frag_ok;
    }

    const bool first_of_role = roles_packed.insert(static_cast<int>(role)).second;
    std::size_t per = std::min({remaining, frag_share_cap, role_share});
    // First control/layout/decl slot: allow up to role_share (not half budget).
    if (!first_of_role) {
      per = std::min(per, role_share * 3 / 4);
    }
    if (f.must_keep) {
      // Must-tier: larger slice so anclas survive budget pressure; keep function bodies.
      per = std::min(remaining, std::max(per, std::min<std::size_t>(role_share + 800, 3200)));
    } else if (f.rank_boost >= 80 && first_of_role && role == FragRole::Control) {
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
      tip = f.explicit_locus || f.must_keep
                ? line_window_target_biased(path_from_plan_target(f.target), line, 15, 70)
                : line_window_target_biased(path_from_plan_target(f.target), line);
    }
    std::vector<std::string> prefer = needles;
    // Must/ancla: center on the requested symbol name before generic role markers.
    {
      const std::string sym = symbol_from_plan_target(f.target);
      if (sym.size() >= 4) {
        prefer.insert(prefer.begin(), sym);
      }
    }
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
    const bool center = line > 0 || f.must_keep || f.rank_boost >= 50 ||
                        role == FragRole::Control || role == FragRole::Layout ||
                        role == FragRole::Decl || role == FragRole::IdConst;
    // Must + ApiFn: first hit of the symbol name (definition), not last incidental mention.
    const bool first_hit = f.must_keep || role == FragRole::Layout || role == FragRole::Decl ||
                           (role == FragRole::IdConst && f.text.find("shortcuts.") != std::string::npos);
    std::string body = center ? truncate_center_budget(f.text, per, tip, prefer, first_hit)
                              : truncate_to_budget(f.text, per, tip);
    // If must truncate dropped the symbol needle, widen once around first hit.
    if (f.must_keep && pack_trunc) {
      const std::string sym = symbol_from_plan_target(f.target);
      std::string needle = sym.size() >= 4 ? sym : std::string{};
      if (!needle.empty() && to_lower_copy(body).find(to_lower_copy(needle)) == std::string::npos) {
        body = truncate_center_budget(f.text, std::min(remaining, std::max(per, std::size_t{2400})),
                                      tip, {needle}, true);
      }
    }
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
    trunc_sec << "## Truncated (refetch tip si editas esa ventana)\n\n";
    trunc_sec << "Ventanas incompletas (" << trunc_index.size() << "). No inventes esas líneas. "
                 "No implica pack incompleto ni bloquea next=edit si el locus de control ya "
                 "está en Fragments. Hueco: `get_code_of path:A-B` / `path:Symbol#mid|#tail`.\n\n";
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
  const bool prev_pack_review_ok = st.pack_review_ok;
  st.pack_review_ok = false;
  const auto gaps = pack_instruction_gaps(pack_body, needles);
  st.pack_incomplete = (frag_ok == 0) || !gaps.empty();
  // Keep review-ok across refine plans when must anchors still have fence bodies.
  if (prev_pack_review_ok && !st.pack_incomplete &&
      pack_must_anchors_covered(pack_body, st.watchlist, 3) &&
      (!std::any_of(st.watchlist.begin(), st.watchlist.end(),
                    [](const std::string& t) { return target_is_lifecycle_set(t); }) ||
       pack_has_lifecycle_pair(pack_body))) {
    st.pack_review_ok = true;
  }
  st.explore_tool_count = 0;
  st.plan_nudge_sent = false;
  st.post_pack_tool_count = 0;
  st.edit_nudge_sent = false;
  st.map_review = false;
  // Watchlist: only targets with OK non-noise fragments; junk → rejected_targets.
  st.watchlist.clear();
  std::unordered_set<std::string> wl_seen;
  std::unordered_set<std::string> ok_paths;
  std::unordered_set<std::string> ok_targets;
  for (std::size_t i = 0; i < frags.size(); ++i) {
    if (frags[i].junk || !frags[i].ok || frag_roles[i] == FragRole::Noise) {
      continue;
    }
    ok_targets.insert(frags[i].target);
    const std::string p = path_from_plan_target(frags[i].target);
    if (!p.empty()) {
      ok_paths.insert(p);
    }
  }
  auto target_has_ok_frag = [&](const std::string& t) -> bool {
    if (ok_targets.count(t)) {
      return true;
    }
    const std::string p = path_from_plan_target(t);
    return !p.empty() && ok_paths.count(p);
  };
  for (const auto& t : uniq_targets) {
    if (skip_fetch.count(t) || target_in_rejected_normalized(t, st.rejected_targets)) {
      continue;
    }
    if (!target_has_ok_frag(t)) {
      if (std::find(st.rejected_targets.begin(), st.rejected_targets.end(), t) ==
          st.rejected_targets.end()) {
        st.rejected_targets.push_back(t);
      }
      continue;
    }
    if (wl_seen.insert(t).second) {
      st.watchlist.push_back(t);
    }
  }
  for (const auto& raw : targets) {
    const std::string t = trim_ws(raw);
    if (t.empty()) {
      continue;
    }
    if (!target_has_ok_frag(t) && !target_in_rejected_normalized(t, st.rejected_targets)) {
      if (std::find(st.rejected_targets.begin(), st.rejected_targets.end(), t) ==
          st.rejected_targets.end()) {
        st.rejected_targets.push_back(t);
      }
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
  const bool repeated_plan_pushback =
      !st.pack_incomplete && st.pack_review_ok &&
      st.consecutive_complete_plans >= Level2Session::kRepeatedPlanEditPushbackAfter &&
      !ai_workflow_is_readonly(parse_ai_workflow_kind(st.workflow));
  if (repeated_plan_pushback) {
    block << "_pushback:_ " << st.consecutive_complete_plans
          << " `plan` seguidos con pack cubierto. Runtime pasa a phase=edit.\n\n";
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
  if (repeated_plan_pushback) {
    out.error = "repeated_plan_pushback";
    out.summary += " repeated_plan_pushback";
  }
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
  if (ai_workflow_is_readonly(parse_ai_workflow_kind(st.workflow))) {
    out.error = "action=edit no permitido en workflow=" + st.workflow;
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
    if (!path.empty() && err_msg.find("0 matches") != std::string::npos) {
      fs::path abs = path;
      if (!abs.is_absolute()) {
        abs = fs::path(workspace_root) / path;
      }
      const std::string on_disk = read_file_raw(abs.lexically_normal().string());
      const std::string excerpt = disk_excerpt_near_search(on_disk, search, 3, 18);
      if (!excerpt.empty()) {
        block << "## on disk (copia literal al `search`; respeta líneas en blanco)\n\n```\n"
              << truncate_observation(excerpt, 20) << "```\n\n";
      }
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
      const bool already_edited =
          std::find(st.edited_paths.begin(), st.edited_paths.end(), path) !=
          st.edited_paths.end();
      if (already_edited) {
        block << "Este path **ya está cubierto** en disco; el span del pack está stale. "
                 "No reemitas el mismo hunk sobre `"
              << path << "`.\n";
        const auto miss = missing_instruction_paths(workspace_root, st.edited_paths);
        if (!miss.empty()) {
          block << "Siguiente locus: `" << miss.front()
                << "` (código fresco / coverage, no el pack).\n\n";
        } else {
          block << '\n';
        }
      } else {
        const std::string hint = pack_span_hint(workspace_root, path);
        if (!hint.empty()) {
          block << "## span sugerido desde pack (úsalo como base del search)\n\n```\n"
                << hint << "```\n\n";
        }
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
  {
    static const std::vector<std::string> kEmptyScope;
    const std::vector<std::string>& scope =
        deps_.path_scope_fn ? deps_.path_scope_fn() : kEmptyScope;
    for (const auto& h : hunks) {
      if (!ai_path_in_scope(workspace_root, h.path, scope)) {
        record_edit_failure("ruta fuera del path_scope AI: " + h.path, h.path, h.search,
                            h.replace, fnv1a_hex(h.path + "\n" + h.search + "\n---\n" +
                                                    h.replace),
                            false);
        return out;
      }
    }
  }

  std::vector<SearchReplaceHunk> work = hunks;
  for (auto& h : work) {
    normalize_hunk_escape_noise(&h);
  }
  for (auto& h : work) {
    rewrite_function_opener_insert(workspace_root, &h);
  }
  {
    const std::string sess = read_file_raw(session_path(workspace_root));
    const auto instr_paths = instruction_src_paths(workspace_root, sess);
    work = split_mixed_sibling_hunks(workspace_root, std::move(work), instr_paths);
  }

  // After a clean compile, refuse re-editing an already-covered Instruction path while
  // other Instruction files are still missing (hpp loop with a cpp gap).
  if (st.pending.empty() && !st.edited_paths.empty()) {
    const auto miss = missing_instruction_paths(workspace_root, st.edited_paths);
    if (!miss.empty()) {
      std::unordered_set<std::string> miss_set(miss.begin(), miss.end());
      std::unordered_set<std::string> edited(st.edited_paths.begin(), st.edited_paths.end());
      bool touches_gap = false;
      bool only_covered = true;
      std::string covered_path;
      for (const auto& h : work) {
        if (h.path.empty()) {
          continue;
        }
        if (miss_set.count(h.path)) {
          touches_gap = true;
        }
        if (!edited.count(h.path)) {
          only_covered = false;
        } else if (covered_path.empty()) {
          covered_path = h.path;
        }
      }
      if (!touches_gap && only_covered && !covered_path.empty()) {
        ++st.turn;
        ++st.covered_path_rejects;
        const bool hit_limit =
            st.covered_path_rejects >= Level2Session::kMaxCoveredPathRejects;
        st.last_action = hit_limit ? "covered_path_limit" : "edit_covered_path";
        out.turn = st.turn;
        out.ok = true;
        InstructionCoverageGaps gaps;
        gaps.missing_paths = miss;
        const std::string bodies =
            fetch_coverage_bodies(deps_, workspace_root, gaps, st.edited_paths);
        std::ostringstream block;
        if (hit_limit) {
          block << "### turn " << st.turn << " — covered_path_limit\n\n";
          block << "Stop: " << st.covered_path_rejects
                << " re-edits de un path ya cubierto (`" << covered_path
                << "`) con Instruction incompleta.\n";
          block << "Se conserva el diff que compiló; no hay rollback. Faltó:\n";
          for (const auto& p : miss) {
            block << "- `" << p << "`\n";
          }
          block << "\n";
          st.done = true;
          st.phase = "done";
          st.continuable = true;
          st.map_review = false;
          st.pending.clear();
          out.phase = "done";
          out.summary = "covered_path_limit";
          out.error = "covered_path_limit";
        } else {
          block << "### turn " << st.turn << " — edit_covered_path\n\n";
          block << "Rechazado: `" << covered_path
                << "` ya está cubierto y la Instruction aún pide:\n";
          for (const auto& p : miss) {
            block << "- `" << p << "`\n";
          }
          block << "\nSiguiente acción: un hunk Aider del primer path de arriba "
                   "(SEARCH = código fresco). PROHIBIDO reedit `"
                << covered_path << "`.\n\n";
          if (l2_feat::enabled("EDIT_LEAN_PROMPT")) {
            block << "Empieza con ese path y `<<<<<<< SEARCH`.\n"
                     "PROHIBIDO JSON / plan / tool / done.\n\n";
          }
          if (!bodies.empty()) {
            block << "## Código fresco (disco actual)\n\n" << bodies;
          }
          out.phase = "edit";
          out.summary = "edit_covered_path";
          out.error = "edit_covered_path";
        }
        if (hit_limit && !bodies.empty()) {
          block << "## Código fresco (disco actual)\n\n" << bodies;
        }
        std::string obs_err;
        append_observation(workspace_root, block.str(), &out.session_chars, &obs_err);
        if (st.has_pack) {
          compact_observations_after_pack(workspace_root, st, &out.session_chars);
        }
        save_state(workspace_root, st, nullptr);
        write_response_json(workspace_root, true, "edit", st.last_action, covered_path,
                            block.str(), st.last_action, st.turn, out.phase);
        append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                         ",\"event\":\"" + st.last_action + "\",\"turn\":" +
                                         std::to_string(st.turn) + ",\"covered\":\"" +
                                         json_escape(covered_path) + "\",\"n\":" +
                                         std::to_string(st.covered_path_rejects) + "}");
        ai_trace(AiTraceChannel::L2, hit_limit ? "l2_covered_path_limit" : "l2_edit_covered_path",
                 "{\"turn\":" + std::to_string(st.turn) + ",\"covered\":\"" +
                     ai_trace_escape(covered_path) + "\",\"n\":" +
                     std::to_string(st.covered_path_rejects) + "}");
        return out;
      }
    }
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
  std::vector<std::string> search_repairs;
  const auto miss_paths = missing_instruction_paths(workspace_root, st.edited_paths);

  std::vector<ApplyHunkResult> applied;
  applied.reserve(work.size());
  for (auto& h : work) {
    ApplyHunkResult r = apply_hunk_to_workspace_file(workspace_root, h, /*write=*/true);
    if (!r.ok && r.error.find("0 matches") != std::string::npos) {
      SearchReplaceHunk repaired = h;
      if (rewrite_missing_path_search(workspace_root, miss_paths, &repaired)) {
        ApplyHunkResult r_fix =
            apply_hunk_to_workspace_file(workspace_root, repaired, /*write=*/true);
        if (r_fix.ok) {
          h = std::move(repaired);
          search_repairs.push_back("`" + h.path + "`");
          r = std::move(r_fix);
          ai_trace(AiTraceChannel::L2, "l2_sibling_search_repair",
                   "{\"path\":\"" + ai_trace_escape(h.path) + "\"}");
        }
      }
    }
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
    remember_edited_path(st.edited_paths, work[i].path);
    append_applied_blob(&st.applied_blob, applied[i].new_text);
  }
  st.covered_path_rejects = 0;
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
  if (!search_repairs.empty()) {
    block << "search reescrito a ancla de disco (Instruction path sin cobertura): ";
    for (std::size_t i = 0; i < search_repairs.size(); ++i) {
      if (i) {
        block << "; ";
      }
      block << search_repairs[i];
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
    const bool cov_on = post_edit_coverage_enabled();
    InstructionCoverageGaps gaps;
    if (cov_on) {
      gaps = collect_instruction_coverage_gaps(workspace_root, st.edited_paths, st.applied_blob);
    }
    const bool coverage_blocks =
        cov_on && !gaps.empty() && coverage_gate_should_block(workspace_root, gaps);
    const bool lean = l2_feat::enabled("EDIT_LEAN_PROMPT");

    block << "### turn " << st.turn << " — compile_ok\n\n";
    block << "attempt: " << st.compile_attempt << "/" << kMaxCompileAttempts
          << "  exit_code: " << exit_code << "\n\n";
    const std::string ok_log = truncate_observation_tail(output, 12);
    block << "```\n" << ok_log;
    if (!ok_log.empty() && ok_log.back() != '\n') {
      block << '\n';
    }
    block << "```\n\n";
    if (lean && coverage_blocks) {
      block << "Compile OK **no** cierra la tarea.\n"
               "¿Algo más?\n"
               "- Falta un path de Instruction → hunk Aider de ese path "
               "(SEARCH = disco actual / código fresco en Observations).\n"
               "- Más cambios → hunk Aider sobre el disco actual.\n"
               "PROHIBIDO JSON plan/tool. Cubierto → "
               "`{\"action\":\"done\",\"summary\":\"…qué cambiaste…\"}` (sin next).\n\n";
    } else if (lean) {
      block << "Instruction cubierta. Compile OK.\n"
               "Cierra con `{\"action\":\"done\",\"summary\":\"…qué cambiaste…\"}` (sin next).\n"
               "PROHIBIDO plan/tool JSON, mapa rankeado y hunks extra.\n\n";
    } else {
      block << "Compile OK **no** cierra la tarea.\n"
               "¿Algo más?\n"
               "- Más sitios → `action=plan` con nuevos targets (arma otro pack).\n"
               "- Más cambios → `get_code_of` del locus **antes** de `edit` (el disco ya "
               "cambió; no reuses el `search` del pack viejo).\n"
               "- Instruction cubierta → "
               "`{\"action\":\"done\",\"summary\":\"…qué cambiaste…\"}` (sin next).\n\n";
    }
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);

    // Lean closeout: do not dump the ranked map (it pulls the 7B off `done`).
    if (!lean && gaps.empty() && !st.map_stale) {
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
    st.pending.clear();
    st.compile_attempt = 0;  // fresh fail budget for the next edit batch

    // Coverage gaps: stay in pack/Aider edit. Do not dump the ranked map (JSON "algo más?").
    st.map_review = !coverage_blocks;

    if (coverage_blocks) {
      const std::string bodies =
          fetch_coverage_bodies(deps_, workspace_root, gaps, st.edited_paths);
      const std::string cov =
          format_coverage_observation(st.turn, "post_edit_coverage", gaps, bodies);
      append_observation(workspace_root, cov, &out.session_chars, nullptr);
      st.last_action = "post_edit_coverage";
    } else if (l2_feat::enabled("MAP_REVIEW_PENDING")) {
      const auto miss = missing_instruction_paths(workspace_root, st.edited_paths);
      if (!miss.empty()) {
        std::ostringstream pend;
        pend << "### turn " << st.turn << " — map_review_pending\n\n";
        pend << "Compile OK, pero la Instruction aún nombra paths sin editar:\n";
        for (const auto& p : miss) {
          pend << "- `" << p << "`\n";
        }
        pend << "\nEmite `action=edit` para esos paths o `done` solo si ya no aplican.\n\n";
        append_observation(workspace_root, pend.str(), &out.session_chars, nullptr);
      }
    }

    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, true, "compile", "", "",
                        gaps.empty() ? "compile ok; map_review — ¿algo más?"
                                     : "compile ok; coverage gaps — edit required",
                        "", st.turn, "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"compile_ok\",\"exit\":0,\"ms\":" +
                                     std::to_string(compile_ms) +
                                     ",\"resume\":\"edit\",\"map_review\":1,\"coverage_gaps\":" +
                                     (gaps.empty() ? "0" : "1") + "}");
    out.ok = true;
    out.action = "compile";
    out.summary = gaps.empty() ? "OK compile; map_review (¿algo más?)"
                               : "OK compile; post_edit_coverage";
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
    if (l2_feat::enabled("SIBLING_UNDECL")) {
      block << "**Siguiente acción obligatoria:** `action=edit` incluyendo el `.hpp`/`.h` "
               "hermano (declaración) además del `.cpp` si falta.\n\n";
    }
  }
  {
    const std::string sess = read_file_raw(session_path(workspace_root));
    const auto want = instruction_src_paths(workspace_root, sess);
    std::unordered_set<std::string> have;
    for (const auto& p : st.pending) {
      have.insert(p.path);
    }
    for (const auto& p : st.edited_paths) {
      have.insert(p);
    }
    std::vector<std::string> miss;
    for (const auto& p : want) {
      if (!have.count(p)) {
        miss.push_back(p);
      }
    }
    if (!miss.empty()) {
      block << "Instruction también pide path(s) sin hunk en este intento:";
      for (const auto& p : miss) {
        block << " `" << p << "`";
      }
      block << "\nNo mezcles declaración + definición en un solo archivo: hunk de `foo();` en "
               "el `.hpp` y `foo() { … }` en el `.cpp` hermano.\n\n";
      for (const auto& p : miss) {
        if (p.size() > 1) {
          const std::string body =
              read_file_raw((fs::path(workspace_root) / p).lexically_normal().string());
          const std::string excerpt = disk_excerpt_near_search(body, "namespace tuide {", 1, 14);
          if (!excerpt.empty()) {
            block << "## on disk `" << p << "` (base del search en el hermano)\n\n```\n"
                  << truncate_observation(excerpt, 16) << "```\n\n";
          }
        }
      }
    }
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
    st.phase = "edit";
    st.compile_attempt = 0;
    st.map_review = false;
    st.last_action = "compile_fail_rollback";
    std::ostringstream extra;
    extra << "### turn " << st.turn << " — compile_fail_rollback\n\n";
    extra << "Agotados " << kMaxCompileAttempts
          << " compiles. Baseline restaurado. Reemite un hunk Aider "
             "(SEARCH = código actual en disco; no copies stderr entero).\n\n";
    append_observation(workspace_root, extra.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "compile", "", "", extra.str(),
                        "compile rollback; re-edit", st.turn, "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"compile_fail\",\"exit\":" +
                                     std::to_string(exit_code) + ",\"attempt\":" +
                                     std::to_string(kMaxCompileAttempts) + ",\"ms\":" +
                                     std::to_string(compile_ms) +
                                     ",\"rollback\":1,\"resume\":\"edit\"}");
    std::ostringstream summary;
    summary << "FAIL compile x" << kMaxCompileAttempts
            << "; rollback al baseline pre-hunk. Reemite edit sobre el baseline.";
    out.ok = false;
    out.phase = "edit";
    out.summary = summary.str();
    out.error = summary.str();
    out.turn = st.turn;
    return out;
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

Level2TurnResult Level2Session::mark_pack_review(const std::string& workspace_root, bool ok,
                                                 const std::string& summary) {
  Level2TurnResult out;
  out.action = "review";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  ++st.pack_review_cycles;
  st.pack_review_ok = ok;
  st.last_action = ok ? "pack_review_ok" : "pack_review_miss";
  if (!ok) {
    st.consecutive_complete_plans = 0;
    st.edit_nudge_sent = false;
  }
  ++st.turn;
  out.turn = st.turn;
  std::ostringstream block;
  block << "### turn " << st.turn << " — pack_review " << (ok ? "covered" : "miss") << "\n\n";
  if (!summary.empty()) {
    block << summary << "\n\n";
  }
  if (!ok) {
    block << "Review: el pack NO cubre la Instruction. Emite `action=plan` con targets NUEVOS "
             "anclados a los hits de search abajo (path:Symbol). No repitas el mismo plan.\n\n";
  } else {
    block << "Review: pack cubre la Instruction. Puedes emitir `done next=edit`.\n\n";
  }
  std::string err;
  append_observation(workspace_root, block.str(), &out.session_chars, &err);
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = ok ? "pack_review covered" : "pack_review miss";
  return out;
}

Level2TurnResult Level2Session::add_review_search_terms(const std::string& workspace_root,
                                                        const std::vector<std::string>& terms) {
  Level2TurnResult out;
  out.action = "review_search";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty() || terms.empty()) {
    out.ok = true;
    return out;
  }
  std::unordered_set<std::string> seen;
  for (const auto& u : st.review_search_terms) {
    seen.insert(u);
  }
  for (const auto& t : terms) {
    if (t.empty() || seen.count(t)) {
      continue;
    }
    seen.insert(t);
    st.review_search_terms.push_back(t);
  }
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = "review_search_terms +" + std::to_string(terms.size());
  return out;
}

Level2TurnResult Level2Session::add_rejected_targets(const std::string& workspace_root,
                                                     const std::vector<std::string>& targets) {
  Level2TurnResult out;
  out.action = "review_reject";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty() || targets.empty()) {
    out.ok = true;
    return out;
  }
  for (const auto& t : targets) {
    if (t.empty()) {
      continue;
    }
    if (std::find(st.rejected_targets.begin(), st.rejected_targets.end(), t) ==
        st.rejected_targets.end()) {
      st.rejected_targets.push_back(t);
    }
  }
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = "rejected_targets +" + std::to_string(targets.size());
  return out;
}

Level2TurnResult Level2Session::prune_watchlist_after_review(const std::string& workspace_root,
                                                           const std::vector<std::string>&
                                                               reject_extra) {
  Level2TurnResult out;
  out.action = "prune";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  for (const auto& t : reject_extra) {
    if (t.empty()) {
      continue;
    }
    if (std::find(st.rejected_targets.begin(), st.rejected_targets.end(), t) ==
        st.rejected_targets.end()) {
      st.rejected_targets.push_back(t);
    }
  }
  std::vector<std::string> kept;
  kept.reserve(st.watchlist.size());
  for (const auto& t : st.watchlist) {
    if (target_in_rejected_normalized(t, st.rejected_targets)) {
      continue;
    }
    kept.push_back(t);
  }
  const int pruned = static_cast<int>(st.watchlist.size()) - static_cast<int>(kept.size());
  st.watchlist = std::move(kept);
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = "watchlist pruned=" + std::to_string(pruned) +
                " rejected=" + std::to_string(st.rejected_targets.size());
  return out;
}

Level2TurnResult Level2Session::unreject_matching(const std::string& workspace_root,
                                                  const std::vector<std::string>& protect) {
  Level2TurnResult out;
  out.action = "unreject";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty() || protect.empty() || st.rejected_targets.empty()) {
    out.ok = true;
    out.summary = "unreject 0";
    return out;
  }
  std::vector<std::string> kept;
  int removed = 0;
  for (const auto& r : st.rejected_targets) {
    if (target_in_watchlist_normalized(r, protect) ||
        target_in_rejected_normalized(r, protect)) {
      ++removed;
      continue;
    }
    bool hit = false;
    for (const auto& p : protect) {
      if (target_in_watchlist_normalized(p, {r})) {
        hit = true;
        break;
      }
    }
    if (hit) {
      ++removed;
      continue;
    }
    kept.push_back(r);
  }
  st.rejected_targets = std::move(kept);
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = "unreject " + std::to_string(removed);
  return out;
}

Level2TurnResult Level2Session::reset_watchlist_priority(
    const std::string& workspace_root, const std::vector<std::string>& priority_targets,
    bool reset_review_cycles) {
  Level2TurnResult out;
  out.action = "priority_reset";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty() || priority_targets.empty()) {
    out.error = "priority_targets vacío";
    return out;
  }
  std::vector<std::string> next;
  std::unordered_set<std::string> seen;
  for (const auto& raw : priority_targets) {
    const std::string t = trim_ws(raw);
    if (t.empty() || !seen.insert(t).second) {
      continue;
    }
    next.push_back(t);
    if (static_cast<int>(next.size()) >= kL2MaxPlanTargets) {
      break;
    }
  }
  st.watchlist = std::move(next);
  if (reset_review_cycles) {
    st.pack_review_cycles = 0;
    st.pack_review_ok = false;
  }
  save_state(workspace_root, st, nullptr);
  out.ok = true;
  out.summary = "watchlist priority n=" + std::to_string(st.watchlist.size()) +
                (reset_review_cycles ? " review_cycles=0" : "");
  return out;
}

Level2TurnResult Level2Session::force_phase_edit(const std::string& workspace_root,
                                                 const std::string& reason) {
  Level2TurnResult out;
  out.action = "done";
  State st = load_state(workspace_root);
  out.phase = st.phase;
  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (st.done || st.phase == "done" || st.phase == "clarify") {
    out.error = "sesión done; reinicia con bootstrap";
    return out;
  }
  if (ai_workflow_is_readonly(parse_ai_workflow_kind(st.workflow))) {
    out.error = "force_phase_edit no permitido en workflow=" + st.workflow;
    return out;
  }
  compact_session_context(workspace_root, nullptr);
  ++st.turn;
  st.phase = "edit";
  st.done = false;
  st.last_action = "ready_to_edit";
  st.pack_incomplete = false;
  st.map_review = false;
  out.turn = st.turn;
  std::ostringstream block;
  block << "### turn " << st.turn << " — ready_to_edit (force)\n\n";
  block << (reason.empty() ? "auto phase=edit" : reason) << "\n\n";
  std::string err;
  append_observation(workspace_root, block.str(), &out.session_chars, &err);
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, true, "done", "force_edit", "", reason, "", st.turn, "edit");
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"force_phase_edit\",\"turn\":" +
                                   std::to_string(st.turn) + "}");
  out.ok = true;
  out.phase = "edit";
  out.summary = "phase=edit";
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
    if (ai_workflow_is_readonly(parse_ai_workflow_kind(st.workflow))) {
      out.error = "next=edit no permitido en workflow=" + st.workflow +
                  " (usa action=synthesize)";
      return out;
    }
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
    if (st.phase != "explore" && st.phase != "edit" && st.phase != "compile") {
      out.error = "next=clarify solo desde explore|edit|compile";
      return out;
    }
    if (l2_feat::enabled("CLARIFY_NEED_PATH")) {
      const std::string sess = read_file(session_path(workspace_root));
      const auto paths = instruction_src_paths(workspace_root, sess);
      // Explicit existing src/ paths → treat as must-edit: keep pushing clarify.
      if (!paths.empty() && deps_.clarify_pushback_max > 0 &&
          st.clarify_pushback < deps_.clarify_pushback_max) {
        // fall through to normal pushback below
      } else if (!paths.empty() && deps_.clarify_pushback_max > 0 &&
                 st.clarify_pushback >= deps_.clarify_pushback_max) {
        // Exhausted pushbacks but paths exist: one more hard pushback as edit nudge.
        ++st.turn;
        st.last_action = "clarify_need_path";
        out.turn = st.turn;
        std::ostringstream block;
        block << "### turn " << st.turn << " — clarify_need_path\n\n";
        block << "Clarify rechazado: la Instruction ya nombra paths existentes. "
                 "Emite `plan`/`edit` sobre:\n";
        for (const auto& p : paths) {
          block << "- `" << p << "`\n";
        }
        block << "\n";
        std::string err;
        append_observation(workspace_root, block.str(), &out.session_chars, &err);
        save_state(workspace_root, st, nullptr);
        write_response_json(workspace_root, false, "done", "clarify_need_path", "", block.str(),
                            "paths presentes; no clarify", st.turn, st.phase);
        out.ok = true;
        out.phase = st.phase;
        out.summary = "clarify_need_path";
        return out;
      }
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
    st.continuable = true;
    st.resume = false;
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — clarify (arreglo cancelado)\n\n";
    block << summary << "\n\n";
    block << "_No se pasa a edit/compile. El usuario puede aclarar en un follow-up (mismo chat) o Reset._\n\n";
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
  // Final done (no next): coverage gate (paths and/or Instruction markers).
  if (post_edit_coverage_enabled()) {
    const auto gaps =
        collect_instruction_coverage_gaps(workspace_root, st.edited_paths, st.applied_blob);
    if (coverage_gate_should_block(workspace_root, gaps) &&
        st.coverage_gate_pushback < kMaxCoverageGatePushbacks) {
      ++st.coverage_gate_pushback;
      st.last_action = "done_coverage_gate";
      out.turn = st.turn;
      const std::string bodies =
          fetch_coverage_bodies(deps_, workspace_root, gaps, st.edited_paths);
      const std::string cov =
          format_coverage_observation(st.turn, "done_coverage_gate", gaps, bodies);
      std::string err;
      append_observation(workspace_root, cov, &out.session_chars, &err);
      save_state(workspace_root, st, nullptr);
      write_response_json(workspace_root, false, "done", "done_coverage_gate", "", cov,
                          "instruction coverage incomplete", st.turn, st.phase);
      out.ok = true;
      out.phase = st.phase;
      out.summary = "done_coverage_gate";
      out.error = "done_coverage_gate";
      return out;
    }
  } else if (l2_feat::enabled("DONE_PATH_GATE")) {
    const std::string sess = read_file(session_path(workspace_root));
    const auto want = instruction_src_paths(workspace_root, sess);
    const auto miss = missing_instruction_paths(workspace_root, st.edited_paths);
    if (want.size() >= 2 && !miss.empty()) {
      st.last_action = "done_path_gate";
      out.turn = st.turn;
      std::ostringstream block;
      block << "### turn " << st.turn << " — done_path_gate\n\n";
      block << "Done rechazado: faltan paths de la Instruction sin editar:\n";
      for (const auto& p : miss) {
        block << "- `" << p << "`\n";
      }
      block << "\nEmite `action=edit` para cubrirlos o aclara por qué no aplican.\n\n";
      std::string err;
      append_observation(workspace_root, block.str(), &out.session_chars, &err);
      save_state(workspace_root, st, nullptr);
      write_response_json(workspace_root, false, "done", "done_path_gate", "", block.str(),
                          "faltan paths Instruction", st.turn, st.phase);
      out.ok = true;
      out.phase = st.phase;
      out.summary = "done_path_gate";
      out.error = "done_path_gate";
      return out;
    }
  }
  st.done = true;
  st.phase = "done";
  st.last_action = "done";
  st.continuable = true;
  st.resume = false;
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

Level2TurnResult Level2Session::apply_synthesize(const std::string& workspace_root,
                                                 const std::string& text) {
  Level2TurnResult out;
  out.action = "synthesize";
  out.summary = text;
  State st = load_state(workspace_root);
  out.phase = st.phase;

  if (workspace_root.empty()) {
    out.error = "workspace_root vacío";
    return out;
  }
  if (st.done || st.phase == "done" || st.phase == "clarify") {
    out.error = "sesión ya cerrada";
    return out;
  }
  const AiWorkflowKind wf = parse_ai_workflow_kind(st.workflow);
  if (!ai_workflow_is_readonly(wf)) {
    out.error = "synthesize solo en workflow ask|plan|git (ahora=" + st.workflow + ")";
    return out;
  }
  if (st.phase != "explore" && st.phase != "edit") {
    out.error = "synthesize solo desde explore (ahora=" + st.phase + ")";
    return out;
  }
  if (text.empty()) {
    out.error = "synthesize sin texto";
    return out;
  }

  ++st.turn;
  st.done = true;
  st.phase = "done";
  st.last_action = "synthesize";
  st.continuable = true;
  st.resume = false;
  out.turn = st.turn;

  const char* heading =
      wf == AiWorkflowKind::Plan ? "plan" : (wf == AiWorkflowKind::Git ? "git_answer" : "answer");
  std::ostringstream block;
  block << "### turn " << st.turn << " — synthesize (" << heading << ")\n\n";
  block << text << "\n\n";

  std::string err;
  if (!append_observation(workspace_root, block.str(), &out.session_chars, &err)) {
    out.error = err.empty() ? "no se pudo escribir synthesize" : err;
    return out;
  }
  {
    const std::string answer_file = answer_path(workspace_root);
    const std::string prev = read_file(answer_file);
    std::ostringstream ans;
    if (prev.empty()) {
      ans << "# L2 " << heading << "\n\n" << text << "\n";
    } else {
      ans << prev;
      if (prev.back() != '\n') {
        ans << '\n';
      }
      ans << "\n## Follow-up answer\n\n" << text << "\n";
    }
    write_file(answer_file, ans.str(), nullptr);
  }
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, true, "synthesize", heading, "", text, "", st.turn, "done");
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"synthesize\",\"workflow\":\"" + st.workflow +
                                   "\",\"chars\":" + std::to_string(text.size()) + "}");
  ai_trace(AiTraceChannel::L2, "l2_synthesize",
           "{\"workflow\":\"" + st.workflow + "\",\"chars\":" + std::to_string(text.size()) + "}");
  out.ok = true;
  out.phase = "done";
  out.summary = text;
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
    if (action == "synthesize" || action == "answer" || action == "explain" ||
        action == "plan_doc") {
      return apply_synthesize(workspace_root,
                              j.value("summary", j.value("text", j.value("answer", ""))));
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
      << "  workflow: " << (st.workflow.empty() ? "agent" : st.workflow) << "\n";
  out << "edit_attempt=" << st.edit_attempt << " compile_attempt=" << st.compile_attempt
      << " pending_hunks=" << st.pending.size() << "\n";
  out << "has_pack: " << (st.has_pack ? "yes" : "no")
      << "  pack_incomplete: " << (st.pack_incomplete ? "yes" : "no")
      << "  map_stale: " << (st.map_stale ? "yes" : "no")
      << "  map_review: " << (st.map_review ? "yes" : "no") << "\n";
  out << "continuable: " << (st.continuable ? "yes" : "no")
      << "  resume: " << (st.resume ? "yes" : "no")
      << "  followups: " << st.followup_count << "\n";
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
      << " workflow=" << (st.workflow.empty() ? "agent" : st.workflow)
      << " has_pack=" << (st.has_pack ? "yes" : "no")
      << " pack_incomplete=" << (st.pack_incomplete ? "yes" : "no")
      << " map_stale=" << (st.map_stale ? "yes" : "no")
      << " resume=" << (st.resume ? "yes" : "no")
      << " continuable=" << (st.continuable ? "yes" : "no")
      << " covered_path_rejects=" << st.covered_path_rejects
      << " pack_review_ok=" << (st.pack_review_ok ? "yes" : "no")
      << " pack_review_cycles=" << st.pack_review_cycles;
  return out.str();
}

}  // namespace tuide
