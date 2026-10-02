#include "ai/l2_graph_query_profile.hpp"

namespace tuide {

const char* registry_match_surface_name(RegistryMatchSurface s) {
  switch (s) {
    case RegistryMatchSurface::CardFull:
      return "card_full";
    case RegistryMatchSurface::Latch:
      return "latch";
    case RegistryMatchSurface::CardAttrs:
      return "card_attrs";
    case RegistryMatchSurface::NodeId:
      return "node_id";
  }
  return "card_full";
}

bool registry_match_surface_parse(const std::string& s, RegistryMatchSurface* out) {
  if (out == nullptr) {
    return false;
  }
  if (s == "card_full" || s == "full" || s.empty()) {
    *out = RegistryMatchSurface::CardFull;
    return true;
  }
  if (s == "latch") {
    *out = RegistryMatchSurface::Latch;
    return true;
  }
  if (s == "card_attrs" || s == "attrs") {
    *out = RegistryMatchSurface::CardAttrs;
    return true;
  }
  if (s == "node_id" || s == "id") {
    *out = RegistryMatchSurface::NodeId;
    return true;
  }
  return false;
}

std::string registry_embed_model_key(const std::string& model, RegistryMatchSurface surface) {
  if (surface == RegistryMatchSurface::CardFull) {
    return model;
  }
  return model + "#surface:" + registry_match_surface_name(surface);
}

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

const char* graph_view_level_name(GraphViewLevel level) {
  switch (level) {
    case GraphViewLevel::Atlas:
      return "atlas";
    case GraphViewLevel::Inspect:
      return "inspect";
    case GraphViewLevel::Deep:
      return "deep";
  }
  return "atlas";
}

bool graph_view_level_parse(const std::string& s, GraphViewLevel* out) {
  if (out == nullptr) {
    return false;
  }
  if (s == "atlas" || s == "simple" || s == "simplificada") {
    *out = GraphViewLevel::Atlas;
    return true;
  }
  if (s == "inspect" || s == "ficha" || s.empty()) {
    *out = GraphViewLevel::Inspect;
    return true;
  }
  if (s == "deep" || s == "profunda" || s == "source") {
    *out = GraphViewLevel::Deep;
    return true;
  }
  return false;
}

GraphViewProfile graph_view_profile_default(GraphViewLevel level) {
  GraphViewProfile p;
  p.level = level;
  switch (level) {
    case GraphViewLevel::Atlas:
      p.max_zones = 12;
      p.max_representatives = 3;
      p.max_edges = 4;
      p.max_trails = 0;
      p.expand_hops = 0;
      p.outline_all_representatives = false;
      p.promote_uncovered = true;
      break;
    case GraphViewLevel::Inspect:
      p.max_zones = 3;
      p.max_representatives = 8;
      p.max_edges = 16;
      p.max_trails = 1;
      p.expand_hops = 2;
      p.outline_all_representatives = false;
      p.promote_uncovered = false;
      break;
    case GraphViewLevel::Deep:
      p.max_zones = 2;
      p.max_representatives = 10;
      p.max_edges = 24;
      p.max_trails = 2;
      p.expand_hops = 2;
      p.outline_all_representatives = true;
      p.promote_uncovered = false;
      break;
  }
  return p;
}

void graph_view_profile_apply(const GraphViewProfile& profile, RegistryCausalJudgeOpts* opts) {
  if (opts == nullptr) {
    return;
  }
  opts->max_zones = profile.max_zones;
  opts->max_representatives = profile.max_representatives;
  opts->max_edges = profile.max_edges;
  opts->max_trails = profile.max_trails;
  opts->expand_hops = profile.expand_hops;
  opts->outline_all_representatives = profile.outline_all_representatives;
  opts->promote_uncovered = profile.promote_uncovered;
}

void graph_query_profile_apply(const GraphQueryProfile& profile, RegistryQueryOpts* opts) {
  if (opts == nullptr) {
    return;
  }
  opts->hops = profile.hops;
  opts->top_k = profile.top_k;
  opts->match_surface = profile.match_surface;
  if (!profile.hop_kinds.empty()) {
    opts->hop_kinds = profile.hop_kinds;
  } else {
    opts->hop_kinds.clear();
  }
  if (profile.match_surface == RegistryMatchSurface::Latch) {
    opts->seed_kinds = {"latch"};
  } else if (profile.match_surface == RegistryMatchSurface::CardAttrs ||
             profile.match_surface == RegistryMatchSurface::CardFull) {
    if (opts->seed_kinds.empty()) {
      opts->seed_kinds = {"fn"};
    }
  }
}

}  // namespace tuide
