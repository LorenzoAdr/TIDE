#pragma once

#include <string>
#include <vector>

#include "ai/search_replace.hpp"

namespace tuide {

enum class L2ActionKind {
  Tool,
  Done,
  Edit,
  Error,
  Unknown,
};

struct L2Action {
  L2ActionKind kind = L2ActionKind::Unknown;
  std::string name;
  std::string arg;
  std::string summary;
  std::string next;  // edit | clarify | empty
  std::vector<SearchReplaceHunk> hunks;
  std::string error;
  std::string raw;
};

// Parse a single JSON L2 action from model output (tolerates prose / fences).
L2Action parse_l2_action(const std::string& model_text);

const char* l2_action_kind_name(L2ActionKind kind);

}  // namespace tuide
