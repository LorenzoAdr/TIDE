#include "ai/l2_entityness.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace tuide {
namespace {

namespace fs = std::filesystem;

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool is_stopword(const std::string& t) {
  static const std::unordered_set<std::string> k = {
      "that", "this", "with", "from", "have", "want", "cuando", "aunque", "donde",
      "donde", "como", "qué", "que", "para", "pero", "porque", "sobre", "entre",
      "sometimes", "a", "the", "and", "or", "el", "la", "los", "las", "un", "una",
      "del", "al", "en", "de", "se", "ya", "me", "mi", "su", "es", "son", "hay",
      "saber", "quiero", "veces", "mostrando", "aunque", "terminó", "termino",
      "respond", "responder", "controla", "cancela", "código", "codigo"};
  return k.count(ascii_lower(t)) != 0;
}

std::vector<std::string> tokenize(const std::string& msg) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 4 && !is_stopword(cur)) {
      out.push_back(cur);
    }
    cur.clear();
  };
  for (unsigned char c : msg) {
    if (std::isalnum(c) || c == '_') {
      cur.push_back(static_cast<char>(std::tolower(c)));
    } else {
      flush();
    }
  }
  flush();
  return out;
}

bool path_twin_exists(const std::string& workspace, const std::string& stem,
                      const std::vector<std::string>& paths) {
  bool hpp = false;
  bool cpp = false;
  for (const auto& p : paths) {
    const std::string s = ascii_lower(p);
    if (s.find(stem) == std::string::npos) {
      continue;
    }
    if (s.size() >= 4 && s.substr(s.size() - 4) == ".hpp") {
      hpp = true;
    }
    if (s.size() >= 4 && s.substr(s.size() - 4) == ".cpp") {
      cpp = true;
    }
    if (s.size() >= 2 && s.substr(s.size() - 2) == ".h") {
      hpp = true;
    }
    if (s.size() >= 2 && s.substr(s.size() - 2) == ".c") {
      cpp = true;
    }
  }
  if (hpp && cpp) {
    return true;
  }
  if (workspace.empty() || stem.empty()) {
    return false;
  }
  // Best-effort: look under src/ for stem twin.
  try {
    for (auto it = fs::recursive_directory_iterator(fs::path(workspace) / "src");
         it != fs::recursive_directory_iterator(); ++it) {
      if (!it->is_regular_file()) {
        continue;
      }
      const auto p = it->path();
      if (p.stem() != stem) {
        continue;
      }
      const auto ext = p.extension().string();
      if (ext == ".hpp" || ext == ".h") {
        hpp = true;
      }
      if (ext == ".cpp" || ext == ".cc" || ext == ".c") {
        cpp = true;
      }
    }
  } catch (...) {
  }
  return hpp && cpp;
}

struct StemMass {
  int mass = 0;
  std::vector<std::string> ids;
  std::vector<std::string> paths;
};

void add_hit(std::unordered_map<std::string, StemMass>* by_stem, const RegistryQueryHit& h) {
  if (by_stem == nullptr) {
    return;
  }
  std::string st = h.node.stem;
  if (st.empty()) {
    st = registry_stem_of(h.node.path);
  }
  if (st.empty() && h.node.id.rfind("latch:", 0) == 0) {
    const auto rest = h.node.id.substr(6);
    const auto c = rest.find(':');
    st = c == std::string::npos ? rest : rest.substr(0, c);
  }
  if (st.empty()) {
    st = "_unknown";
  }
  auto& m = (*by_stem)[st];
  ++m.mass;
  m.ids.push_back(h.node.id);
  if (!h.node.path.empty()) {
    m.paths.push_back(h.node.path);
  }
}

float score_concentration(const std::unordered_map<std::string, StemMass>& by_stem,
                          std::string* owner_out, int* files_out, bool* twin_out,
                          std::vector<std::string>* evidence, const std::string& workspace,
                          int* hit_count_out) {
  if (owner_out) {
    owner_out->clear();
  }
  if (files_out) {
    *files_out = 0;
  }
  if (twin_out) {
    *twin_out = false;
  }
  if (evidence) {
    evidence->clear();
  }
  if (hit_count_out) {
    *hit_count_out = 0;
  }
  if (by_stem.empty()) {
    return 0.f;
  }
  int total = 0;
  std::string best;
  int best_mass = 0;
  for (const auto& [st, m] : by_stem) {
    if (st == "_unknown") {
      continue;
    }
    total += m.mass;
    if (m.mass > best_mass) {
      best_mass = m.mass;
      best = st;
    }
  }
  if (hit_count_out) {
    *hit_count_out = total;
  }
  if (total <= 0 || best.empty()) {
    return 0.f;
  }
  const int owners = static_cast<int>(
      std::count_if(by_stem.begin(), by_stem.end(),
                    [](const auto& kv) { return kv.first != "_unknown" && kv.second.mass > 0; }));
  if (owners > 2) {
    // Diffused: still return weak concentration signal.
  }
  float c = static_cast<float>(best_mass) / static_cast<float>(total);
  if (owners > 2) {
    c *= 0.5f;
  }
  const auto it = by_stem.find(best);
  bool twin = false;
  if (it != by_stem.end()) {
    twin = path_twin_exists(workspace, best, it->second.paths);
    if (evidence) {
      *evidence = it->second.ids;
      if (evidence->size() > 8) {
        evidence->resize(8);
      }
    }
    if (files_out) {
      std::unordered_set<std::string> files(it->second.paths.begin(), it->second.paths.end());
      *files_out = static_cast<int>(files.size());
    }
  }
  if (twin) {
    c = std::min(1.f, c * 1.0f / 0.85f * 0.85f + 0.15f);  // small twin boost → ≤1
    c = std::min(1.f, c + 0.08f);
  }
  if (owner_out) {
    *owner_out = best;
  }
  if (twin_out) {
    *twin_out = twin;
  }
  return std::max(0.f, std::min(1.f, c));
}

}  // namespace

std::vector<std::string> entityness_prompt_terms(const std::string& query) {
  auto toks = tokenize(query);
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const auto& t : toks) {
    if (seen.insert(t).second) {
      out.push_back(t);
    }
    if (out.size() >= 12) {
      break;
    }
  }
  return out;
}

nlohmann::json EntitynessReport::to_json() const {
  nlohmann::json j;
  j["query"] = query;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& c : candidates) {
    arr.push_back({{"term", c.term},
                   {"aliases", c.aliases},
                   {"entityness", c.entityness},
                   {"concentration", c.concentration},
                   {"hit_score", c.hit_score},
                   {"hit_count", c.hit_count},
                   {"owner_stem", c.owner_stem},
                   {"owner_files", c.owner_files},
                   {"twin", c.twin},
                   {"evidence_ids", c.evidence_ids}});
  }
  j["candidates"] = std::move(arr);
  return j;
}

nlohmann::json EntitynessLinkReport::to_json() const {
  nlohmann::json j;
  j["query"] = query;
  j["best_entityness"] = best_entityness;
  j["best_role"] = best_role;
  j["explore_mode"] = explore_mode;
  j["threshold"] = threshold;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& L : links) {
    arr.push_back({{"role", L.role},
                   {"kind", L.kind},
                   {"objective", L.objective},
                   {"search_terms", L.search_terms},
                   {"entityness", L.entityness},
                   {"concentration", L.concentration},
                   {"hit_score", L.hit_score},
                   {"hit_count", L.hit_count},
                   {"owner_stem", L.owner_stem},
                   {"owner_files", L.owner_files},
                   {"twin", L.twin},
                   {"evidence_ids", L.evidence_ids}});
  }
  j["links"] = std::move(arr);
  return j;
}

bool entityness_score_terms(EffectRegistry* r, const std::vector<std::string>& terms,
                            const RegistryEmbedFn& embed, const EntitynessOpts& opts,
                            EntityCandidate* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    if (err) {
      *err = "entityness_score_terms args";
    }
    return false;
  }
  *out = {};
  if (terms.empty()) {
    return true;
  }
  out->term = terms.front();
  out->aliases = terms;

  float best = 0.f;
  EntityCandidate best_hit;
  for (const auto& alias : terms) {
    if (alias.size() < 3) {
      continue;
    }
    std::unordered_map<std::string, StemMass> by_stem;

    RegistryQueryOpts qopts;
    qopts.hops = 0;
    qopts.top_k = opts.top_k;
    qopts.match_surface = RegistryMatchSurface::NodeId;
    qopts.seed_kinds = {"fn", "latch"};
    RegistryQueryResult node_res;
    std::string qerr;
    if (registry_query(r, alias, embed, qopts, &node_res, &qerr)) {
      for (const auto& h : node_res.hits) {
        add_hit(&by_stem, h);
      }
    }

    qopts.match_surface = RegistryMatchSurface::Latch;
    qopts.seed_kinds = {"latch"};
    RegistryQueryResult latch_res;
    if (embed && registry_query(r, alias, embed, qopts, &latch_res, &qerr)) {
      for (const auto& h : latch_res.hits) {
        add_hit(&by_stem, h);
      }
    }

    if (opts.use_card_attrs && embed) {
      qopts.match_surface = RegistryMatchSurface::CardAttrs;
      qopts.seed_kinds = {"fn"};
      RegistryQueryResult attrs_res;
      if (registry_query(r, alias, embed, qopts, &attrs_res, &qerr)) {
        for (const auto& h : attrs_res.hits) {
          add_hit(&by_stem, h);
        }
      }
    }

    EntityCandidate scored;
    scored.term = alias;
    scored.aliases = {alias};
    int hits = 0;
    scored.concentration =
        score_concentration(by_stem, &scored.owner_stem, &scored.owner_files, &scored.twin,
                            &scored.evidence_ids, r->workspace_root, &hits);
    scored.hit_count = hits;
    scored.hit_score = entityness_hit_score(hits);
    scored.entityness = entityness_combine(scored.concentration, hits);
    if (scored.entityness > best) {
      best = scored.entityness;
      best_hit = scored;
    }
  }
  if (best > 0.f) {
    *out = best_hit;
    out->aliases = terms;
    out->term = terms.front();
  }
  return true;
}

bool entityness_probe(EffectRegistry* r, const std::string& query, const RegistryEmbedFn& embed,
                      const EntitynessOpts& opts, EntitynessReport* out, std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    if (err) {
      *err = "entityness_probe args";
    }
    return false;
  }
  *out = {};
  out->query = query;
  const auto terms = entityness_prompt_terms(query);
  for (const auto& term : terms) {
    std::vector<std::string> aliases;
    auto it = opts.aliases_by_term.find(term);
    if (it != opts.aliases_by_term.end() && !it->second.empty()) {
      aliases = it->second;
    } else {
      aliases = {term};
    }
    if (std::find(aliases.begin(), aliases.end(), term) == aliases.end()) {
      aliases.insert(aliases.begin(), term);
    }
    EntityCandidate cand;
    std::string terr;
    if (!entityness_score_terms(r, aliases, embed, opts, &cand, &terr)) {
      if (err) {
        *err = terr;
      }
      return false;
    }
    cand.term = term;
    cand.aliases = aliases;
    out->candidates.push_back(std::move(cand));
  }
  std::sort(out->candidates.begin(), out->candidates.end(),
            [](const EntityCandidate& a, const EntityCandidate& b) {
              return a.entityness > b.entityness;
            });
  return true;
}

bool entityness_score_problem_frame(EffectRegistry* r, const ProblemFrame& pf,
                                    const std::string& query, const RegistryEmbedFn& embed,
                                    const EntitynessOpts& opts, EntitynessLinkReport* out,
                                    std::string* err) {
  if (r == nullptr || r->db == nullptr || out == nullptr) {
    if (err) {
      *err = "entityness_score_problem_frame args";
    }
    return false;
  }
  *out = {};
  out->query = query.empty() ? pf.instruction : query;
  out->threshold = opts.f1_threshold > 0.f ? opts.f1_threshold : kEntitynessF1ThresholdDefault;

  auto score_link = [&](const std::string& role, const std::string& kind,
                        const std::string& objective, const std::vector<std::string>& terms) {
    EntityLinkScore L;
    L.role = role;
    L.kind = kind;
    L.objective = objective;
    L.search_terms = terms;
    EntityCandidate cand;
    std::string terr;
    if (!terms.empty()) {
      entityness_score_terms(r, terms, embed, opts, &cand, &terr);
      L.entityness = cand.entityness;
      L.concentration = cand.concentration;
      L.hit_score = cand.hit_score;
      L.hit_count = cand.hit_count;
      L.owner_stem = cand.owner_stem;
      L.owner_files = cand.owner_files;
      L.twin = cand.twin;
      L.evidence_ids = cand.evidence_ids;
    }
    out->links.push_back(std::move(L));
  };

  score_link("primary", pf.primary_anchor.kind, pf.primary_anchor.objective,
             pf.primary_anchor.search_terms);
  for (std::size_t i = 0; i < pf.secondary_anchors.size(); ++i) {
    const auto& sec = pf.secondary_anchors[i];
    score_link("secondary_" + std::to_string(i), sec.kind, sec.objective, sec.search_terms);
  }
  for (std::size_t i = 0; i < pf.anchor_hypotheses.size(); ++i) {
    const auto& hyp = pf.anchor_hypotheses[i];
    const auto terms = hypothesis_anchor_terms(hyp);
    score_link("hyp_" + std::to_string(i),
               hyp.anchor_role.empty() ? hyp.mechanism_slot : hyp.anchor_role,
               hyp.claim.empty() ? hyp.objective : hyp.claim, terms);
  }

  // Prefer the best hyp; if several hyps share the same owner_stem, that cluster wins
  // (ideal: one anchor, multiple claims to falsify).
  out->best_entityness = 0.f;
  out->best_role.clear();
  std::unordered_map<std::string, float> stem_best;
  std::unordered_map<std::string, std::string> stem_role;
  std::unordered_map<std::string, int> stem_count;
  for (const auto& L : out->links) {
    if (L.role.size() >= 4 && L.role.compare(0, 4, "hyp_") == 0 && L.entityness >= out->threshold &&
        !L.owner_stem.empty()) {
      stem_count[L.owner_stem] += 1;
      if (L.entityness > stem_best[L.owner_stem]) {
        stem_best[L.owner_stem] = L.entityness;
        stem_role[L.owner_stem] = L.role;
      }
    }
  }
  std::string cluster_stem;
  int cluster_n = 0;
  float cluster_ent = 0.f;
  for (const auto& [stem, n] : stem_count) {
    const float e = stem_best[stem];
    if (n > cluster_n || (n == cluster_n && e > cluster_ent)) {
      cluster_n = n;
      cluster_ent = e;
      cluster_stem = stem;
    }
  }
  if (cluster_n >= 2 && !cluster_stem.empty()) {
    out->best_entityness = cluster_ent;
    out->best_role = stem_role[cluster_stem];
  } else {
    for (const auto& L : out->links) {
      if (L.entityness > out->best_entityness) {
        out->best_entityness = L.entityness;
        out->best_role = L.role;
      }
    }
  }
  out->explore_mode =
      (out->best_entityness >= out->threshold) ? "f1_anchor" : "classic_scan";
  return true;
}

std::string entityness_prompt_block(const EntitynessReport& report, int max_rows) {
  std::ostringstream oss;
  oss << "ENTITYNESS:\n";
  int n = 0;
  for (const auto& c : report.candidates) {
    if (n >= max_rows) {
      break;
    }
    if (c.entityness <= 0.01f && c.owner_stem.empty()) {
      continue;
    }
    oss << "- " << c.term << ": " << c.entityness;
    if (c.hit_count > 0) {
      oss << " conc=" << c.concentration << " hits=" << c.hit_count << " hit_s=" << c.hit_score;
    }
    if (!c.owner_stem.empty()) {
      oss << " stem=" << c.owner_stem;
    }
    if (c.owner_files > 0) {
      oss << " files=" << c.owner_files;
    }
    if (c.twin) {
      oss << " twin=1";
    }
    oss << "\n";
    ++n;
  }
  if (n == 0) {
    oss << "(none)\n";
  }
  return oss.str();
}

std::string entityness_links_prompt_block(const EntitynessLinkReport& report, int max_rows) {
  std::ostringstream oss;
  oss << "ENTITYNESS_LINKS mode=" << report.explore_mode << " best=" << report.best_entityness
      << " thr=" << report.threshold << "\n";
  int n = 0;
  for (const auto& L : report.links) {
    if (n >= max_rows) {
      break;
    }
    oss << "- [" << L.role << "] " << L.entityness;
    if (L.hit_count > 0) {
      oss << " conc=" << L.concentration << " hits=" << L.hit_count << " hit_s=" << L.hit_score;
    }
    if (!L.owner_stem.empty()) {
      oss << " stem=" << L.owner_stem;
    }
    oss << " terms=";
    for (std::size_t i = 0; i < L.search_terms.size(); ++i) {
      if (i) {
        oss << ',';
      }
      oss << L.search_terms[i];
    }
    oss << "\n";
    ++n;
  }
  return oss.str();
}

}  // namespace tuide
