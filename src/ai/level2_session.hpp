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
};

class Level2Session {
 public:
  static constexpr std::size_t kCharBudget = 140000;
  static constexpr int kMaxObservationLines = 200;
  // Compile stderr kept as a short TAIL (errors are usually at the end).
  static constexpr int kMaxCompileLogLines = 40;
  static constexpr int kMaxCompileAttempts = 3;
  // Tighter per-tool observation when several tools run in one propose.
  static constexpr int kMaxObservationLinesBatch = 80;

  explicit Level2Session(Level2SessionDeps deps);
  explicit Level2Session(ToolRegistry* tools);

  static std::string dir_for(const std::string& workspace_root);
  static std::string session_path(const std::string& workspace_root);
  static std::string request_path(const std::string& workspace_root);
  static std::string response_path(const std::string& workspace_root);
  static std::string trace_path(const std::string& workspace_root);
  static std::string state_path(const std::string& workspace_root);
  static std::string pending_edits_path(const std::string& workspace_root);

  static std::string tool_guide_markdown();
  static bool tool_allowed(const std::string& name);

  // Keep first max_lines (tools). Keep last max_lines (compile stderr).
  static std::string truncate_observation(const std::string& text, int max_lines);
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
  Level2TurnResult apply_edit(const std::string& workspace_root,
                              const std::vector<SearchReplaceHunk>& hunks);
  Level2TurnResult run_compile(const std::string& workspace_root);
  Level2TurnResult process_request_file(const std::string& workspace_root);
  Level2TurnResult mark_done(const std::string& workspace_root, const std::string& summary,
                             const std::string& next = {});
  Level2TurnResult rollback_pending(const std::string& workspace_root);

  std::string status_text(const std::string& workspace_root) const;

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
    uint64_t last_op_id = 0;
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
  Level2TurnResult after_successful_edit(const std::string& workspace_root, State st);
};

}  // namespace tuide
