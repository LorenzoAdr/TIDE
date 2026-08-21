#include "ai/l2_explore_a.hpp"

#include <algorithm>
#include <cctype>
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
  st->queue = build_a_scan_queue(ranked, opts);
  st->cursor = 0;
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
  j["done"] = st.done;
  j["orphans"] = st.orphans;
  j["rejected_stems"] = st.rejected_stems;

  nlohmann::json queue = nlohmann::json::array();
  for (const auto& it : st.queue) {
    queue.push_back({{"target", it.target},
                     {"path", it.path},
                     {"stem", it.stem},
                     {"symbol", it.symbol},
                     {"line", it.line},
                     {"window_hint", it.window_hint},
                     {"score", it.score}});
  }
  j["queue"] = std::move(queue);

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
  st.done = j.value("done", false);
  if (j.contains("orphans") && j["orphans"].is_array()) {
    for (const auto& o : j["orphans"]) {
      if (o.is_string()) {
        st.orphans.push_back(o.get<std::string>());
      }
    }
  }
  if (j.contains("rejected_stems") && j["rejected_stems"].is_array()) {
    for (const auto& o : j["rejected_stems"]) {
      if (o.is_string()) {
        st.rejected_stems.push_back(o.get<std::string>());
      }
    }
  }
  if (j.contains("queue") && j["queue"].is_array()) {
    for (const auto& it : j["queue"]) {
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
        st.queue.push_back(std::move(q));
      }
    }
  }
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
      << " expansions=" << st.expansions << "\n\n";
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
