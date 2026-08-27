#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tuide {

inline constexpr const char* kProblemFrameSchema = "problem_frame_v1";

struct PrimaryAnchor {
  // feature | module | entrypoint | control | state (legacy kinds still accepted)
  std::string kind;
  std::string objective;
  std::vector<std::string> search_terms;
  std::vector<std::string> edge_hints;
};

struct MechanismGap {
  std::string slot;     // trigger | state | cleanup | effect
  std::string question;
};

struct SecondaryAnchor {
  std::string kind;
  std::string objective;
  std::vector<std::string> search_terms;
  bool deferred = true;
  std::string why_later;
};

// Candidate anchor when the prompt-grounded PF is too diffuse (menu-grounded, not query-grounded).
struct AnchorHypothesis {
  std::string objective;
  std::vector<std::string> search_terms;
  std::string mechanism_slot;  // effect | state | control | entrypoint | …
  std::string why;
};

struct ProblemFrame {
  std::string schema = kProblemFrameSchema;
  std::string instruction;
  std::string problem_kind;  // debug | locate | implement | explain
  std::string problem_frame;
  PrimaryAnchor primary_anchor;
  std::vector<MechanismGap> mechanism_gaps;
  std::vector<SecondaryAnchor> secondary_anchors;
  std::vector<AnchorHypothesis> anchor_hypotheses;
  std::vector<std::string> reject_noise;
  std::string anchor_confidence;  // high | medium | low
  std::string provenance;         // l1_distill | deterministic_fallback | manual
  // When entityness picks hyp_N as best F1 link, set to N; else -1 (use primary seeds).
  int active_hypothesis_index = -1;
};

// Parse v1 JSON (tolerates legacy distilled-intent fields).
bool problem_frame_from_json(const nlohmann::json& j, ProblemFrame* out, std::string* err = nullptr);
bool problem_frame_from_json_string(const std::string& raw, ProblemFrame* out,
                                    std::string* err = nullptr);

nlohmann::json problem_frame_to_json(const ProblemFrame& pf);

// Seeds for map/registry hop0. If active_hypothesis_index >= 0, use that hyp's terms.
std::vector<std::string> problem_frame_anchor_seeds(const ProblemFrame& pf);

// Minimal validity for bootstrap (primary objective + ≥1 search term).
bool problem_frame_minimally_valid(const ProblemFrame& pf);

// True when prompt-grounded PF is too weak to hunt F1 alone (low/medium confidence).
bool problem_frame_wants_anchor_hypotheses(const ProblemFrame& pf);

// Best-effort frame when L1 distillation fails.
ProblemFrame problem_frame_fallback_from_query(const std::string& user_message);

// Structural cleanup only (drop NL phrases). Never injects domain stems.
// Does NOT ground or strip anchor_hypotheses (those use refine_hypotheses_to_menu).
void problem_frame_refine_from_query(ProblemFrame* pf, const std::string& user_message);

// Drop hyp search_terms that do not share stem ≥4 with any menu token; drop empty hyps.
void problem_frame_refine_hypotheses_to_menu(ProblemFrame* pf,
                                            const std::vector<std::string>& menu_tokens);

std::string problem_frame_path(const std::string& workspace_root);
bool save_problem_frame(const std::string& workspace_root, const ProblemFrame& pf,
                        std::string* err = nullptr);
std::optional<ProblemFrame> load_problem_frame(const std::string& workspace_root);

}  // namespace tuide
