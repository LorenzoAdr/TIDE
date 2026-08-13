#pragma once

#include <string>
#include <vector>

#include "ai/search_replace.hpp"

namespace tuide {

enum class L2ActionKind {
  Tool,
  Tools,  // batch: several read tools in one propose
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
  std::vector<L2ToolCall> calls;  // filled for Tools (and optionally mirrored for Tool)
  std::string summary;
  std::string next;  // edit | clarify | empty
  std::vector<SearchReplaceHunk> hunks;
  std::string error;
  std::string raw;
};

// Max read tools per propose (keeps Observations / n_ctx bounded).
inline constexpr int kL2MaxToolBatch = 4;

// Parse a single JSON L2 action from model output (tolerates prose / fences).
L2Action parse_l2_action(const std::string& model_text);

const char* l2_action_kind_name(L2ActionKind kind);

}  // namespace tuide
