#pragma once

#include <string>
#include <vector>

#include "ai/search_replace.hpp"

namespace tuide {

enum class L2ActionKind {
  Tool,
  Tools,  // ad-hoc batch (legacy / extras)
  Plan,   // watchlist of path:Symbol targets → runtime builds code pack
  Done,
  Edit,
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
  std::string error;
  std::string raw;
};

// Ad-hoc tools batch size (legacy). Plan targets use kL2MaxPlanTargets.
inline constexpr int kL2MaxToolBatch = 4;
inline constexpr int kL2MaxPlanTargets = 16;

L2Action parse_l2_action(const std::string& model_text);

const char* l2_action_kind_name(L2ActionKind kind);

}  // namespace tuide
