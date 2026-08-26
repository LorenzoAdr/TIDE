#include "ai/l2_graph_query_profile.hpp"

namespace tuide {

const char* graph_query_phase_name(GraphQueryPhase p) {
  switch (p) {
    case GraphQueryPhase::AnchorHunt:
      return "anchor_hunt";
    case GraphQueryPhase::CausalSurvey:
      return "causal_survey";
    case GraphQueryPhase::CausalDeep:
      return "causal_deep";
  }
  return "anchor_hunt";
}

GraphQueryProfile graph_query_profile_default(GraphQueryPhase phase) {
  GraphQueryProfile p;
  p.phase = phase;
  switch (phase) {
    case GraphQueryPhase::AnchorHunt:
      p.hops = 0;
      p.top_k = 12;
      p.hop_kinds = {"call", "write", "read"};
      p.allow_trails = false;
      break;
    case GraphQueryPhase::CausalSurvey:
      p.hops = 2;
      p.top_k = 16;
      p.hop_kinds = {"call", "write", "read", "handoff", "ctrl"};
      p.allow_trails = true;
      break;
    case GraphQueryPhase::CausalDeep:
      p.hops = 3;
      p.top_k = 20;
      p.hop_kinds = {};
      p.allow_trails = true;
      break;
  }
  return p;
}

void graph_query_profile_apply(const GraphQueryProfile& profile, RegistryQueryOpts* opts) {
  if (opts == nullptr) {
    return;
  }
  opts->hops = profile.hops;
  opts->top_k = profile.top_k;
  if (!profile.hop_kinds.empty()) {
    opts->hop_kinds = profile.hop_kinds;
  } else {
    opts->hop_kinds.clear();
  }
}

}  // namespace tuide
