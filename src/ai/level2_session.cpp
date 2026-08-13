#include "ai/level2_session.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
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

int score_symbol_against_needles(const std::string& sym, const std::vector<std::string>& needles) {
  std::string s = sym;
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  int score = 0;
  for (const auto& n : needles) {
    if (n.size() < 3) {
      continue;
    }
    if (s == n) {
      score += 100;
    } else if (s.find(n) != std::string::npos || n.find(s) != std::string::npos) {
      score += 40;
    }
  }
  return score;
}

// Pick best path:Symbol from file_outline text using Instruction needles.
std::string best_symbol_target_from_outline(const std::string& path, const std::string& outline,
                                            const std::vector<std::string>& needles) {
  std::string best_sym;
  int best = -1;
  std::istringstream in(outline);
  std::string line;
  while (std::getline(in, line)) {
    // Common outline forms: "  foo" / "N:foo" / "foo(" / "`foo`"
    std::string cand;
    for (char c : line) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':') {
        cand.push_back(c);
      } else if (!cand.empty()) {
        break;
      }
    }
    if (cand.size() < 3) {
      continue;
    }
    // Skip path-like tokens.
    if (cand.find('/') != std::string::npos || cand.find('.') != std::string::npos) {
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
  return path + ":" + best_sym;
}

std::vector<std::string> instruction_needles_from_session(const std::string& session_body) {
  const auto instr = session_body.find("## Instruction");
  const auto map = session_body.find("## Ranked map");
  std::string slice;
  if (instr != std::string::npos) {
    const auto end = (map != std::string::npos && map > instr) ? map : session_body.size();
    slice = session_body.substr(instr, end - instr);
  }
  return tokenize_needles(slice);
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
inicial y pregunta si falta algo. Si explore **no** localiza el código: **clarify**.

### Explore (primera mirada = plan)
Con el ## Ranked map, emite **un plan** `path:Symbol` o `path:A-B` (evitar path bare). El
runtime **merge** de packs, prioriza fragmentos pequeños, tope ~25%/pieza, **auto-refetch**
de truncados y marca `pack_incomplete` si quedan huecos. Si `map_stale=1`, no confíes en
el top del mapa: `search` / plan anclado a la Instruction.

```json
{"action":"plan","targets":["src/a.cpp:Foo","src/b.cpp:42","src/c.hpp:Bar"],"summary":"…"}
```
Máx. 16 targets. Tras el pack → Instruction+pack (sin mapa completo). Extras: `tools` (máx. 4).
Si `[TRUNCATED]` / `## Truncated`: refetch `path:A-B` / `#mid|#tail` antes de inventar.

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
- `done next=edit` con `pack_incomplete` puede rechazarse (pushback; refetch truncados).

Edit / tras pack:
```json
{"action":"edit","hunks":[{"path":"src/foo.cpp","search":"…exact…","replace":"…"}]}
{"action":"plan","targets":["…"]}
{"action":"done","summary":"cambios listos: paths…"}
```
- Zona en Truncated → refetch antes del hunk.
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
                      {"has_pack", st.has_pack},
                      {"pack_incomplete", st.pack_incomplete},
                      {"map_stale", st.map_stale},
                      {"map_review", st.map_review},
                      {"last_op_id", st.last_op_id},
                      {"watchlist", st.watchlist},
                      {"pending", pending}};
  return write_file(state_path(workspace_root), j.dump(2) + "\n", err);
}

std::string Level2Session::truncate_observation(const std::string& text, int max_lines) {
  if (max_lines <= 0) {
    return text;
  }
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
  return out.str();
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
  std::string obs_section = body.substr(obs_pos);
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
  while (drop < turns.size() && head.size() + obs_section.size() > kCharBudget) {
    ++drop;
    std::ostringstream rebuilt;
    rebuilt << "## Observations\n\n<!-- older observations trimmed (" << drop << ") -->\n\n";
    if (drop < turns.size()) {
      rebuilt << obs_section.substr(turns[drop]);
    }
    obs_section = rebuilt.str();
  }
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
    md << "**map_stale=1**: el ## Ranked map parece de otra query (`" << map_query
       << "`; overlap=" << static_cast<int>(overlap * 100)
       << "%). No confíes en el top del mapa: usa `search` / `plan` anclado a esta Instruction.\n\n";
  }
  md << "Fase inicial: **explore**. Primera mirada = `action=plan` con targets "
        "`path:Symbol` o `path:A-B` (evitar path bare). El runtime arma un pack "
        "(fragmentos + outlines; merge si ya había pack). Luego "
        "`{\"action\":\"done\",\"summary\":\"…\",\"next\":\"edit\"}` o `edit` directo.\n\n";
  md << "## Ranked map\n\n";
  md << map_body;
  if (!map_body.empty() && map_body.back() != '\n') {
    md << '\n';
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
  st.map_stale = map_stale;
  st.map_review = false;
  st.watchlist.clear();
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

  const auto t0 = std::chrono::steady_clock::now();
  const AiToolResult tr = deps_.tools->invoke(name, arg);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  ++st.turn;
  st.last_action = "tool:" + name;
  out.turn = st.turn;

  const std::string obs_text = truncate_observation(tr.text, kMaxObservationLines);
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
  // Shrink ranked map: name-only except stems/paths this explore already touched.
  if (st.phase == "explore") {
    compact_session_context(workspace_root, nullptr);
    out.session_chars = read_file(session_path(workspace_root)).size();
  }
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

    std::string obs_text = truncate_observation(tr.text, kMaxObservationLinesBatch);
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
    out.session_chars = read_file(session_path(workspace_root)).size();
  }

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
  const auto needles = instruction_needles_from_session(session_body);

  // Merge with previous watchlist / pack header (plan2 does not wipe plan1).
  std::vector<std::string> merged = st.watchlist;
  if (merged.empty()) {
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

  // Normalize bare paths → path:BestSymbol via outline + Instruction needles.
  std::vector<std::string> uniq_targets;
  uniq_targets.reserve(merged.size());
  std::vector<std::string> normalize_notes;
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
      if (!resolved.empty()) {
        normalize_notes.push_back("- bare `" + t + "` → `" + resolved + "` (needles)");
        t = resolved;
      } else {
        normalize_notes.push_back(
            "- bare `" + t +
            "` sin símbolo claro — preferir `path:Symbol` / `path:A-B` en el próximo plan");
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
    const std::string p = path_from_plan_target(t);
    if (!p.empty() && seen_paths.insert(p).second) {
      uniq_paths.push_back(p);
    }
  }

  struct Frag {
    std::string target;
    std::string text;
    bool ok = false;
    bool truncated = false;
    std::string refetch;
    std::size_t rank_size = 0;
  };
  struct Outline {
    std::string path;
    std::string text;
    bool ok = false;
  };

  const auto plan_t0 = std::chrono::steady_clock::now();
  std::vector<Frag> frags;
  frags.reserve(uniq_targets.size() + 4);
  for (const auto& t : uniq_targets) {
    Frag f;
    f.target = t;
    if (!deps_.tools->has("get_code_of")) {
      f.text = "error: get_code_of no registrado";
    } else {
      const AiToolResult tr = deps_.tools->invoke("get_code_of", t);
      f.ok = tr.ok;
      f.text = tr.text.empty() ? (tr.ok ? "(vacío)" : "error get_code_of") : tr.text;
      f.truncated = text_looks_truncated(f.text);
      if (f.truncated) {
        f.refetch = extract_refetch_hint(f.text, t);
      }
      const std::string got_name = resolved_name_from_tool_text(f.text);
      if (f.ok && !name_matches_needles(got_name, needles)) {
        f.text +=
            "\nWARN wrong_symbol: resuelto `" + got_name +
            "` no solapa Instruction — revisa outline / pide path:Symbol concreto.\n";
      }
    }
    f.rank_size = f.text.size();
    frags.push_back(std::move(f));
  }

  // Auto-refetch truncated gaps once (explore-fill without extra propose).
  std::vector<Frag> extras;
  for (const auto& f : frags) {
    if (!f.truncated || f.refetch.empty() || !deps_.tools->has("get_code_of")) {
      continue;
    }
    if (std::find(uniq_targets.begin(), uniq_targets.end(), f.refetch) != uniq_targets.end()) {
      continue;
    }
    const AiToolResult tr = deps_.tools->invoke("get_code_of", f.refetch);
    Frag e;
    e.target = f.refetch;
    e.ok = tr.ok;
    e.text = tr.text.empty() ? "(vacío)" : tr.text;
    e.truncated = text_looks_truncated(e.text);
    e.refetch = extract_refetch_hint(e.text, f.refetch);
    e.rank_size = e.text.size();
    extras.push_back(std::move(e));
  }
  for (auto& e : extras) {
    frags.push_back(std::move(e));
  }

  std::stable_sort(frags.begin(), frags.end(),
                   [](const Frag& a, const Frag& b) { return a.rank_size < b.rank_size; });

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
  pack << "targets (" << uniq_targets.size() << "):";
  for (const auto& t : uniq_targets) {
    pack << " `" << t << "`";
  }
  pack << "\n\n## Fragments\n\n";

  std::size_t used_frags = 0;
  int frag_ok = 0;
  std::vector<std::string> trunc_index;
  for (std::size_t i = 0; i < frags.size(); ++i) {
    const auto& f = frags[i];
    if (f.ok) {
      ++frag_ok;
    }
    const std::size_t remaining =
        frag_budget > used_frags ? frag_budget - used_frags : 0;
    if (remaining < 80) {
      pack << "_pack: fragmentos restantes omitidos por presupuesto (" << (frags.size() - i)
           << ")._\n\n";
      for (std::size_t j = i; j < frags.size(); ++j) {
        trunc_index.push_back("- `" + frags[j].target +
                              "` — omitido por presupuesto pack; refetch get_code_of `" +
                              frags[j].target + "`");
      }
      break;
    }
    const std::size_t fair =
        std::max<std::size_t>(120, remaining / std::max<std::size_t>(1, frags.size() - i));
    const std::size_t per = std::min(fair, kMaxFragShareChars);
    const bool pack_trunc = f.text.size() > per;
    std::string body = truncate_to_budget(f.text, per, f.truncated ? f.refetch : f.target);
    const bool is_trunc = f.truncated || pack_trunc;
    if (is_trunc) {
      std::string tip = !f.refetch.empty() ? f.refetch : (f.target + "#mid");
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
    sec << "\n\n```\n" << body;
    if (!body.empty() && body.back() != '\n') {
      sec << '\n';
    }
    sec << "```\n\n";
    pack << sec.str();
    used_frags += sec.str().size();
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
    std::string body = truncate_to_budget(o.text, per, {});
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

  if (!headers.empty() && outline_budget > 120) {
    pack << "## Headers\n\n";
    for (const auto& h : headers) {
      if (outline_budget < 60) {
        break;
      }
      std::string body = truncate_to_budget(h.text, std::min<std::size_t>(outline_budget, 400), {});
      std::ostringstream sec;
      sec << "### headers_of `" << h.path << "`\n\n```\n" << body;
      if (!body.empty() && body.back() != '\n') {
        sec << '\n';
      }
      sec << "```\n\n";
      const std::string s = sec.str();
      if (s.size() > outline_budget) {
        break;
      }
      pack << s;
      outline_budget -= s.size();
    }
  }

  if (!trunc_index.empty()) {
    pack << "## Truncated (refetch before editing these)\n\n";
    pack << "Cuerpos incompletos (" << trunc_index.size() << "). No inventes código. "
            "Pide el hueco con `get_code_of path:A-B` o `path:Symbol#mid|#tail`.\n\n";
    for (const auto& line : trunc_index) {
      pack << line << "\n";
    }
    pack << "\n";
  }

  std::string pack_body = pack.str();
  if (pack_body.size() > budget) {
    pack_body = truncate_to_budget(std::move(pack_body), budget, {});
  }
  std::string err;
  if (!write_file(pack_path(workspace_root), pack_body, &err)) {
    out.error = err.empty() ? "no se pudo escribir pack.md" : err;
    return out;
  }

  ++st.turn;
  st.last_action = "plan";
  st.has_pack = true;
  st.pack_incomplete = !trunc_index.empty();
  st.map_review = false;
  st.watchlist = uniq_targets;
  out.turn = st.turn;

  std::ostringstream block;
  block << "### turn " << st.turn << " — plan\n\n";
  if (!summary.empty()) {
    block << summary << "\n\n";
  }
  block << "targets: " << uniq_targets.size() << "  fragments_ok: " << frag_ok << "/"
        << frags.size() << "  outlines_ok: " << outline_ok << "/" << uniq_paths.size()
        << "  truncated: " << trunc_index.size() << "  pack_chars: " << pack_body.size() << "/"
        << budget << "  auto_refetch: " << extras.size() << "\n";
  block << "Archivo: `.tuide/ai/l2/pack.md`. El siguiente prompt usa Instruction+pack "
           "(mapa fuera). ";
  if (st.pack_incomplete) {
    block << "**pack_incomplete=1:** quedan truncados — refetch / otro `plan` antes de "
             "`done next=edit` (pushback activo). ";
  }
  block << "Emite `done next=edit`, `edit`, o amplía con otro `plan`/`tools`.\n\n";
  if (!append_observation(workspace_root, block.str(), &out.session_chars, &err)) {
    out.error = err;
    return out;
  }

  compact_session_context(workspace_root, nullptr);
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
  auto record_edit_failure = [&](const std::string& err_msg, const std::string& path,
                                 const std::string& search, const std::string& replace) {
    const auto ms = edit_ms();
    ++st.turn;
    st.last_action = "edit_feedback";
    out.turn = st.turn;
    out.error = err_msg;
    out.summary = err_msg;
    out.ok = false;
    out.phase = "edit";

    std::ostringstream block;
    block << "### turn " << st.turn << " — edit_feedback\n\n";
    block << "error: " << err_msg << "\n\n";
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
    block << "Reemite `action=edit` con `search` exacto y único (get_code_of si hace falta). "
             "No repitas el mismo hunk fallido.\n\n";
    std::string obs_err;
    append_observation(workspace_root, block.str(), &out.session_chars, &obs_err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "edit", "", path, block.str(), err_msg, st.turn,
                        "edit");
    append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                     ",\"event\":\"edit_fail\",\"turn\":" +
                                     std::to_string(st.turn) + ",\"ms\":" + std::to_string(ms) +
                                     ",\"error\":\"" + json_escape(err_msg) + "\"}");
    ai_trace(AiTraceChannel::L2, "l2_edit",
             "{\"turn\":" + std::to_string(st.turn) + ",\"ok\":0,\"hunks\":" +
                 std::to_string(hunks.size()) + ",\"duration_ms\":" + std::to_string(ms) +
                 ",\"error\":\"" + ai_trace_escape(err_msg) + "\"}");
  };

  if (hunks.empty()) {
    record_edit_failure("hunks vacío", "", "", "");
    return out;
  }

  std::vector<ApplyHunkResult> applied;
  applied.reserve(hunks.size());
  for (const auto& h : hunks) {
    ApplyHunkResult r = apply_hunk_to_workspace_file(workspace_root, h, /*write=*/true);
    if (!r.ok) {
      // Rollback any already written hunks in this batch.
      for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
        write_text_file(it->abs_path, it->before, nullptr);
      }
      record_edit_failure("hunk falló (" + h.path + "): " + r.error, h.path, h.search, h.replace);
      return out;
    }
    if (deps_.sync_edit) {
      deps_.sync_edit(r);
    }
    applied.push_back(std::move(r));
  }

  ++st.turn;
  ++st.edit_attempt;
  st.last_action = "edit";
  st.last_op_id = static_cast<uint64_t>(st.turn);
  st.pending.clear();
  for (std::size_t i = 0; i < applied.size(); ++i) {
    PendingHunk p;
    p.path = hunks[i].path;
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
  for (std::size_t i = 0; i < applied.size(); ++i) {
    block << "#### hunk " << (i + 1) << " `" << hunks[i].path << "` @"
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
    block << "Compile OK **no** cierra la tarea. Se restauró el **mapa inicial completo**.\n"
             "¿Algo más?\n"
             "- Más sitios → `action=plan` con nuevos targets (arma otro pack).\n"
             "- Más cambios → `action=edit` (o tools + edit).\n"
             "- Instruction cubierta → "
             "`{\"action\":\"done\",\"summary\":\"…qué cambiaste…\"}` (sin next).\n\n";
    std::string err;
    append_observation(workspace_root, block.str(), &out.session_chars, &err);

    // Restore full initial map for "¿algo más?" review.
    const std::string initial_map = read_file(map_initial_path(workspace_root));
    if (!initial_map.empty()) {
      std::string map_err;
      if (replace_ranked_map_in_session(session_path(workspace_root), initial_map, &map_err)) {
        out.session_chars = read_file(session_path(workspace_root)).size();
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
  for (std::size_t i = 0; i < st.pending.size(); ++i) {
    const auto& p = st.pending[i];
    block << "## old (hunk " << (i + 1) << " `" << p.path << "`)\n\n```\n"
          << truncate_observation(p.old_text, 24) << "```\n\n";
    block << "## new (hunk " << (i + 1) << " `" << p.path << "`)\n\n```\n"
          << truncate_observation(p.new_text, 24) << "```\n\n";
  }
  block << "Reemite `action=edit` corrigiendo el error (o el runtime hará rollback si se agotan "
           "intentos).\n\n";
  std::string err;
  append_observation(workspace_root, block.str(), &out.session_chars, &err);

  if (st.compile_attempt >= kMaxCompileAttempts) {
    // Rollback files to before.
    for (const auto& p : st.pending) {
      if (!p.before.empty() && !p.abs_path.empty()) {
        write_text_file(p.abs_path, p.before, nullptr);
      }
    }
    st.phase = "done";
    st.done = true;
    st.last_action = "compile_fail_rollback";
    save_state(workspace_root, st, nullptr);
    std::ostringstream summary;
    summary << "FAIL compile x" << st.compile_attempt << "; rollback aplicado.";
    std::ostringstream done_block;
    done_block << "### turn " << (++st.turn) << " — done\n\n" << summary.str() << "\n\n";
    append_observation(workspace_root, done_block.str(), &out.session_chars, &err);
    save_state(workspace_root, st, nullptr);
    write_response_json(workspace_root, false, "done", "compile", "", summary.str(), summary.str(),
                        st.turn, "done");
    out.ok = false;
    out.action = "done";
    out.summary = summary.str();
    out.phase = "done";
    out.turn = st.turn;
    out.error = summary.str();
    return out;
  }

  st.phase = "edit";
  st.last_action = "compile_feedback";
  save_state(workspace_root, st, nullptr);
  write_response_json(workspace_root, false, "compile", "", "", trunc,
                      "compile failed; re-edit", st.turn, "edit");
  append_trace(workspace_root, std::string("{\"ts\":") + now_ms_str() +
                                   ",\"event\":\"compile_fail\",\"exit\":" +
                                   std::to_string(exit_code) + ",\"attempt\":" +
                                   std::to_string(st.compile_attempt) + ",\"ms\":" +
                                   std::to_string(compile_ms) + "}");
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
                                          const std::string& summary, const std::string& next) {
  Level2TurnResult out;
  out.action = "done";
  out.summary = summary;
  State st = load_state(workspace_root);
  out.phase = st.phase;

  if (next == "edit") {
    if (st.phase != "explore") {
      out.error = "next=edit solo desde explore";
      return out;
    }
    const int max_pack_push = deps_.pack_incomplete_pushback_max;
    if (st.pack_incomplete && max_pack_push > 0 &&
        st.pack_incomplete_pushback < max_pack_push) {
      ++st.pack_incomplete_pushback;
      ++st.turn;
      st.last_action = "pack_incomplete_pushback";
      out.turn = st.turn;
      std::ostringstream block;
      block << "### turn " << st.turn << " — pack_incomplete_pushback ("
            << st.pack_incomplete_pushback << "/" << max_pack_push << ")\n\n";
      block << "Pack incompleto (truncados pendientes). Motivo del modelo: " << summary << "\n\n";
      block << "Antes de `done next=edit`, refetch huecos (`get_code_of path:A-B` / "
               "`path:Symbol#mid`) o amplía con `action=plan`. Mira `## Truncated` en pack.md.\n\n";
      std::string err;
      append_observation(workspace_root, block.str(), &out.session_chars, &err);
      save_state(workspace_root, st, nullptr);
      write_response_json(workspace_root, false, "done", "pack_incomplete_pushback", "",
                          block.str(), "pack incomplete; refetch truncados", st.turn, st.phase);
      append_trace(workspace_root,
                   std::string("{\"ts\":") + now_ms_str() +
                       ",\"event\":\"pack_incomplete_pushback\",\"n\":" +
                       std::to_string(st.pack_incomplete_pushback) + ",\"max\":" +
                       std::to_string(max_pack_push) + "}");
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
    out.turn = st.turn;
    std::ostringstream block;
    block << "### turn " << st.turn << " — ready_to_edit\n\n";
    block << summary << "\n\n";
    block << "Fase **edit**: emite `action=edit` con hunks Search/Replace. "
             "Si el pack marcó [TRUNCATED] en la zona a editar, refetch antes.\n\n";
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

}  // namespace tuide
