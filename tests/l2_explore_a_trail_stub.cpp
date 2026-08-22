#include "ai/l2_explore_a.hpp"
#include "ai/l2_effect_summary.hpp"

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

std::vector<ATrailCondBranch> a_trail_build_cond_branches(
    const std::string& /*workspace_root*/, const std::string& /*focus_symbol*/,
    const std::string& /*focus_path_hint*/, const std::vector<std::string>& /*seeds*/,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& /*search*/,
    const std::vector<ATrailStack>& /*stacks*/) {
  return {};
}

bool a_target_matches_verdict_anchor(const std::string& queue_target,
                                     const std::string& verdict_target) {
  return !queue_target.empty() && queue_target == verdict_target;
}

A0TrancheShown a_build_a0_tranche_shown(const std::string& /*workspace_root*/, const AState& st,
                                        int max_cards, const A0TrancheBuildOpts* /*opts*/) {
  A0TrancheShown out;
  if (st.queue.empty() || st.cursor >= static_cast<int>(st.queue.size())) {
    return out;
  }
  const int cap = max_cards > 0 ? max_cards : kA0MaxCardsPerTurn;
  const int n = std::min(cap, static_cast<int>(st.queue.size()) - st.cursor);
  out.slice_n = n;
  for (int i = 0; i < n; ++i) {
    out.items.push_back(st.queue[static_cast<std::size_t>(st.cursor + i)]);
  }
  return out;
}

EffectSummary effect_summary_for_queue_item(const std::string& /*workspace_root*/,
                                            const AQueueItem& item,
                                            const EffectSummaryOpts& /*opts*/) {
  EffectSummary es;
  es.path = item.path;
  es.symbol = item.symbol;
  es.anchor = item.target;
  es.kind = "stub";
  es.card_text = "(stub effect summary)";
  return es;
}

}  // namespace tuide
