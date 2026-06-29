#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct SemanticTokenSpan {
  int start_col = 0;
  int length = 0;
  int type = 0;
  int modifiers = 0;
};

struct SemanticTokenDocument {
  std::vector<std::string> token_types;
  std::vector<std::vector<SemanticTokenSpan>> lines;
  bool ready = false;
  uint64_t source_generation = 0;
};

}  // namespace tgdb
