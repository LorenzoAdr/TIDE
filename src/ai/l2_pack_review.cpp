#include "ai/l2_pack_review.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace tuide {
namespace {

std::string trim_ws(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

std::string to_snake_token(std::string s) {
  s = trim_ws(s);
  std::string out;
  out.reserve(s.size());
  bool prev_lower = false;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == '-' || c == '/') {
      if (!out.empty() && out.back() != '_') {
        out.push_back('_');
      }
      prev_lower = false;
      continue;
    }
    if (c >= 'A' && c <= 'Z') {
      if (prev_lower && !out.empty() && out.back() != '_') {
        out.push_back('_');
      }
      out.push_back(static_cast<char>(c - 'A' + 'a'));
      prev_lower = false;
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      out.push_back(c);
      prev_lower = c >= 'a' && c <= 'z';
    }
  }
  while (!out.empty() && out.back() == '_') {
    out.pop_back();
  }
  return out;
}

std::string extract_json_object(const std::string& raw) {
  const auto a = raw.find('{');
  const auto b = raw.rfind('}');
  if (a == std::string::npos || b == std::string::npos || b <= a) {
    return {};
  }
  return raw.substr(a, b - a + 1);
}

}  // namespace

std::string build_pack_digest(const std::string& pack_body, std::size_t max_chars) {
  std::ostringstream out;
  std::istringstream in(pack_body);
  std::string line;
  bool in_frag = false;
  int frag_lines = 0;
  int frags = 0;
  while (std::getline(in, line)) {
    if (line.rfind("targets (", 0) == 0 || line.rfind("pack_roles:", 0) == 0) {
      out << line << '\n';
      continue;
    }
    if (line.rfind("### get_code_of ", 0) == 0) {
      if (frags >= 14) {
        break;
      }
      in_frag = true;
      frag_lines = 0;
      out << line << '\n';
      ++frags;
      continue;
    }
    if (in_frag) {
      if (line == "```") {
        out << line << '\n';
        in_frag = false;
        continue;
      }
      if (frag_lines < 10) {
        out << line << '\n';
        ++frag_lines;
      } else if (frag_lines == 10) {
        out << "…\n";
        ++frag_lines;
      }
      continue;
    }
  }
  std::string digest = out.str();
  if (digest.size() > max_chars) {
    digest.resize(max_chars);
    digest += "\n…[pack digest truncated]…\n";
  }
  return digest;
}

std::string extract_session_instruction_block(const std::string& session_body) {
  const auto start = session_body.find("## Instruction");
  if (start == std::string::npos) {
    return session_body.substr(0, std::min<std::size_t>(session_body.size(), 2000));
  }
  const auto end = session_body.find("\n## ", start + 12);
  return session_body.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string extract_distilled_intent_block(const std::string& session_body) {
  const auto start = session_body.find("## Distilled intent");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = session_body.find("\n## ", start + 10);
  return session_body.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

PackReviewVerdict parse_pack_review_json(const std::string& raw) {
  PackReviewVerdict out;
  out.raw = raw;
  const std::string json = extract_json_object(raw);
  if (json.empty()) {
    out.error = "no JSON object in review response";
    return out;
  }
  try {
    const auto j = nlohmann::json::parse(json);
    out.verdict = j.value("verdict", "");
    out.reason = j.value("reason", "");
    out.confidence = j.value("confidence", 0.f);
    if (j.contains("present") && j["present"].is_array()) {
      for (const auto& v : j["present"]) {
        if (v.is_string()) {
          out.present.push_back(v.get<std::string>());
        }
      }
    }
    if (j.contains("missing") && j["missing"].is_array()) {
      for (const auto& v : j["missing"]) {
        if (v.is_string()) {
          out.missing.push_back(v.get<std::string>());
        }
      }
    }
    if (j.contains("reject") && j["reject"].is_array()) {
      for (const auto& v : j["reject"]) {
        if (v.is_string()) {
          out.reject.push_back(v.get<std::string>());
        }
      }
    }
    out.ok = !out.verdict.empty();
  } catch (const std::exception& e) {
    out.error = e.what();
  }
  return out;
}

std::string pack_review_system_prompt() {
  return R"(You are a semantic code-pack reviewer for a programming assistant.
The user query may be in Spanish; the code pack is English source code.
Decide whether the pack contains enough IMPLEMENTATION context to fulfill the request —
not merely related UI, rendering, or surface plumbing.

Respond ONLY with JSON:
{"verdict":"covered|partial|miss","reason":"...","present":["..."],"missing":["..."],"reject":["path:Symbol"],"confidence":0.0}

Rules:
- verdict=covered: core control/state/lifecycle logic for the request is likely present.
- verdict=partial: related modules but missing key control pieces needed to edit correctly.
- verdict=miss: mostly unrelated, wrong subsystem, or too shallow (headers-only noise).
- present/missing: short ENGLISH implementation concepts (snake_case or technical phrases).
- reject: optional path:Symbol entries already in the pack that are noise/wrong for this task
  (surface UI, invented symbols, wrong subsystem). Only real paths from the pack digest.
- missing: semantic gaps only — do NOT echo symbol names the model invented if they are not in the repo.
- Be strict: visible spinner ≠ AI generation cancel; syntax highlight ≠ compile error gutter;
  editor mouse zoom ≠ font size settings; generic escape handler ≠ AI loop cancel.
- Do NOT require exact symbol names; judge semantic coverage.
)";
}

std::string pack_review_user_prompt(const std::string& instruction_block,
                                    const std::string& distilled_block,
                                    const std::string& pack_digest,
                                    const std::vector<std::string>& watchlist) {
  std::ostringstream user;
  user << "USER REQUEST / INSTRUCTION:\n" << instruction_block << "\n\n";
  if (!distilled_block.empty()) {
    user << distilled_block << "\n\n";
  }
  if (!watchlist.empty()) {
    user << "WATCHLIST (plan targets fetched):\n";
    const int show = std::min(static_cast<int>(watchlist.size()), 24);
    for (int i = 0; i < show; ++i) {
      user << "- " << watchlist[static_cast<std::size_t>(i)] << '\n';
    }
    user << '\n';
  }
  user << "PACK DIGEST:\n" << pack_digest << "\n\nJSON:";
  return user.str();
}

std::vector<std::string> review_search_terms(const PackReviewVerdict& verdict,
                                             const std::string& distilled_block, int max_terms) {
  std::vector<std::string> terms;
  std::unordered_set<std::string> seen;
  auto push = [&](std::string t) {
    t = to_snake_token(std::move(t));
    if (t.size() < 4 || !seen.insert(t).second) {
      return;
    }
    terms.push_back(t);
  };
  for (const auto& m : verdict.missing) {
    push(m);
    std::istringstream ws(m);
    std::string word;
    while (ws >> word) {
      push(word);
    }
    if (static_cast<int>(terms.size()) >= max_terms) {
      return terms;
    }
  }
  if (static_cast<int>(terms.size()) >= max_terms) {
    return terms;
  }
  try {
    const auto jpos = distilled_block.find('{');
    const auto jend = distilled_block.rfind('}');
    if (jpos != std::string::npos && jend != std::string::npos && jend > jpos) {
      const auto j = nlohmann::json::parse(distilled_block.substr(jpos, jend - jpos + 1));
      if (j.contains("search_terms") && j["search_terms"].is_array()) {
        for (const auto& v : j["search_terms"]) {
          if (v.is_string()) {
            push(v.get<std::string>());
            if (static_cast<int>(terms.size()) >= max_terms) {
              return terms;
            }
          }
        }
      }
    }
  } catch (...) {
  }
  return terms;
}

std::vector<std::string> parse_search_hits_menu(const std::string& search_text, int max_hits) {
  std::vector<std::string> out;
  std::istringstream in(search_text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line.rfind("q=", 0) == 0 || line == "(sin hits)") {
      continue;
    }
    const auto c1 = line.find(':');
    if (c1 == std::string::npos) {
      continue;
    }
    const auto c2 = line.find(':', c1 + 1);
    if (c2 == std::string::npos) {
      continue;
    }
    std::string path = line.substr(0, c1);
    std::string rest = line.substr(c2 + 1);
    if (rest.size() > 72) {
      rest.resize(72);
      rest += "…";
    }
    const std::string item = path + " — " + rest;
    if (std::find(out.begin(), out.end(), item) == out.end()) {
      out.push_back(item);
    }
    if (static_cast<int>(out.size()) >= max_hits) {
      break;
    }
  }
  return out;
}

std::string path_from_hit_menu_line(const std::string& hit_line) {
  const auto dash = hit_line.find(" — ");
  std::string path = dash == std::string::npos ? hit_line : hit_line.substr(0, dash);
  while (!path.empty() && (path.back() == ' ' || path.back() == '\r')) {
    path.pop_back();
  }
  return path;
}

std::string path_from_plan_target_simple(const std::string& target) {
  const auto colon = target.rfind(':');
  if (colon == std::string::npos || colon == 0) {
    return target;
  }
  const std::string left = target.substr(0, colon);
  if (left.find('/') != std::string::npos || left.find('.') != std::string::npos) {
    return left;
  }
  return target;
}

bool target_in_watchlist_normalized(const std::string& target,
                                    const std::vector<std::string>& watchlist) {
  const std::string key = to_snake_token(target);
  if (key.empty()) {
    return false;
  }
  const std::string path = path_from_hit_menu_line(target);
  const std::string path_key = to_snake_token(path);
  for (const auto& w : watchlist) {
    const std::string wk = to_snake_token(w);
    if (wk == key) {
      return true;
    }
    const std::string wp = path_from_plan_target_simple(w);
    if (!wp.empty() && !path_key.empty() && to_snake_token(wp) == path_key) {
      return true;
    }
  }
  return false;
}

bool target_in_rejected_normalized(const std::string& target,
                                   const std::vector<std::string>& rejected) {
  return target_in_watchlist_normalized(target, rejected);
}

std::vector<std::string> filter_search_hits_excluding_watchlist(
    const std::vector<std::string>& hits, const std::vector<std::string>& watchlist) {
  if (watchlist.empty()) {
    return hits;
  }
  std::unordered_set<std::string> seen_paths;
  for (const auto& w : watchlist) {
    seen_paths.insert(to_snake_token(path_from_plan_target_simple(w)));
  }
  std::vector<std::string> out;
  for (const auto& h : hits) {
    const std::string p = to_snake_token(path_from_hit_menu_line(h));
    if (p.empty() || seen_paths.count(p)) {
      continue;
    }
    out.push_back(h);
  }
  return out;
}

std::vector<std::string> filter_unused_review_search_terms(
    const std::vector<std::string>& terms, const std::vector<std::string>& already_used) {
  std::unordered_set<std::string> seen;
  for (const auto& u : already_used) {
    seen.insert(to_snake_token(u));
  }
  std::vector<std::string> out;
  for (const auto& t : terms) {
    const std::string k = to_snake_token(t);
    if (k.size() < 4 || seen.count(k)) {
      continue;
    }
    seen.insert(k);
    out.push_back(k);
  }
  return out;
}

std::string load_pack_fragment_body(const std::string& pack_body, const std::string& target) {
  if (pack_body.empty() || target.empty()) {
    return {};
  }
  const std::string needle = "### get_code_of `" + target + "`";
  const auto pos = pack_body.find(needle);
  if (pos == std::string::npos) {
    return {};
  }
  const auto code_start = pack_body.find("```", pos);
  if (code_start == std::string::npos) {
    return {};
  }
  const auto body_start = code_start + 3;
  if (body_start < pack_body.size() && pack_body[body_start] == '\n') {
    // skip newline after ```
  }
  const auto code_end = pack_body.find("```", body_start);
  if (code_end == std::string::npos) {
    return pack_body.substr(body_start, std::min<std::size_t>(1200, pack_body.size() - body_start));
  }
  return pack_body.substr(body_start, code_end - body_start);
}

namespace {

bool is_ranked_entry_start(const std::string& line) {
  if (line.empty() || line[0] < '0' || line[0] > '9') {
    return false;
  }
  std::size_t i = 0;
  while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
    ++i;
  }
  return i < line.size() && line[i] == '.';
}

std::string extract_ranked_map_body(const std::string& session_body) {
  const auto start = session_body.find("## Ranked map");
  if (start == std::string::npos) {
    return {};
  }
  const auto body_start = start + 13;
  const auto obs = session_body.find("## Observations", body_start);
  if (obs != std::string::npos) {
    return session_body.substr(body_start, obs - body_start);
  }
  return session_body.substr(body_start);
}

// Session ## Ranked map or L1 map_last.md (## Ranked entries).
std::string extract_ranked_entries_body(const std::string& body) {
  const auto entries = body.find("## Ranked entries");
  if (entries != std::string::npos) {
    const auto bodies = body.find("\n## Bodies");
    const auto end = body.find("\n<!--", entries);
    std::size_t stop = body.size();
    if (bodies != std::string::npos && bodies > entries) {
      stop = std::min(stop, bodies);
    }
    if (end != std::string::npos && end > entries) {
      stop = std::min(stop, end);
    }
    return body.substr(entries, stop - entries);
  }
  return extract_ranked_map_body(body);
}

std::string symbol_from_ranked_line(const std::string& line) {
  const auto tick1 = line.rfind('`');
  if (tick1 == std::string::npos) {
    return {};
  }
  const auto tick0 = line.rfind('`', tick1 - 1);
  if (tick0 == std::string::npos || tick0 + 1 >= tick1) {
    return {};
  }
  return line.substr(tick0 + 1, tick1 - tick0 - 1);
}

std::string path_line_from_ranked_line(const std::string& line) {
  const auto dot = line.find('.');
  if (dot == std::string::npos) {
    return {};
  }
  std::size_t i = dot + 1;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  const auto bracket = line.find("  [", i);
  const auto dash = line.find(" — ", i);
  const auto end = std::min(bracket == std::string::npos ? line.size() : bracket,
                            dash == std::string::npos ? line.size() : dash);
  if (end <= i) {
    return {};
  }
  std::string pl = trim_ws(line.substr(i, end - i));
  return pl;
}

std::vector<std::string> review_match_tokens(const PackReviewVerdict& verdict,
                                             const std::string& distilled_block) {
  std::vector<std::string> tokens;
  std::unordered_set<std::string> seen;
  auto add = [&](std::string t) {
    t = to_snake_token(std::move(t));
    if (t.size() < 4 || !seen.insert(t).second) {
      return;
    }
    tokens.push_back(t);
  };
  for (const auto& m : verdict.missing) {
    add(m);
    std::istringstream ws(m);
    std::string word;
    while (ws >> word) {
      add(word);
    }
  }
  for (const auto& t : review_search_terms(verdict, distilled_block, 8)) {
    add(t);
  }
  return tokens;
}

int score_ranked_entry(const std::string& path_line, const std::string& symbol,
                       const std::vector<std::string>& tokens) {
  int score = 0;
  const std::string path_key = to_snake_token(path_from_plan_target_simple(path_line));
  const std::string sym_key = to_snake_token(symbol);
  const std::string blob = path_key + " " + sym_key;
  if (path_line.find("src/ai/") != std::string::npos) {
    score += 200;
  }
  for (const auto& tok : tokens) {
    if (tok.empty()) {
      continue;
    }
    if (path_key.find(tok) != std::string::npos || sym_key.find(tok) != std::string::npos ||
        blob.find(tok) != std::string::npos) {
      score += 50;
    }
  }
  return score;
}

}  // namespace

std::vector<std::string> infer_invented_rejects(const PackReviewVerdict& verdict,
                                                 const std::string& map_last_body) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  std::string map_low = map_last_body;
  for (char& c : map_low) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  auto maybe_add = [&](const std::string& raw) {
    const std::string tok = to_snake_token(raw);
    if (tok.size() < 4 || !seen.insert(tok).second) {
      return;
    }
    if (raw.find('/') != std::string::npos || raw.find('.') != std::string::npos) {
      return;
    }
    if (map_low.find(tok) != std::string::npos) {
      return;
    }
    out.push_back(tok);
  };
  for (const auto& m : verdict.missing) {
    maybe_add(m);
    std::istringstream ws(m);
    std::string word;
    while (ws >> word) {
      maybe_add(word);
    }
  }
  for (const auto& r : verdict.reject) {
    maybe_add(r);
  }
  return out;
}

std::vector<std::string> ranked_map_fallback_hits(const std::string& session_body,
                                                  const std::vector<std::string>& watchlist,
                                                  const PackReviewVerdict& verdict,
                                                  const std::string& distilled_block,
                                                  int max_hits) {
  const std::string map_body = extract_ranked_entries_body(session_body);
  if (map_body.empty() || max_hits <= 0) {
    return {};
  }
  const std::vector<std::string> tokens = review_match_tokens(verdict, distilled_block);
  std::unordered_set<std::string> seen_paths;
  for (const auto& w : watchlist) {
    seen_paths.insert(to_snake_token(path_from_plan_target_simple(w)));
  }

  struct Cand {
    int score = 0;
    std::string menu_line;
  };
  std::vector<Cand> cands;

  std::istringstream in(map_body);
  std::string line;
  while (std::getline(in, line)) {
    if (!is_ranked_entry_start(line)) {
      continue;
    }
    const std::string path_line = path_line_from_ranked_line(line);
    if (path_line.empty()) {
      continue;
    }
    const std::string path_only = path_from_plan_target_simple(path_line);
    const std::string path_key = to_snake_token(path_only);
    if (path_key.empty() || seen_paths.count(path_key)) {
      continue;
    }
    const std::string symbol = symbol_from_ranked_line(line);
    const int score = score_ranked_entry(path_line, symbol, tokens);
    if (score <= 0) {
      continue;
    }
    std::string rest = symbol.empty() ? path_line : symbol;
    if (rest.size() > 72) {
      rest.resize(72);
      rest += "…";
    }
    Cand c;
    c.score = score;
    c.menu_line = path_line + " — " + rest + " (ranked map)";
    cands.push_back(std::move(c));
  }

  std::stable_sort(cands.begin(), cands.end(),
                   [](const Cand& a, const Cand& b) { return a.score > b.score; });

  std::vector<std::string> out;
  std::unordered_set<std::string> seen_menu;
  for (const auto& c : cands) {
    if (seen_menu.insert(c.menu_line).second) {
      out.push_back(c.menu_line);
    }
    if (static_cast<int>(out.size()) >= max_hits) {
      break;
    }
  }
  return out;
}

std::vector<std::string> ranked_map_replan_hits(const std::string& map_last_body,
                                                 const std::vector<std::string>& watchlist,
                                                 const std::vector<std::string>& rejected,
                                                 const PackReviewVerdict& verdict,
                                                 const std::string& distilled_block,
                                                 int max_hits) {
  std::vector<std::string> blocklist = watchlist;
  blocklist.insert(blocklist.end(), rejected.begin(), rejected.end());
  return ranked_map_fallback_hits(map_last_body, blocklist, verdict, distilled_block, max_hits);
}

std::vector<std::string> ranked_map_unseen_hits(const std::string& map_last_body,
                                                 const std::vector<std::string>& watchlist,
                                                 const std::vector<std::string>& rejected,
                                                 int max_hits) {
  if (map_last_body.empty() || max_hits <= 0) {
    return {};
  }
  std::unordered_set<std::string> seen_paths;
  for (const auto& w : watchlist) {
    seen_paths.insert(to_snake_token(path_from_plan_target_simple(w)));
  }
  std::unordered_set<std::string> rejected_set;
  for (const auto& r : rejected) {
    rejected_set.insert(to_snake_token(r));
  }

  struct Cand {
    int pri = 1;
    int order = 0;
    std::string menu_line;
    std::string plan_target;
  };
  std::vector<Cand> cands;
  int order = 0;

  const std::string map_body = extract_ranked_entries_body(map_last_body);
  std::istringstream in(map_body);
  std::string line;
  while (std::getline(in, line)) {
    if (!is_ranked_entry_start(line)) {
      continue;
    }
    const std::string path_line = path_line_from_ranked_line(line);
    if (path_line.empty()) {
      continue;
    }
    const std::string path_only = path_from_plan_target_simple(path_line);
    const std::string path_key = to_snake_token(path_only);
    if (path_key.empty() || seen_paths.count(path_key)) {
      continue;
    }
    const std::string symbol = symbol_from_ranked_line(line);
    if (!symbol.empty() && rejected_set.count(to_snake_token(symbol))) {
      continue;
    }
    std::string rest = symbol.empty() ? path_line : symbol;
    if (rest.size() > 72) {
      rest.resize(72);
      rest += "…";
    }
    Cand c;
    c.order = order++;
    c.pri = path_line.find("src/ai/") != std::string::npos ? 0 : 1;
    c.menu_line = path_line + " — " + rest + " (ranked map)";
    if (!symbol.empty()) {
      const auto colon = path_line.find(':');
      c.plan_target =
          (colon != std::string::npos ? path_line.substr(0, colon) : path_line) + ":" + symbol;
    } else {
      c.plan_target = path_line;
    }
    cands.push_back(std::move(c));
  }

  std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.pri != b.pri) {
      return a.pri < b.pri;
    }
    return a.order < b.order;
  });

  std::vector<std::string> out;
  std::unordered_set<std::string> seen_menu;
  for (const auto& c : cands) {
    if (seen_menu.insert(c.menu_line).second) {
      out.push_back(c.menu_line);
    }
    if (static_cast<int>(out.size()) >= max_hits) {
      break;
    }
  }
  return out;
}

std::vector<std::string> plan_targets_from_map_hits(const std::vector<std::string>& hit_menu,
                                                     int max_targets) {
  if (hit_menu.empty() || max_targets <= 0) {
    return {};
  }
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const auto& h : hit_menu) {
    const auto dash = h.find(" — ");
    const std::string path_line = dash == std::string::npos ? h : trim_ws(h.substr(0, dash));
    if (path_line.empty()) {
      continue;
    }
    std::string symbol;
    if (dash != std::string::npos) {
      const auto tail = h.substr(dash + 3);
      const auto paren = tail.rfind(" (ranked map)");
      symbol = trim_ws(paren == std::string::npos ? tail : tail.substr(0, paren));
    }
    std::string target;
    if (!symbol.empty()) {
      const auto colon = path_line.find(':');
      target = (colon != std::string::npos ? path_line.substr(0, colon) : path_line) + ":" +
               symbol;
    } else {
      target = path_line;
    }
    if (!seen.insert(target).second) {
      continue;
    }
    out.push_back(target);
    if (static_cast<int>(out.size()) >= max_targets) {
      break;
    }
  }
  return out;
}

std::string build_pack_replan_menu(const std::string& session_body,
                                   const std::string& map_last_body,
                                   const std::vector<std::string>& watchlist,
                                   const std::vector<std::string>& rejected,
                                   const PackReviewVerdict& verdict,
                                   const std::string& distilled_block,
                                   const std::vector<std::string>* hits_override,
                                   const std::vector<std::string>* search_terms,
                                   bool pushback) {
  std::vector<std::string> hit_menu;
  if (hits_override != nullptr && !hits_override->empty()) {
    hit_menu = *hits_override;
  } else if (pushback && !map_last_body.empty()) {
    hit_menu = ranked_map_unseen_hits(map_last_body, watchlist, rejected, 12);
  } else {
    if (!map_last_body.empty()) {
      hit_menu = ranked_map_replan_hits(map_last_body, watchlist, rejected, verdict,
                                        distilled_block, 12);
    }
    if (hit_menu.empty()) {
      std::vector<std::string> blocklist = watchlist;
      blocklist.insert(blocklist.end(), rejected.begin(), rejected.end());
      hit_menu = ranked_map_fallback_hits(session_body, blocklist, verdict, distilled_block, 10);
    }
    if (pushback && hit_menu.empty() && !map_last_body.empty()) {
      hit_menu = ranked_map_unseen_hits(map_last_body, watchlist, rejected, 12);
    }
  }
  std::stable_sort(hit_menu.begin(), hit_menu.end(),
                   [](const std::string& a, const std::string& b) {
                     const bool aa = a.find("src/ai/") != std::string::npos;
                     const bool ba = b.find("src/ai/") != std::string::npos;
                     if (aa != ba) {
                       return aa > ba;
                     }
                     return a < b;
                   });
  std::ostringstream menu;
  if (pushback) {
    menu << "Plan repetido: todos los targets ya están en watchlist/pack.\n";
  } else {
    menu << "Review: pack insuficiente (" << verdict.verdict << ").\n";
  }
  menu << "Emite `action=plan` con targets NUEVOS path:Symbol anclados a hits reales.\n";
  menu << "PROHIBIDO inventar símbolos; usa solo MAP HITS / SEARCH HITS.\n";
  if (!rejected.empty()) {
    menu << "Rejected (no replan): ";
    for (std::size_t i = 0; i < rejected.size() && i < 8; ++i) {
      if (i) {
        menu << ", ";
      }
      menu << rejected[i];
    }
    if (rejected.size() > 8) {
      menu << "…";
    }
    menu << "\n";
  }
  if (!verdict.missing.empty()) {
    menu << "Missing: ";
    for (std::size_t i = 0; i < verdict.missing.size(); ++i) {
      if (i) {
        menu << ", ";
      }
      menu << verdict.missing[i];
    }
    menu << "\n";
  }
  if (!hit_menu.empty()) {
    const char* label =
        hit_menu.front().find("(ranked map)") != std::string::npos ? "MAP HITS" : "SEARCH HITS";
    menu << label << " (prioriza src/ai y control de carga/cancel):\n";
    for (const auto& h : hit_menu) {
      menu << "- " << h << "\n";
    }
  } else if (search_terms != nullptr && !search_terms->empty()) {
    menu << "Search terms: ";
    for (std::size_t i = 0; i < search_terms->size(); ++i) {
      if (i) {
        menu << ", ";
      }
      menu << (*search_terms)[i];
    }
    menu << " (sin hits — elige path:Symbol del mapa L1)\n";
  }
  return menu.str();
}

}  // namespace tuide
