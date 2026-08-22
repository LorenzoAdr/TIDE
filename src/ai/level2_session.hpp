#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"
#include "ai/l2_action.hpp"
#include "ai/l2_context_budget.hpp"

namespace tuide {

struct SymbolIndexSnapshot;

struct Level2BootstrapOpts {
  std::string workspace_root;
  std::string query;
  std::string instruction;
  std::vector<std::string> seeds;
  std::string map_path;
  // agent|ask|plan|git (default agent = legacy edit/compile machine).
  std::string workflow = "agent";
  // Prebuilt markdown for Git workflow (## Git context body without heading).
  std::string git_context;
  // If non-empty, written as pack.md and State.has_pack=true after bootstrap.
  std::string seed_pack_markdown;
  // Optional JSON from L1 semantic distillation (bridges ES query → EN search_terms).
  std::string distilled_intent_json;
  // Directory prefixes (empty = unrestricted). Used for session prompt hint.
  std::vector<std::string> path_scope;
};

struct Level2TurnResult {
  bool ok = false;
  std::string action;
  std::string name;
  std::string arg;
  std::string summary;
  std::string error;
  int turn = 0;
  std::size_t session_chars = 0;
  std::string phase;
};

struct Level2SessionDeps {
  ToolRegistry* tools = nullptr;
  // After a successful on-disk hunk apply (optional buffer/journal sync).
  std::function<void(const ApplyHunkResult&)> sync_edit;
  // Returns process exit code; fills combined stdout/stderr.
  std::function<int(std::string* combined_output)> run_compile;
  // Reject clarify this many times (force more code tools) before accepting. 0 = never push back.
  int clarify_pushback_max = 3;
  // Reject done next=edit while pack has Instruction gaps or no pack (default 2).
  int pack_incomplete_pushback_max = 2;
  // Live accessor for AI path_scope (empty = unrestricted).
  std::function<const std::vector<std::string>&()> path_scope_fn;
  // Optional symbol index for Effect Summary callers: (A0).
  std::function<std::shared_ptr<const SymbolIndexSnapshot>()> symbol_snapshot_fn;
};

class Level2Session {
 public:
  static constexpr std::size_t kCharBudget = 140000;
  static constexpr int kMaxObservationLines = 200;
  // Compile stderr kept as a short TAIL (errors are usually at the end).
  static constexpr int kMaxCompileLogLines = 40;
  static constexpr int kMaxCompileAttempts = 3;
  static constexpr int kMaxObservationLinesBatch = 80;
  // After a pack exists, each tool dump is much smaller (section cap is ~8k).
  static constexpr int kMaxObservationLinesPacked = 40;
  static constexpr std::size_t kMaxObservationCharsPerTurnPacked = 2400;
  // Soft nudge after this many explore tools without a plan.
  static constexpr int kExplorePlanNudgeAfter = 8;
  // Soft nudge after this many extras once pack covers Instruction (no gaps).
  static constexpr int kPostPackEditNudgeAfter = 2;
  // Hard reject further extras after this many post-pack tools (Instruction cubierta).
  static constexpr int kPostPackEditPushbackAfter = 4;
  // In phase=edit: nudge after this many tools, then reject further tools.
  static constexpr int kEditPhaseToolNudgeAfter = 2;
  static constexpr int kEditPhaseToolPushbackAfter = 5;
  // Soft nudge after this many consecutive plans with a covering pack (no tools).
  static constexpr int kRepeatedPlanEditNudgeAfter = 2;
  // Hard pivot: N covering plans in a row → loop force_phase_edit (modelo no emite done/edit).
  static constexpr int kRepeatedPlanEditPushbackAfter = 2;
  // Re-edits of an already-covered Instruction path while gaps remain, then stop.
  static constexpr int kMaxCoveredPathRejects = 3;
  // Identical failed edit fingerprint repeats before forcing clarify.
  static constexpr int kMaxIdenticalEditRepeats = 2;
  // Total edit apply failures (any cause) before forcing clarify.
  static constexpr int kMaxEditApplyFails = 5;
  // Reject Search/Replace searches shorter than this (too generic / ambiguous).
  static constexpr std::size_t kMinEditSearchChars = 24;
  // After a pack exists, keep Observations under this (prompt uses a short tail anyway).
  static constexpr std::size_t kMaxObservationCharsPacked = 8000;
  // Code pack for plan targets (fragments + outlines) kept under this size.
  static constexpr std::size_t kMaxPackChars = 9000;
  // Soft cap per fragment (~25% of pack) so one huge body cannot dominate.
  static constexpr std::size_t kMaxFragShareChars = 2250;

  explicit Level2Session(Level2SessionDeps deps);
  explicit Level2Session(ToolRegistry* tools);

  void set_context_budget(L2ContextBudget budget);
  const L2ContextBudget& context_budget() const { return budget_; }
  // If pack.md exceeds budget.pack_chars * 1.2, rewrite a compact head+tail view.
  bool maybe_trim_pack_to_budget(const std::string& workspace_root, std::string* summary = nullptr);

  static std::string dir_for(const std::string& workspace_root);
  static std::string session_path(const std::string& workspace_root);
  static std::string request_path(const std::string& workspace_root);
  static std::string response_path(const std::string& workspace_root);
  static std::string trace_path(const std::string& workspace_root);
  static std::string state_path(const std::string& workspace_root);
  static std::string pending_edits_path(const std::string& workspace_root);
  static std::string map_initial_path(const std::string& workspace_root);
  static std::string pack_path(const std::string& workspace_root);
  static std::string answer_path(const std::string& workspace_root);
  // Phase A locate artifacts (L2_EXPLORE_PHASE_A).
  static std::string a_state_path(const std::string& workspace_root);
  static std::string a_notes_path(const std::string& workspace_root);

  // True when a prior L2 run finished (done/clarify) and may accept follow-ups without bootstrap.
  static bool is_continuable(const std::string& workspace_root);
  // Wipe L2 artifacts so the next NL message bootstraps a fresh session.
  static bool clear_session(const std::string& workspace_root, std::string* err_out = nullptr);
  // Reopen a continuable session for another user turn (preserves pack/answer/observations).
  static bool reopen_for_followup(const std::string& workspace_root, const std::string& user_text,
                                  const std::string& workflow_opt = {},
                                  std::string* err_out = nullptr);
  // Mark session continuable after a successful done/clarify close (idempotent).
  static bool mark_continuable(const std::string& workspace_root, std::string* err_out = nullptr);
  // Prior answer.md + pack summary for resume prompts (budgeted).
  static std::string resume_context_markdown(const std::string& workspace_root,
                                             std::size_t max_chars = 6000);

  static std::string tool_guide_markdown();
  // Short system for phase=edit (no explore table) — A+B+D lean prompt.
  static std::string tool_guide_edit_markdown();
  // Compact explore system when EDIT_LEAN_PROMPT (avoids n_ctx blow on ranked map).
  static std::string tool_guide_explore_markdown();
  static std::string tool_guide_explore_a_markdown();
  static std::string tool_guide_explore_b_markdown();
  static bool tool_allowed(const std::string& name);
  // Phase-aware: with L2_EXPLORE_PHASE_A, explore_a/b only allow local (non-LSP) tools.
  static bool tool_allowed_in_phase(const std::string& name, const std::string& phase);

  // Last compile_feedback or edit_feedback turn, whole block, capped (not a byte-tail).
  static std::string last_edit_relevant_observation(const std::string& session_md,
                                                    std::size_t max_chars = 1600);
  // Drop sibling "on disk" excerpts that are not the failed hunk path.
  static std::string strip_unrelated_on_disk_excerpts(const std::string& obs);

  // Keep first max_lines (tools). Keep last max_lines (compile stderr).
  // max_chars > 0 also hard-caps the result (post-pack per-turn budget).
  static std::string truncate_observation(const std::string& text, int max_lines,
                                          std::size_t max_chars = 0);
  static std::string truncate_observation_tail(const std::string& text, int max_lines);

  bool bootstrap(const Level2BootstrapOpts& opts, std::string* err_out = nullptr);

  // After explore tools (or when entering edit): shrink ## Ranked map to name lines,
  // keeping full entry detail only for stems/paths L2 already touched in Observations.
  // Also drops ## Bodies (code lives in Observations). Idempotent.
  static bool compact_session_context(const std::string& workspace_root,
                                      std::string* err_out = nullptr);

  // Pure helpers (also used by tests).
  static std::string compact_ranked_map_markdown(const std::string& map_section,
                                                 const std::vector<std::string>& hot_keys);
  static std::vector<std::string> hot_keys_from_observations(const std::string& observations);

  Level2TurnResult apply_tool(const std::string& workspace_root, const std::string& name,
                              const std::string& arg);
  // Several read tools in one turn (max kL2MaxToolBatch). Compacts map once at the end.
  Level2TurnResult apply_tools(const std::string& workspace_root,
                               const std::vector<L2ToolCall>& calls);
  // Watchlist → runtime fetches all targets + per-file outlines into pack.md (budgeted).
  Level2TurnResult apply_plan(const std::string& workspace_root,
                              const std::vector<std::string>& targets,
                              const std::string& summary = {});
  Level2TurnResult apply_edit(const std::string& workspace_root,
                              const std::vector<SearchReplaceHunk>& hunks);
  Level2TurnResult run_compile(const std::string& workspace_root);
  Level2TurnResult process_request_file(const std::string& workspace_root);
  Level2TurnResult mark_done(const std::string& workspace_root, const std::string& summary,
                             const std::string& next = {});
  // Force phase=edit without pack_incomplete pushback (auto-promote when model emits edit).
  Level2TurnResult force_phase_edit(const std::string& workspace_root, const std::string& reason);
  // Ask/Plan/Git: write natural-language answer (or plan doc) and finish the session.
  Level2TurnResult apply_synthesize(const std::string& workspace_root, const std::string& text);
  Level2TurnResult rollback_pending(const std::string& workspace_root);

  // Phase A (locate): seed peek queue, judge peeks, close with loci[] → explore_b.
  // No pack.md writes. Behind L2_EXPLORE_PHASE_A (callers should gate).
  Level2TurnResult seed_a_queue(const std::string& workspace_root,
                                const std::vector<AQueueBuildInput>& ranked,
                                const AQueueBuildOpts& opts = {});
  Level2TurnResult apply_a_judge(const std::string& workspace_root,
                                 const std::vector<AVerdict>& verdicts,
                                 bool turn_done_hint = false);
  Level2TurnResult apply_a_trail_judge(const std::string& workspace_root,
                                       const std::vector<AVerdict>& verdicts);
  Level2TurnResult apply_a_done(const std::string& workspace_root,
                                const std::vector<ALocus>& loci,
                                const std::string& summary = {});
  // Phase B miss → allow paths for a short micro-A / plan outside original loci (capa 4).
  Level2TurnResult allow_micro_a_paths(const std::string& workspace_root,
                                       const std::vector<std::string>& paths);
  // Next ≤5 peek bodies for the explore_a prompt (ephemeral; not persisted as pack).
  // When trail is active, returns call-stack markdown instead of queue peeks.
  std::string build_a_peek_tranche_markdown(const std::string& workspace_root,
                                            int max_peeks = kAMaxPeeksPerTurn);
  // Refresh pending_stacks for active trail (search + TS hops).
  bool refresh_a_trail_stacks(const std::string& workspace_root, AState* ast, std::string* err);
  static AState load_a_state(const std::string& workspace_root);
  static bool save_a_state(const std::string& workspace_root, const AState& st, std::string* err);

  // Semantic pack review (PACK_REVIEW): persist verdict for explore close gate.
  Level2TurnResult mark_pack_review(const std::string& workspace_root, bool ok,
                                    const std::string& summary);
  Level2TurnResult add_review_search_terms(const std::string& workspace_root,
                                           const std::vector<std::string>& terms);
  Level2TurnResult add_rejected_targets(const std::string& workspace_root,
                                        const std::vector<std::string>& targets);
  // Drop low-value watchlist entries after pack_review miss; persist rejected_targets.
  Level2TurnResult prune_watchlist_after_review(const std::string& workspace_root,
                                                const std::vector<std::string>& reject_extra);
  // Remove denylist entries that match protected anchors (map/seeds).
  Level2TurnResult unreject_matching(const std::string& workspace_root,
                                     const std::vector<std::string>& protect);
  // Replace watchlist with priority targets (purge noise) and optionally reset review cycles.
  Level2TurnResult reset_watchlist_priority(const std::string& workspace_root,
                                            const std::vector<std::string>& priority_targets,
                                            bool reset_review_cycles);

  std::string status_text(const std::string& workspace_root) const;
  // One-line flags for harness CLI (avoids extra `status` roundtrips).
  std::string status_flags(const std::string& workspace_root) const;

 private:
  Level2SessionDeps deps_;
  L2ContextBudget budget_ = default_l2_context_budget();

  struct PendingHunk {
    std::string path;
    std::string abs_path;
    std::string old_text;
    std::string new_text;
    std::string before;
  };

  struct State {
    int turn = 0;
    bool done = false;
    std::string last_action;
    std::string phase = "explore";
    std::string workflow = "agent";  // agent|ask|plan|git
    int edit_attempt = 0;
    int compile_attempt = 0;
    int clarify_pushback = 0;
    int pack_incomplete_pushback = 0;
    int explore_tool_count = 0;       // tools before first successful plan
    bool plan_nudge_sent = false;     // soft nudge after N explore tools without plan
    int post_pack_tool_count = 0;     // tools after pack, still in explore
    bool edit_nudge_sent = false;     // soft nudge to done next=edit / edit
    std::vector<std::string> seen_tool_keys;  // name\\targ already fetched this session
    int edit_phase_tool_count = 0;    // tools while already in phase=edit
    bool edit_phase_nudge_sent = false;
    int consecutive_complete_plans = 0;  // plans while pack covers Instruction
    int edit_fail_count = 0;          // apply failures in this edit phase
    int identical_edit_repeats = 0;   // consecutive identical failed fingerprints
    std::string last_failed_edit_fp;  // fingerprint of last failed hunk batch
    bool has_pack = false;
    bool pack_incomplete = false;  // Instruction gaps vs pack (not mere truncation)
    bool map_stale = false;        // map_last query poorly overlaps session Instruction
    bool map_review = false;  // after compile_ok: full map restored, ask "algo más?"
    bool continuable = false;  // after done/clarify: Enter reopens instead of bootstrap
    bool resume = false;       // true for the duration of a follow-up autonomous run
    int followup_count = 0;    // number of user follow-ups appended
    uint64_t last_op_id = 0;
    std::vector<std::string> watchlist;  // merged plan targets
    std::vector<std::string> edited_paths;  // unique rel paths successfully applied
    // Concat of applied new_text (for Instruction marker coverage); capped.
    std::string applied_blob;
    int coverage_gate_pushback = 0;  // done/compile coverage rejects this session
    int covered_path_rejects = 0;    // consecutive edit_covered_path while gaps remain
    bool pack_review_ok = false;     // L2 semantic pack review passed (PACK_REVIEW)
    int pack_review_cycles = 0;      // review→expand cycles this session
    std::vector<std::string> review_search_terms;  // review expand grep terms used
    std::vector<std::string> rejected_targets;     // path:Symbol denylist (no re-fetch / re-plan)
    std::vector<PendingHunk> pending;
  };

  static State load_state(const std::string& workspace_root);
  static bool save_state(const std::string& workspace_root, const State& st, std::string* err);
  static std::string read_file(const std::string& path);
  static bool write_file(const std::string& path, const std::string& body, std::string* err);
  static std::string trim_session_body(std::string body);
  static void append_trace(const std::string& workspace_root, const std::string& json_line);
  static void write_response_json(const std::string& workspace_root, bool ok,
                                  const std::string& action, const std::string& name,
                                  const std::string& arg, const std::string& text,
                                  const std::string& error, int turn, const std::string& phase = {});

  bool append_observation(const std::string& workspace_root, const std::string& block,
                          std::size_t* session_chars, std::string* err);
  void compact_observations_after_pack(const std::string& workspace_root, const State& st,
                                       std::size_t* session_chars);
  Level2TurnResult after_successful_edit(const std::string& workspace_root, State st);
  // Increments explore/post-pack counters; returns observation block if a nudge fires.
  static std::string maybe_tool_nudge(State& st, int tools_added);
};

}  // namespace tuide
