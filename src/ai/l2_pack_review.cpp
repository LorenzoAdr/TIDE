#include "ai/l2_pack_review.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

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
  int frag_line_cap = 10;
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
      // First must/ancla fragments: keep more body so review sees real loci.
      frag_line_cap = frags < 4 ? 28 : 10;
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
      if (frag_lines < frag_line_cap) {
        out << line << '\n';
        ++frag_lines;
      } else if (frag_lines == frag_line_cap) {
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
- verdict=covered: enough implementation context to edit or answer the request (control/state for the asked behavior is present). Prefer covered when the digest shows the real locus, even if extras are thin or some windows are marked Truncated.
- Truncated / incomplete snippet windows alone do NOT force partial or miss if the control/state locus bodies are present.
- verdict=partial: related modules but missing key control pieces needed to edit correctly.
- verdict=miss: mostly unrelated, wrong subsystem, or too shallow (headers-only noise).
- present/missing: short ENGLISH implementation concepts (snake_case or technical phrases).
- reject: optional path:Symbol entries already in the pack that are noise/wrong for this task
  (wrong subsystem, invented symbols). Only real paths from the pack digest. Do NOT reject the
  primary control/state files for the request just to free budget.
- missing: semantic gaps only — do NOT echo symbol names the model invented if they are not in the repo.
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
    // Skip absolute paths and test fixtures (noise for pack priority).
    if (!path.empty() && path[0] == '/') {
      continue;
    }
    if (path.find("tests/") != std::string::npos || path.find("/tests/") != std::string::npos) {
      continue;
    }
    // Skip observation/runtime prose mistaken for path:line hits.
    if (path.find(' ') != std::string::npos || path.find("Review") != std::string::npos ||
        path.find("Rechazado") != std::string::npos || path.find("note") == 0 ||
        path.find("runtime") == 0 || path.find("target_count") == 0) {
      continue;
    }
    if (path.find("src/") == std::string::npos && path.find('.') == std::string::npos) {
      continue;
    }
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

std::string path_key_from_any_target(const std::string& target) {
  if (target.find(" — ") != std::string::npos) {
    return to_snake_token(path_from_hit_menu_line(target));
  }
  return to_snake_token(path_from_plan_target_simple(target));
}

std::string symbol_key_from_plan_target(const std::string& target) {
  const std::string path = path_from_plan_target_simple(target);
  if (path == target) {
    if (target.find('/') == std::string::npos && target.find('.') == std::string::npos) {
      return to_snake_token(target);
    }
    return {};
  }
  const auto colon = target.rfind(':');
  if (colon == std::string::npos || colon + 1 >= target.size()) {
    return {};
  }
  return to_snake_token(target.substr(colon + 1));
}

bool token_matches_field(const std::string& field_key, const std::string& reject_key) {
  if (field_key.empty() || reject_key.empty()) {
    return false;
  }
  if (field_key == reject_key) {
    return true;
  }
  if (reject_key.size() >= 4 && field_key.find(reject_key) != std::string::npos) {
    return true;
  }
  if (field_key.size() >= 4 && reject_key.find(field_key) != std::string::npos) {
    return true;
  }
  return false;
}

std::vector<std::string> reject_match_tokens(const std::string& reject) {
  std::vector<std::string> tokens;
  std::istringstream in(reject);
  std::string word;
  while (in >> word) {
    const std::string t = to_snake_token(word);
    if (t.size() >= 3) {
      tokens.push_back(t);
    }
  }
  if (tokens.empty()) {
    const std::string t = to_snake_token(reject);
    if (!t.empty()) {
      tokens.push_back(t);
    }
  }
  return tokens;
}

bool target_in_watchlist_normalized(const std::string& target,
                                    const std::vector<std::string>& watchlist) {
  const std::string key = to_snake_token(target);
  if (key.empty()) {
    return false;
  }
  const std::string path_key = path_key_from_any_target(target);
  const std::string sym_key = symbol_key_from_plan_target(target);

  for (const auto& w : watchlist) {
    const std::string wk = to_snake_token(w);
    if (wk == key) {
      return true;
    }
    if (!path_key.empty()) {
      const std::string wp = path_key_from_any_target(w);
      if (!wp.empty() && wp == path_key) {
        return true;
      }
    }
    if (!sym_key.empty()) {
      const std::string ws = symbol_key_from_plan_target(w);
      if (!ws.empty() && ws == sym_key) {
        return true;
      }
    }
  }
  return false;
}

bool watchlist_entry_matches_reject(const std::string& watchlist_entry,
                                    const std::string& reject) {
  if (target_in_watchlist_normalized(reject, {watchlist_entry})) {
    return true;
  }
  const auto tokens = reject_match_tokens(reject);
  if (tokens.empty()) {
    return false;
  }
  const std::string wk = to_snake_token(watchlist_entry);
  const std::string wp = path_key_from_any_target(watchlist_entry);
  const std::string ws = symbol_key_from_plan_target(watchlist_entry);
  int hits = 0;
  for (const auto& t : tokens) {
    if (token_matches_field(wk, t) || token_matches_field(wp, t) ||
        token_matches_field(ws, t)) {
      ++hits;
    }
  }
  if (tokens.size() == 1) {
    return hits >= 1;
  }
  return hits >= 2;
}

std::vector<std::string> expand_review_rejects_for_watchlist(
    const std::vector<std::string>& rejects, const std::vector<std::string>& watchlist) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto add = [&](const std::string& s) {
    const std::string t = trim_ws(s);
    if (!t.empty() && seen.insert(t).second) {
      out.push_back(t);
    }
  };
  for (const auto& r : rejects) {
    add(r);
  }
  for (const auto& r : rejects) {
    for (const auto& w : watchlist) {
      if (watchlist_entry_matches_reject(w, r)) {
        add(w);
      }
    }
  }
  return out;
}

bool all_plan_target_paths_in_watchlist(const std::vector<std::string>& targets,
                                        const std::vector<std::string>& watchlist) {
  if (targets.empty() || watchlist.empty()) {
    return false;
  }
  std::unordered_set<std::string> seen_paths;
  for (const auto& w : watchlist) {
    const std::string pk = path_key_from_any_target(w);
    if (!pk.empty()) {
      seen_paths.insert(pk);
    }
  }
  bool any = false;
  for (const auto& raw : targets) {
    const std::string t = trim_ws(raw);
    if (t.empty()) {
      continue;
    }
    any = true;
    const std::string pk = path_key_from_any_target(t);
    if (pk.empty() || !seen_paths.count(pk)) {
      return false;
    }
  }
  return any;
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
  std::unordered_set<std::string> seen_plan;
  for (const auto& c : cands) {
    // Prefer clean path:Symbol for consumers that re-parse the menu (avoid UTF-8 sep bugs).
    const std::string& item = c.plan_target.empty() ? c.menu_line : c.plan_target;
    if (!seen_plan.insert(item).second) {
      continue;
    }
    if (seen_menu.insert(c.menu_line).second) {
      out.push_back(c.menu_line);
    }
    if (static_cast<int>(out.size()) >= max_hits) {
      break;
    }
  }
  return out;
}

int numeric_line_suffix(const std::string& path_line) {
  const auto colon = path_line.rfind(':');
  if (colon == std::string::npos || colon + 1 >= path_line.size()) {
    return 0;
  }
  const std::string right = path_line.substr(colon + 1);
  if (right.empty() || right.find('-') != std::string::npos) {
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

std::vector<std::string> plan_targets_from_map_hits(const std::vector<std::string>& hit_menu,
                                                     int max_targets) {
  if (hit_menu.empty() || max_targets <= 0) {
    return {};
  }
  static const std::string kMenuSep = " — ";
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const auto& h : hit_menu) {
    const auto dash = h.find(kMenuSep);
    const std::string path_line = dash == std::string::npos ? h : trim_ws(h.substr(0, dash));
    if (path_line.empty()) {
      continue;
    }
    std::string symbol;
    if (dash != std::string::npos) {
      // kMenuSep is UTF-8 (" — " = 5 bytes); never use a hardcoded +3 byte skip.
      const auto tail = h.substr(dash + kMenuSep.size());
      const auto paren = tail.rfind(" (ranked map)");
      symbol = trim_ws(paren == std::string::npos ? tail : tail.substr(0, paren));
    }
    // Prefer path:line when the map gave a real locus (line > 1). path:Symbol often
    // resolves to a huge enclosing span and then gets head-truncated in the pack.
    std::string target;
    const int line = numeric_line_suffix(path_line);
    if (line > 1) {
      target = path_line;
    } else if (!symbol.empty()) {
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

std::vector<std::string> retrieval_anchor_targets(const std::string& map_last_body,
                                                  const std::string& session_body,
                                                  int max_map) {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto add = [&](const std::string& raw) {
    const std::string t = trim_ws(raw);
    if (t.size() < 3 || !seen.insert(t).second) {
      return;
    }
    out.push_back(t);
  };

  // Session seeds line: "seeds: a b c"
  {
    const auto pos = session_body.find("\nseeds:");
    const auto pos2 = session_body.find("seeds:");
    const auto start = pos != std::string::npos ? pos + 1 : pos2;
    if (start != std::string::npos) {
      const auto line_end = session_body.find('\n', start);
      std::string line = session_body.substr(
          start, line_end == std::string::npos ? std::string::npos : line_end - start);
      const auto colon = line.find(':');
      if (colon != std::string::npos) {
        std::istringstream in(line.substr(colon + 1));
        std::string tok;
        while (in >> tok) {
          add(tok);
        }
      }
    }
  }

  const std::string map_body = extract_ranked_entries_body(map_last_body);
  if (!map_body.empty() && max_map > 0) {
    std::vector<std::string> ai_first;
    std::vector<std::string> rest;
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
      const std::string symbol = symbol_from_ranked_line(line);
      std::string target;
      const int loc = numeric_line_suffix(path_line);
      if (loc > 1) {
        target = path_line;
      } else if (!symbol.empty()) {
        const auto colon = path_line.find(':');
        target = (colon != std::string::npos ? path_line.substr(0, colon) : path_line) + ":" +
                 symbol;
      } else {
        target = path_line;
      }
      if (path_line.find("src/ai/") != std::string::npos ||
          to_snake_token(target).find("ai_controller") != std::string::npos ||
          to_snake_token(target).find("set_busy") != std::string::npos ||
          to_snake_token(target).find("agent_busy") != std::string::npos ||
          to_snake_token(target).find("clear_busy") != std::string::npos) {
        ai_first.push_back(target);
      } else {
        rest.push_back(target);
      }
      if (static_cast<int>(ai_first.size() + rest.size()) >= max_map * 2) {
        break;
      }
    }
    for (const auto& t : ai_first) {
      add(t);
      if (static_cast<int>(out.size()) >= max_map) {
        break;
      }
    }
    for (const auto& t : rest) {
      if (static_cast<int>(out.size()) >= max_map) {
        break;
      }
      add(t);
    }
  }

  return out;
}

std::vector<std::string> expand_anchor_api_siblings(const std::vector<std::string>& seeds,
                                                     const std::string& map_last_body,
                                                     int max_extra,
                                                     const std::string& workspace_root) {
  // Generic: ranked-map neighbors on same TU + complementary decls in twin files.
  if (seeds.empty() || max_extra <= 0) {
    return {};
  }
  std::unordered_set<std::string> seed_keys;
  std::unordered_set<std::string> seed_paths;
  std::vector<std::string> seed_path_list;
  std::vector<std::string> seed_syms;
  for (const auto& s : seeds) {
    seed_keys.insert(to_snake_token(s));
    const std::string p = path_from_plan_target_simple(s);
    const std::string sym = symbol_key_from_plan_target(s);
    if (!sym.empty()) {
      seed_syms.push_back(sym);
    }
    if (!p.empty() && p.find('/') != std::string::npos) {
      const std::string pk = to_snake_token(p);
      if (seed_paths.insert(pk).second) {
        seed_path_list.push_back(p);
      }
      // path:line seeds often lack a symbol name — use file stem as a weak hint.
      {
        const auto slash = p.rfind('/');
        const std::string base =
            slash == std::string::npos ? p : p.substr(slash + 1);
        std::string stem = base;
        const auto dot = stem.rfind('.');
        if (dot != std::string::npos) {
          stem = stem.substr(0, dot);
        }
        const std::string sk = to_snake_token(stem);
        if (sk.find("busy") != std::string::npos || sk.find("spinner") != std::string::npos ||
            sk.find("agent") != std::string::npos || sk.find("controller") != std::string::npos ||
            sk.find("loading") != std::string::npos) {
          seed_syms.push_back(sk);
        }
      }
      // Same translation unit: .cpp ↔ .hpp/.h for neighbor discovery.
      if (p.size() > 4 && p.compare(p.size() - 4, 4, ".cpp") == 0) {
        const std::string hpp = p.substr(0, p.size() - 4) + ".hpp";
        const std::string h = p.substr(0, p.size() - 4) + ".h";
        if (seed_paths.insert(to_snake_token(hpp)).second) {
          seed_path_list.push_back(hpp);
        }
        if (seed_paths.insert(to_snake_token(h)).second) {
          seed_path_list.push_back(h);
        }
      } else if (p.size() > 4 && p.compare(p.size() - 4, 4, ".hpp") == 0) {
        const std::string cpp = p.substr(0, p.size() - 4) + ".cpp";
        if (seed_paths.insert(to_snake_token(cpp)).second) {
          seed_path_list.push_back(cpp);
        }
      } else if (p.size() > 2 && p.compare(p.size() - 2, 2, ".h") == 0) {
        const std::string cpp = p.substr(0, p.size() - 2) + ".cpp";
        if (seed_paths.insert(to_snake_token(cpp)).second) {
          seed_path_list.push_back(cpp);
        }
      }
    }
  }

  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  auto push_target = [&](const std::string& target) {
    if (target.empty() || static_cast<int>(out.size()) >= max_extra) {
      return;
    }
    const std::string k = to_snake_token(target);
    if (seed_keys.count(k) || !seen.insert(k).second) {
      return;
    }
    out.push_back(target);
  };

  // 1) Complementary verbs from twin files FIRST (clear/cancel under budget).
  if (!workspace_root.empty() && !seed_path_list.empty()) {
    std::unordered_set<std::string> want_names;
    bool want_clear_family = false;
    for (const auto& sym : seed_syms) {
      if (sym.empty()) {
        continue;
      }
      if (sym.rfind("set_", 0) == 0 && sym.size() > 4) {
        const std::string rest = sym.substr(4);
        want_names.insert("clear_" + rest);
        want_names.insert("clear_" + rest + "_if");
        want_names.insert("reset_" + rest);
        want_clear_family = true;
      } else if (sym.rfind("start_", 0) == 0 && sym.size() > 6) {
        const std::string rest = sym.substr(6);
        want_names.insert("stop_" + rest);
        want_names.insert("cancel_" + rest);
        want_clear_family = true;
      } else if (sym.rfind("enable_", 0) == 0 && sym.size() > 7) {
        want_names.insert("disable_" + sym.substr(7));
        want_clear_family = true;
      } else if (sym.rfind("begin_", 0) == 0 && sym.size() > 6) {
        want_names.insert("end_" + sym.substr(6));
        want_names.insert("cancel_" + sym.substr(6));
        want_clear_family = true;
      }
      if (sym.find("busy") != std::string::npos || sym.find("spinner") != std::string::npos ||
          sym.find("loading") != std::string::npos) {
        want_names.insert("clear_busy");
        want_names.insert("clear_busy_if");
        want_names.insert("cancel_current");
        want_names.insert("cancel_all");
        want_clear_family = true;
      }
    }
    // Busy/spinner seeds often live outside the controller TU — pull controller paths
    // from the ranked map so cancel_all / agent_busy can be discovered on disk.
    if ((want_names.count("cancel_all") || want_names.count("cancel_current")) &&
        !map_last_body.empty()) {
      const std::string map_body = extract_ranked_entries_body(map_last_body);
      std::istringstream min(map_body);
      std::string mline;
      while (std::getline(min, mline)) {
        if (!is_ranked_entry_start(mline)) {
          continue;
        }
        const std::string path_line = path_line_from_ranked_line(mline);
        const std::string path_only = path_from_plan_target_simple(path_line);
        const std::string symbol = symbol_from_ranked_line(mline);
        const std::string pk = to_snake_token(path_only);
        const std::string sk = to_snake_token(symbol);
        if (pk.find("controller") == std::string::npos && pk.find("agent") == std::string::npos &&
            sk.find("cancel_all") == std::string::npos && sk.find("agent_busy") == std::string::npos &&
            sk.find("cancel_current") == std::string::npos) {
          continue;
        }
        if (path_only.find('/') == std::string::npos) {
          continue;
        }
        if (seed_paths.insert(to_snake_token(path_only)).second) {
          seed_path_list.push_back(path_only);
        }
      }
    }
    // Prefer clear-side siblings first when packing under budget.
    std::vector<std::pair<std::string, std::string>> found;  // path, symbol
    for (const auto& rel : seed_path_list) {
      const fs::path abs = fs::path(workspace_root) / rel;
      std::error_code ec;
      if (!fs::is_regular_file(abs, ec)) {
        continue;
      }
      std::ifstream in(abs);
      if (!in) {
        continue;
      }
      std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (text.size() > 400000) {
        text.resize(400000);
      }
      auto consider = [&](const std::string& name) {
        if (name.size() < 4) {
          return;
        }
        if (name.find("inflight") != std::string::npos ||
            name.find("completion") != std::string::npos ||
            name.find("fetch") != std::string::npos) {
          return;
        }
        const bool named = want_names.count(name) > 0;
        const bool family =
            want_clear_family &&
            (name.rfind("clear_", 0) == 0 || name.rfind("cancel_", 0) == 0 ||
             name.rfind("reset_", 0) == 0 || name.rfind("stop_", 0) == 0 ||
             name.rfind("disable_", 0) == 0);
        if (!named && !family) {
          return;
        }
        // Family hits only on control-ish TUs (busy/spinner/controller/agent).
        if (!named && family) {
          const auto slash = rel.rfind('/');
          const std::string base =
              slash == std::string::npos ? rel : rel.substr(slash + 1);
          const std::string sk = to_snake_token(base);
          if (sk.find("busy") == std::string::npos && sk.find("spinner") == std::string::npos &&
              sk.find("controller") == std::string::npos && sk.find("agent") == std::string::npos &&
              sk.find("loading") == std::string::npos) {
            return;
          }
        }
        // Require a declaration/definition-looking occurrence.
        const std::string needle = name + "(";
        if (text.find(needle) == std::string::npos) {
          return;
        }
        found.emplace_back(rel, name);
      };
      // Scan identifier tokens before '('.
      for (std::size_t i = 0; i + 4 < text.size(); ++i) {
        if (!((text[i] >= 'a' && text[i] <= 'z') || text[i] == '_')) {
          continue;
        }
        if (i > 0) {
          const char p = text[i - 1];
          if (std::isalnum(static_cast<unsigned char>(p)) || p == '_') {
            continue;
          }
        }
        std::size_t j = i;
        while (j < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) {
          ++j;
        }
        if (j >= text.size() || text[j] != '(') {
          i = j;
          continue;
        }
        consider(text.substr(i, j - i));
        i = j;
      }
    }
    std::stable_sort(found.begin(), found.end(),
                     [&](const auto& a, const auto& b) {
                       const bool aw = want_names.count(a.second) > 0;
                       const bool bw = want_names.count(b.second) > 0;
                       if (aw != bw) {
                         return aw && !bw;
                       }
                       auto rank = [](const std::string& n) {
                         if (n.rfind("cancel_", 0) == 0) {
                           return 3;
                         }
                         if (n.rfind("clear_", 0) == 0) {
                           return 2;
                         }
                         return 1;
                       };
                       const int ra = rank(a.second);
                       const int rb = rank(b.second);
                       if (ra != rb) {
                         return ra > rb;
                       }
                       // Prefer .cpp definitions over .hpp decls for the same symbol.
                       auto is_hdr = [](const std::string& p) {
                         return p.size() > 4 &&
                                (p.compare(p.size() - 4, 4, ".hpp") == 0 ||
                                 (p.size() > 2 && p.compare(p.size() - 2, 2, ".h") == 0));
                       };
                       const bool ah = is_hdr(a.first);
                       const bool bh = is_hdr(b.first);
                       if (ah != bh) {
                         return !ah && bh;
                       }
                       return a.second < b.second;
                     });
    // Cap noisy family hits: keep exact wants + at most 2 clears + 2 cancels per file.
    std::unordered_map<std::string, int> clears_per_file;
    std::unordered_map<std::string, int> cancels_per_file;
    for (const auto& f : found) {
      const bool exact = want_names.count(f.second) > 0;
      if (!exact) {
        if (f.second.rfind("clear_", 0) == 0) {
          if (clears_per_file[f.first] >= 2) {
            continue;
          }
          ++clears_per_file[f.first];
        } else if (f.second.rfind("cancel_", 0) == 0) {
          if (cancels_per_file[f.first] >= 2) {
            continue;
          }
          ++cancels_per_file[f.first];
        }
      }
      push_target(f.first + ":" + f.second);
      if (static_cast<int>(out.size()) >= max_extra) {
        break;
      }
    }
  }

  // 2) Same-file / twin-path neighbors already on the ranked map (fill remaining slots).
  if (!map_last_body.empty() && !seed_paths.empty() &&
      static_cast<int>(out.size()) < max_extra) {
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
      if (!seed_paths.count(to_snake_token(path_only))) {
        continue;
      }
      const std::string symbol = symbol_from_ranked_line(line);
      const int loc = numeric_line_suffix(path_line);
      std::string target;
      if (loc > 1) {
        target = path_line;
      } else if (!symbol.empty()) {
        target = path_only + ":" + symbol;
      } else {
        target = path_line;
      }
      push_target(target);
      if (static_cast<int>(out.size()) >= max_extra) {
        break;
      }
    }
  }
  return out;
}

bool target_is_lifecycle_clear(const std::string& target) {
  const std::string sym = symbol_key_from_plan_target(target);
  const std::string k = sym.empty() ? to_snake_token(target) : sym;
  // LSP/completion cancel helpers are not the agent/UI busy clear locus.
  if (k.find("inflight") != std::string::npos || k.find("completion") != std::string::npos ||
      k.find("fetch") != std::string::npos) {
    return false;
  }
  const std::string path = path_from_plan_target_simple(target);
  const auto slash = path.rfind('/');
  const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  const std::string stem = to_snake_token(base);
  const bool control_tu =
      stem.find("busy") != std::string::npos || stem.find("spinner") != std::string::npos ||
      stem.find("controller") != std::string::npos || stem.find("agent") != std::string::npos ||
      stem.find("loading") != std::string::npos;
  if (k.rfind("clear_", 0) == 0 || k.find("clear_") != std::string::npos) {
    return control_tu || k.find("busy") != std::string::npos;
  }
  if (k.rfind("cancel_", 0) == 0 || k.find("cancel_") != std::string::npos) {
    // cancel_all / cancel_current only count on controller/agent TUs.
    return control_tu;
  }
  if (k.rfind("reset_", 0) == 0 || k.rfind("stop_", 0) == 0 || k.rfind("disable_", 0) == 0 ||
      k.rfind("unset_", 0) == 0) {
    return control_tu;
  }
  return false;
}

bool target_is_lifecycle_set(const std::string& target) {
  const std::string sym = symbol_key_from_plan_target(target);
  const std::string k = sym.empty() ? to_snake_token(target) : sym;
  if (k.rfind("set_", 0) == 0 || k.rfind("start_", 0) == 0 || k.rfind("enable_", 0) == 0 ||
      k.rfind("begin_", 0) == 0 || k.rfind("open_", 0) == 0 || k.rfind("ensure_", 0) == 0) {
    return true;
  }
  return k.find("busy") != std::string::npos || k.find("spinner") != std::string::npos ||
         k.find("loading") != std::string::npos;
}

bool pack_has_lifecycle_pair(const std::string& pack_body) {
  if (pack_body.empty()) {
    return false;
  }
  bool has_set = false;
  bool has_clear = false;
  bool has_clear_busy = false;
  bool has_cancel_agent = false;
  std::size_t search_from = 0;
  while (true) {
    const auto head = pack_body.find("### get_code_of `", search_from);
    if (head == std::string::npos) {
      break;
    }
    const auto tick0 = head + std::string("### get_code_of `").size();
    const auto tick1 = pack_body.find('`', tick0);
    if (tick1 == std::string::npos) {
      break;
    }
    const std::string tgt = pack_body.substr(tick0, tick1 - tick0);
    search_from = tick1 + 1;
    if (tgt.find("(omitido)") != std::string::npos) {
      continue;
    }
    const auto fence0 = pack_body.find("```", tick1);
    const auto next_head = pack_body.find("### get_code_of `", tick1);
    if (fence0 == std::string::npos || (next_head != std::string::npos && fence0 > next_head)) {
      continue;
    }
    const auto fence1 = pack_body.find("```", fence0 + 3);
    if (fence1 == std::string::npos || fence1 <= fence0 + 3) {
      continue;
    }
    const std::string body = pack_body.substr(fence0 + 3, fence1 - (fence0 + 3));
    if (body.size() < 40) {
      continue;
    }
    const std::string body_key = to_snake_token(body);
    const std::string tgt_key = to_snake_token(tgt);
    auto hit = [&](const char* p) {
      return tgt_key.find(p) != std::string::npos || body_key.find(p) != std::string::npos;
    };
    if (hit("set_busy") || hit("set_busy_spinner") || hit("agent_busy") || hit("ensure_spinner") ||
        (hit("set_") && (tgt_key.find("busy") != std::string::npos || hit("busy_spinner")))) {
      has_set = true;
    }
    // Prefer real busy/agent teardown; ignore LSP/console cancel noise.
    const bool lsp_noise =
        hit("inflight") || hit("completion_fetch") || hit("cancel_completion");
    const bool control_tgt = tgt_key.find("busy") != std::string::npos ||
                             tgt_key.find("spinner") != std::string::npos ||
                             tgt_key.find("controller") != std::string::npos ||
                             tgt_key.find("agent") != std::string::npos;
    if (!lsp_noise &&
        (hit("clear_busy") || hit("clear_busy_if") ||
         (control_tgt && (hit("cancel_all") || hit("cancel_current"))) ||
         (hit("clear_") && hit("busy")))) {
      has_clear = true;
    }
    if (body.find("void clear_busy(") != std::string::npos ||
        body.find("void clear_busy_if(") != std::string::npos) {
      has_clear_busy = true;
      has_clear = true;
    }
    if (control_tgt &&
        (body.find("cancel_all(") != std::string::npos ||
         body.find("::cancel_all(") != std::string::npos ||
         body.find("cancel_current(") != std::string::npos ||
         body.find("::cancel_current(") != std::string::npos) &&
        body.find('{') != std::string::npos) {
      has_cancel_agent = true;
      has_clear = true;
    }
    if ((has_set && has_clear) || (has_clear_busy && has_cancel_agent)) {
      return true;
    }
  }
  return false;
}

bool pack_has_anchor_fragment(const std::string& pack_body,
                              const std::vector<std::string>& anchors) {
  if (pack_body.empty() || anchors.empty()) {
    return false;
  }
  for (const auto& a : anchors) {
    const std::string path = path_from_plan_target_simple(a);
    const std::string stem = to_snake_token(a);
    if (path.empty() && stem.size() < 4) {
      continue;
    }
    std::string needle = path.empty() ? stem : path;
    const auto pos = pack_body.find("### get_code_of `");
    std::size_t search_from = 0;
    while (true) {
      const auto head = pack_body.find("### get_code_of `", search_from);
      if (head == std::string::npos) {
        break;
      }
      const auto tick0 = head + std::string("### get_code_of `").size();
      const auto tick1 = pack_body.find('`', tick0);
      if (tick1 == std::string::npos) {
        break;
      }
      const std::string tgt = pack_body.substr(tick0, tick1 - tick0);
      search_from = tick1 + 1;
      if (tgt.find("(omitido)") != std::string::npos) {
        continue;
      }
      const bool path_hit =
          !path.empty() && (tgt.find(path) != std::string::npos ||
                            to_snake_token(tgt).find(to_snake_token(path)) != std::string::npos);
      const bool stem_hit =
          stem.size() >= 4 && to_snake_token(tgt).find(stem) != std::string::npos;
      if (!path_hit && !stem_hit) {
        continue;
      }
      // Require a code fence after the header (real body, not omit note only).
      const auto fence = pack_body.find("```", tick1);
      const auto next_head = pack_body.find("### get_code_of `", tick1);
      if (fence != std::string::npos && (next_head == std::string::npos || fence < next_head)) {
        return true;
      }
    }
  }
  return false;
}

std::string pack_code_fences_only(const std::string& pack_body) {
  std::ostringstream out;
  std::size_t pos = 0;
  while (pos < pack_body.size()) {
    const auto head = pack_body.find("### get_code_of `", pos);
    if (head == std::string::npos) {
      break;
    }
    const auto tick0 = head + std::string("### get_code_of `").size();
    const auto tick1 = pack_body.find('`', tick0);
    if (tick1 == std::string::npos) {
      break;
    }
    const std::string tgt = pack_body.substr(tick0, tick1 - tick0);
    pos = tick1 + 1;
    if (tgt.find("(omitido)") != std::string::npos) {
      continue;
    }
    const auto fence0 = pack_body.find("```", tick1);
    const auto next_head = pack_body.find("### get_code_of `", tick1);
    if (fence0 == std::string::npos || (next_head != std::string::npos && fence0 > next_head)) {
      continue;
    }
    const auto fence1 = pack_body.find("```", fence0 + 3);
    if (fence1 == std::string::npos) {
      break;
    }
    out << pack_body.substr(fence0 + 3, fence1 - (fence0 + 3)) << '\n';
    pos = fence1 + 3;
  }
  return out.str();
}

bool pack_target_has_symbol_body(const std::string& pack_body, const std::string& target) {
  if (pack_body.empty() || target.size() < 3) {
    return false;
  }
  const std::string path = path_from_plan_target_simple(target);
  const std::string sym = symbol_key_from_plan_target(target);
  const int line = numeric_line_suffix(target);
  std::size_t search_from = 0;
  while (true) {
    const auto head = pack_body.find("### get_code_of `", search_from);
    if (head == std::string::npos) {
      return false;
    }
    const auto tick0 = head + std::string("### get_code_of `").size();
    const auto tick1 = pack_body.find('`', tick0);
    if (tick1 == std::string::npos) {
      return false;
    }
    const std::string tgt = pack_body.substr(tick0, tick1 - tick0);
    search_from = tick1 + 1;
    if (tgt.find("(omitido)") != std::string::npos) {
      continue;
    }
    const bool path_hit =
        !path.empty() && path.find('/') != std::string::npos &&
        (tgt.find(path) != std::string::npos ||
         to_snake_token(tgt).find(to_snake_token(path)) != std::string::npos);
    const bool stem_hit =
        sym.size() >= 4 && to_snake_token(tgt).find(sym) != std::string::npos;
    if (!path_hit && !stem_hit) {
      continue;
    }
    const auto fence0 = pack_body.find("```", tick1);
    const auto next_head = pack_body.find("### get_code_of `", tick1);
    if (fence0 == std::string::npos || (next_head != std::string::npos && fence0 > next_head)) {
      continue;
    }
    const auto fence1 = pack_body.find("```", fence0 + 3);
    if (fence1 == std::string::npos || fence1 <= fence0 + 3) {
      continue;
    }
    const std::string body = pack_body.substr(fence0 + 3, fence1 - (fence0 + 3));
    const std::string body_key = to_snake_token(body);
    // Skip near-empty / include-only noise.
    if (body.size() < 40) {
      continue;
    }
    if (sym.size() >= 4) {
      if (body_key.find(sym) != std::string::npos) {
        return true;
      }
      continue;
    }
    if (line > 1) {
      // path:line locus: accept if fence has real code (not only includes).
      const bool has_code =
          body.find('{') != std::string::npos || body.find('(') != std::string::npos ||
          body.find("void ") != std::string::npos || body.find("bool ") != std::string::npos ||
          body.find("int ") != std::string::npos;
      if (has_code) {
        return true;
      }
    }
  }
}

std::vector<std::string> pack_targets_missing_bodies(const std::string& pack_body,
                                                      const std::vector<std::string>& targets) {
  std::vector<std::string> missing;
  for (const auto& t : targets) {
    if (t.find('/') == std::string::npos) {
      continue;  // bare stems are protect-only, not body requirements
    }
    if (!pack_target_has_symbol_body(pack_body, t)) {
      missing.push_back(t);
    }
  }
  return missing;
}

bool pack_must_anchors_covered(const std::string& pack_body,
                               const std::vector<std::string>& anchors, int min_ok) {
  if (pack_body.empty() || min_ok <= 0) {
    return false;
  }
  int ok = 0;
  for (const auto& a : anchors) {
    if (a.find('/') == std::string::npos) {
      continue;
    }
    if (pack_target_has_symbol_body(pack_body, a)) {
      ++ok;
      if (ok >= min_ok) {
        return true;
      }
    }
  }
  return false;
}

bool reject_hits_anchor(const std::string& reject, const std::vector<std::string>& anchors) {
  if (reject.empty() || anchors.empty()) {
    return false;
  }
  if (target_in_watchlist_normalized(reject, anchors)) {
    return true;
  }
  const auto expanded = expand_review_rejects_for_watchlist({reject}, anchors);
  for (const auto& e : expanded) {
    if (e != reject && target_in_watchlist_normalized(e, anchors)) {
      return true;
    }
  }
  const std::string rk = to_snake_token(reject);
  for (const auto& a : anchors) {
    const std::string ak = to_snake_token(a);
    if (ak.size() >= 4 && rk.find(ak) != std::string::npos) {
      return true;
    }
    if (rk.size() >= 4 && ak.find(rk) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> filter_rejects_excluding_anchors(
    const std::vector<std::string>& rejects, const std::vector<std::string>& anchors) {
  if (anchors.empty()) {
    return rejects;
  }
  std::vector<std::string> out;
  for (const auto& r : rejects) {
    if (!reject_hits_anchor(r, anchors)) {
      out.push_back(r);
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
    menu << label << " (prioriza hits del control pedido; orden = importancia):\n";
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
