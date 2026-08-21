#include "ai/l2_explore_a.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace tuide {
namespace {

std::string trim_copy(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '`')) {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '`')) {
    s.pop_back();
  }
  return s;
}

std::string path_file_stem(const std::string& file) {
  std::string base = file;
  const auto slash = base.find_last_of("/\\");
  if (slash != std::string::npos) {
    base = base.substr(slash + 1);
  }
  const auto dot = base.rfind('.');
  if (dot != std::string::npos && dot > 0) {
    base = base.substr(0, dot);
  }
  return base;
}

bool looks_like_ident(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  const unsigned char c0 = static_cast<unsigned char>(name[0]);
  if (!(std::isalpha(c0) || c0 == '_' || c0 == '~')) {
    return false;
  }
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (!(std::isalnum(u) || c == '_' || c == ':' || c == '~')) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::vector<AQueueItem> build_a_scan_queue(const std::vector<AQueueBuildInput>& ranked,
                                           const AQueueBuildOpts& opts) {
  std::vector<AQueueBuildInput> ordered = ranked;
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const AQueueBuildInput& a, const AQueueBuildInput& b) {
                     if (a.score != b.score) {
                       return a.score > b.score;
                     }
                     if (a.file != b.file) {
                       return a.file < b.file;
                     }
                     return a.line < b.line;
                   });

  const std::size_t limit =
      opts.max_items == 0 ? ordered.size() : std::min(opts.max_items, ordered.size());
  const int max_per = opts.max_per_stem <= 0 ? 1000000 : opts.max_per_stem;

  std::vector<AQueueItem> out;
  out.reserve(limit);
  std::unordered_map<std::string, int> per_stem;

  for (const auto& in : ordered) {
    if (out.size() >= limit) {
      break;
    }
    if (in.file.empty()) {
      continue;
    }
    std::string stem = in.stem.empty() ? path_file_stem(in.file) : in.stem;
    if (stem.empty()) {
      continue;
    }
    if (per_stem[stem] >= max_per) {
      continue;
    }

    AQueueItem item;
    item.path = in.file;
    item.stem = std::move(stem);
    item.line = in.line;
    item.score = static_cast<float>(in.score);
    item.symbol = in.name;

    std::string anchor;
    if (!in.name.empty() && looks_like_ident(in.name)) {
      anchor = in.file + ":" + in.name;
    } else if (in.line > 0) {
      anchor = in.file + ":" + std::to_string(in.line);
    } else {
      continue;  // bare path is not a useful A window
    }

    // Window policy: long bodies → #tail first; unknown function bodies → #tail if opted.
    if (in.body_lines > 0 && in.body_lines > opts.long_body_lines) {
      item.window_hint = "tail";
    } else if (in.body_lines > 0) {
      item.window_hint.clear();
    } else if (opts.prefer_tail_for_functions && in.functionish) {
      item.window_hint = "tail";
    }

    item.target = anchor;
    if (!item.window_hint.empty()) {
      item.target += "#" + item.window_hint;
    }

    ++per_stem[item.stem];
    out.push_back(std::move(item));
  }
  return out;
}

void a_state_seed_queue(AState* st, const std::vector<AQueueBuildInput>& ranked,
                        const AQueueBuildOpts& opts) {
  if (st == nullptr) {
    return;
  }
  // Build a wider pool (queue + reserve) without diversify starving later ranks.
  AQueueBuildOpts wide = opts;
  const std::size_t primary = opts.max_items == 0
                                  ? static_cast<std::size_t>(kAMaxQueueDefault)
                                  : opts.max_items;
  wide.max_items = primary + primary;  // top-K + next-K for layer-1 expansion
  wide.max_per_stem = opts.max_per_stem;
  auto pool = build_a_scan_queue(ranked, wide);
  st->queue.clear();
  st->reserve.clear();
  for (std::size_t i = 0; i < pool.size(); ++i) {
    if (i < primary) {
      st->queue.push_back(std::move(pool[i]));
    } else {
      st->reserve.push_back(std::move(pool[i]));
    }
  }
  st->cursor = 0;
  st->expansions = 0;
  st->last_expand_layer = 0;
  st->expand_exhausted = false;
}

namespace {

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool item_covers_needle(const AQueueItem& it, const std::string& needle_l) {
  if (needle_l.size() < 3) {
    return false;
  }
  const std::string hay =
      ascii_lower(it.path + " " + it.stem + " " + it.symbol + " " + it.target);
  return hay.find(needle_l) != std::string::npos;
}

int orphan_hit_score(const AQueueItem& it, const std::vector<std::string>& orphans) {
  int score = 0;
  for (const auto& o : orphans) {
    if (item_covers_needle(it, ascii_lower(o))) {
      score += 10;
    }
  }
  return score;
}

bool target_already_queued(const AState& st, const std::string& target) {
  for (const auto& q : st.queue) {
    if (q.target == target) {
      return true;
    }
  }
  for (const auto& q : st.reserve) {
    if (q.target == target) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<std::string> a_compute_orphans(const AState& st,
                                           const std::vector<std::string>& needles) {
  std::vector<std::string> orphans;
  std::string covered;
  for (const auto& n : st.notes) {
    if (n.verdict != AVerdictKind::Useful) {
      continue;
    }
    covered += " " + ascii_lower(n.target + " " + n.anchor + " " + n.stem + " " + n.why);
  }
  for (const auto& loc : st.loci_draft) {
    covered += " " + ascii_lower(loc.anchor + " " + loc.stem + " " + loc.why);
  }
  for (const auto& needle : needles) {
    std::string n = ascii_lower(needle);
    while (!n.empty() && (n.front() == ' ' || n.front() == '`')) {
      n.erase(n.begin());
    }
    while (!n.empty() && (n.back() == ' ' || n.back() == '`')) {
      n.pop_back();
    }
    if (n.size() < 4) {
      continue;
    }
    if (covered.find(n) != std::string::npos) {
      continue;
    }
    orphans.push_back(needle);
  }
  return orphans;
}

AExpandResult maybe_expand_a_queue(AState* st, const std::vector<std::string>& orphans,
                                   const std::vector<AQueueBuildInput>& extra_recall,
                                   const AQueueBuildOpts& opts) {
  AExpandResult r;
  if (st == nullptr) {
    r.reason = "null state";
    return r;
  }
  int useful = 0;
  for (const auto& n : st->notes) {
    if (n.verdict == AVerdictKind::Useful) {
      ++useful;
    }
  }
  const bool queue_done = st->cursor >= static_cast<int>(st->queue.size());
  const bool weak = useful == 0;
  const bool has_orphans = !orphans.empty();
  if (!queue_done && !(weak && st->turns >= 3 && has_orphans)) {
    r.reason = "no expand signal";
    return r;
  }
  if (!weak && !has_orphans) {
    r.reason = "have useful and no orphans";
    return r;
  }
  if (st->expansions >= kAMaxExpansions) {
    r.exhausted = true;
    st->expand_exhausted = true;
    r.reason = "expansion cap";
    return r;
  }

  // Layer 1: append next reserve slice (same ranking).
  if (st->last_expand_layer < 1 && !st->reserve.empty()) {
    const int take = std::min(static_cast<int>(st->reserve.size()), kAMaxQueueDefault);
    for (int i = 0; i < take; ++i) {
      st->queue.push_back(std::move(st->reserve[static_cast<std::size_t>(i)]));
    }
    st->reserve.erase(st->reserve.begin(),
                      st->reserve.begin() + take);
    st->last_expand_layer = 1;
    ++st->expansions;
    r.expanded = true;
    r.layer = 1;
    r.added = take;
    r.reason = "layer1 extend reserve";
    return r;
  }

  // Layer 2: re-rank remaining reserve by orphan hits and take top N.
  if (st->last_expand_layer < 2 && !st->reserve.empty() && has_orphans) {
    std::stable_sort(st->reserve.begin(), st->reserve.end(),
                     [&](const AQueueItem& a, const AQueueItem& b) {
                       const int sa = orphan_hit_score(a, orphans);
                       const int sb = orphan_hit_score(b, orphans);
                       if (sa != sb) {
                         return sa > sb;
                       }
                       return a.score > b.score;
                     });
    const int take = std::min(20, static_cast<int>(st->reserve.size()));
    int added = 0;
    for (int i = 0; i < take; ++i) {
      if (orphan_hit_score(st->reserve[static_cast<std::size_t>(i)], orphans) <= 0 && i >= 5) {
        break;
      }
      st->queue.push_back(std::move(st->reserve[static_cast<std::size_t>(i)]));
      ++added;
    }
    st->reserve.erase(st->reserve.begin(), st->reserve.begin() + added);
    st->last_expand_layer = 2;
    ++st->expansions;
    r.expanded = true;
    r.layer = 2;
    r.added = added;
    r.reason = "layer2 orphan rerank";
    return r;
  }

  // Layer 3: inject extra recall (stem/search) matching orphans.
  if (st->last_expand_layer < 3 && (!extra_recall.empty() || has_orphans)) {
    AQueueBuildOpts o = opts;
    o.max_items = 20;
    o.max_per_stem = 3;
    auto extras = build_a_scan_queue(extra_recall, o);
    // If no extras, synthesize weak items from orphan tokens (path-like only skipped).
    if (extras.empty()) {
      for (const auto& orphan : orphans) {
        if (orphan.find('/') == std::string::npos) {
          continue;
        }
        AQueueItem it;
        it.path = orphan;
        it.target = orphan;
        it.stem = orphan;
        const auto slash = orphan.find_last_of('/');
        if (slash != std::string::npos) {
          it.stem = orphan.substr(slash + 1);
          const auto dot = it.stem.rfind('.');
          if (dot != std::string::npos) {
            it.stem = it.stem.substr(0, dot);
          }
        }
        it.score = 1.f;
        extras.push_back(std::move(it));
      }
    }
    int added = 0;
    for (auto& it : extras) {
      if (target_already_queued(*st, it.target)) {
        continue;
      }
      if (!orphans.empty() && orphan_hit_score(it, orphans) <= 0) {
        // Prefer orphan hits; still allow a few high-score recall items.
        if (it.score < 100.f && added >= 5) {
          continue;
        }
      }
      st->queue.push_back(std::move(it));
      ++added;
      if (added >= 20) {
        break;
      }
    }
    st->last_expand_layer = 3;
    ++st->expansions;
    if (added == 0) {
      r.exhausted = st->expansions >= kAMaxExpansions;
      st->expand_exhausted = r.exhausted;
      r.reason = "layer3 no new candidates";
      return r;
    }
    r.expanded = true;
    r.layer = 3;
    r.added = added;
    r.reason = "layer3 recall";
    return r;
  }

  r.exhausted = true;
  st->expand_exhausted = true;
  r.reason = "no more layers";
  return r;
}

bool a_plan_target_allowed(const AState& st, const std::string& target) {
  if (target.empty()) {
    return false;
  }
  const std::string tl = ascii_lower(target);
  for (const auto& loc : st.loci_draft) {
    if (loc.anchor.empty()) {
      continue;
    }
    const std::string al = ascii_lower(loc.anchor);
    if (tl.find(al) != std::string::npos || al.find(tl) != std::string::npos) {
      return true;
    }
    // Same path prefix (path:Symbol vs path).
    const auto colon = loc.anchor.find(':');
    const std::string path = colon == std::string::npos ? loc.anchor : loc.anchor.substr(0, colon);
    if (!path.empty() && tl.find(ascii_lower(path)) != std::string::npos) {
      return true;
    }
    if (!loc.stem.empty() && tl.find(ascii_lower(loc.stem)) != std::string::npos) {
      return true;
    }
  }
  for (const auto& p : st.b_allow_paths) {
    if (!p.empty() && tl.find(ascii_lower(p)) != std::string::npos) {
      return true;
    }
  }
  // Sibling header of a locus path.
  for (const auto& loc : st.loci_draft) {
    const auto colon = loc.anchor.find(':');
    std::string path = colon == std::string::npos ? loc.anchor : loc.anchor.substr(0, colon);
    if (path.size() > 4) {
      const std::string base = path.substr(0, path.size() - 4);  // rough .cpp
      if (tl.find(ascii_lower(base)) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

std::vector<ALocus> a_loci_must_ordered(std::vector<ALocus> loci) {
  auto rank = [](ALocusRole r) {
    switch (r) {
      case ALocusRole::Primary:
        return 0;
      case ALocusRole::Secondary:
        return 1;
      case ALocusRole::Suspect:
        return 2;
      default:
        return 3;
    }
  };
  std::stable_sort(loci.begin(), loci.end(), [&](const ALocus& a, const ALocus& b) {
    return rank(a.role) < rank(b.role);
  });
  return loci;
}

std::vector<AQueueBuildInput> a_queue_inputs_from_ranked_map_markdown(const std::string& map_md,
                                                                      std::size_t max_n) {
  std::vector<AQueueBuildInput> out;
  if (map_md.empty() || max_n == 0) {
    return out;
  }
  std::istringstream iss(map_md);
  std::string line;
  int rank_score = 100000;
  while (std::getline(iss, line)) {
    if (line.empty() || line[0] < '0' || line[0] > '9') {
      continue;
    }
    std::size_t i = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
      ++i;
    }
    if (i >= line.size() || line[i] != '.') {
      continue;
    }
    ++i;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    if (i >= line.size()) {
      continue;
    }
    // path or path:line or path:Symbol before space / em-dash / [
    std::size_t path_end = i;
    while (path_end < line.size() && line[path_end] != ' ' && line[path_end] != '\t' &&
           line[path_end] != '[' && static_cast<unsigned char>(line[path_end]) != 0xE2) {
      ++path_end;
    }
    std::string loc = line.substr(i, path_end - i);
    while (!loc.empty() && (loc.back() == ',' || loc.back() == ';')) {
      loc.pop_back();
    }
    if (loc.find('/') == std::string::npos && loc.find('.') == std::string::npos) {
      continue;
    }
    AQueueBuildInput in;
    in.score = rank_score;
    rank_score = std::max(1, rank_score - 10);
    const auto colon = loc.rfind(':');
    if (colon != std::string::npos && colon + 1 < loc.size()) {
      const std::string tail = loc.substr(colon + 1);
      bool all_digit = !tail.empty();
      for (char c : tail) {
        if (c < '0' || c > '9') {
          all_digit = false;
          break;
        }
      }
      in.file = loc.substr(0, colon);
      if (all_digit) {
        in.line = std::atoi(tail.c_str());
      } else {
        in.name = tail;
      }
    } else {
      in.file = loc;
    }
    // Prefer backtick symbol if present
    const auto tick0 = line.find('`', path_end);
    if (tick0 != std::string::npos) {
      const auto tick1 = line.find('`', tick0 + 1);
      if (tick1 != std::string::npos && tick1 > tick0 + 1) {
        const std::string sym = line.substr(tick0 + 1, tick1 - tick0 - 1);
        if (!sym.empty() && looks_like_ident(sym)) {
          in.name = sym;
        }
      }
    }
    if (in.name.empty() && in.line <= 0) {
      continue;
    }
    in.functionish = true;
    out.push_back(std::move(in));
    if (out.size() >= max_n) {
      break;
    }
  }
  return out;
}

const char* a_verdict_kind_name(AVerdictKind kind) {
  switch (kind) {
    case AVerdictKind::Useful:
      return "useful";
    case AVerdictKind::Reject:
      return "reject";
    case AVerdictKind::Uncertain:
      return "uncertain";
    case AVerdictKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

AVerdictKind parse_a_verdict_kind(const std::string& s) {
  if (s == "useful" || s == "keep" || s == "hit") {
    return AVerdictKind::Useful;
  }
  if (s == "reject" || s == "discard" || s == "no") {
    return AVerdictKind::Reject;
  }
  if (s == "uncertain" || s == "maybe" || s == "weak") {
    return AVerdictKind::Uncertain;
  }
  return AVerdictKind::Unknown;
}

const char* a_locus_role_name(ALocusRole role) {
  switch (role) {
    case ALocusRole::Primary:
      return "primary";
    case ALocusRole::Secondary:
      return "secondary";
    case ALocusRole::Suspect:
      return "suspect";
    case ALocusRole::Unknown:
      return "unknown";
  }
  return "unknown";
}

ALocusRole parse_a_locus_role(const std::string& s) {
  if (s == "primary" || s == "must" || s == "main") {
    return ALocusRole::Primary;
  }
  if (s == "secondary" || s == "support") {
    return ALocusRole::Secondary;
  }
  if (s == "suspect" || s == "maybe") {
    return ALocusRole::Suspect;
  }
  return ALocusRole::Unknown;
}

AVerdict parse_a_verdict_json(const nlohmann::json& j) {
  AVerdict v;
  if (!j.is_object()) {
    return v;
  }
  v.target = trim_copy(j.value("target", ""));
  v.verdict = parse_a_verdict_kind(j.value("verdict", j.value("v", "")));
  v.anchor = trim_copy(j.value("anchor", ""));
  v.stem = trim_copy(j.value("stem", ""));
  v.role = parse_a_locus_role(j.value("role", ""));
  v.why = j.value("why", j.value("reason", ""));
  if (v.anchor.empty() && !v.target.empty() && v.verdict == AVerdictKind::Useful) {
    // Soft default: useful without explicit anchor → use target (strip #window).
    v.anchor = v.target;
    const auto hash = v.anchor.find('#');
    if (hash != std::string::npos) {
      v.anchor = v.anchor.substr(0, hash);
    }
  }
  return v;
}

ALocus parse_a_locus_json(const nlohmann::json& j) {
  ALocus loc;
  if (!j.is_object()) {
    return loc;
  }
  loc.stem = trim_copy(j.value("stem", ""));
  loc.anchor = trim_copy(j.value("anchor", j.value("target", "")));
  loc.role = parse_a_locus_role(j.value("role", "primary"));
  loc.why = j.value("why", j.value("reason", ""));
  loc.window = trim_copy(j.value("window", ""));
  return loc;
}

bool parse_a_verdicts_array(const nlohmann::json& j, std::vector<AVerdict>* out, std::string* err) {
  if (out == nullptr) {
    return false;
  }
  out->clear();
  if (!j.contains("verdicts") || !j["verdicts"].is_array()) {
    if (err) {
      *err = "a_judge sin array verdicts";
    }
    return false;
  }
  for (const auto& item : j["verdicts"]) {
    AVerdict v = parse_a_verdict_json(item);
    if (v.target.empty() && v.anchor.empty()) {
      continue;
    }
    if (v.verdict == AVerdictKind::Unknown) {
      continue;
    }
    out->push_back(std::move(v));
    if (static_cast<int>(out->size()) >= kAMaxPeeksPerTurn + 2) {
      break;
    }
  }
  if (out->empty()) {
    if (err) {
      *err = "a_judge.verdicts vacío o inválido";
    }
    return false;
  }
  return true;
}

bool parse_a_loci_array(const nlohmann::json& j, std::vector<ALocus>* out, std::string* err) {
  if (out == nullptr) {
    return false;
  }
  out->clear();
  if (!j.contains("loci") || !j["loci"].is_array()) {
    if (err) {
      *err = "a_done sin array loci";
    }
    return false;
  }
  for (const auto& item : j["loci"]) {
    ALocus loc = parse_a_locus_json(item);
    if (loc.anchor.empty()) {
      continue;
    }
    if (loc.role == ALocusRole::Unknown) {
      loc.role = ALocusRole::Primary;
    }
    out->push_back(std::move(loc));
    if (out->size() >= 12) {
      break;
    }
  }
  if (out->empty()) {
    if (err) {
      *err = "a_done.loci vacío (hace falta al menos un anchor)";
    }
    return false;
  }
  return true;
}

nlohmann::json a_state_to_json(const AState& st) {
  nlohmann::json j;
  j["cursor"] = st.cursor;
  j["peeks_used"] = st.peeks_used;
  j["turns"] = st.turns;
  j["expansions"] = st.expansions;
  j["last_expand_layer"] = st.last_expand_layer;
  j["done"] = st.done;
  j["expand_exhausted"] = st.expand_exhausted;
  j["orphans"] = st.orphans;
  j["rejected_stems"] = st.rejected_stems;
  j["b_allow_paths"] = st.b_allow_paths;

  auto items_to_json = [](const std::vector<AQueueItem>& items) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& it : items) {
      arr.push_back({{"target", it.target},
                     {"path", it.path},
                     {"stem", it.stem},
                     {"symbol", it.symbol},
                     {"line", it.line},
                     {"window_hint", it.window_hint},
                     {"score", it.score}});
    }
    return arr;
  };
  j["queue"] = items_to_json(st.queue);
  j["reserve"] = items_to_json(st.reserve);

  nlohmann::json loci = nlohmann::json::array();
  for (const auto& loc : st.loci_draft) {
    loci.push_back({{"stem", loc.stem},
                    {"anchor", loc.anchor},
                    {"role", a_locus_role_name(loc.role)},
                    {"why", loc.why},
                    {"window", loc.window}});
  }
  j["loci_draft"] = std::move(loci);

  nlohmann::json notes = nlohmann::json::array();
  for (const auto& v : st.notes) {
    notes.push_back({{"target", v.target},
                     {"verdict", a_verdict_kind_name(v.verdict)},
                     {"anchor", v.anchor},
                     {"stem", v.stem},
                     {"role", a_locus_role_name(v.role)},
                     {"why", v.why}});
  }
  j["notes"] = std::move(notes);
  return j;
}

bool a_state_from_json(const nlohmann::json& j, AState* out, std::string* err) {
  if (out == nullptr || !j.is_object()) {
    if (err) {
      *err = "a_state JSON inválido";
    }
    return false;
  }
  AState st;
  st.cursor = j.value("cursor", 0);
  st.peeks_used = j.value("peeks_used", 0);
  st.turns = j.value("turns", 0);
  st.expansions = j.value("expansions", 0);
  st.last_expand_layer = j.value("last_expand_layer", 0);
  st.done = j.value("done", false);
  st.expand_exhausted = j.value("expand_exhausted", false);
  auto read_str_array = [&](const char* key, std::vector<std::string>* dest) {
    if (j.contains(key) && j[key].is_array()) {
      for (const auto& o : j[key]) {
        if (o.is_string()) {
          dest->push_back(o.get<std::string>());
        }
      }
    }
  };
  read_str_array("orphans", &st.orphans);
  read_str_array("rejected_stems", &st.rejected_stems);
  read_str_array("b_allow_paths", &st.b_allow_paths);

  auto read_items = [&](const char* key, std::vector<AQueueItem>* dest) {
    if (!j.contains(key) || !j[key].is_array()) {
      return;
    }
    for (const auto& it : j[key]) {
      if (!it.is_object()) {
        continue;
      }
      AQueueItem q;
      q.target = it.value("target", "");
      q.path = it.value("path", "");
      q.stem = it.value("stem", "");
      q.symbol = it.value("symbol", "");
      q.line = it.value("line", 0);
      q.window_hint = it.value("window_hint", "");
      q.score = it.value("score", 0.f);
      if (!q.target.empty() || !q.path.empty()) {
        dest->push_back(std::move(q));
      }
    }
  };
  read_items("queue", &st.queue);
  read_items("reserve", &st.reserve);

  if (j.contains("loci_draft") && j["loci_draft"].is_array()) {
    for (const auto& it : j["loci_draft"]) {
      ALocus loc = parse_a_locus_json(it);
      if (!loc.anchor.empty()) {
        st.loci_draft.push_back(std::move(loc));
      }
    }
  }
  if (j.contains("notes") && j["notes"].is_array()) {
    for (const auto& it : j["notes"]) {
      AVerdict v = parse_a_verdict_json(it);
      if (v.verdict != AVerdictKind::Unknown) {
        st.notes.push_back(std::move(v));
      }
    }
  }
  *out = std::move(st);
  return true;
}

std::string a_notes_markdown(const AState& st) {
  std::ostringstream out;
  out << "# Phase A notes\n\n";
  out << "peeks_used=" << st.peeks_used << " turns=" << st.turns
      << " cursor=" << st.cursor << "/" << st.queue.size()
      << " reserve=" << st.reserve.size() << " expansions=" << st.expansions
      << " layer=" << st.last_expand_layer
      << (st.expand_exhausted ? " EXHAUSTED" : "") << "\n\n";
  if (!st.orphans.empty()) {
    out << "## Orphans\n";
    for (const auto& o : st.orphans) {
      out << "- " << o << "\n";
    }
    out << "\n";
  }
  if (!st.rejected_stems.empty()) {
    out << "## Rejected stems\n";
    for (const auto& s : st.rejected_stems) {
      out << "- " << s << "\n";
    }
    out << "\n";
  }
  out << "## Verdicts\n";
  if (st.notes.empty()) {
    out << "_(none)_\n";
  } else {
    for (const auto& v : st.notes) {
      out << "- [" << a_verdict_kind_name(v.verdict) << "] `" << v.target << "`";
      if (!v.stem.empty()) {
        out << " stem=" << v.stem;
      }
      if (!v.anchor.empty() && v.anchor != v.target) {
        out << " anchor=`" << v.anchor << "`";
      }
      if (v.role != ALocusRole::Unknown) {
        out << " role=" << a_locus_role_name(v.role);
      }
      if (!v.why.empty()) {
        out << " — " << v.why;
      }
      out << "\n";
    }
  }
  out << "\n## Loci draft\n";
  if (st.loci_draft.empty()) {
    out << "_(none)_\n";
  } else {
    for (const auto& loc : st.loci_draft) {
      out << "- [" << a_locus_role_name(loc.role) << "] `" << loc.anchor << "`";
      if (!loc.stem.empty()) {
        out << " stem=" << loc.stem;
      }
      if (!loc.window.empty()) {
        out << " window=" << loc.window;
      }
      if (!loc.why.empty()) {
        out << " — " << loc.why;
      }
      out << "\n";
    }
  }
  return out.str();
}

}  // namespace tuide
