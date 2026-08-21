#include "ai/l2_explore_a.hpp"

namespace tuide {

// Stub for unit tests that only exercise trail state/judge (no TS parse).
ATrailHop a_trail_enrich_hop(const std::string& abs_path, const std::string& rel_path,
                             int call_line, const std::string& called_symbol) {
  ATrailHop h;
  h.path = rel_path.empty() ? abs_path : rel_path;
  h.call_line = call_line;
  h.symbol = called_symbol;
  h.anchor = h.path + ":" + (called_symbol.empty() ? std::to_string(call_line) : called_symbol);
  h.snippet = "(stub hop)\n";
  h.summary = "stub";
  h.is_call_site = true;
  return h;
}

std::vector<ATrailStack> a_trail_build_stacks(const std::string& /*workspace_root*/,
                                              const std::string& /*focus_symbol*/,
                                              const std::string& /*focus_path_hint*/,
                                              const std::vector<ATrailSearchHit>& /*search_hits*/,
                                              int /*max_stacks*/, int /*max_depth*/) {
  return {};
}

std::vector<ATrailStack> a_trail_build_full_stacks(
    const std::string& /*workspace_root*/, const std::string& /*focus_symbol*/,
    const std::string& /*focus_path_hint*/,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& /*search*/,
    int /*max_stacks*/, int /*max_depth*/) {
  return {};
}

}  // namespace tuide
