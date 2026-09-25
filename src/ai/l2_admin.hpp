#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_brain.hpp"

namespace tuide {

inline constexpr int kAdminMaxEpisodes = 6;
inline constexpr int kAdminEpisodeReplyChars = 4000;
inline constexpr int kAdminMaxProposes = 12;
inline constexpr int kAdminMaxSpawns = 8;
inline constexpr int kAdminMaxExplores = 4;
inline constexpr int kAdminMaxVerifyPasses = 2;
inline constexpr int kAdminVerifyMaxSteps = 4;
inline constexpr int kAdminExploreMaxSteps = 6;
inline constexpr int kAdminExploreMaxGrep = 4;
inline constexpr int kAdminExploreMaxRead = 3;
// Por ola (conservador): varios greps independientes; reads solo anclados a hits.
inline constexpr int kAdminExploreMaxGrepPerWave = 3;
inline constexpr int kAdminExploreMaxReadPerWave = 2;
inline constexpr int kAdminSummaryChars = 4000;
inline constexpr int kAdminNotebookSummaryChars = 2000;
// Prompt del verificador: no recortar el summary del job (ya ≤ kAdminSummaryChars).
// Extra: extracto de log_tail en read/search.
inline constexpr int kAdminVerifySummaryChars = kAdminSummaryChars;
inline constexpr int kAdminVerifyLogTailChars = 2500;
inline constexpr int kAdminVerifyMaxEvidenciaPerJob = 12;
inline constexpr int kAdminVerifyMaxEvidenciaTotal = 64;
inline constexpr int kAdminVerifyMaxFacts = 48;
inline constexpr int kAdminVerifyMaxAnchors = 32;
inline constexpr int kAdminClipHeadChars = 1200;
inline constexpr int kAdminClipTailChars = 1200;
inline constexpr int kAdminWhyMin = 4;
inline constexpr int kAdminWhyMax = 400;
inline constexpr int kAdminReplyMax = 8000;
inline constexpr int kAdminBriefMax = 400;
inline constexpr int kAdminNotebookMaxItems = 24;
inline constexpr int kAdminNotebookFactChars = 240;
inline constexpr int kAdminUiSelectionChars = 400;
inline constexpr int kAdminReadMaxChars = 3000;

enum class AdminDo {
  Invalid,
  Spawn,
  Cerrar,
  AskUser,
  Editar,            // pide pasar a edición → runtime pide confirmación
  ConfirmarEditar,   // segunda pasada: cubre + falta
  SeguirExplorando   // tras confirmación: hueco → explore
};

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
  std::string cubre;  // confirmar_editar: qué del pedido cubre el notebook
  std::string falta;  // confirmar_editar: qué falta (o "nada")
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

struct AdminClarifyTurn {
  std::string question;
  std::string answer;
};

// Episodio cerrado (do=cerrar): conserva hilo entre mensajes del usuario.
struct AdminEpisode {
  std::string consulta;
  std::string reply;
};

struct AdminState {
  std::string consulta;
  std::vector<AdminJob> jobs;
  std::vector<AdminEvidenceItem> notebook;
  AdminSessionUi ui;
  int proposes = 0;
  int spawns = 0;
  // Explores en `jobs` anteriores a esta consulta (no cuentan para kAdminMaxExplores).
  // Al abrir un follow-up con evidencia, se fija al nº actual de explores.
  int explore_jobs_baseline = 0;
  bool done = false;
  bool clarify = false;  // pausa: esperando respuesta del usuario en el panel AI
  std::string pending_question;  // texto de ask_user mientras clarify
  std::vector<AdminClarifyTurn> clarifies;  // Q&A ya respondidas (historial)
  std::vector<AdminEpisode> episodes;  // consultas/replies cerradas (mismo hilo)
  bool awaiting_edit_confirm = false;  // tras do=editar
  bool edit_confirmed = false;         // tras confirmar_editar → spawn edit legal
  bool verify_reject_pending = false;  // tras verificador refuta/dudoso → piloto decide
  int verify_passes = 0;               // verificador adversarial (editar/cerrar)
  std::string last_verify_verdict;     // sostiene|refuta|dudoso
  std::string last_verify_why;
  std::string reply;
  std::string last_error;
  std::string edit_cubre;
  std::string edit_falta;
  std::vector<std::string> legal_hint;
};

struct AdminVerifyResult {
  bool ok = false;
  bool blocks = false;  // refuta|dudoso
  std::string veredicto;  // sostiene|refuta|dudoso
  std::string why;
  std::string report;  // texto para last_error / piloto
  int steps = 0;
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
// true = conservar notebook/episodios (siempre mid-run; el clear es solo boot o Reset).
bool admin_should_keep_session(const AdminState& st, const std::string& message);
bool admin_clear_session(const std::string& workspace_root, std::string* err);
bool admin_save_state(const std::string& workspace_root, const AdminState& st, std::string* err);
bool admin_load_state(const std::string& workspace_root, AdminState* st, std::string* err);

nlohmann::json admin_state_to_json(const AdminState& st);
bool admin_state_from_json(const nlohmann::json& j, AdminState* st, std::string* err);

void admin_notebook_append(AdminState* st, const AdminJob& job, const AdminJobResult& jr);
std::string admin_notebook_markdown(const AdminState& st);
bool admin_notebook_has_path(const AdminState& st, const std::string& path);
std::vector<std::string> admin_notebook_paths(const AdminState& st);

// Explores que cuentan para el tope de esta consulta (jobs totales − baseline).
int admin_count_explore_jobs(const AdminState& st);
int admin_explores_used_this_consulta(const AdminState& st);
// Marca el baseline = explores actuales (presupuesto fresco; conserva jobs/notebook).
void admin_begin_consulta_budgets(AdminState* st);

AdminOla admin_parse(const std::string& raw);
bool admin_legal(const AdminState& st, const AdminOla& ola, int max_proposes, int max_spawns,
                 std::string* err);
std::vector<std::string> admin_legal_dos(const AdminState& st, int max_proposes, int max_spawns);

AdminJobResult admin_explore_stub(const AdminSpawn& spawn);
// Grep del explore: por defecto admin_run_search_rg; el controller puede pasar ToolRegistry.
using AdminGrepFn = std::function<AdminJobResult(const std::string& pattern)>;
// Hijo lite: grep+read vía brain (mismo contrato que tools/l2_wave/explore_lite_local).
AdminJobResult admin_run_explore_lite(const AdminSpawn& spawn, L2Brain& brain,
                                      const std::string& workspace_root,
                                      const AdminLoopOpts& opts,
                                      AdminGrepFn grep_fn = {});
bool admin_shell_cmd_allowed(const std::string& cmd);
std::string admin_clip_output(const std::string& text, bool* truncated, int* raw_bytes);
// Clip con presupuestos explícitos (p.ej. read de archivos grandes: más head+tail).
std::string admin_clip_output(const std::string& text, int head_chars, int tail_chars,
                              bool* truncated, int* raw_bytes);
// Infer typed paths/facts from shell cmd + raw stdout (ls/find/head/tail/wc/…).
void admin_shell_enrich_result(const std::string& cmd, const std::string& captured,
                               const std::string& cwd, AdminJobResult* r);
AdminJobResult admin_run_shell_safe(const std::string& cmd, const std::string& cwd);
// Built-in FS helpers (CLI / fallback when ops unset). Confinadas a workspace_root.
bool admin_path_inside_workspace(const std::string& workspace_root, const std::string& path);
// Resuelve path (relativo o absoluto) bajo root → abs canónico + rel genérico.
// false si escapa del root o root vacío con path absoluto externo.
bool admin_resolve_in_workspace(const std::string& workspace_root, const std::string& path,
                                std::string* abs_out, std::string* rel_out, std::string* err);
bool admin_shell_stays_in_workspace(const std::string& cmd, const std::string& workspace_root,
                                    std::string* err);

AdminJobResult admin_run_search_rg(const std::string& query, const std::string& cwd);
// Parsea salida de search (rg crudo o ToolRegistry: top_files + path:line:col).
void admin_collect_search_hits(const std::string& text, AdminJobResult* r);
AdminJobResult admin_run_read_file(const std::string& target, const std::string& cwd);

// Parte arg de read en targets. "a.hpp,b.hpp" → 2; "a.hpp:5-7,15-17" → 1 (rangos).
std::vector<std::string> admin_split_read_args(const std::string& raw);
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

// Contexto factual del verificador (veredictos + evidencia tipada + hechos;
// sin tesis/prosa del piloto). Usado por admin_run_verify y tests.
std::string admin_verify_context_prompt(const AdminState& st);
// Paths legibles por el verificador: notebook + paths inferidos de evidencia.
std::vector<std::string> admin_verify_readable_paths(const AdminState& st);

// Verificador adversarial (post-explore, pre-aceptar editar/cerrar).
AdminVerifyResult admin_run_verify(AdminState* st, L2Brain& brain, const std::string& thesis,
                                   const std::string& workspace_root, const AdminLoopOpts& opts);

bool admin_apply(AdminState* st, const AdminOla& ola, const AdminOps& ops, std::string* err);

std::string admin_system_prompt();
// workspace_root: proyecto abierto (perímetro FS). Vacío solo en tests sin cwd.
std::string admin_user_prompt(const AdminState& st, int max_proposes, int max_spawns,
                              const std::string& workspace_root = {});

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
