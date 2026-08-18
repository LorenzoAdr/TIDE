#pragma once

#include <string>
#include <vector>

namespace tuide {

enum class Level1ActionKind {
  Tool,
  Seeds,
  PickStem,
  Final,
  NeedsLevel2,
  Error,
  Unknown,
};

struct Level1Action {
  Level1ActionKind kind = Level1ActionKind::Unknown;
  std::string tool_name;
  std::string arg;
  std::vector<std::string> seeds;
  std::string stem;  // PickStem: basename chosen from shortlist
  std::string text;
  std::string instruction;
  std::string raw;
};

// Parse a single JSON action object from model output (tolerates prose around JSON).
Level1Action parse_level1_action(const std::string& model_text);

}  // namespace tuide
