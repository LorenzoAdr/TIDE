#include "ai/l2_problem_frame.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace tuide {
namespace {

std::string trim_copy(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string ascii_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

void read_string_array(const nlohmann::json& j, const char* key, std::vector<std::string>* dst) {
  if (dst == nullptr || !j.contains(key) || !j[key].is_array()) {
    return;
  }
  for (const auto& it : j[key]) {
    if (it.is_string()) {
      const std::string s = trim_copy(it.get<std::string>());
      if (!s.empty()) {
        dst->push_back(s);
      }
    }
  }
}

void read_primary_anchor(const nlohmann::json& j, PrimaryAnchor* out) {
  if (out == nullptr) {
    return;
  }
  if (j.contains("primary_anchor") && j["primary_anchor"].is_object()) {
    const auto& pa = j["primary_anchor"];
    out->kind = trim_copy(pa.value("kind", ""));
    out->objective = trim_copy(pa.value("objective", ""));
    read_string_array(pa, "search_terms", &out->search_terms);
    read_string_array(pa, "edge_hints", &out->edge_hints);
    return;
  }
  // Legacy distilled-intent: primary_goal + search_terms at root.
  out->objective = trim_copy(j.value("primary_goal", j.value("primary_anchor_objective", "")));
  read_string_array(j, "search_terms", &out->search_terms);
  read_string_array(j, "facets", &out->search_terms);
}

std::string infer_problem_kind(const std::string& msg) {
  const std::string m = ascii_lower(msg);
  // Lightweight, domain-agnostic cues only (no product-specific stems).
  if (m.find("bug") != std::string::npos || m.find("error") != std::string::npos ||
      m.find("no funciona") != std::string::npos || m.find("atascad") != std::string::npos ||
      m.find("bloquead") != std::string::npos || m.find("falla") != std::string::npos) {
    return "debug";
  }
  if (m.find("dónde") != std::string::npos || m.find("donde") != std::string::npos ||
      m.find("qué código") != std::string::npos || m.find("que codigo") != std::string::npos ||
      m.find("muéstrame") != std::string::npos || m.find("muestrame") != std::string::npos) {
    return "locate";
  }
  if (m.find("añadir") != std::string::npos || m.find("anadir") != std::string::npos ||
      m.find("implement") != std::string::npos || m.find("cambia") != std::string::npos ||
      m.find("quiero que") != std::string::npos) {
    return "implement";
  }
  return "explain";
}

std::string infer_anchor_kind(const std::string& /*msg*/, const std::string& problem_kind) {
  if (problem_kind == "locate") {
    return "entrypoint";
  }
  if (problem_kind == "debug") {
    return "control";
  }
  if (problem_kind == "implement") {
    return "feature";
  }
  return "module";
}

std::vector<std::string> tokenize_codeish(const std::string& msg) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 3) {
      out.push_back(cur);
    }
    cur.clear();
  };
  for (unsigned char c : msg) {
    if (std::isalnum(c) || c == '_') {
      cur.push_back(static_cast<char>(c));
    } else {
      flush();
    }
  }
  flush();
  return out;
}

void push_unique_term(std::vector<std::string>* terms, const std::string& raw) {
  if (terms == nullptr) {
    return;
  }
  const std::string t = trim_copy(raw);
  if (t.size() < 3) {
    return;
  }
  const std::string tl = ascii_lower(t);
  for (const auto& x : *terms) {
    if (ascii_lower(x) == tl) {
      return;
    }
  }
  terms->push_back(t);
}

// Drop NL phrases; keep code-like tokens only. No domain stem injection.
// Preserve dotted/hyphen compounds (build.gradle) as one term so grounding can
// reject partially invented compounds; do not explode them into grounded parts.
void sanitize_search_terms(std::vector<std::string>* terms) {
  if (terms == nullptr) {
    return;
  }
  std::vector<std::string> out;
  for (const auto& t : *terms) {
    if (t.find(' ') != std::string::npos) {
      continue;
    }
    const std::string trimmed = trim_copy(t);
    if (trimmed.empty()) {
      continue;
    }
    // Already a single identifier-like token (may include . _ -): keep intact.
    bool codeish = true;
    for (unsigned char c : trimmed) {
      if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) {
        codeish = false;
        break;
      }
    }
    if (codeish) {
      push_unique_term(&out, trimmed);
      continue;
    }
    for (const auto& tok : tokenize_codeish(t)) {
      push_unique_term(&out, tok);
    }
  }
  *terms = std::move(out);
}

// Fold common Latin-1 accents → ASCII so "compilación"↔"compile" can share a stem.
std::string fold_ascii_alnum(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      if (std::isalnum(c) || c == '_') {
        out.push_back(static_cast<char>(std::tolower(c)));
      }
      ++i;
      continue;
    }
    // UTF-8 2-byte Latin supplements used in ES/EN prompts (áéíóúñü…).
    if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
      const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
      const unsigned code = ((c & 0x1F) << 6) | (c1 & 0x3F);
      char mapped = 0;
      switch (code) {
        case 0xE1: case 0xE0: case 0xE2: case 0xE4: mapped = 'a'; break;  // áàâä
        case 0xE9: case 0xE8: case 0xEA: case 0xEB: mapped = 'e'; break;
        case 0xED: case 0xEC: case 0xEE: case 0xEF: mapped = 'i'; break;
        case 0xF3: case 0xF2: case 0xF4: case 0xF6: mapped = 'o'; break;
        case 0xFA: case 0xF9: case 0xFB: case 0xFC: mapped = 'u'; break;
        case 0xF1: mapped = 'n'; break;  // ñ
        case 0xC1: case 0xC0: case 0xC2: case 0xC4: mapped = 'a'; break;
        case 0xC9: case 0xC8: case 0xCA: case 0xCB: mapped = 'e'; break;
        case 0xCD: case 0xCC: case 0xCE: case 0xCF: mapped = 'i'; break;
        case 0xD3: case 0xD2: case 0xD4: case 0xD6: mapped = 'o'; break;
        case 0xDA: case 0xD9: case 0xDB: case 0xDC: mapped = 'u'; break;
        case 0xD1: mapped = 'n'; break;
        default: break;
      }
      if (mapped) {
        out.push_back(mapped);
      }
      i += 2;
      continue;
    }
    // Skip other multi-byte sequences.
    if ((c & 0xF0) == 0xE0) {
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      i += 4;
    } else {
      ++i;
    }
  }
  return out;
}

std::vector<std::string> split_term_parts(const std::string& term) {
  std::vector<std::string> parts;
  std::string cur;
  auto flush = [&]() {
    if (cur.size() >= 3) {
      parts.push_back(cur);
    }
    cur.clear();
  };
  for (unsigned char c : term) {
    if (c == '_' || c == '-' || c == '.' || std::isspace(c)) {
      flush();
    } else if (std::isupper(c) && !cur.empty() && !std::isupper(static_cast<unsigned char>(cur.back()))) {
      flush();
      cur.push_back(static_cast<char>(std::tolower(c)));
    } else {
      cur.push_back(static_cast<char>(std::tolower(c)));
    }
  }
  flush();
  return parts;
}

// True if a single folded token is grounded in the query (exact or shared ≥4 prefix).
bool part_grounded_in_query(const std::string& part_folded, const std::string& query_folded) {
  if (part_folded.size() < 4) {
    return true;  // too short to judge; ignored by callers that filter ≥4
  }
  if (query_folded.find(part_folded) != std::string::npos) {
    return true;
  }
  // Morphological cousins: shared prefix of length ≥4 (compile↔compilacion).
  return query_folded.find(part_folded.substr(0, 4)) != std::string::npos;
}

// True if term is a lexical projection of the query. Compounds require EVERY part
// of length ≥4 to ground (so "build.gradle" fails when only "build" appears).
// Language-agnostic; never injects replacement stems.
bool term_grounded_in_query(const std::string& term, const std::string& query_folded) {
  if (query_folded.empty()) {
    return true;  // no query → do not strip
  }
  const std::string tf = fold_ascii_alnum(term);
  if (tf.size() >= 4 && query_folded.find(tf) != std::string::npos) {
    return true;
  }
  const auto parts = split_term_parts(term);
  int significant = 0;
  for (const auto& part : parts) {
    const std::string p = fold_ascii_alnum(part);
    if (p.size() < 4) {
      continue;
    }
    ++significant;
    if (!part_grounded_in_query(p, query_folded)) {
      return false;
    }
  }
  if (significant > 0) {
    return true;
  }
  // Single opaque token with no split parts ≥4: require whole-term grounding.
  return tf.size() >= 4 && part_grounded_in_query(tf, query_folded);
}

void ground_search_terms_to_query(std::vector<std::string>* terms, const std::string& user_message,
                                  bool fallback_to_query_tokens) {
  if (terms == nullptr) {
    return;
  }
  const std::string qf = fold_ascii_alnum(user_message);
  std::vector<std::string> out;
  for (const auto& t : *terms) {
    if (term_grounded_in_query(t, qf)) {
      push_unique_term(&out, t);
    }
  }
  // If LLM invented everything on primary, fall back to query tokens (still no domain inject).
  if (out.empty() && fallback_to_query_tokens) {
    for (const auto& tok : tokenize_codeish(user_message)) {
      if (tok.size() >= 4) {
        push_unique_term(&out, tok);
      }
      if (out.size() >= 6) {
        break;
      }
    }
  }
  *terms = std::move(out);
}

// True if term shares a ≥4-char folded stem with any menu token (either direction / parts).
bool term_grounded_in_menu(const std::string& term, const std::vector<std::string>& menu_folded) {
  if (menu_folded.empty()) {
    return false;
  }
  const std::string tf = fold_ascii_alnum(term);
  if (tf.size() < 4) {
    return false;
  }
  for (const auto& mf : menu_folded) {
    if (mf.size() < 4) {
      continue;
    }
    if (mf.find(tf) != std::string::npos || tf.find(mf) != std::string::npos) {
      return true;
    }
    if (mf.find(tf.substr(0, 4)) != std::string::npos ||
        tf.find(mf.substr(0, 4)) != std::string::npos) {
      return true;
    }
  }
  for (const auto& part : split_term_parts(term)) {
    const std::string pf = fold_ascii_alnum(part);
    if (pf.size() < 4) {
      continue;
    }
    for (const auto& mf : menu_folded) {
      if (mf.size() < 4) {
        continue;
      }
      if (mf.find(pf) != std::string::npos || pf.find(mf.substr(0, std::min<std::size_t>(4, mf.size()))) !=
                                                   std::string::npos) {
        return true;
      }
      if (mf.find(pf.substr(0, 4)) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

bool problem_frame_from_json(const nlohmann::json& j, ProblemFrame* out, std::string* err) {
  if (out == nullptr || !j.is_object()) {
    if (err) {
      *err = "problem_frame: se esperaba objeto JSON";
    }
    return false;
  }
  ProblemFrame pf;
  pf.schema = trim_copy(j.value("schema", kProblemFrameSchema));
  pf.instruction = trim_copy(j.value("instruction", ""));
  pf.problem_kind = trim_copy(j.value("problem_kind", ""));
  pf.problem_frame =
      trim_copy(j.value("problem_frame", j.value("intent", j.value("primary_goal", ""))));
  read_primary_anchor(j, &pf.primary_anchor);
  if (j.contains("mechanism_gaps") && j["mechanism_gaps"].is_array()) {
    for (const auto& g : j["mechanism_gaps"]) {
      if (!g.is_object()) {
        continue;
      }
      MechanismGap gap;
      gap.slot = trim_copy(g.value("slot", ""));
      gap.question = trim_copy(g.value("question", ""));
      if (!gap.question.empty()) {
        pf.mechanism_gaps.push_back(std::move(gap));
      }
    }
  }
  if (j.contains("secondary_anchors") && j["secondary_anchors"].is_array()) {
    for (const auto& sa : j["secondary_anchors"]) {
      if (!sa.is_object()) {
        continue;
      }
      SecondaryAnchor sec;
      sec.kind = trim_copy(sa.value("kind", ""));
      sec.objective = trim_copy(sa.value("objective", ""));
      read_string_array(sa, "search_terms", &sec.search_terms);
      sec.deferred = sa.value("deferred", true);
      sec.why_later = trim_copy(sa.value("why_later", ""));
      if (!sec.objective.empty()) {
        pf.secondary_anchors.push_back(std::move(sec));
      }
    }
  }
  if (j.contains("anchor_hypotheses") && j["anchor_hypotheses"].is_array()) {
    for (const auto& h : j["anchor_hypotheses"]) {
      if (!h.is_object()) {
        continue;
      }
      AnchorHypothesis hyp;
      hyp.objective = trim_copy(h.value("objective", ""));
      read_string_array(h, "search_terms", &hyp.search_terms);
      hyp.mechanism_slot = trim_copy(h.value("mechanism_slot", ""));
      hyp.why = trim_copy(h.value("why", ""));
      if (!hyp.search_terms.empty() || !hyp.objective.empty()) {
        pf.anchor_hypotheses.push_back(std::move(hyp));
      }
    }
  }
  read_string_array(j, "reject_noise", &pf.reject_noise);
  read_string_array(j, "ignore", &pf.reject_noise);
  pf.anchor_confidence = trim_copy(j.value("anchor_confidence", "medium"));
  pf.provenance = trim_copy(j.value("provenance", "l1_distill"));
  if (j.contains("active_hypothesis_index") && j["active_hypothesis_index"].is_number_integer()) {
    pf.active_hypothesis_index = j["active_hypothesis_index"].get<int>();
  } else {
    pf.active_hypothesis_index = -1;
  }
  if (pf.problem_kind.empty()) {
    pf.problem_kind = "explain";
  }
  if (pf.primary_anchor.objective.empty() && !pf.problem_frame.empty()) {
    pf.primary_anchor.objective = pf.problem_frame;
  }
  *out = std::move(pf);
  return true;
}

bool problem_frame_from_json_string(const std::string& raw, ProblemFrame* out, std::string* err) {
  try {
    const auto j = nlohmann::json::parse(raw);
    return problem_frame_from_json(j, out, err);
  } catch (const std::exception& e) {
    if (err) {
      *err = std::string("problem_frame JSON: ") + e.what();
    }
    return false;
  }
}

nlohmann::json problem_frame_to_json(const ProblemFrame& pf) {
  nlohmann::json j;
  j["schema"] = pf.schema.empty() ? kProblemFrameSchema : pf.schema;
  if (!pf.instruction.empty()) {
    j["instruction"] = pf.instruction;
  }
  if (!pf.problem_kind.empty()) {
    j["problem_kind"] = pf.problem_kind;
  }
  if (!pf.problem_frame.empty()) {
    j["problem_frame"] = pf.problem_frame;
  }
  nlohmann::json pa = nlohmann::json::object();
  if (!pf.primary_anchor.kind.empty()) {
    pa["kind"] = pf.primary_anchor.kind;
  }
  if (!pf.primary_anchor.objective.empty()) {
    pa["objective"] = pf.primary_anchor.objective;
  }
  if (!pf.primary_anchor.search_terms.empty()) {
    pa["search_terms"] = pf.primary_anchor.search_terms;
  }
  if (!pf.primary_anchor.edge_hints.empty()) {
    pa["edge_hints"] = pf.primary_anchor.edge_hints;
  }
  j["primary_anchor"] = std::move(pa);
  if (!pf.mechanism_gaps.empty()) {
    nlohmann::json gaps = nlohmann::json::array();
    for (const auto& g : pf.mechanism_gaps) {
      gaps.push_back({{"slot", g.slot}, {"question", g.question}});
    }
    j["mechanism_gaps"] = std::move(gaps);
  }
  if (!pf.secondary_anchors.empty()) {
    nlohmann::json secs = nlohmann::json::array();
    for (const auto& s : pf.secondary_anchors) {
      nlohmann::json o = {{"objective", s.objective}, {"deferred", s.deferred}};
      if (!s.kind.empty()) {
        o["kind"] = s.kind;
      }
      if (!s.search_terms.empty()) {
        o["search_terms"] = s.search_terms;
      }
      if (!s.why_later.empty()) {
        o["why_later"] = s.why_later;
      }
      secs.push_back(std::move(o));
    }
    j["secondary_anchors"] = std::move(secs);
  }
  if (!pf.anchor_hypotheses.empty()) {
    nlohmann::json hyps = nlohmann::json::array();
    for (const auto& h : pf.anchor_hypotheses) {
      nlohmann::json o = nlohmann::json::object();
      if (!h.objective.empty()) {
        o["objective"] = h.objective;
      }
      if (!h.search_terms.empty()) {
        o["search_terms"] = h.search_terms;
      }
      if (!h.mechanism_slot.empty()) {
        o["mechanism_slot"] = h.mechanism_slot;
      }
      if (!h.why.empty()) {
        o["why"] = h.why;
      }
      hyps.push_back(std::move(o));
    }
    j["anchor_hypotheses"] = std::move(hyps);
  }
  if (!pf.reject_noise.empty()) {
    j["reject_noise"] = pf.reject_noise;
  }
  if (!pf.anchor_confidence.empty()) {
    j["anchor_confidence"] = pf.anchor_confidence;
  }
  if (!pf.provenance.empty()) {
    j["provenance"] = pf.provenance;
  }
  if (pf.active_hypothesis_index >= 0) {
    j["active_hypothesis_index"] = pf.active_hypothesis_index;
  }
  return j;
}

std::vector<std::string> problem_frame_anchor_seeds(const ProblemFrame& pf) {
  std::vector<std::string> seeds;
  auto push = [&](const std::string& s) {
    const std::string t = trim_copy(s);
    if (t.size() < 2) {
      return;
    }
    for (const auto& x : seeds) {
      if (ascii_lower(x) == ascii_lower(t)) {
        return;
      }
    }
    seeds.push_back(t);
  };
  if (pf.active_hypothesis_index >= 0 &&
      static_cast<std::size_t>(pf.active_hypothesis_index) < pf.anchor_hypotheses.size()) {
    const auto& hyp = pf.anchor_hypotheses[static_cast<std::size_t>(pf.active_hypothesis_index)];
    for (const auto& t : hyp.search_terms) {
      push(t);
    }
    if (!seeds.empty()) {
      return seeds;
    }
  }
  for (const auto& t : pf.primary_anchor.search_terms) {
    push(t);
  }
  for (const auto& h : pf.primary_anchor.edge_hints) {
    push(h);
  }
  if (seeds.empty() && !pf.primary_anchor.objective.empty()) {
    for (const auto& tok : tokenize_codeish(pf.primary_anchor.objective)) {
      push(tok);
    }
  }
  return seeds;
}

bool problem_frame_minimally_valid(const ProblemFrame& pf) {
  if (pf.primary_anchor.objective.empty()) {
    return false;
  }
  return !problem_frame_anchor_seeds(pf).empty();
}

bool problem_frame_wants_anchor_hypotheses(const ProblemFrame& pf) {
  const std::string c = ascii_lower(pf.anchor_confidence);
  return c == "low" || c == "medium" || c.empty();
}

ProblemFrame problem_frame_fallback_from_query(const std::string& user_message) {
  ProblemFrame pf;
  pf.schema = kProblemFrameSchema;
  pf.instruction = user_message;
  pf.problem_kind = infer_problem_kind(user_message);
  pf.problem_frame = user_message.size() > 240 ? user_message.substr(0, 239) + "…" : user_message;
  pf.primary_anchor.kind = infer_anchor_kind(user_message, pf.problem_kind);
  pf.primary_anchor.objective = "localizar el módulo o pieza de código más cercana a la petición";
  for (const auto& tok : tokenize_codeish(user_message)) {
    if (tok.size() >= 4) {
      pf.primary_anchor.search_terms.push_back(tok);
    }
    if (pf.primary_anchor.search_terms.size() >= 6) {
      break;
    }
  }
  pf.anchor_confidence = "low";
  pf.provenance = "deterministic_fallback";
  return pf;
}

void problem_frame_refine_from_query(ProblemFrame* pf, const std::string& user_message) {
  if (pf == nullptr) {
    return;
  }
  // Structural cleanup + lexical grounding to the query. Never inject product stems.
  // anchor_hypotheses are NOT query-grounded here (see refine_hypotheses_to_menu).
  sanitize_search_terms(&pf->primary_anchor.search_terms);
  ground_search_terms_to_query(&pf->primary_anchor.search_terms, user_message,
                               /*fallback_to_query_tokens=*/true);
  if (pf->primary_anchor.search_terms.size() > 8) {
    pf->primary_anchor.search_terms.resize(8);
  }
  {
    std::vector<SecondaryAnchor> kept_sec;
    kept_sec.reserve(pf->secondary_anchors.size());
    for (auto& sec : pf->secondary_anchors) {
      sanitize_search_terms(&sec.search_terms);
      // No fallback: empty after grounding → drop secondary (was invented).
      ground_search_terms_to_query(&sec.search_terms, user_message,
                                   /*fallback_to_query_tokens=*/false);
      if (sec.search_terms.size() > 6) {
        sec.search_terms.resize(6);
      }
      if (!sec.search_terms.empty()) {
        kept_sec.push_back(std::move(sec));
      }
    }
    pf->secondary_anchors = std::move(kept_sec);
  }
  if (pf->problem_kind.empty()) {
    pf->problem_kind = "explain";
  }
  if (pf->primary_anchor.kind.empty()) {
    pf->primary_anchor.kind = "module";
  }
}

void problem_frame_refine_hypotheses_to_menu(ProblemFrame* pf,
                                            const std::vector<std::string>& menu_tokens) {
  if (pf == nullptr) {
    return;
  }
  std::vector<std::string> menu_folded;
  menu_folded.reserve(menu_tokens.size() * 2);
  for (const auto& m : menu_tokens) {
    const std::string folded = fold_ascii_alnum(m);
    if (folded.size() >= 4) {
      menu_folded.push_back(folded);
    }
    for (const auto& part : split_term_parts(m)) {
      const std::string pfld = fold_ascii_alnum(part);
      if (pfld.size() >= 4) {
        menu_folded.push_back(pfld);
      }
    }
  }
  std::vector<AnchorHypothesis> kept;
  kept.reserve(pf->anchor_hypotheses.size());
  for (auto& hyp : pf->anchor_hypotheses) {
    sanitize_search_terms(&hyp.search_terms);
    std::vector<std::string> grounded;
    for (const auto& t : hyp.search_terms) {
      if (term_grounded_in_menu(t, menu_folded)) {
        push_unique_term(&grounded, t);
      }
    }
    if (grounded.size() > 4) {
      grounded.resize(4);
    }
    hyp.search_terms = std::move(grounded);
    if (!hyp.search_terms.empty()) {
      kept.push_back(std::move(hyp));
    }
  }
  if (kept.size() > 4) {
    kept.resize(4);
  }
  pf->anchor_hypotheses = std::move(kept);
  if (pf->active_hypothesis_index >= 0 &&
      static_cast<std::size_t>(pf->active_hypothesis_index) >= pf->anchor_hypotheses.size()) {
    pf->active_hypothesis_index = -1;
  }
}

std::string problem_frame_path(const std::string& workspace_root) {
  return (fs::path(workspace_root) / ".tuide" / "ai" / "l2" / "problem_frame.json").string();
}

bool save_problem_frame(const std::string& workspace_root, const ProblemFrame& pf,
                        std::string* err) {
  if (workspace_root.empty()) {
    if (err) {
      *err = "workspace_root vacío";
    }
    return false;
  }
  const fs::path path = problem_frame_path(workspace_root);
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (!out) {
    if (err) {
      *err = "no se pudo escribir problem_frame.json";
    }
    return false;
  }
  out << problem_frame_to_json(pf).dump(2) << '\n';
  return true;
}

std::optional<ProblemFrame> load_problem_frame(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return std::nullopt;
  }
  std::ifstream in(problem_frame_path(workspace_root));
  if (!in) {
    return std::nullopt;
  }
  try {
    nlohmann::json j;
    in >> j;
    ProblemFrame pf;
    std::string err;
    if (!problem_frame_from_json(j, &pf, &err)) {
      return std::nullopt;
    }
    return pf;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace tuide
