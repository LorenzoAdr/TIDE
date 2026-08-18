#pragma once

#include <functional>
#include <string>

#include "ai/ai_types.hpp"

namespace tuide {

struct Level0IntentMatch {
  bool ok = false;
  bool is_task = false;
  std::string name;
  std::string arg_policy;
  float score = 0.0f;
  float margin = 0.0f;
};

// Optional semantic matcher (embeddings). When null, L0 uses keyword heuristics.
using Level0SemanticMatcher = std::function<Level0IntentMatch(const std::string& query)>;

// Deterministic router: slash commands + structural heuristics + optional semantic intents.
// previous_tool / previous_arg: último tool L0 (p.ej. git_status + "tests") para
// follow-ups ("y dentro de src?", "esos archivos").
AiRouteResult route_level0(const std::string& input,
                           const std::string& previous_tool = {},
                           const std::string& previous_arg = {},
                           const Level0SemanticMatcher& semantic = {});

}  // namespace tuide
