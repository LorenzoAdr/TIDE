#pragma once

#include <string>
#include <vector>

#include "ai/l2_explore_a.hpp"
#include "ai/search_replace.hpp"

namespace tuide {

enum class L2ActionKind {
  Tool,
  Tools,  // ad-hoc batch (legacy / extras)
  Plan,   // watchlist of path:Symbol targets → runtime builds code pack
  AJudge, // phase A: verdicts over runtime-supplied peeks
  ATrailJudge, // phase A: interesting/reject over call-stacks
  ADone,  // phase A: stable loci[] → handoff to pack (B)
  F1Done,       // F1 anchor hunt closure (1 primary locus)
  AnchorMiss,   // F1 explicit failure (anchor_miss_v1)
  Done,
  Edit,
  Synthesize,  // ask/plan/git: natural-language answer / plan doc → done
  Error,
  Unknown,
};

struct L2ToolCall {
  std::string name;
  std::string arg;
};

struct L2Action {
  L2ActionKind kind = L2ActionKind::Unknown;
  std::string name;
  std::string arg;
  std::vector<L2ToolCall> calls;
  std::vector<std::string> targets;  // plan: path:Symbol | path:line | path
  std::string summary;
  std::string next;  // edit | clarify | empty
  std::vector<SearchReplaceHunk> hunks;
  std::vector<AVerdict> a_verdicts;  // a_judge
  std::vector<ALocus> a_loci;        // a_done
  bool a_turn_done = false;          // a_judge.done hint (early-stop request)
  std::string f1_failure_reason;
  std::vector<std::string> f1_failure_candidates;
  bool f1_retrieval_needed = false;
  std::string error;
  std::string raw;
};

// Ad-hoc tools batch size (legacy). Plan targets use kL2MaxPlanTargets.
inline constexpr int kL2MaxToolBatch = 4;
inline constexpr int kL2MaxPlanTargets = 16;
// First N plan targets (7B order) are must-keep under pack budget.
inline constexpr int kL2MustPlanTargets = 4;

L2Action parse_l2_action(const std::string& model_text);

const char* l2_action_kind_name(L2ActionKind kind);

}  // namespace tuide
