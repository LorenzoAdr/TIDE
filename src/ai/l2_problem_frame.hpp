#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tuide {

inline constexpr const char* kProblemFrameSchema = "problem_frame_v1";

struct PrimaryAnchor {
  std::string kind;       // symptom_control | effect_surface | entrypoint | state_latch
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

struct ProblemFrame {
  std::string schema = kProblemFrameSchema;
  std::string instruction;
  std::string problem_kind;  // debug | locate | implement | explain
  std::string problem_frame;
  PrimaryAnchor primary_anchor;
  std::vector<MechanismGap> mechanism_gaps;
  std::vector<SecondaryAnchor> secondary_anchors;
  std::vector<std::string> reject_noise;
  std::string anchor_confidence;  // high | medium | low
  std::string provenance;         // l1_distill | deterministic_fallback | manual
};

// Parse v1 JSON (tolerates legacy distilled-intent fields).
bool problem_frame_from_json(const nlohmann::json& j, ProblemFrame* out, std::string* err = nullptr);
bool problem_frame_from_json_string(const std::string& raw, ProblemFrame* out,
                                    std::string* err = nullptr);

nlohmann::json problem_frame_to_json(const ProblemFrame& pf);

// Seeds for map/registry hop0 (primary search_terms + edge-derived tokens).
std::vector<std::string> problem_frame_anchor_seeds(const ProblemFrame& pf);

// Minimal validity for bootstrap (primary objective + ≥1 search term).
bool problem_frame_minimally_valid(const ProblemFrame& pf);

// Best-effort frame when L1 distillation fails.
ProblemFrame problem_frame_fallback_from_query(const std::string& user_message);

std::string problem_frame_path(const std::string& workspace_root);
bool save_problem_frame(const std::string& workspace_root, const ProblemFrame& pf,
                        std::string* err = nullptr);
std::optional<ProblemFrame> load_problem_frame(const std::string& workspace_root);

}  // namespace tuide
