#pragma once

#include <atomic>
#include <functional>
#include <string>

#include "ai/ai_types.hpp"
#include "ai/l2_brain.hpp"
#include "ai/l2_context_budget.hpp"
#include "ai/level2_session.hpp"

namespace tuide {

struct Level2AutonomousLoopOpts {
  std::string workspace_root;
  AiLevel2Settings settings;
  // agent|ask|plan|git — selects post-explore machine + prompts.
  std::string workflow = "agent";
  // Optional override; default from Level2Session::tool_guide_markdown().
  std::string system_prompt_extra;
  // If non-empty, replaces the default tool_guide_markdown() block.
  std::string tool_guide_override;
  // Phase-specific user-prompt overlays (appended after the stock phase blurb).
  std::string user_overlay_explore;
  std::string user_overlay_pack;
  std::string user_overlay_edit;
  std::string user_overlay_map_review;
  // Prompt/pack slice sizes for the active backend (default = n_ctx 8192 baseline).
  L2ContextBudget budget = default_l2_context_budget();
  // If true, stop when explore succeeds (pack complete → phase=edit); do not run edit/compile.
  bool stop_at_explore = false;
  // If true, stop after successful a_done (Phase A locate only); skip explore_b auto-plan/pack.
  bool stop_at_phase_a = false;
};

struct Level2AutonomousLoopResult {
  bool ok = false;
  std::string phase;  // done | clarify | edit | …
  std::string summary;
  std::string error;
  int steps = 0;
};

using Level2PhaseLogFn = std::function<void(const std::string& line)>;

// Runs explore → edit → compile (runtime) until done/clarify/cancel/max_steps.
// Streams short phase lines via `log`. Brain proposes JSON; Level2Session executes.
Level2AutonomousLoopResult run_level2_autonomous(Level2Session& session, L2Brain& brain,
                                                 const Level2AutonomousLoopOpts& opts,
                                                 const Level2PhaseLogFn& log,
                                                 std::atomic<bool>* cancel = nullptr);

}  // namespace tuide
