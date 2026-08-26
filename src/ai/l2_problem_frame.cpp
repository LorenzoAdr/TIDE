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
  if (m.find("spinner") != std::string::npos || m.find("atascad") != std::string::npos ||
      m.find("bug") != std::string::npos || m.find("error") != std::string::npos ||
      m.find("no funciona") != std::string::npos || m.find("bloquead") != std::string::npos) {
    return "debug";
  }
  if ((m.find("cancel") != std::string::npos || m.find("escape") != std::string::npos) &&
      (m.find("gener") != std::string::npos || m.find("ia") != std::string::npos ||
       m.find("asistente") != std::string::npos)) {
    return "debug";
  }
  if (m.find("dónde") != std::string::npos || m.find("donde") != std::string::npos ||
      m.find("qué código") != std::string::npos || m.find("que codigo") != std::string::npos ||
      m.find("muéstrame") != std::string::npos || m.find("muestrame") != std::string::npos) {
    return "locate";
  }
  if (m.find("añadir") != std::string::npos || m.find("anadir") != std::string::npos ||
      m.find("implement") != std::string::npos || m.find("quiero que") != std::string::npos ||
      m.find("cambia el código") != std::string::npos || m.find("cambia el codigo") != std::string::npos) {
    return "implement";
  }
  return "explain";
}

std::string infer_anchor_kind(const std::string& msg, const std::string& problem_kind) {
  const std::string m = ascii_lower(msg);
  if (m.find("spinner") != std::string::npos || m.find("busy") != std::string::npos ||
      m.find("carga") != std::string::npos || m.find("estado") != std::string::npos) {
    return "symptom_control";
  }
  if (problem_kind == "locate") {
    return "entrypoint";
  }
  if (m.find("primera vez") != std::string::npos || m.find("persist") != std::string::npos) {
    return "state_latch";
  }
  return "effect_surface";
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

void augment_search_terms_from_query(const std::string& msg, std::vector<std::string>* terms) {
  if (terms == nullptr) {
    return;
  }
  const std::string m = ascii_lower(msg);
  if (m.find("spinner") != std::string::npos || m.find("busy") != std::string::npos ||
      m.find("carga") != std::string::npos || m.find("bloquead") != std::string::npos) {
    push_unique_term(terms, "busy_strip");
    push_unique_term(terms, "set_busy");
    push_unique_term(terms, "clear_busy");
    push_unique_term(terms, "agent_busy");
  }
  if (m.find("ia") != std::string::npos || m.find("ai") != std::string::npos ||
      m.find("asistente") != std::string::npos) {
    push_unique_term(terms, "ai_controller");
    push_unique_term(terms, "level2_autonomous_loop");
  }
  if (m.find("compil") != std::string::npos || m.find("build") != std::string::npos) {
    push_unique_term(terms, "task_runner");
    push_unique_term(terms, "ai_controller");
  }
  if (m.find("cerrar") != std::string::npos || m.find("salir") != std::string::npos ||
      m.find("quit") != std::string::npos) {
    push_unique_term(terms, "quit_confirm");
  }
  if (m.find("configur") != std::string::npos || m.find("settings") != std::string::npos ||
      m.find("preferenc") != std::string::npos) {
    push_unique_term(terms, "settings_modal");
    push_unique_term(terms, "app_settings");
  }
  if (m.find("barra") != std::string::npos && m.find("estado") != std::string::npos) {
    push_unique_term(terms, "busy_strip");
  }
}

void sanitize_search_terms(std::vector<std::string>* terms) {
  if (terms == nullptr) {
    return;
  }
  std::vector<std::string> out;
  for (const auto& t : *terms) {
    if (t.find(' ') != std::string::npos) {
      continue;
    }
    for (const auto& tok : tokenize_codeish(t)) {
      push_unique_term(&out, tok);
    }
  }
  *terms = std::move(out);
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
  read_string_array(j, "reject_noise", &pf.reject_noise);
  read_string_array(j, "ignore", &pf.reject_noise);
  pf.anchor_confidence = trim_copy(j.value("anchor_confidence", "medium"));
  pf.provenance = trim_copy(j.value("provenance", "l1_distill"));
  if (pf.problem_kind.empty()) {
    pf.problem_kind = "debug";
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
  if (!pf.reject_noise.empty()) {
    j["reject_noise"] = pf.reject_noise;
  }
  if (!pf.anchor_confidence.empty()) {
    j["anchor_confidence"] = pf.anchor_confidence;
  }
  if (!pf.provenance.empty()) {
    j["provenance"] = pf.provenance;
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

ProblemFrame problem_frame_fallback_from_query(const std::string& user_message) {
  ProblemFrame pf;
  pf.schema = kProblemFrameSchema;
  pf.instruction = user_message;
  pf.problem_kind = infer_problem_kind(user_message);
  pf.problem_frame = user_message.size() > 240 ? user_message.substr(0, 239) + "…" : user_message;
  pf.primary_anchor.kind = infer_anchor_kind(user_message, pf.problem_kind);
  pf.primary_anchor.objective = pf.problem_frame;
  for (const auto& tok : tokenize_codeish(user_message)) {
    if (tok.size() >= 4) {
      pf.primary_anchor.search_terms.push_back(tok);
    }
    if (pf.primary_anchor.search_terms.size() >= 8) {
      break;
    }
  }
  if (pf.problem_kind == "debug") {
    pf.primary_anchor.edge_hints = {"set_", "clear_", "cancel_"};
    MechanismGap g;
    g.slot = "cleanup";
    g.question = "¿quién debería desactivar o limpiar el estado observable?";
    pf.mechanism_gaps.push_back(std::move(g));
  }
  pf.anchor_confidence = "low";
  pf.provenance = "deterministic_fallback";
  return pf;
}

void problem_frame_refine_from_query(ProblemFrame* pf, const std::string& user_message) {
  if (pf == nullptr || user_message.empty()) {
    return;
  }
  const std::string inferred = infer_problem_kind(user_message);
  pf->problem_kind = inferred;
  sanitize_search_terms(&pf->primary_anchor.search_terms);
  augment_search_terms_from_query(user_message, &pf->primary_anchor.search_terms);
  if (pf->primary_anchor.search_terms.size() > 8) {
    pf->primary_anchor.search_terms.resize(8);
  }
  if (pf->primary_anchor.kind.empty()) {
    pf->primary_anchor.kind = infer_anchor_kind(user_message, pf->problem_kind);
  }
  if (pf->problem_kind == "debug" && pf->primary_anchor.edge_hints.empty()) {
    pf->primary_anchor.edge_hints = {"set_", "clear_", "cancel_"};
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
