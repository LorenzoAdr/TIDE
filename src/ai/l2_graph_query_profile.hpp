#pragma once

#include <string>
#include <vector>

#include "ai/l2_effect_registry.hpp"

namespace tuide {

enum class GraphQueryPhase {
  AnchorHunt,
  CausalSurvey,
  CausalDeep,
};

struct GraphQueryProfile {
  GraphQueryPhase phase = GraphQueryPhase::AnchorHunt;
  int hops = 0;
  int top_k = 12;
  std::vector<std::string> hop_kinds;
  bool allow_trails = false;
  RegistryMatchSurface match_surface = RegistryMatchSurface::CardFull;
};

const char* graph_query_phase_name(GraphQueryPhase p);

GraphQueryProfile graph_query_profile_default(GraphQueryPhase phase);

// Apply profile onto registry query opts (does not overwrite boost_fns).
void graph_query_profile_apply(const GraphQueryProfile& profile, RegistryQueryOpts* opts);

}  // namespace tuide
