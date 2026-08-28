#include "ai/l2_explore_a.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

std::string path_family_from_file(const std::string& file) {
  // src/ui/foo.cpp → ui; src/ai/x.hpp → ai; tests/foo → tests; bare → other
  std::string p = file;
  while (!p.empty() && (p[0] == '.' || p[0] == '/')) {
    p.erase(p.begin());
  }
  const auto slash = p.find('/');
  if (slash == std::string::npos) {
    return p.empty() ? "other" : path_file_stem(p);
  }
  std::string top = p.substr(0, slash);
  std::string rest = p.substr(slash + 1);
  if ((top == "src" || top == "include" || top == "lib") && !rest.empty()) {
    const auto slash2 = rest.find('/');
    return slash2 == std::string::npos ? path_file_stem(rest) : rest.substr(0, slash2);
  }
  return top.empty() ? "other" : top;
}

bool is_srcish_path(const std::string& file) {
  std::string p = file;
  while (!p.empty() && (p[0] == '.' || p[0] == '/')) {
    p.erase(p.begin());
  }
  return p.rfind("src/", 0) == 0 || p.rfind("include/", 0) == 0 || p.rfind("lib/", 0) == 0;
}

bool is_noise_path_family(const std::string& fam) {
  return fam == "tests" || fam == "tools" || fam == "examples" || fam == "docs" ||
         fam == "Testing" || fam == "build" || fam == "other";
}

void diversify_round_robin_by_family(std::vector<AQueueItem>* items) {
  if (items == nullptr || items->size() < 2) {
    return;
  }
  // Preserve score order within each family (input already score-sorted).
  // Drain src families completely via round-robin; noise (tests/tools/…) only after.
  std::vector<std::string> src_order;
  std::vector<std::string> noise_order;
  std::unordered_map<std::string, std::deque<AQueueItem>> buckets;
  for (auto& it : *items) {
    const std::string fam = path_family_from_file(it.path);
    const bool noise = is_noise_path_family(fam) || !is_srcish_path(it.path);
    if (buckets.find(fam) == buckets.end()) {
      if (noise) {
        noise_order.push_back(fam);
      } else {
        src_order.push_back(fam);
      }
    }
    buckets[fam].push_back(std::move(it));
  }
  items->clear();
  auto drain_rr = [&](const std::vector<std::string>& order) {
    bool any = true;
    while (any) {
      any = false;
      for (const auto& fam : order) {
        auto& q = buckets[fam];
        if (q.empty()) {
          continue;
        }
        items->push_back(std::move(q.front()));
        q.pop_front();
        any = true;
      }
    }
  };
  drain_rr(src_order);
  // Noise: keep relative score order (concat buckets in first-seen order).
  for (const auto& fam : noise_order) {
    auto& q = buckets[fam];
    while (!q.empty()) {
      items->push_back(std::move(q.front()));
      q.pop_front();
    }
  }
}

void parse_map_why_line(const std::string& line, AQueueBuildInput* in) {
  if (in == nullptr || line.find("why:") == std::string::npos) {
    return;
  }
  auto tag_until = [&](const std::string& tag) -> std::string {
    const std::size_t pos = line.find(tag);
    if (pos == std::string::npos) {
      return {};
    }
    std::size_t start = pos + tag.size();
    std::size_t end = line.size();
    const std::size_t dot = line.find("\xc2\xb7", start);
    if (dot != std::string::npos) {
      end = dot;
    }
    return trim_copy(line.substr(start, end - start));
  };
  const std::string stem = tag_until("stem=");
  if (!stem.empty()) {
    const auto hash = stem.find('#');
    if (hash != std::string::npos) {
      in->stem_sem_rank = std::atoi(stem.substr(hash + 1).c_str());
      in->stem = stem.substr(0, hash);
    } else {
      const auto sp = stem.find(' ');
      in->stem = sp == std::string::npos ? stem : stem.substr(0, sp);
    }
    if (stem.find("dup_stem") != std::string::npos) {
      in->dup_stem = true;
    }
  }
  if (line.find("dup_stem") != std::string::npos) {
    in->dup_stem = true;
  }
  const std::string refs = tag_until("refs≈");
  if (!refs.empty()) {
    in->refs_in = std::atoi(refs.c_str());
  }
  const std::string body = tag_until("body=");
  if (!body.empty()) {
    in->body_sem_permille = static_cast<int>(std::atof(body.c_str()) * 1000.0);
  }
  const std::string fr = tag_until("file_rank=");
  if (!fr.empty()) {
    const auto slash = fr.find('/');
    if (slash != std::string::npos) {
      in->file_rank = std::atoi(fr.substr(0, slash).c_str());
      in->file_count = std::atoi(fr.substr(slash + 1).c_str());
    }
  }
  in->map_related = tag_until("related=");
}

bool is_file_level_map_entry(const AQueueBuildInput& in) {
  if (in.line > 1) {
    return false;
  }
  if (in.name.empty()) {
    return true;
  }
  const std::string stem = in.stem.empty() ? path_file_stem(in.file) : in.stem;
  return !stem.empty() && in.name == stem;
}

bool is_generic_cancel_name(const std::string& name) {
  if (name == "cancel") {
    return true;
  }
  return name.size() > 8 && name.compare(name.size() - 8, 8, "::cancel") == 0;
}

bool map_path_is_header(const std::string& path) {
  if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".hpp") == 0) {
    return true;
  }
  return path.size() >= 2 && path.compare(path.size() - 2, 2, ".h") == 0;
}

std::string map_path_to_cpp(const std::string& path) {
  if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".hpp") == 0) {
    return path.substr(0, path.size() - 4) + ".cpp";
  }
  if (path.size() >= 2 && path.compare(path.size() - 2, 2, ".h") == 0) {
    return path.substr(0, path.size() - 2) + ".cpp";
  }
  return path;
}

bool map_input_skip_header_twin(const AQueueBuildInput& in, const std::string& workspace_root,
                                bool enabled) {
  if (!enabled || !map_path_is_header(in.file)) {
    return false;
  }
  namespace fs = std::filesystem;
  const fs::path cpp = fs::path(workspace_root) / map_path_to_cpp(in.file);
  return fs::exists(cpp);
}

}  // namespace

std::string a_path_family(const std::string& path_or_target) {
  std::string file = path_or_target;
  // strip fake "path:" prefix the model sometimes emits
  if (file.rfind("path:", 0) == 0) {
    file = file.substr(5);
  }
  const auto hash = file.find('#');
  if (hash != std::string::npos) {
    file = file.substr(0, hash);
  }
  const auto colon = file.rfind(':');
  if (colon != std::string::npos && file.find('/') != std::string::npos) {
    file = file.substr(0, colon);
  }
  return path_family_from_file(file);
}

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

  auto try_add = [&](const AQueueBuildInput& in) -> bool {
    if (out.size() >= limit) {
      return false;
    }
    if (in.file.empty()) {
      return false;
    }
    std::string stem = in.stem.empty() ? path_file_stem(in.file) : in.stem;
    if (stem.empty()) {
      return false;
    }
    if (per_stem[stem] >= max_per) {
      return false;
    }

    AQueueItem item;
    item.path = in.file;
    item.stem = std::move(stem);
    item.line = in.line;
    item.score = static_cast<float>(in.score);
    item.symbol = in.name;
    item.map_related = in.map_related;
    item.refs_in = in.refs_in;
    item.body_sem_permille = in.body_sem_permille;
    item.file_rank = in.file_rank;
    item.file_count = in.file_count;
    item.dup_stem = in.dup_stem;
    item.stem_sem_rank = in.stem_sem_rank;

    std::string anchor;
    if (!in.name.empty() && looks_like_ident(in.name)) {
      anchor = in.file + ":" + in.name;
    } else if (in.line > 0) {
      anchor = in.file + ":" + std::to_string(in.line);
    } else {
      return false;
    }

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
    return true;
  };

  // Pass 1: src/include/lib (locate signal). Pass 2: fill remainder (tests/tools last).
  if (opts.prefer_src_paths) {
    for (const auto& in : ordered) {
      if (out.size() >= limit) {
        break;
      }
      if (is_srcish_path(in.file)) {
        try_add(in);
      }
    }
    for (const auto& in : ordered) {
      if (out.size() >= limit) {
        break;
      }
      if (!is_srcish_path(in.file)) {
        try_add(in);
      }
    }
  } else {
    for (const auto& in : ordered) {
      if (out.size() >= limit) {
        break;
      }
      try_add(in);
    }
  }
  if (opts.diversify_path_family) {
    diversify_round_robin_by_family(&out);
  }
  return out;
}

void a_state_seed_queue(AState* st, const std::vector<AQueueBuildInput>& ranked,
                        const AQueueBuildOpts& opts) {
  if (st == nullptr) {
    return;
  }
  // Primary queue: map score order (no family RR) so top ranked L0s are actually peeked.
  // Reserve: wider pool; optional light diversify so expansions are not one-module-only.
  const std::size_t primary = opts.max_items == 0
                                  ? static_cast<std::size_t>(kAMaxQueueDefault)
                                  : opts.max_items;

  AQueueBuildOpts prefix = opts;
  prefix.max_items = primary;
  prefix.diversify_path_family = false;
  st->queue = build_a_scan_queue(ranked, prefix);

  AQueueBuildOpts wide = opts;
  wide.max_items = primary + primary;
  wide.diversify_path_family = true;  // only for reserve / expansion pool
  auto pool = build_a_scan_queue(ranked, wide);
  st->reserve.clear();
  std::unordered_set<std::string> in_q;
  for (const auto& q : st->queue) {
    in_q.insert(q.target);
  }
  for (auto& it : pool) {
    if (in_q.count(it.target)) {
      continue;
    }
    if (st->reserve.size() >= primary) {
      break;
    }
    in_q.insert(it.target);
    st->reserve.push_back(std::move(it));
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

std::string a_stem_from_path(const std::string& path_or_anchor) {
  std::string file = path_or_anchor;
  const auto colon = file.find(':');
  if (colon != std::string::npos && file.find('/') != std::string::npos) {
    file = file.substr(0, colon);
  }
  return path_file_stem(file);
}

bool a_looks_like_path_anchor(const std::string& s) {
  if (s.empty()) {
    return false;
  }
  // Real repo anchors look like src/foo.cpp:Sym or path:12 — not "path:Sym".
  if (s.rfind("path:", 0) == 0 && s.find('/') == std::string::npos) {
    return false;
  }
  return s.find('/') != std::string::npos || s.find('.') != std::string::npos;
}

void a_strip_window(std::string* anchor, std::string* window_out) {
  if (anchor == nullptr) {
    return;
  }
  const auto hash = anchor->find('#');
  if (hash == std::string::npos) {
    return;
  }
  if (window_out != nullptr && window_out->empty()) {
    *window_out = anchor->substr(hash + 1);
  }
  *anchor = anchor->substr(0, hash);
}

void a_normalize_verdict(AVerdict* v) {
  if (v == nullptr) {
    return;
  }
  std::string win;
  a_strip_window(&v->target, &win);
  if (!a_looks_like_path_anchor(v->anchor)) {
    v->anchor = v->target;
  }
  a_strip_window(&v->anchor, &win);
  if (v->stem.empty() || v->stem.find('/') != std::string::npos ||
      v->stem.find(':') != std::string::npos) {
    const std::string src = a_looks_like_path_anchor(v->anchor) ? v->anchor : v->target;
    v->stem = a_stem_from_path(src);
  }
}

void a_normalize_locus(ALocus* loc) {
  if (loc == nullptr) {
    return;
  }
  a_strip_window(&loc->anchor, &loc->window);
  if (!a_looks_like_path_anchor(loc->anchor) && loc->stem.find('/') != std::string::npos) {
    // Model swapped stem/anchor (stem=src/…:Sym, anchor=path:Sym).
    loc->anchor = loc->stem;
    a_strip_window(&loc->anchor, &loc->window);
  }
  if (loc->stem.empty() || loc->stem.find('/') != std::string::npos ||
      loc->stem.find(':') != std::string::npos) {
    loc->stem = a_stem_from_path(loc->anchor);
  }
}

bool a_anchor_resolvable(const std::string& anchor) {
  if (!a_looks_like_path_anchor(anchor)) {
    return false;
  }
  const auto colon = anchor.rfind(':');
  if (colon == std::string::npos || colon + 1 >= anchor.size()) {
    return false;
  }
  return true;
}

void a_cap_locus_roles(std::vector<ALocus>* loci) {
  if (loci == nullptr) {
    return;
  }
  *loci = a_loci_must_ordered(*loci);
  int primary = 0;
  int secondary = 0;
  for (auto& loc : *loci) {
    if (loc.role == ALocusRole::Primary) {
      if (primary >= kAMaxPrimaryLoci) {
        loc.role = ALocusRole::Secondary;
      } else {
        ++primary;
      }
    }
    if (loc.role == ALocusRole::Secondary) {
      if (secondary >= kAMaxSecondaryLoci) {
        loc.role = ALocusRole::Suspect;
      } else {
        ++secondary;
      }
    }
  }
}

bool a_budget_relaxed(const AState& st) {
  if (st.peeks_used >= kAMaxPeeksTotal || st.turns >= kAMaxTurns) {
    return true;
  }
  const bool queue_done = st.cursor >= static_cast<int>(st.queue.size());
  return queue_done && st.reserve.empty() &&
         (st.expand_exhausted || st.expansions >= kAMaxExpansions);
}

int a_notes_distinct_stems(const AState& st) {
  std::unordered_set<std::string> stems;
  for (const auto& n : st.notes) {
    AVerdict tmp = n;
    a_normalize_verdict(&tmp);
    if (!tmp.stem.empty()) {
      stems.insert(tmp.stem);
    }
  }
  return static_cast<int>(stems.size());
}

int a_notes_path_families(const AState& st) {
  std::unordered_set<std::string> fams;
  for (const auto& n : st.notes) {
    const std::string src = !n.target.empty() ? n.target : n.anchor;
    if (src.empty()) {
      continue;
    }
    const std::string fam = a_path_family(src);
    if (fam == "tests" || fam == "tools" || fam == "examples" || fam == "docs" ||
        fam == "Testing" || fam == "build") {
      continue;
    }
    fams.insert(fam);
  }
  return static_cast<int>(fams.size());
}

int a_queue_path_families(const AState& st) {
  std::unordered_set<std::string> fams;
  auto consider = [&](std::string path) {
    while (!path.empty() && (path[0] == '.' || path[0] == '/')) {
      path.erase(path.begin());
    }
    if (path.rfind("src/", 0) != 0 && path.rfind("include/", 0) != 0 &&
        path.rfind("lib/", 0) != 0) {
      return;
    }
    fams.insert(a_path_family(path));
  };
  for (const auto& q : st.queue) {
    consider(q.path);
  }
  for (const auto& q : st.reserve) {
    consider(q.path);
  }
  return static_cast<int>(fams.size());
}

bool a_enough_locate_breadth(const AState& st) {
  if (a_budget_relaxed(st)) {
    return true;
  }
  const int stems = a_notes_distinct_stems(st);
  const bool breadth =
      st.peeks_used >= kAMinPeeksBeforeDone || stems >= kAMinStemsBeforeDone;
  // Effect Summary A0: cards replace body peeks for breadth.
  const bool es_cards =
      a_effect_summary_enabled() && st.cards_used >= kA0MaxCardsPerTurn && stems >= 2;
  // Small queues: seeing the whole queue counts as enough peeks.
  const int qn = static_cast<int>(st.queue.size());
  const bool small_queue_done =
      qn > 0 && qn < kAMinPeeksBeforeDone && st.peeks_used >= qn && stems >= 1;
  return breadth || es_cards || small_queue_done;
}

bool a_validate_a_done(const AState& st, const std::vector<ALocus>& loci, std::string* err) {
  auto fail = [&](const std::string& msg) {
    if (err) {
      *err = msg;
    }
    return false;
  };
  if (loci.empty()) {
    return fail("a_done.loci vacío");
  }
  int primary = 0;
  for (const auto& loc : loci) {
    if (!a_anchor_resolvable(loc.anchor)) {
      return fail("a_done: ancla no resoluble `" + loc.anchor +
                  "` (usa path:Symbol del peek; no path:Sym genérico)");
    }
    if (loc.stem.empty()) {
      return fail("a_done: stem vacío tras normalizar `" + loc.anchor + "`");
    }
    if (loc.role == ALocusRole::Primary) {
      ++primary;
    }
  }
  if (primary > kAMaxPrimaryLoci) {
    return fail("a_done: máx " + std::to_string(kAMaxPrimaryLoci) +
                " primary (got " + std::to_string(primary) +
                "). Elige dónde editarías; UI/siblings → secondary/reject.");
  }
  if (primary < 1) {
    return fail("a_done: hace falta ≥1 primary (locus a editar)");
  }

  if (!a_enough_locate_breadth(st)) {
    return fail("a_done prematuro: peeks≥" + std::to_string(kAMinPeeksBeforeDone) + " o stems≥" +
                std::to_string(kAMinStemsBeforeDone) + " (ahora peeks=" +
                std::to_string(st.peeks_used) + " stems=" +
                std::to_string(a_notes_distinct_stems(st)) +
                "). Sigue a_judge; no corones el primer useful.");
  }

  int reject = 0;
  for (const auto& n : st.notes) {
    if (n.verdict == AVerdictKind::Reject) {
      ++reject;
    }
  }
  if (reject < 1 && !a_budget_relaxed(st)) {
    return fail(
        "a_done: sin contraste — marca ≥1 reject de competidor (keyword/UI ≠ locus del "
        "bug) antes de cerrar. Duda → reject/uncertain, no useful.");
  }

  // Cross-module contrast: require ≥2 path families among judged notes when available.
  const int fams_notes = a_notes_path_families(st);
  const int fams_avail = a_queue_path_families(st);
  const int fams_need = std::min(kAMinPathFamiliesBeforeDone, std::max(1, fams_avail));
  if (fams_notes < fams_need && !a_budget_relaxed(st)) {
    return fail("a_done: contraste cross-módulo insuficiente (familias juzgadas=" +
                std::to_string(fams_notes) + ", hace falta ≥" + std::to_string(fams_need) +
                "). Juzga peeks de otro dir (ui/ vs ai/ vs lsp/…) antes de cerrar.");
  }
  return true;
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
    if (line.find("why:") != std::string::npos && !out.empty()) {
      parse_map_why_line(line, &out.back());
      continue;
    }
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
    const auto score_tag = line.find("[score=");
    if (score_tag != std::string::npos) {
      in.score = std::atoi(line.c_str() + static_cast<int>(score_tag + 7));
    }
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
    if (in.stem.empty()) {
      in.stem = path_file_stem(in.file);
    }
    in.functionish = true;
    out.push_back(std::move(in));
    if (out.size() >= max_n) {
      break;
    }
  }
  return out;
}

std::vector<AQueueBuildInput> a_queue_inputs_from_ranked_map_filtered(
    const std::string& map_md, const AQueueMapFilterOpts& opts,
    const std::string& workspace_root) {
  const auto all = a_queue_inputs_from_ranked_map_markdown(map_md, 512);
  std::unordered_set<std::string> seen;
  int generic_cancel = 0;
  auto matches_orphan = [&](const AQueueBuildInput& in) -> bool {
    if (opts.orphans.empty()) {
      return false;
    }
    std::string hay = in.name + " " + in.stem + " " + in.map_related + " " + in.file;
    for (char& c : hay) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto& o : opts.orphans) {
      if (o.size() < 3) {
        continue;
      }
      std::string ol = o;
      for (char& c : ol) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (hay.find(ol) != std::string::npos) {
        return true;
      }
    }
    return false;
  };
  auto accept = [&](const AQueueBuildInput& in) -> bool {
    if (map_input_skip_header_twin(in, workspace_root, opts.skip_header_with_cpp)) {
      return false;
    }
    if (opts.skip_file_level && is_file_level_map_entry(in)) {
      return false;
    }
    if (opts.skip_examples && in.file.compare(0, 9, "examples/") == 0) {
      return false;
    }
    if (is_generic_cancel_name(in.name)) {
      if (generic_cancel >= opts.max_generic_cancel) {
        return false;
      }
      ++generic_cancel;
    }
    return true;
  };
  std::vector<AQueueBuildInput> orphan_pool;
  std::vector<AQueueBuildInput> src_first;
  std::vector<AQueueBuildInput> rest;
  for (const auto& in : all) {
    if (!accept(in)) {
      continue;
    }
    if (matches_orphan(in)) {
      orphan_pool.push_back(in);
    }
    if (opts.deprioritize_tests &&
        (in.file.compare(0, 6, "tests/") == 0 || in.file.find("/tests/") != std::string::npos)) {
      rest.push_back(in);
      continue;
    }
    src_first.push_back(in);
  }
  std::unordered_map<std::string, int> stem_count;
  std::vector<AQueueBuildInput> out;
  out.reserve(opts.want_n);
  auto try_take = [&](const AQueueBuildInput& in, bool force_orphan) {
    if (out.size() >= opts.want_n) {
      return;
    }
    std::string stem = in.stem;
    if (stem.empty()) {
      const auto slash = in.file.find_last_of('/');
      stem = slash == std::string::npos ? in.file : in.file.substr(slash + 1);
      const auto dot = stem.rfind('.');
      if (dot != std::string::npos) {
        stem = stem.substr(0, dot);
      }
    }
    const bool orphan = matches_orphan(in);
    if (!force_orphan && !orphan && opts.max_per_stem > 0 &&
        stem_count[stem] >= opts.max_per_stem) {
      return;
    }
    if (!in.name.empty()) {
      const std::string key = in.file + ":" + in.name;
      if (!seen.insert(key).second) {
        return;
      }
    }
    out.push_back(in);
    ++stem_count[stem];
  };
  int orphan_added = 0;
  for (const auto& in : orphan_pool) {
    if (orphan_added >= opts.orphan_rescue_slots) {
      break;
    }
    const std::size_t before = out.size();
    try_take(in, true);
    if (out.size() > before) {
      ++orphan_added;
    }
  }
  for (const auto& in : src_first) {
    try_take(in, false);
  }
  for (const auto& in : rest) {
    try_take(in, false);
  }
  return out;
}

std::vector<AQueueItem> a_order_a0_tranche(const std::vector<AQueueItem>& slice, const AState& st,
                                           int max_n) {
  if (max_n <= 0 || slice.empty()) {
    return {};
  }
  auto needle_hit = [](const AQueueItem& it, const std::string& needle) -> bool {
    if (needle.size() < 3) {
      return false;
    }
    std::string hay = it.path + " " + it.stem + " " + it.symbol + " " + it.target + " " +
                      it.map_related;
    for (char& c : hay) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string n = needle;
    for (char& c : n) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return hay.find(n) != std::string::npos;
  };
  auto rerank_score = [&](const AQueueItem& item) -> int {
    int s = static_cast<int>(item.score);
    if (item.body_sem_permille > 0) {
      s += item.body_sem_permille / 2;
    }
    if (item.dup_stem) {
      s -= 80000;
    }
    if (item.file_rank > 1) {
      s -= 25000 * (item.file_rank - 1);
    }
    for (const auto& seed : st.seeds) {
      if (needle_hit(item, seed)) {
        s += 120000;
        break;
      }
    }
    for (const auto& o : st.orphans) {
      if (needle_hit(item, o)) {
        s += 160000;
        break;
      }
    }
    const std::string fam = a_path_family(item.path);
    if (fam == "ai" || fam == "ui") {
      s += 50000;
    } else if (fam == "lsp" || fam == "search") {
      s -= 35000;
    }
    return s;
  };
  struct Row {
    AQueueItem item;
    int score = 0;
  };
  std::vector<Row> rows;
  rows.reserve(slice.size());
  for (const auto& item : slice) {
    rows.push_back({item, rerank_score(item)});
  }
  std::stable_sort(rows.begin(), rows.end(),
                   [](const Row& a, const Row& b) { return a.score > b.score; });
  std::vector<AQueueItem> out;
  out.reserve(static_cast<std::size_t>(max_n));
  std::unordered_map<std::string, int> stem_n;
  for (const auto& row : rows) {
    if (static_cast<int>(out.size()) >= max_n) {
      break;
    }
    bool orphan_hit = false;
    for (const auto& o : st.orphans) {
      if (needle_hit(row.item, o)) {
        orphan_hit = true;
        break;
      }
    }
    if (!orphan_hit && stem_n[row.item.stem] >= 2) {
      continue;
    }
    out.push_back(row.item);
    ++stem_n[row.item.stem];
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
    case AVerdictKind::Interesting:
      return "interesting";
    case AVerdictKind::Expand:
      return "expand";
    case AVerdictKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

const char* a_expand_modality_name(AExpandModality m) {
  switch (m) {
    case AExpandModality::Peek:
      return "peek";
    case AExpandModality::Trail:
      return "trail";
    case AExpandModality::Dataflow:
      return "dataflow";
    default:
      return "";
  }
}

AExpandModality parse_a_expand_modality(const std::string& s) {
  if (s == "peek" || s == "body" || s == "code") {
    return AExpandModality::Peek;
  }
  if (s == "trail" || s == "call" || s == "hierarchy") {
    return AExpandModality::Trail;
  }
  if (s == "dataflow" || s == "df" || s == "var") {
    return AExpandModality::Dataflow;
  }
  return AExpandModality::None;
}

std::string a_target_symbol_name(const std::string& target) {
  std::string sym = target;
  const auto hash = sym.find('#');
  if (hash != std::string::npos) {
    sym = sym.substr(0, hash);
  }
  const auto colon = sym.rfind(':');
  if (colon != std::string::npos) {
    sym = sym.substr(colon + 1);
  }
  return sym;
}

bool a_is_symptom_edge_name(const std::string& name) {
  static const char* kPrefixes[] = {"set_",    "clear_", "begin_",  "end_",   "start_",
                                    "stop_",   "enable_", "disable_", "reset_", "cancel_"};
  for (const char* prefix : kPrefixes) {
    if (name.rfind(prefix, 0) == 0 && name.size() > std::strlen(prefix)) {
      return true;
    }
  }
  return false;
}

bool a_writes_suggest_trail_a0(const std::vector<std::string>& writes) {
  for (const std::string& w : writes) {
    if (w.rfind("state.", 0) == 0) {
      return true;
    }
  }
  return false;
}

bool a_target_prefers_trail_a0(const std::string& target,
                               const std::vector<std::string>* writes) {
  if (a_is_symptom_edge_name(a_target_symbol_name(target))) {
    return true;
  }
  std::string path = target;
  const auto hash = path.find('#');
  if (hash != std::string::npos) {
    path = path.substr(0, hash);
  }
  const auto colon = path.rfind(':');
  if (colon != std::string::npos) {
    path = path.substr(0, colon);
  }
  if (writes != nullptr && a_writes_suggest_trail_a0(*writes)) {
    return true;
  }
  return false;
}

AExpandModality a_coerce_a0_expand_modality(const std::string& target, AExpandModality m,
                                            const std::vector<std::string>* writes) {
  if (!a_target_prefers_trail_a0(target, writes)) {
    return m;
  }
  if (m == AExpandModality::Dataflow || m == AExpandModality::Peek ||
      m == AExpandModality::None) {
    return AExpandModality::Trail;
  }
  return m;
}

namespace {

bool f1_term_matches(const std::string& hay, const std::string& needle) {
  if (hay.empty() || needle.empty()) {
    return false;
  }
  std::string h = hay;
  std::string n = needle;
  for (char& c : h) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  for (char& c : n) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (n.size() < 2) {
    return false;
  }
  return h.find(n) != std::string::npos;
}

}  // namespace

float a_f1_anchor_match_score(const AQueueItem& item, const ProblemFrame& pf) {
  float score = item.score;
  const auto seeds = problem_frame_anchor_seeds(pf);
  for (const auto& seed : seeds) {
    if (f1_term_matches(item.symbol, seed) || f1_term_matches(item.path, seed) ||
        f1_term_matches(item.stem, seed) || f1_term_matches(item.target, seed)) {
      score += 2.5f;
    }
  }
  if (a_is_symptom_edge_name(item.symbol)) {
    score += 1.5f;
  }
  for (const auto& hint : pf.primary_anchor.edge_hints) {
    if (!hint.empty() && item.symbol.rfind(hint, 0) == 0) {
      score += 2.f;
    }
  }
  for (const auto& noise : pf.reject_noise) {
    if (f1_term_matches(item.stem, noise) && !f1_term_matches(item.symbol, noise)) {
      score -= 1.f;
    }
  }
  return score;
}

void a_apply_f1_anchor_queue_filter(AState* st, const ProblemFrame& pf) {
  if (st == nullptr) {
    return;
  }
  auto rerank = [&](std::vector<AQueueItem>* items) {
    if (items == nullptr || items->empty()) {
      return;
    }
    for (auto& it : *items) {
      it.score = a_f1_anchor_match_score(it, pf);
    }
    std::stable_sort(items->begin(), items->end(),
                     [](const AQueueItem& a, const AQueueItem& b) { return a.score > b.score; });
  };
  rerank(&st->queue);
  rerank(&st->reserve);
  st->seeds = problem_frame_anchor_seeds(pf);
}

AExpandModality a_f1_coerce_expand_modality(AExpandModality m) {
  if (m == AExpandModality::Trail || m == AExpandModality::Dataflow) {
    return AExpandModality::Peek;
  }
  if (m == AExpandModality::None) {
    return AExpandModality::Peek;
  }
  return m;
}

bool a_validate_f1_anchor_done(const AState& st, const std::vector<ALocus>& loci, std::string* err) {
  auto fail = [&](const std::string& msg) {
    if (err) {
      *err = msg;
    }
    return false;
  };
  if (!a_in_f1_anchor_mode(st)) {
    return a_validate_a_done(st, loci, err);
  }
  if (loci.empty()) {
    return fail("f1_done.loci vacío");
  }
  int primary = 0;
  for (const auto& loc : loci) {
    if (!a_anchor_resolvable(loc.anchor)) {
      return fail("f1_done: ancla no resoluble `" + loc.anchor + "`");
    }
    if (loc.stem.empty()) {
      return fail("f1_done: stem vacío");
    }
    if (loc.role == ALocusRole::Primary) {
      ++primary;
    }
  }
  if (primary != 1) {
    return fail("f1_done: exactamente 1 primary (got " + std::to_string(primary) + ")");
  }
  if (st.peeks_used < 1 && st.cards_used < 1) {
    return fail("f1_done: falta confirmación (≥1 peek o card juzgada)");
  }
  bool has_useful = false;
  for (const auto& n : st.notes) {
    if (n.verdict == AVerdictKind::Useful || n.verdict == AVerdictKind::Expand) {
      has_useful = true;
      break;
    }
  }
  if (!has_useful && st.anchor_confirmed.empty()) {
    return fail("f1_done: sin useful/expand en notes ni anchor_confirmed");
  }
  int rejects = 0;
  for (const auto& n : st.notes) {
    if (n.verdict == AVerdictKind::Reject) {
      ++rejects;
    }
  }
  if (rejects < 1 && static_cast<int>(st.notes.size()) >= 3 && !a_in_f1_anchor_mode(st)) {
    return fail("f1_done: marca ≥1 reject en competidores antes de cerrar");
  }
  return true;
}

std::string a_path_from_anchor(const std::string& anchor) {
  std::string p = anchor;
  const auto hash = p.find('#');
  if (hash != std::string::npos) {
    p = p.substr(0, hash);
  }
  const auto colon = p.rfind(':');
  if (colon != std::string::npos) {
    p = p.substr(0, colon);
  }
  return p;
}

bool a_a0_dataflow_allowed_without_trail(const std::string& target,
                                         const std::string& suspect_var) {
  if (suspect_var.empty()) {
    return false;
  }
  if (a_target_prefers_trail_a0(target, nullptr)) {
    return false;
  }
  if (suspect_var.rfind("strip.", 0) == 0 || suspect_var.rfind("state.", 0) == 0) {
    return false;
  }
  const std::string lower = [&]() {
    std::string s = suspect_var;
    for (char& c : s) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
  }();
  if (lower.find("cancel") != std::string::npos) {
    return false;
  }
  if (lower == "main" || lower.find("::") != std::string::npos) {
    return false;
  }
  return suspect_var.size() >= 3 &&
         (suspect_var.back() == '_' || lower.find("busy") != std::string::npos ||
          lower.find("active") != std::string::npos || lower.find("pending") != std::string::npos);
}

void a_sort_a1_queue(std::vector<AExpansionItem>* queue) {
  if (queue == nullptr || queue->size() < 2) {
    return;
  }
  auto modality_rank = [](AExpandModality m) {
    switch (m) {
      case AExpandModality::Trail:
        return 0;
      case AExpandModality::Peek:
        return 1;
      case AExpandModality::Dataflow:
        return 2;
      default:
        return 3;
    }
  };
  std::stable_sort(queue->begin(), queue->end(),
                   [&](const AExpansionItem& a, const AExpansionItem& b) {
                     return modality_rank(a.modality) < modality_rank(b.modality);
                   });
}

void a_a1_clear_trail_frame(AState* st) {
  if (st == nullptr) {
    return;
  }
  st->a1_trail_recap.clear();
  st->a1_df_scope_path.clear();
  st->a1_df_caller_anchor.clear();
  st->a1_suspect_context.clear();
  st->a1_job_root.clear();
  st->a1_suspect_done = false;
}

void a_a1_begin_job(AState* st, const AExpansionItem& item) {
  if (st == nullptr) {
    return;
  }
  // Dataflow after a successful trail must keep recap/job_root so reject can backtrack.
  const bool keep_trail_frame = item.modality == AExpandModality::Dataflow && st->trail.active &&
                                (!st->a1_trail_recap.empty() || !st->a1_job_root.empty());
  std::string saved_recap;
  std::string saved_scope;
  std::string saved_caller;
  std::string saved_ctx;
  std::string saved_root;
  bool saved_suspect_done = false;
  if (keep_trail_frame) {
    saved_recap = st->a1_trail_recap;
    saved_scope = st->a1_df_scope_path;
    saved_caller = st->a1_df_caller_anchor;
    saved_ctx = st->a1_suspect_context;
    saved_root = st->a1_job_root;
    saved_suspect_done = st->a1_suspect_done;
  }
  a_a1_clear_trail_frame(st);
  if (keep_trail_frame) {
    st->a1_trail_recap = std::move(saved_recap);
    st->a1_df_scope_path = std::move(saved_scope);
    st->a1_df_caller_anchor = std::move(saved_caller);
    st->a1_suspect_context = std::move(saved_ctx);
    st->a1_job_root = saved_root.empty() ? item.target : std::move(saved_root);
    st->a1_suspect_done = saved_suspect_done;
  } else {
    st->a1_job_root = item.target;
  }
  st->a1_active = item;
  st->a1_active_set = true;
  switch (item.modality) {
    case AExpandModality::Trail:
      st->a_subphase = "a1_trail";
      break;
    case AExpandModality::Dataflow:
      st->a_subphase = "a1_dataflow";
      break;
    default:
      st->a_subphase = "a1_peek";
      break;
  }
}

void a_a1_backtrack_to_trail(AState* st) {
  if (st == nullptr || !st->trail.active) {
    return;
  }
  if (!st->a1_job_root.empty()) {
    st->a1_active.target = st->a1_job_root;
  }
  st->a1_active.modality = AExpandModality::Trail;
  st->a1_active.suspect_var.clear();
  st->a1_active_set = true;
  st->a_subphase = "a1_trail";
  st->a1_suspect_done = false;
  st->trail.awaiting_judge = true;
}

std::string a_a1_dataflow_path_hint(const AState& st, const AExpansionItem& item) {
  if (!st.a1_df_scope_path.empty()) {
    return st.a1_df_scope_path;
  }
  return a_path_from_anchor(item.target);
}

bool a_in_a0_sniff(const AState& st) {
  if (!a_effect_summary_enabled()) {
    return false;
  }
  if (st.a1_active_set) {
    return false;
  }
  if (!st.a1_queue.empty()) {
    return false;
  }
  return st.a_subphase.empty() || st.a_subphase == "a0_sniff";
}

float a_queue_item_score(const AState& st, const std::string& target) {
  auto base = [](std::string t) {
    a_strip_window(&t, nullptr);
    return t;
  };
  const std::string want = base(target);
  if (want.empty()) {
    return 0.f;
  }
  auto matches = [&](const std::string& qt) {
    return qt == target || base(qt) == want;
  };
  for (const auto& q : st.queue) {
    if (matches(q.target)) {
      return q.score;
    }
  }
  for (const auto& q : st.reserve) {
    if (matches(q.target)) {
      return q.score * 0.9f;
    }
  }
  return 0.f;
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
  if (s == "interesting" || s == "follow" || s == "deepen") {
    return AVerdictKind::Interesting;
  }
  if (s == "expand" || s == "EXPAND") {
    return AVerdictKind::Expand;
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
  const std::string ew = j.value("expand_with", j.value("modality", ""));
  v.expand_with = parse_a_expand_modality(ew);
  v.suspect_var = trim_copy(j.value("suspect_var", j.value("var", "")));
  if (v.verdict == AVerdictKind::Useful && a_effect_summary_enabled()) {
    // A0 must not crown useful from cards alone; caller may still downgrade.
  }
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
  const std::string phase = j.value("phase", "");
  const bool a0 = phase == "a0_sniff" || phase == "a0";
  // Flat a_judge: {"action":"a_judge","target":"…","verdict":"useful"} (7B omits array).
  if (!j.contains("verdicts")) {
    AVerdict flat = parse_a_verdict_json(j);
    if (flat.verdict != AVerdictKind::Unknown &&
        (!flat.target.empty() || !flat.anchor.empty())) {
      if (a0 && flat.verdict == AVerdictKind::Useful) {
        flat.verdict = AVerdictKind::Expand;
        if (flat.expand_with == AExpandModality::None) {
          flat.expand_with = AExpandModality::Peek;
        }
      }
      out->push_back(std::move(flat));
      return true;
    }
    // Bare {"action":"a_judge"} / phase-only — empty OK; apply decides by subphase.
    return true;
  }
  if (!j["verdicts"].is_array()) {
    if (err) {
      *err = "a_judge sin array verdicts";
    }
    return false;
  }
  const int cap = a0 ? kA0MaxCardsPerTurn + 2 : kAMaxPeeksPerTurn + 2;
  for (const auto& item : j["verdicts"]) {
    AVerdict v = parse_a_verdict_json(item);
    if (v.target.empty() && v.anchor.empty()) {
      continue;
    }
    if (v.verdict == AVerdictKind::Unknown) {
      continue;
    }
    if (a0 && v.verdict == AVerdictKind::Useful) {
      v.verdict = AVerdictKind::Expand;
      if (v.expand_with == AExpandModality::None) {
        v.expand_with = AExpandModality::Peek;
      }
    }
    if (a0 && v.verdict == AVerdictKind::Expand && v.expand_with == AExpandModality::None) {
      v.expand_with = AExpandModality::Peek;
    }
    out->push_back(std::move(v));
    if (static_cast<int>(out->size()) >= cap) {
      break;
    }
  }
  if (out->empty()) {
    // Empty array is OK at parse time; apply_a_judge decides by subphase
    // (a1_suspect_vars allows []; A0/classic reject).
    return true;
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
                     {"score", it.score},
                     {"map_related", it.map_related},
                     {"refs_in", it.refs_in},
                     {"body_sem_permille", it.body_sem_permille},
                     {"file_rank", it.file_rank},
                     {"file_count", it.file_count},
                     {"dup_stem", it.dup_stem},
                     {"stem_sem_rank", it.stem_sem_rank}});
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
  j["trail"] = a_trail_to_json(st.trail);
  j["a_subphase"] = st.a_subphase;
  j["cards_used"] = st.cards_used;
  j["a0_turns"] = st.a0_turns;
  if (!st.a0_shown_targets.empty()) {
    j["a0_shown_targets"] = st.a0_shown_targets;
  }
  j["seeds"] = st.seeds;
  j["a1_active_set"] = st.a1_active_set;
  j["a1_suspect_done"] = st.a1_suspect_done;
  if (!st.a1_suspect_context.empty()) {
    j["a1_suspect_context"] = st.a1_suspect_context;
  }
  if (!st.a1_job_root.empty()) {
    j["a1_job_root"] = st.a1_job_root;
  }
  if (!st.a1_trail_recap.empty()) {
    j["a1_trail_recap"] = st.a1_trail_recap;
  }
  if (!st.a1_df_scope_path.empty()) {
    j["a1_df_scope_path"] = st.a1_df_scope_path;
  }
  if (!st.a1_df_caller_anchor.empty()) {
    j["a1_df_caller_anchor"] = st.a1_df_caller_anchor;
  }
  if (!st.explore_mode.empty()) {
    j["explore_mode"] = st.explore_mode;
  }
  if (!st.anchor_confirmed.empty()) {
    j["anchor_confirmed"] = st.anchor_confirmed;
  }
  if (!st.anchor_understanding.empty()) {
    j["anchor_understanding"] = st.anchor_understanding;
  }
  if (!st.f1_failure_reason.empty()) {
    j["f1_failure_reason"] = st.f1_failure_reason;
  }
  if (st.a1_active_set) {
    j["a1_active"] = {{"target", st.a1_active.target},
                      {"modality", a_expand_modality_name(st.a1_active.modality)},
                      {"suspect_var", st.a1_active.suspect_var},
                      {"score", st.a1_active.score}};
  }
  nlohmann::json q = nlohmann::json::array();
  for (const auto& it : st.a1_queue) {
    q.push_back({{"target", it.target},
                 {"modality", a_expand_modality_name(it.modality)},
                 {"suspect_var", it.suspect_var},
                 {"score", it.score}});
  }
  j["a1_queue"] = std::move(q);
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
  read_str_array("seeds", &st.seeds);
  st.a_subphase = j.value("a_subphase", "");
  st.cards_used = j.value("cards_used", 0);
  st.a0_turns = j.value("a0_turns", 0);
  read_str_array("a0_shown_targets", &st.a0_shown_targets);
  st.a1_active_set = j.value("a1_active_set", false);
  st.a1_suspect_done = j.value("a1_suspect_done", false);
  st.a1_suspect_context = j.value("a1_suspect_context", "");
  st.a1_job_root = j.value("a1_job_root", "");
  st.a1_trail_recap = j.value("a1_trail_recap", "");
  st.a1_df_scope_path = j.value("a1_df_scope_path", "");
  st.a1_df_caller_anchor = j.value("a1_df_caller_anchor", "");
  st.explore_mode = j.value("explore_mode", "");
  st.anchor_confirmed = j.value("anchor_confirmed", "");
  st.anchor_understanding = j.value("anchor_understanding", "");
  st.f1_failure_reason = j.value("f1_failure_reason", "");
  if (j.contains("a1_active") && j["a1_active"].is_object()) {
    st.a1_active.target = j["a1_active"].value("target", "");
    st.a1_active.modality = parse_a_expand_modality(j["a1_active"].value("modality", "peek"));
    st.a1_active.suspect_var = j["a1_active"].value("suspect_var", "");
    st.a1_active.score = j["a1_active"].value("score", 0.f);
  }
  if (j.contains("a1_queue") && j["a1_queue"].is_array()) {
    for (const auto& o : j["a1_queue"]) {
      if (!o.is_object()) {
        continue;
      }
      AExpansionItem it;
      it.target = o.value("target", "");
      it.modality = parse_a_expand_modality(o.value("modality", "peek"));
      it.suspect_var = o.value("suspect_var", "");
      it.score = o.value("score", 0.f);
      if (!it.target.empty()) {
        st.a1_queue.push_back(std::move(it));
      }
    }
  }
  if (j.contains("trail") && j["trail"].is_object()) {
    a_trail_from_json(j["trail"], &st.trail);
  }

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
      q.map_related = it.value("map_related", "");
      q.refs_in = it.value("refs_in", 0);
      q.body_sem_permille = it.value("body_sem_permille", 0);
      q.file_rank = it.value("file_rank", 0);
      q.file_count = it.value("file_count", 0);
      q.dup_stem = it.value("dup_stem", false);
      q.stem_sem_rank = it.value("stem_sem_rank", 0);
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

bool a_apply_a0_verdicts(AState* st, const std::vector<AVerdict>& verdicts, std::string* err,
                         const std::string* workspace_root) {
  if (st == nullptr || verdicts.empty()) {
    if (err) {
      *err = "a0 verdicts vacío";
    }
    return false;
  }
  if (workspace_root != nullptr && !workspace_root->empty() && a_in_a0_sniff(*st)) {
    std::vector<std::string> expected_targets = st->a0_shown_targets;
    if (expected_targets.empty()) {
      const A0TrancheShown shown =
          a_build_a0_tranche_shown(*workspace_root, *st, kA0MaxCardsPerTurn);
      for (const auto& item : shown.items) {
        expected_targets.push_back(item.target);
      }
    }
    if (!expected_targets.empty()) {
      int covered = 0;
      for (const auto& target : expected_targets) {
        bool hit = false;
        for (const auto& v : verdicts) {
          if (a_target_matches_verdict_anchor(target, v.target)) {
            hit = true;
            break;
          }
        }
        if (hit) {
          ++covered;
        }
      }
      if (covered < static_cast<int>(expected_targets.size())) {
        if (err) {
          *err = "a0: faltan veredictos (" + std::to_string(covered) + "/" +
                 std::to_string(expected_targets.size()) +
                 ") — emite expand|reject|uncertain por cada card mostrada";
        }
        return false;
      }
    }
  }
  int expands = 0;
  int rejects = 0;
  int queued_expands = 0;
  for (const auto& raw : verdicts) {
    AVerdict v = raw;
    a_normalize_verdict(&v);
    if (v.verdict == AVerdictKind::Useful) {
      v.verdict = AVerdictKind::Expand;
      if (v.expand_with == AExpandModality::None) {
        v.expand_with = AExpandModality::Peek;
      }
    }
    if (v.verdict == AVerdictKind::Reject) {
      const float sc = a_queue_item_score(*st, v.target);
      int rank = 0;
      for (int qi = 0; qi < static_cast<int>(st->queue.size()); ++qi) {
        if (st->queue[static_cast<std::size_t>(qi)].target == v.target) {
          rank = qi + 1;
          break;
        }
      }
      if (sc >= 0.5f || (rank > 0 && rank <= 15)) {
        v.verdict = AVerdictKind::Uncertain;
        v.expand_with = AExpandModality::Peek;
      }
      ++rejects;
    } else if (v.verdict == AVerdictKind::Expand) {
      ++expands;
    }
    if (v.verdict == AVerdictKind::Expand || v.verdict == AVerdictKind::Uncertain) {
      if (v.expand_with == AExpandModality::None) {
        v.expand_with = AExpandModality::Peek;
      }
      v.expand_with = a_coerce_a0_expand_modality(v.target, v.expand_with, nullptr);
      if (st != nullptr && a_in_f1_anchor_mode(*st)) {
        v.expand_with = a_f1_coerce_expand_modality(v.expand_with);
      }
      if (v.expand_with == AExpandModality::Dataflow &&
          !a_a0_dataflow_allowed_without_trail(v.target, v.suspect_var)) {
        v.expand_with = AExpandModality::Trail;
        v.suspect_var.clear();
      }
      if (v.verdict == AVerdictKind::Expand) {
        if (static_cast<int>(st->a1_queue.size()) >= kA0MaxExpandTotal) {
          v.verdict = AVerdictKind::Uncertain;
        } else {
          AExpansionItem item;
          item.target = v.target;
          item.modality = v.expand_with;
          item.suspect_var = v.suspect_var;
          item.score = a_queue_item_score(*st, v.target);
          st->a1_queue.push_back(std::move(item));
          ++queued_expands;
        }
      }
    }
    if (!v.why.empty() && v.why.size() > 80) {
      v.why = v.why.substr(0, 79) + "…";
    }
    st->notes.push_back(v);
    if (v.verdict == AVerdictKind::Reject && !v.stem.empty()) {
      if (std::find(st->rejected_stems.begin(), st->rejected_stems.end(), v.stem) ==
          st->rejected_stems.end()) {
        st->rejected_stems.push_back(v.stem);
      }
    }
  }
  if (expands == 0 && rejects == 0) {
    if (err) {
      *err = "a0: todos uncertain — marca reject en glue o expand si hot/writes cuadra";
    }
    return false;
  }
  a_sort_a1_queue(&st->a1_queue);
  // Consume the rerank window (not only shown cards) so rescued-from-window items
  // are not re-offered on the next A0 turn.
  const int remain = std::max(0, static_cast<int>(st->queue.size()) - st->cursor);
  const int window =
      std::min({kA0RerankWindow, remain, std::max(kA0MaxCardsPerTurn, kA0MaxCardsPerTurn * 3)});
  st->cards_used += std::max(static_cast<int>(verdicts.size()), kA0MaxCardsPerTurn);
  st->cursor = std::min(static_cast<int>(st->queue.size()), st->cursor + std::max(window, 1));
  ++st->a0_turns;
  ++st->turns;
  st->a_subphase = "a0_sniff";
  if (!st->a1_queue.empty() && !st->a1_active_set) {
    a_a1_begin_job(st, st->a1_queue.front());
    st->a1_queue.erase(st->a1_queue.begin());
  }
  st->a0_shown_targets.clear();
  return true;
}

void a_fill_a1_trail_frame(AState* st, const std::vector<AVerdict>& verdicts) {
  if (st == nullptr) {
    return;
  }
  st->a1_trail_recap = a_build_a1_suspect_context(*st, verdicts);
  st->a1_suspect_context = st->a1_trail_recap;
  st->a1_df_scope_path.clear();
  st->a1_df_caller_anchor.clear();
  auto take_branch = [&](const ATrailCondBranch& b) {
    st->a1_df_caller_anchor = b.anchor.empty() ? (b.path + ":" + b.symbol) : b.anchor;
    st->a1_df_scope_path = b.path.empty() ? a_path_from_anchor(st->a1_df_caller_anchor) : b.path;
  };
  auto take_stack = [&](const ATrailStack& s) {
    if (s.hops.empty()) {
      return false;
    }
    const ATrailHop& hop = s.hops.front();
    st->a1_df_caller_anchor = hop.anchor;
    st->a1_df_scope_path = hop.path.empty() ? a_path_from_anchor(hop.anchor) : hop.path;
    return true;
  };
  // Prefer already-matched Interesting on the trail (ids the runtime accepted).
  for (const auto& b : st->trail.cond_branches) {
    if (b.verdict == AVerdictKind::Interesting) {
      take_branch(b);
      return;
    }
  }
  for (const auto& s : st->trail.pending_stacks) {
    if (s.verdict == AVerdictKind::Interesting && take_stack(s)) {
      return;
    }
  }
  for (const auto& v : verdicts) {
    if (v.verdict != AVerdictKind::Interesting) {
      continue;
    }
    for (const auto& b : st->trail.cond_branches) {
      if (b.id != v.target) {
        continue;
      }
      take_branch(b);
      return;
    }
    for (const auto& s : st->trail.pending_stacks) {
      if (s.id != v.target) {
        continue;
      }
      if (take_stack(s)) {
        return;
      }
    }
  }
}

std::string a_build_a1_suspect_context(const AState& st, const std::vector<AVerdict>& verdicts) {
  std::ostringstream out;
  for (const auto& v : verdicts) {
    if (v.verdict != AVerdictKind::Interesting) {
      continue;
    }
    bool wrote = false;
    for (const auto& b : st.trail.cond_branches) {
      if (b.id != v.target) {
        continue;
      }
      out << "### cond `" << b.id << "`\n";
      if (!b.when_text.empty()) {
        out << "when: `" << b.when_text << "`\n";
      }
      if (!b.then_text.empty()) {
        out << "then: `" << b.then_text << "`\n";
      }
      if (!b.note.empty()) {
        out << "note: " << b.note << "\n";
      }
      if (!b.snippet.empty()) {
        out << "```\n" << b.snippet;
        if (b.snippet.back() != '\n') {
          out << '\n';
        }
        out << "```\n\n";
      }
      wrote = true;
      break;
    }
    for (const auto& s : st.trail.pending_stacks) {
      if (s.id != v.target) {
        continue;
      }
      out << "### stack `" << s.id << "`\n";
      if (!s.hops.empty()) {
        const auto& key = s.hops.front();
        out << "caller `" << key.anchor << "` line=" << key.call_line << "\n";
        if (!key.signature.empty()) {
          out << "sig: `" << key.signature << "`\n";
        }
        if (!key.control_cond.empty()) {
          out << "cond: `" << key.control_cond << "`\n";
        }
        out << "```\n" << key.snippet;
        if (!key.snippet.empty() && key.snippet.back() != '\n') {
          out << '\n';
        }
        out << "```\n\n";
      }
      wrote = true;
      break;
    }
    (void)wrote;
  }
  return out.str();
}

bool a_apply_a1_suspect_verdicts(AState* st, const std::vector<AVerdict>& verdicts,
                                 std::string* err) {
  if (st == nullptr) {
    if (err) {
      *err = "a1_suspect state nulo";
    }
    return false;
  }
  std::string anchor_hint = st->a1_df_caller_anchor;
  if (anchor_hint.empty()) {
    anchor_hint = st->a1_active.target;
  }
  if (anchor_hint.empty() && !st->trail.pending_stacks.empty()) {
    for (const auto& s : st->trail.pending_stacks) {
      if (s.hops.empty()) {
        continue;
      }
      anchor_hint = s.hops.front().anchor;
      break;
    }
  }
  int queued = 0;
  for (const auto& raw : verdicts) {
    AVerdict v = raw;
    a_normalize_verdict(&v);
    if (v.verdict != AVerdictKind::Expand || v.expand_with != AExpandModality::Dataflow ||
        v.suspect_var.empty()) {
      continue;
    }
    if (queued >= 2) {
      break;
    }
    AExpansionItem item;
    item.modality = AExpandModality::Dataflow;
    item.suspect_var = v.suspect_var;
    item.target = v.target.empty() ? anchor_hint : v.target;
    item.score = a_queue_item_score(*st, item.target);
    st->a1_queue.insert(st->a1_queue.begin(), std::move(item));
    ++queued;
  }
  st->a1_suspect_context.clear();
  st->a1_active_set = false;
  st->a1_active = {};
  if (!st->a1_queue.empty()) {
    const AExpansionItem next = st->a1_queue.front();
    st->a1_queue.erase(st->a1_queue.begin());
    a_a1_begin_job(st, next);
  } else if (st->trail.active && !st->a1_trail_recap.empty()) {
    a_a1_backtrack_to_trail(st);
  } else {
    a_a1_clear_trail_frame(st);
    st->a_subphase = "a0_sniff";
  }
  ++st->turns;
  return true;
}

std::string a_notes_markdown_compact(const AState& st, int max_chars) {
  std::ostringstream out;
  out << "# A notes (compact)\n";
  if (!st.orphans.empty()) {
    out << "orphans: ";
    for (std::size_t i = 0; i < st.orphans.size() && i < 6; ++i) {
      if (i) {
        out << ", ";
      }
      out << st.orphans[i];
    }
    out << "\n";
  }
  std::unordered_map<std::string, int> reject_by_stem;
  for (const auto& v : st.notes) {
    if (v.verdict == AVerdictKind::Reject && !v.stem.empty()) {
      ++reject_by_stem[v.stem];
    }
  }
  for (const auto& kv : reject_by_stem) {
    if (kv.second > 1) {
      out << "- reject×" << kv.second << " stem=" << kv.first << "\n";
    }
  }
  for (const auto& v : st.notes) {
    if (v.verdict == AVerdictKind::Reject && reject_by_stem[v.stem] > 1) {
      continue;
    }
    std::string why = v.why;
    if (why.size() > 80) {
      why = why.substr(0, 79) + "…";
    }
    out << "- [" << a_verdict_kind_name(v.verdict) << "] `" << v.target << "`";
    if (!v.stem.empty()) {
      out << " stem=" << v.stem;
    }
    if (!why.empty()) {
      out << " — " << why;
    }
    out << "\n";
  }
  std::string s = out.str();
  if (max_chars > 0 && static_cast<int>(s.size()) > max_chars) {
    s = s.substr(0, static_cast<std::size_t>(max_chars - 1)) + "…";
  }
  return s;
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
  if (st.trail.active) {
    out << "\n## Trail\n";
    out << "root=`" << st.trail.root_anchor << "` focus=`" << st.trail.focus_anchor
        << "` depth=" << st.trail.depth << " awaiting_judge="
        << (st.trail.awaiting_judge ? "1" : "0") << "\n";
    for (const auto& h : st.trail.trail) {
      out << "- `" << h.anchor << "`";
      if (!h.summary.empty()) {
        out << " — " << h.summary;
      }
      out << "\n";
    }
  }
  return out.str();
}

nlohmann::json a_trail_hop_to_json(const ATrailHop& h) {
  return nlohmann::json{{"path", h.path},
                        {"symbol", h.symbol},
                        {"scope_chain", h.scope_chain},
                        {"signature", h.signature},
                        {"control_kind", h.control_kind},
                        {"control_chain", h.control_chain},
                        {"control_cond", h.control_cond},
                        {"call_line", h.call_line},
                        {"snippet", h.snippet},
                        {"summary", h.summary},
                        {"anchor", h.anchor},
                        {"is_call_site", h.is_call_site}};
}

ATrailHop a_trail_hop_from_json(const nlohmann::json& j) {
  ATrailHop h;
  if (!j.is_object()) {
    return h;
  }
  h.path = j.value("path", "");
  h.symbol = j.value("symbol", "");
  h.scope_chain = j.value("scope_chain", "");
  h.signature = j.value("signature", "");
  h.control_kind = j.value("control_kind", "");
  h.control_chain = j.value("control_chain", "");
  h.control_cond = j.value("control_cond", "");
  h.call_line = j.value("call_line", 0);
  h.snippet = j.value("snippet", "");
  h.summary = j.value("summary", "");
  h.anchor = j.value("anchor", "");
  h.is_call_site = j.value("is_call_site", false);
  return h;
}

nlohmann::json a_trail_to_json(const ATrail& tr) {
  nlohmann::json j;
  j["active"] = tr.active;
  j["awaiting_judge"] = tr.awaiting_judge;
  j["root_anchor"] = tr.root_anchor;
  j["root_stem"] = tr.root_stem;
  j["root_why"] = tr.root_why;
  j["focus_anchor"] = tr.focus_anchor;
  j["focus_symbol"] = tr.focus_symbol;
  j["depth"] = tr.depth;
  j["branches_open"] = tr.branches_open;
  j["invalidations"] = tr.invalidations;
  j["force_queue"] = tr.force_queue;
  nlohmann::json trail = nlohmann::json::array();
  for (const auto& h : tr.trail) {
    trail.push_back(a_trail_hop_to_json(h));
  }
  j["trail"] = std::move(trail);
  nlohmann::json stacks = nlohmann::json::array();
  for (const auto& s : tr.pending_stacks) {
    nlohmann::json hops = nlohmann::json::array();
    for (const auto& h : s.hops) {
      hops.push_back(a_trail_hop_to_json(h));
    }
    stacks.push_back({{"id", s.id},
                      {"verdict", a_verdict_kind_name(s.verdict)},
                      {"why", s.why},
                      {"hops", std::move(hops)}});
  }
  j["pending_stacks"] = std::move(stacks);
  nlohmann::json cond = nlohmann::json::array();
  for (const auto& b : tr.cond_branches) {
    cond.push_back({{"id", b.id},
                    {"when", b.when_text},
                    {"then", b.then_text},
                    {"note", b.note},
                    {"anchor", b.anchor},
                    {"path", b.path},
                    {"symbol", b.symbol},
                    {"line", b.line},
                    {"snippet", b.snippet},
                    {"verdict", a_verdict_kind_name(b.verdict)},
                    {"why", b.why}});
  }
  j["cond_branches"] = std::move(cond);
  return j;
}

bool a_trail_from_json(const nlohmann::json& j, ATrail* out) {
  if (out == nullptr || !j.is_object()) {
    return false;
  }
  ATrail tr;
  tr.active = j.value("active", false);
  tr.awaiting_judge = j.value("awaiting_judge", false);
  tr.root_anchor = j.value("root_anchor", "");
  tr.root_stem = j.value("root_stem", "");
  tr.root_why = j.value("root_why", "");
  tr.focus_anchor = j.value("focus_anchor", "");
  tr.focus_symbol = j.value("focus_symbol", "");
  tr.depth = j.value("depth", 0);
  tr.branches_open = j.value("branches_open", 0);
  tr.invalidations = j.value("invalidations", 0);
  if (j.contains("force_queue") && j["force_queue"].is_array()) {
    for (const auto& o : j["force_queue"]) {
      if (o.is_string()) {
        tr.force_queue.push_back(o.get<std::string>());
      }
    }
  }
  if (j.contains("trail") && j["trail"].is_array()) {
    for (const auto& o : j["trail"]) {
      tr.trail.push_back(a_trail_hop_from_json(o));
    }
  }
  if (j.contains("pending_stacks") && j["pending_stacks"].is_array()) {
    for (const auto& o : j["pending_stacks"]) {
      if (!o.is_object()) {
        continue;
      }
      ATrailStack s;
      s.id = o.value("id", "");
      s.verdict = parse_a_verdict_kind(o.value("verdict", ""));
      s.why = o.value("why", "");
      if (o.contains("hops") && o["hops"].is_array()) {
        for (const auto& hj : o["hops"]) {
          s.hops.push_back(a_trail_hop_from_json(hj));
        }
      }
      tr.pending_stacks.push_back(std::move(s));
    }
  }
  if (j.contains("cond_branches") && j["cond_branches"].is_array()) {
    for (const auto& o : j["cond_branches"]) {
      if (!o.is_object()) {
        continue;
      }
      ATrailCondBranch b;
      b.id = o.value("id", "");
      b.when_text = o.value("when", "");
      b.then_text = o.value("then", "");
      b.note = o.value("note", "");
      b.anchor = o.value("anchor", "");
      b.path = o.value("path", "");
      b.symbol = o.value("symbol", "");
      b.line = o.value("line", 0);
      b.snippet = o.value("snippet", "");
      b.verdict = parse_a_verdict_kind(o.value("verdict", ""));
      b.why = o.value("why", "");
      tr.cond_branches.push_back(std::move(b));
    }
  }
  *out = std::move(tr);
  return true;
}

bool a_trail_judge_show_stacks(const ATrail& tr) {
  return !tr.pending_stacks.empty();
}

std::string a_trail_stacks_markdown(const ATrail& tr) {
  std::ostringstream out;
  out << "## Trail (fase A — rama acumulativa)\n";
  out << "L0 `" << tr.root_anchor << "`";
  if (!tr.root_why.empty()) {
    out << " — " << tr.root_why;
  }
  out << " · focus=`" << tr.focus_anchor << "` depth=" << tr.depth << "/" << kATrailMaxDepth
      << "\n\n";

  const bool show_stacks = a_trail_judge_show_stacks(tr);
  const bool show_cond = !tr.cond_branches.empty() && !show_stacks;

  if (show_cond) {
    out << "### Ramas condicionales (ON/CXL/OFF de este L0)\n";
    out << "Juzga estas ids. No hay call-stacks en este turno.\n\n";
    for (const auto& b : tr.cond_branches) {
      out << "#### `" << b.id << "`";
      if (!b.symbol.empty()) {
        out << " · " << b.symbol;
      }
      if (b.verdict != AVerdictKind::Unknown) {
        out << " · " << a_verdict_kind_name(b.verdict);
      }
      out << "\n";
      if (!b.when_text.empty()) {
        out << "- **when** `" << b.when_text << "`\n";
      }
      if (!b.then_text.empty()) {
        out << "- **then** `" << b.then_text << "`\n";
      }
      if (!b.note.empty()) {
        out << "- **note** " << b.note << "\n";
      }
      if (!b.anchor.empty()) {
        out << "- **site** `" << b.anchor << "` line=" << b.line << "\n";
      }
      if (!b.snippet.empty()) {
        out << "```\n" << b.snippet;
        if (b.snippet.back() != '\n') {
          out << '\n';
        }
        out << "```\n";
      }
      out << "\n";
    }
  }

  if (!tr.trail.empty()) {
    out << "### Padres (resumen)\n";
    for (std::size_t i = 0; i < tr.trail.size(); ++i) {
      const auto& h = tr.trail[i];
      out << "- L" << i << " `" << h.anchor << "`";
      if (!h.scope_chain.empty()) {
        out << " [" << h.scope_chain << "]";
      }
      if (!h.control_chain.empty()) {
        out << " ctrl=" << h.control_chain;
      } else if (!h.control_kind.empty()) {
        out << " ctrl=" << h.control_kind;
      }
      if (!h.summary.empty()) {
        out << " — " << h.summary;
      }
      out << "\n";
    }
    out << "\n";
  }
  if (tr.pending_stacks.empty()) {
    if (!show_cond) {
      out << "_(sin stacks — sin callers indexados; no falsear L0; vuelve a cola o sibling)_\n";
    } else {
      out << "Responde `a_trail_judge` solo sobre ON|CXL|OFF|LINK de este turno "
             "(interesting|reject). PROHIBIDO S*.\n";
    }
    return out.str();
  }

  // Compact chains first (7B-friendly) — supporting evidence
  out << "### Call-stacks (elige ≤" << kATrailMaxInterestingPerLevel << " interesting)\n";
  out << "★ = caller distinto del L0. ctrl = if/switch/…; cond = condición.\n";
  for (const auto& s : tr.pending_stacks) {
    const bool distinct =
        s.hops.size() >= 2 && !s.hops.front().symbol.empty() &&
        s.hops.front().symbol != tr.focus_symbol && s.hops.front().symbol != tr.root_stem;
    out << "- " << (distinct ? "★ " : "") << "`" << s.id << "`: ";
    for (std::size_t i = 0; i < s.hops.size(); ++i) {
      if (i) {
        out << " → ";
      }
      const auto& h = s.hops[i];
      if (!h.scope_chain.empty() && h.scope_chain.find("::") != std::string::npos) {
        out << h.scope_chain;
      } else {
        out << (h.symbol.empty() ? "?" : h.symbol);
      }
      if (!h.control_cond.empty() && i + 1 < s.hops.size()) {
        // Only annotate non-L0 hops with condition (short)
        const auto cut = h.control_cond.find(" · ");
        const std::string one =
            cut == std::string::npos ? h.control_cond : h.control_cond.substr(0, cut);
        out << "{" << one << "}";
      } else if (!h.control_chain.empty() && i + 1 < s.hops.size()) {
        out << "@{" << h.control_chain << "}";
      }
    }
    out << "\n";
  }
  out << "\nSi **todos** reject → L0 se invalida. Runtime profundiza los interesting.\n\n";

  // Detail: key caller + optional mid hop + L0
  for (const auto& s : tr.pending_stacks) {
    out << "### `" << s.id << "`\n";
    if (s.hops.empty()) {
      continue;
    }
    const ATrailHop& key = s.hops.front();
    out << "**caller** `" << key.anchor << "` line=" << key.call_line;
    if (key.is_call_site) {
      out << " ✓call";
    }
    out << "\n";
    if (!key.signature.empty()) {
      out << "sig: `" << key.signature << "`\n";
    }
    if (!key.scope_chain.empty()) {
      out << "scopes: " << key.scope_chain << "\n";
    }
    if (!key.control_cond.empty()) {
      out << "cond: `" << key.control_cond << "`\n";
    } else if (!key.control_chain.empty()) {
      out << "controls (call→fn): `" << key.control_chain << "`\n";
    } else if (!key.control_kind.empty()) {
      out << "control: `" << key.control_kind << "`\n";
    }
    out << "```\n" << key.snippet;
    if (!key.snippet.empty() && key.snippet.back() != '\n') {
      out << '\n';
    }
    out << "```\n";

    // Intermediate hops (entry … → caller): one-line each, no big snippet
    if (s.hops.size() >= 3) {
      bool any_mid = false;
      for (std::size_t i = 1; i + 1 < s.hops.size(); ++i) {
        const auto& mid = s.hops[i];
        // Skip duplicate of key caller (stack builder sometimes repeats focus)
        if (mid.symbol == key.symbol && mid.path == key.path) {
          continue;
        }
        if (!any_mid) {
          out << "**mid:**\n";
          any_mid = true;
        }
        out << "- `" << mid.anchor << "`";
        if (!mid.signature.empty()) {
          out << " sig=`" << mid.signature << "`";
        } else if (!mid.scope_chain.empty()) {
          out << " [" << mid.scope_chain << "]";
        }
        if (!mid.control_cond.empty()) {
          out << " cond=`" << mid.control_cond << "`";
        } else if (!mid.control_kind.empty()) {
          out << " @" << mid.control_kind;
        }
        out << "\n";
      }
    }

    if (s.hops.size() >= 2) {
      out << "→ L0 `" << s.hops.back().anchor << "`\n";
    }
    out << "\n";
  }
  if (show_stacks) {
    out << "Responde `a_trail_judge` solo sobre S* de este turno (interesting|reject).\n"
           "{\"action\":\"a_trail_judge\",\"verdicts\":["
           "{\"target\":\"S1\",\"verdict\":\"interesting\",\"why\":\"caller del síntoma\"},"
           "{\"target\":\"S2\",\"verdict\":\"reject\",\"why\":\"otro feature\"}]}\n"
           "Si **todos** reject → L0 se invalida. interesting ≤3. "
           "PROHIBIDO ON|CXL|OFF en este turno.\n";
  }
  return out.str();
}

void a_trail_begin(AState* st, const AVerdict& useful) {
  if (st == nullptr) {
    return;
  }
  ATrail& tr = st->trail;
  tr = ATrail{};
  tr.active = true;
  tr.awaiting_judge = true;
  tr.root_anchor = useful.anchor.empty() ? useful.target : useful.anchor;
  tr.root_stem = useful.stem;
  tr.root_why = useful.why;
  tr.focus_anchor = tr.root_anchor;
  // Bare symbol from path:Symbol
  {
    const auto colon = tr.focus_anchor.rfind(':');
    if (colon != std::string::npos && tr.focus_anchor.find('/') != std::string::npos) {
      tr.focus_symbol = tr.focus_anchor.substr(colon + 1);
      const auto hash = tr.focus_symbol.find('#');
      if (hash != std::string::npos) {
        tr.focus_symbol = tr.focus_symbol.substr(0, hash);
      }
    } else {
      tr.focus_symbol = useful.stem;
    }
  }
  ATrailHop root;
  root.anchor = tr.root_anchor;
  root.symbol = tr.focus_symbol;
  root.path = tr.root_anchor.substr(0, tr.root_anchor.rfind(':'));
  root.summary = useful.why.empty() ? "hipótesis L0 (peek useful)" : useful.why;
  tr.trail.push_back(std::move(root));
}

void a_trail_invalidate_root(AState* st, const std::string& why) {
  if (st == nullptr) {
    return;
  }
  const std::string root = st->trail.root_anchor;
  const std::string stem = st->trail.root_stem;
  ++st->trail.invalidations;
  // Demote matching useful notes → reject
  for (auto& n : st->notes) {
    if (n.verdict != AVerdictKind::Useful) {
      continue;
    }
    const std::string a = n.anchor.empty() ? n.target : n.anchor;
    if (a == root || (!stem.empty() && n.stem == stem)) {
      n.verdict = AVerdictKind::Reject;
      n.role = ALocusRole::Unknown;
      if (n.why.empty()) {
        n.why = why;
      } else {
        n.why += " | invalidated: " + why;
      }
    }
  }
  // Drop loci_draft for root
  std::vector<ALocus> kept;
  for (auto& loc : st->loci_draft) {
    if (loc.anchor == root || (!stem.empty() && loc.stem == stem)) {
      continue;
    }
    kept.push_back(std::move(loc));
  }
  st->loci_draft = std::move(kept);
  if (!stem.empty() &&
      std::find(st->rejected_stems.begin(), st->rejected_stems.end(), stem) ==
          st->rejected_stems.end()) {
    st->rejected_stems.push_back(stem);
  }
  st->trail = ATrail{};
}

bool parse_a_trail_verdicts_array(const nlohmann::json& j, std::vector<AVerdict>* out,
                                  std::string* err) {
  return parse_a_verdicts_array(j, out, err);
}

bool a_is_trail_judge_target_id(const std::string& target) {
  if (target.empty() || target.find('/') != std::string::npos ||
      target.find(':') != std::string::npos) {
    return false;
  }
  std::string n = target;
  for (char& c : n) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (n == "CANCEL") {
    n = "CXL";
  }
  if (n == "ON" || n == "OFF" || n == "CXL" || n == "LINK") {
    return true;
  }
  if (n.size() >= 2 && n[0] == 'S') {
    for (size_t i = 1; i < n.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(n[i]))) {
        return false;
      }
    }
    return true;
  }
  return false;
}

namespace {

std::string normalize_cond_target(std::string t) {
  for (char& c : t) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (t == "CANCEL" || t == "CXL") {
    return "CXL";
  }
  return t;
}

ATrailStack* find_pending_stack(ATrail& tr, const std::string& target) {
  if (target.empty()) {
    return nullptr;
  }
  for (auto& s : tr.pending_stacks) {
    if (s.id == target) {
      return &s;
    }
  }
  if (!target.empty() && target[0] == 'S') {
    for (auto& s : tr.pending_stacks) {
      if (s.id == target) {
        return &s;
      }
    }
  }
  if (!target.empty() && std::isdigit(static_cast<unsigned char>(target[0]))) {
    const int idx = std::atoi(target.c_str()) - 1;
    if (idx >= 0 && idx < static_cast<int>(tr.pending_stacks.size())) {
      return &tr.pending_stacks[static_cast<std::size_t>(idx)];
    }
  }
  // Fuzzy: symbol name, path:symbol, or substring of hop anchor (7B often omits S*).
  auto strip_win = [](std::string t) {
    const auto hash = t.find('#');
    if (hash != std::string::npos) {
      t = t.substr(0, hash);
    }
    return t;
  };
  const std::string tn = strip_win(target);
  const std::string tsym = a_target_symbol_name(tn);
  ATrailStack* fuzzy = nullptr;
  int fuzzy_hits = 0;
  for (auto& s : tr.pending_stacks) {
    bool hit = false;
    for (const auto& h : s.hops) {
      if (h.symbol == tn || h.symbol == tsym || strip_win(h.anchor) == tn) {
        hit = true;
        break;
      }
      if (!tsym.empty() && h.symbol == tsym) {
        hit = true;
        break;
      }
      if (!tn.empty() && (strip_win(h.anchor).find(tn) != std::string::npos ||
                          tn.find(h.symbol) != std::string::npos)) {
        hit = true;
        break;
      }
    }
    if (!hit && !s.hops.empty()) {
      const auto& front = s.hops.front();
      if (front.symbol == tn || front.symbol == tsym) {
        hit = true;
      }
    }
    if (hit) {
      ++fuzzy_hits;
      fuzzy = &s;
    }
  }
  if (fuzzy_hits == 1) {
    return fuzzy;
  }
  // Ambiguous symbol → prefer first ★/distinct caller stack if any, else first hit.
  if (fuzzy_hits > 1 && !tsym.empty()) {
    for (auto& s : tr.pending_stacks) {
      if (s.hops.size() >= 2 && !s.hops.front().symbol.empty() &&
          (s.hops.front().symbol == tsym || s.hops.front().symbol == tn)) {
        return &s;
      }
    }
    return fuzzy;
  }
  return nullptr;
}

ATrailCondBranch* find_cond_branch(ATrail& tr, const std::string& target) {
  const std::string norm = normalize_cond_target(target);
  if (norm.empty()) {
    return nullptr;
  }
  for (auto& b : tr.cond_branches) {
    if (b.id == target || b.id == norm || normalize_cond_target(b.id) == norm) {
      return &b;
    }
  }
  return nullptr;
}

void queue_force_branch(ATrail& tr, const std::string& id) {
  if (id.empty()) {
    return;
  }
  if (std::find(tr.force_queue.begin(), tr.force_queue.end(), id) == tr.force_queue.end()) {
    tr.force_queue.push_back(id);
  }
}

}  // namespace

bool a_validate_a_trail_judge(const AState& st, const std::vector<AVerdict>& verdicts,
                              std::string* err) {
  auto fail = [&](const std::string& msg) {
    if (err) {
      *err = msg;
    }
    return false;
  };
  if (!st.trail.active || !st.trail.awaiting_judge) {
    return fail("a_trail_judge: no hay trail activa esperando juicio");
  }
  if (verdicts.empty()) {
    return fail("a_trail_judge.verdicts vacío");
  }
  // Too many interesting is soft-capped in a_trail_apply_judge (not a hard fail).
  return true;
}

bool a_trail_apply_judge(AState* st, const std::vector<AVerdict>& verdicts, std::string* err) {
  if (st == nullptr) {
    if (err) {
      *err = "a_trail_apply_judge: null state";
    }
    return false;
  }
  if (!a_validate_a_trail_judge(*st, verdicts, err)) {
    return false;
  }
  ATrail& tr = st->trail;
  int interesting = 0;
  int reject = 0;
  int unmatched = 0;
  for (const auto& v : verdicts) {
    AVerdict nv = v;
    a_normalize_verdict(&nv);
    if (nv.verdict == AVerdictKind::Useful) {
      nv.verdict = AVerdictKind::Interesting;
    }

    if (ATrailCondBranch* branch = find_cond_branch(tr, nv.target); branch != nullptr) {
      if (!tr.pending_stacks.empty()) {
        // Stacks-first judge: ignore ON/CXL leaked from prompts / leftover cond.
        continue;
      }
      branch->verdict = nv.verdict;
      branch->why = nv.why;
      if (nv.verdict == AVerdictKind::Interesting) {
        if (interesting >= kATrailMaxInterestingPerLevel) {
          branch->verdict = AVerdictKind::Reject;
          ++reject;
        } else {
          ++interesting;
          queue_force_branch(tr, branch->id);
        }
      } else if (nv.verdict == AVerdictKind::Reject) {
        ++reject;
      }
      continue;
    }

    ATrailStack* stack = find_pending_stack(tr, nv.target);
    if (stack == nullptr) {
      ++unmatched;
      continue;
    }
    stack->verdict = nv.verdict;
    stack->why = nv.why;
    if (nv.verdict == AVerdictKind::Interesting) {
      if (interesting >= kATrailMaxInterestingPerLevel) {
        stack->verdict = AVerdictKind::Reject;
        ++reject;
      } else {
        ++interesting;
        queue_force_branch(tr, stack->id);
      }
    } else if (nv.verdict == AVerdictKind::Reject) {
      ++reject;
    }
  }

  const int n_stacks = static_cast<int>(tr.pending_stacks.size());
  const int n_cond = static_cast<int>(tr.cond_branches.size());
  const int n_items = n_stacks > 0 ? n_stacks : n_cond;
  if (interesting == 0 && reject == 0 && n_items > 0) {
    // Model emitted symbol names / garbage — keep trail open; do not soft-close or suspect.
    tr.awaiting_judge = true;
    if (err) {
      *err =
          "a_trail_judge: ningún target matcheó ids mostrados (ON|CXL|OFF|LINK|S1…); "
          "reemite interesting|reject solo sobre esos ids"
          " (unmatched=" +
          std::to_string(unmatched) + ")";
    }
    return false;
  }
  if (interesting == 0 && reject >= std::max(kATrailMinCallersToFalsify, n_items) &&
      n_items >= kATrailMinCallersToFalsify) {
    a_trail_invalidate_root(st, "todos los callers/stacks reject — L0 falso positivo");
    return true;
  }
  if (interesting == 0 && n_items > 0 && reject >= n_items) {
    a_trail_invalidate_root(st, "stacks/ramas todos reject — L0 invalidado");
    return true;
  }
  if (interesting == 0 && n_items == 0) {
    // No callers: do not falsify; close trail softly and resume queue
    tr.active = false;
    tr.awaiting_judge = false;
    return true;
  }

  // Cap branches
  while (static_cast<int>(tr.force_queue.size()) > kATrailMaxBranches) {
    tr.force_queue.pop_back();
  }
  tr.branches_open = static_cast<int>(tr.force_queue.size());
  tr.awaiting_judge = false;  // runtime will deepen next force_queue item
  return true;
}

}  // namespace tuide
