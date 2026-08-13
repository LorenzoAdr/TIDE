#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ai/search_replace.hpp"
#include "ai/tool_registry.hpp"

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
};

class Level2Session {
 public:
  static constexpr std::size_t kCharBudget = 140000;
  static constexpr int kMaxObservationLines = 200;
  static constexpr int kMaxCompileAttempts = 3;

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

  bool bootstrap(const Level2BootstrapOpts& opts, std::string* err_out = nullptr);

  Level2TurnResult apply_tool(const std::string& workspace_root, const std::string& name,
                              const std::string& arg);
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
    uint64_t last_op_id = 0;
    std::vector<PendingHunk> pending;
  };

  static State load_state(const std::string& workspace_root);
  static bool save_state(const std::string& workspace_root, const State& st, std::string* err);
  static std::string read_file(const std::string& path);
  static bool write_file(const std::string& path, const std::string& body, std::string* err);
  static std::string truncate_observation(const std::string& text, int max_lines);
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
