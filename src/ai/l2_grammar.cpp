#include "ai/l2_grammar.hpp"

#include <filesystem>

#include "ai/l2_feat.hpp"

namespace fs = std::filesystem;

namespace tuide {
namespace l2_grammar {

std::string resolve_for_phase(const std::string& workspace_root, const std::string& phase) {
  if (!l2_feat::enabled("JSON_GRAMMAR")) {
    return {};
  }
  if (workspace_root.empty()) {
    return {};
  }
  if (phase != "edit" && phase != "compile") {
    return {};
  }
  const char* file = "l2_edit.gbnf";
  const fs::path path = fs::path(workspace_root) / "tools/l2_battery/grammars" / file;
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return {};
  }
  return fs::absolute(path).string();
}

}  // namespace l2_grammar
}  // namespace tuide
