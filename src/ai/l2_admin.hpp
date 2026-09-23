#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_brain.hpp"

namespace tuide {

inline constexpr int kAdminMaxProposes = 12;
inline constexpr int kAdminMaxSpawns = 8;
inline constexpr int kAdminSummaryChars = 4000;
inline constexpr int kAdminClipHeadChars = 1200;
inline constexpr int kAdminClipTailChars = 1200;
inline constexpr int kAdminWhyMin = 4;
inline constexpr int kAdminWhyMax = 400;
inline constexpr int kAdminReplyMax = 2000;
inline constexpr int kAdminBriefMax = 400;
inline constexpr int kAdminNotebookMaxItems = 24;
inline constexpr int kAdminNotebookFactChars = 240;
inline constexpr int kAdminUiSelectionChars = 400;
inline constexpr int kAdminReadMaxChars = 3000;

enum class AdminDo { Invalid, Spawn, Cerrar, AskUser };

enum class AdminSpawnTipo {
  Invalid,
  Explore,
  Build,
  Git,
  Shell,
  Search,
  Read,
  Diagnostics,
  Test,
  Edit,
  Web,
  WebFetch
};

struct AdminSpawn {
  AdminSpawnTipo tipo = AdminSpawnTipo::Invalid;
  std::string brief;
  std::string arg;
  // edit only
  std::string search;
  std::string replace;
};

struct AdminOla {
  bool ok = false;
  AdminDo do_kind = AdminDo::Invalid;
  AdminSpawn spawn;
  std::string why;
  std::string reply;
  std::string error;
  std::string raw_json;
};

struct AdminJob {
  int id = 0;
  std::string tipo;
  bool ok = false;
  std::string summary;
  std::string veredicto;
  std::vector<std::string> simbolos;
  std::vector<std::string> evidencia;
  std::string log_tail;
  bool truncated = false;
  int raw_bytes = 0;
};

// Accumulated session evidence — the "plan" without plan mode.
struct AdminEvidenceItem {
  int job_id = 0;
  std::string tipo;
  std::string summary;
  std::vector<std::string> paths;
  std::vector<std::string> simbolos;
  std::vector<std::string> facts;
};

struct AdminSessionUi {
  std::string active_path;
  int cursor_line = -1;
  std::string selection;
  std::string git_branch;
};

struct AdminState {
  std::string consulta;
  std::vector<AdminJob> jobs;
  std::vector<AdminEvidenceItem> notebook;
  AdminSessionUi ui;
  int proposes = 0;
  int spawns = 0;
  bool done = false;
  bool clarify = false;
  std::string reply;
  std::string last_error;
  std::vector<std::string> legal_hint;
};

struct AdminJobResult {
  bool ok = false;
  std::string summary;
  std::string veredicto;
  std::vector<std::string> simbolos;
  std::vector<std::string> evidencia;
  std::vector<std::string> paths;
  std::vector<std::string> facts;
  std::string log_tail;
  std::string error;
  bool truncated = false;
  int raw_bytes = 0;
};

struct AdminOps {
  std::function<AdminJobResult(const AdminSpawn&)> run_explore;
  std::function<AdminJobResult(const AdminSpawn&)> run_build;
  std::function<AdminJobResult(const AdminSpawn&)> run_git;
  std::function<AdminJobResult(const AdminSpawn&)> run_shell;
  std::function<AdminJobResult(const AdminSpawn&)> run_search;
  std::function<AdminJobResult(const AdminSpawn&)> run_read;
  std::function<AdminJobResult(const AdminSpawn&)> run_diagnostics;
  std::function<AdminJobResult(const AdminSpawn&)> run_test;
  std::function<AdminJobResult(const AdminSpawn&)> run_edit;
  std::function<AdminJobResult(const AdminSpawn&)> run_web;
  std::function<AdminJobResult(const AdminSpawn&)> run_web_fetch;
};

struct AdminLoopOpts {
  std::string workspace_root;
  AiLevel2Settings settings;
  AdminSessionUi ui;
  int max_proposes = kAdminMaxProposes;
  int max_spawns = kAdminMaxSpawns;
  std::function<void(const std::string&)> on_line;
  std::atomic<bool>* cancel = nullptr;
};

struct AdminLoopResult {
  bool ok = false;
  bool clarify = false;
  std::string reply;
  std::string error;
  int proposes = 0;
  int spawns = 0;
};

const char* admin_do_name(AdminDo d);
const char* admin_spawn_tipo_name(AdminSpawnTipo t);
AdminSpawnTipo admin_spawn_tipo_parse(const std::string& s);

std::string admin_dir(const std::string& workspace_root);
std::string admin_state_path(const std::string& workspace_root);
std::string admin_notebook_path(const std::string& workspace_root);
bool admin_is_continuable(const std::string& workspace_root);
bool admin_clear_session(const std::string& workspace_root, std::string* err);
bool admin_save_state(const std::string& workspace_root, const AdminState& st, std::string* err);
bool admin_load_state(const std::string& workspace_root, AdminState* st, std::string* err);

nlohmann::json admin_state_to_json(const AdminState& st);
bool admin_state_from_json(const nlohmann::json& j, AdminState* st, std::string* err);

void admin_notebook_append(AdminState* st, const AdminJob& job, const AdminJobResult& jr);
std::string admin_notebook_markdown(const AdminState& st);
bool admin_notebook_has_path(const AdminState& st, const std::string& path);
std::vector<std::string> admin_notebook_paths(const AdminState& st);

AdminOla admin_parse(const std::string& raw);
bool admin_legal(const AdminState& st, const AdminOla& ola, int max_proposes, int max_spawns,
                 std::string* err);
std::vector<std::string> admin_legal_dos(const AdminState& st, int max_proposes, int max_spawns);

AdminJobResult admin_explore_stub(const AdminSpawn& spawn);
bool admin_shell_cmd_allowed(const std::string& cmd);
std::string admin_clip_output(const std::string& text, bool* truncated, int* raw_bytes);
// Infer typed paths/facts from shell cmd + raw stdout (ls/find/head/tail/wc/…).
void admin_shell_enrich_result(const std::string& cmd, const std::string& captured,
                               const std::string& cwd, AdminJobResult* r);
AdminJobResult admin_run_shell_safe(const std::string& cmd, const std::string& cwd);
// Built-in FS helpers (CLI / fallback when ops unset).
AdminJobResult admin_run_search_rg(const std::string& query, const std::string& cwd);
AdminJobResult admin_run_read_file(const std::string& target, const std::string& cwd);
AdminJobResult admin_run_diagnostics_stub(const AdminSpawn& spawn);
AdminJobResult admin_run_test_stub(const AdminSpawn& spawn);
AdminJobResult admin_run_edit_file(const AdminSpawn& spawn, const AdminState& st,
                                  const std::string& cwd);
// Internet search (Brave si TUIDE_WEB_SEARCH_API_KEY/BRAVE_API_KEY; si no DDG HTML;
// TUIDE_WEB_SEARCH_STUB=1 fuerza stub determinista).
AdminJobResult admin_run_web_search(const std::string& query);
AdminJobResult admin_run_web_search_stub(const std::string& query);
// Fetch HTTP(S) body for a URL already in notebook paths (anclado).
AdminJobResult admin_run_web_fetch(const std::string& url, const AdminState& st);
AdminJobResult admin_run_web_fetch_stub(const std::string& url);
bool admin_url_fetch_allowed(const std::string& url);

bool admin_apply(AdminState* st, const AdminOla& ola, const AdminOps& ops, std::string* err);

std::string admin_system_prompt();
std::string admin_user_prompt(const AdminState& st, int max_proposes, int max_spawns);

AdminLoopResult run_admin_loop(AdminState* st, L2Brain& brain, const AdminOps& ops,
                               const AdminLoopOpts& opts);

class AdminScriptedBrain : public L2Brain {
 public:
  explicit AdminScriptedBrain(std::vector<std::string> script);
  std::string name() const override;
  bool ensure_ready(const AiSettings& settings,
                    const std::function<void(const std::string&)>& on_progress,
                    std::string* error) override;
  L2BrainResult propose(const L2BrainRequest& req, std::atomic<bool>* cancel) override;

 private:
  std::vector<std::string> script_;
  std::size_t idx_ = 0;
};

}  // namespace tuide
