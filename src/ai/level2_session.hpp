#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"
#include "ai/l2_action.hpp"

namespace tuide {

struct Level2BootstrapOpts {
  std::string workspace_root;
  std::string query;
  std::string instruction;
  std::vector<std::string> seeds;
  std::string map_path;
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
  static constexpr int kPostPackEditNudgeAfter = 8;
  // Hard reject further extras after this many post-pack tools (Instruction cubierta).
  static constexpr int kPostPackEditPushbackAfter = 12;
  // In phase=edit: nudge after this many tools, then reject further tools.
  static constexpr int kEditPhaseToolNudgeAfter = 2;
  static constexpr int kEditPhaseToolPushbackAfter = 5;
  // Soft nudge after this many consecutive plans with a covering pack (no tools).
  static constexpr int kRepeatedPlanEditNudgeAfter = 2;
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

  static std::string dir_for(const std::string& workspace_root);
  static std::string session_path(const std::string& workspace_root);
  static std::string request_path(const std::string& workspace_root);
  static std::string response_path(const std::string& workspace_root);
  static std::string trace_path(const std::string& workspace_root);
  static std::string state_path(const std::string& workspace_root);
  static std::string pending_edits_path(const std::string& workspace_root);
  static std::string map_initial_path(const std::string& workspace_root);
  static std::string pack_path(const std::string& workspace_root);

  static std::string tool_guide_markdown();
  static bool tool_allowed(const std::string& name);

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
  Level2TurnResult rollback_pending(const std::string& workspace_root);

  std::string status_text(const std::string& workspace_root) const;
  // One-line flags for harness CLI (avoids extra `status` roundtrips).
  std::string status_flags(const std::string& workspace_root) const;

 private:
  Level2SessionDeps deps_;

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
    int edit_attempt = 0;
    int compile_attempt = 0;
    int clarify_pushback = 0;
    int pack_incomplete_pushback = 0;
    int explore_tool_count = 0;       // tools before first successful plan
    bool plan_nudge_sent = false;     // soft nudge after N explore tools without plan
    int post_pack_tool_count = 0;     // tools after pack, still in explore
    bool edit_nudge_sent = false;     // soft nudge to done next=edit / edit
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
    uint64_t last_op_id = 0;
    std::vector<std::string> watchlist;  // merged plan targets
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
