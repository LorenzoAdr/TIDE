#pragma once

#include <string>
#include <vector>

#include "ai/level2_autonomous_loop.hpp"

namespace tuide {

// One runtime-grounded fact. `tag` is a stable label; `detail` is the Spanish line.
struct Level2DebriefFact {
  std::string tag;
  std::string detail;
};

struct Level2Debrief {
  // success | clarify | cancelled | compile_fail | error | incomplete
  std::string outcome_tag;
  std::vector<Level2DebriefFact> facts;
};

// Build facts from loop result + state.json + session Observations + l2/trace.ndjson.
Level2Debrief build_level2_debrief(const std::string& workspace_root,
                                   const Level2AutonomousLoopResult& result);

// Deterministic markdown (always shown in the AI tab).
std::string format_level2_debrief(const Level2Debrief& debrief);

}  // namespace tuide
