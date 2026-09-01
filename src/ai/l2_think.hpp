#pragma once

#include <string_view>

#include "ai/l2_brain.hpp"
#include "ai/llama_backend.hpp"

namespace tuide {

enum class L2ThinkLevel { Off, Low, Medium, High };

struct L2ThinkProfile {
  L2ThinkLevel level = L2ThinkLevel::Low;
  bool enable_thinking = true;
  int budget = 64;  // 0 = off
};

inline const char* l2_think_level_name(L2ThinkLevel level) {
  switch (level) {
    case L2ThinkLevel::Off:
      return "off";
    case L2ThinkLevel::Low:
      return "low";
    case L2ThinkLevel::Medium:
      return "medium";
    case L2ThinkLevel::High:
      return "high";
  }
  return "low";
}

inline L2ThinkProfile think_profile(L2ThinkLevel level) {
  switch (level) {
    case L2ThinkLevel::Off:
      return {L2ThinkLevel::Off, false, 0};
    case L2ThinkLevel::Low:
      return {L2ThinkLevel::Low, true, 64};
    case L2ThinkLevel::Medium:
      return {L2ThinkLevel::Medium, true, 512};
    case L2ThinkLevel::High:
      return {L2ThinkLevel::High, true, 1536};
  }
  return {L2ThinkLevel::Low, true, 64};
}

// Phase + pack occupancy. Do not use step / last_action (Phase A first plan is
// explore_b after a_done with has_pack still false).
inline L2ThinkProfile think_profile_for(std::string_view phase, bool has_pack,
                                        bool is_pack_review) {
  if (is_pack_review) {
    return think_profile(L2ThinkLevel::Low);
  }
  if (phase == "edit" || phase == "causal_wave_cover") {
    return think_profile(L2ThinkLevel::Off);
  }
  if (phase == "explore_a" || phase == "causal_pilot_worker" ||
      phase == "causal_zone_judge" || phase == "causal_atlas_survey" ||
      phase == "causal_atlas_cover" || phase == "causal_zone_triage" ||
      phase == "causal_zone_slot_hyp") {
    return think_profile(L2ThinkLevel::Low);
  }
  if (phase == "causal_pilot_plan" || phase == "causal_wave_pilot") {
    return think_profile(L2ThinkLevel::Medium);
  }
  if (phase == "causal_zone_hyp" || phase == "causal_zone_anchor" ||
      ((phase == "explore" || phase == "explore_b") && !has_pack)) {
    return think_profile(L2ThinkLevel::High);
  }
  return think_profile(L2ThinkLevel::Medium);
}

template <typename Req>
void apply_think_profile(Req* req, const L2ThinkProfile& profile) {
  if (req == nullptr) {
    return;
  }
  req->enable_thinking = profile.enable_thinking;
  req->reasoning_budget = profile.budget;
  if (profile.budget > 0) {
    req->max_tokens += profile.budget;
    req->grammar_file.clear();
  }
}

inline void apply_think_for_request(L2BrainRequest* req) {
  if (req == nullptr) {
    return;
  }
  apply_think_profile(req, think_profile_for(req->phase, false, false));
}

inline L2BrainResult propose_with_think(L2Brain& brain, L2BrainRequest* req,
                                       std::atomic<bool>* cancel = nullptr) {
  apply_think_for_request(req);
  return brain.propose(*req, cancel);
}

}  // namespace tuide
