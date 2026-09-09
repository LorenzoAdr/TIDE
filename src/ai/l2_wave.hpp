#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace tuide {

inline constexpr int kWaveMaxHits = 32;
inline constexpr int kWaveMaxNeedles = 12;
inline constexpr int kWaveMaxPeeks = 3;
inline constexpr int kWaveMaxFollows = 3;
inline constexpr int kWavePeekChars = 8000;
inline constexpr int kWaveFollowChars = 2000;
inline constexpr int kWaveFollowTreeChars = 12000;
inline constexpr int kWaveInBodyChars = 6000;
inline constexpr int kWaveMaxWaves = 8;
inline constexpr int kWaveMaxMentions = 16;
inline constexpr int kWaveMaxOutgoing = 12;
inline constexpr int kWaveCoverKeepMax = 3;
inline constexpr int kWaveMaxFichaPerZone = 12;
inline constexpr int kWaveWorkChars = 24000;
inline constexpr int kWaveWorkPeekChars = 2400;
inline constexpr int kWaveWorkFollowChars = 4000;
inline constexpr int kWaveWorkInChars = 2000;
inline constexpr int kWaveCoverPeekMax = 4;
inline constexpr int kWavePeekNeighborMax = 4;
inline constexpr int kWavePeekHopDepth = 2;
inline constexpr int kWavePeekExpandCap = 6;
inline constexpr int kWaveSketchRaysPerPeek = 2;
inline constexpr int kWaveSketchFanInMax = 2;
inline constexpr int kWaveSketchEdgesMax = 12;
inline constexpr int kWaveCercaMaxNeedles = 6;
inline constexpr int kWaveCercaMinNeedles = 1;
inline constexpr int kWaveCercaMaxHits = 8;
inline constexpr int kWaveCercaHopsDefault = 1;
inline constexpr int kWaveCercaHopsMax = 2;
inline constexpr int kWaveCercaMaxSeeds = 12;
inline constexpr int kWaveGuionMin = 2;
inline constexpr int kWaveGuionMax = 6;
inline constexpr int kWaveGuionWordsMin = 2;
inline constexpr int kWaveGuionWordsMax = 16;
inline constexpr int kWaveGuionPapelChars = 120;
inline constexpr int kWaveIndependienteMaxWaves = 6;
inline constexpr int kWaveControlMaxJobs = 3;
inline constexpr int kWaveControlLatePlanJobs = 1;
inline constexpr int kWaveControlEvitaMax = 3;
inline constexpr int kWaveControlMaxConsultas = 1;
inline constexpr int kWaveControlPlanFasesMin = 2;
inline constexpr int kWaveControlPlanFasesMax = 4;
inline constexpr int kWaveControlPlanPasosMin = kWaveControlPlanFasesMin;
inline constexpr int kWaveControlPlanPasosMax = kWaveControlPlanFasesMax;
inline constexpr int kWaveControlSeguirHops = 8;
inline constexpr int kWaveControlInheritVistoMax = 12;
inline constexpr int kWaveControlMaxAmpliar = 3;
inline constexpr int kWaveControlMaxAmpliarTurns = 2;
inline constexpr int kWaveControlMaxZoom = 3;
inline constexpr int kWaveControlMaxBosquejar = 2;
inline constexpr int kWaveControlBosquejarMin = 2;
inline constexpr int kWaveControlBosquejarMax = 8;
inline constexpr int kWaveControlMaxAgujas = 1;
inline constexpr int kWaveControlAgujasMin = 2;
inline constexpr int kWaveControlAgujasMax = 8;
// Parse, CLI y tests del censo (agujas/plano/zoom de barrio) siguen vivos.
// El piloto de control no los ofrece: planifica con fichas M* (ampliar → explorar).
inline constexpr bool kWaveControlCatalogLive = false;
// Foto de olores → barrios del grafo. Parse y `wave-bosquejo` siguen; el piloto no la pide.
inline constexpr bool kWaveControlBosquejoLive = false;
inline constexpr int kWaveControlMaxOpened = 6;
inline constexpr int kWaveControlOpenedChars = 8000;
inline constexpr int kWaveControlRestChars = 1800;
inline constexpr int kWaveControlBarrioKeep = 3;
inline constexpr int kWaveControlConsultaChars = 720;
inline constexpr int kWaveControlConsultaWordsMin = 4;
inline constexpr int kWaveControlConsultaWordsMax = 120;
inline constexpr int kWaveControlCircuitVistoCap = 4;
inline constexpr int kWaveControlCircuitPeekCap = 2;
inline constexpr int kWaveControlCircuitEntreCap = 8;
inline constexpr int kWaveControlCircuitRestoCap = 6;
inline constexpr int kWaveControlCircuitPairTries = 6;
inline constexpr int kWaveControlAbiertoCap = 6;

enum class WaveDo {
  Needles,
  Juicio,
  Peek,
  Follow,
  Entre,
  Tanda,
  Cerca,
  Guion,
  Independiente,
  Cerrar,
  Invalid
};

inline const char* wave_do_name(WaveDo d) {
  switch (d) {
    case WaveDo::Needles:
      return "needles";
    case WaveDo::Juicio:
      return "juicio";
    case WaveDo::Peek:
      return "peek";
    case WaveDo::Follow:
      return "follow";
    case WaveDo::Entre:
      return "entre";
    case WaveDo::Tanda:
      return "tanda";
    case WaveDo::Cerca:
      return "cerca";
    case WaveDo::Guion:
      return "guion";
    case WaveDo::Independiente:
      return "independiente";
    case WaveDo::Cerrar:
      return "cerrar";
    case WaveDo::Invalid:
      break;
  }
  return "invalid";
}

inline WaveDo wave_do_parse(const std::string& s) {
  if (s == "needles") {
    return WaveDo::Needles;
  }
  if (s == "juicio") {
    return WaveDo::Juicio;
  }
  if (s == "peek") {
    return WaveDo::Peek;
  }
  if (s == "follow") {
    return WaveDo::Follow;
  }
  if (s == "entre" || s == "path") {
    return WaveDo::Entre;
  }
  if (s == "tanda") {
    return WaveDo::Tanda;
  }
  if (s == "cerca") {
    return WaveDo::Cerca;
  }
  if (s == "guion") {
    return WaveDo::Guion;
  }
  if (s == "independiente") {
    return WaveDo::Independiente;
  }
  if (s == "cerrar") {
    return WaveDo::Cerrar;
  }
  return WaveDo::Invalid;
}

struct WaveHit {
  std::string id;
  std::string path;
  std::string symbol;
  std::string stem;
  std::string kind;
  std::string needle;
  std::vector<std::string> files;  // cpp + header pares; peekables
};

struct WaveZone {
  std::string id;
  std::string verdict;  // keep | drop
};

struct WaveNeedleLog {
  std::string needle;
  std::string in_locus;  // empty = registry NodeId; else grep in that body
  int hits = 0;
  int added = 0;
  std::vector<std::string> ids;
};

struct WaveCercaHit {
  std::string id;
  std::string path;
  std::string symbol;
  std::string stem;
  std::string kind;
  float cosine = 0.f;
  int hop = 0;
  std::string card;
  bool already_read = false;
};

struct WaveCercaLog {
  std::vector<std::string> needles;
  std::vector<std::string> in_scopes;
  std::string query;
  int hops = 1;
  int hits = 0;
  int added = 0;
  std::vector<WaveCercaHit> rows;
  std::string wake_note;  // dir/archivo frío → inventario barato + oleada
};

struct WaveOlaLog {
  int n = 0;
  std::string do_name;
  std::string why;
  std::string detail;
};

struct WavePeekHop {
  std::string loc;
  std::string stem;
  int hops = 1;
  float cosine = -1.f;  // CardFull vs consulta; <0 si no hay ficha
};

struct WavePeekNeighbors {
  std::string loc;
  std::vector<WavePeekHop> callers;
  std::vector<WavePeekHop> callees;
  // Other callers of a cross-stem callee (fan-in of the exported hop).
  std::string export_loc;
  std::vector<WavePeekHop> export_callers;
};

struct WaveSketchLink {
  std::string from;
  std::string to;
  std::string via;  // call | follow | entre | ray
};

struct WaveOla {
  bool ok = false;
  std::string error;
  WaveDo do_kind = WaveDo::Invalid;
  std::string campo;
  std::vector<std::string> needles;
  std::vector<std::string> keep;
  std::vector<std::string> drop;
  std::string peek;
  std::vector<std::string> peeks;
  std::vector<std::string> follows;
  std::string from;
  std::string to;
  std::string in_locus;  // grep needles inside this anchored function
  std::vector<std::string> in_scopes;  // cerca: barrios (M*, path:fn, stem, prefijo)
  int hops = 0;                        // cerca: 0 = default 1; tope 2
  std::string why;
  std::vector<std::string> huecos;  // optional: claimed unread (cerrar)
  std::vector<std::string> papeles;  // guion: preguntas pequeñas de comprensión (no ids)
  int papel = 0;                     // independiente: índice 1-based del guion
  nlohmann::json raw;
};

enum class WaveControlDo {
  Explorar,
  Ampliar,
  Zoom,
  Agujas,
  Bosquejar,
  Cerrar,
  Plan,
  Pasar,
  NoPasar,
  Revisar,
  Invalid
};

enum class WaveControlCue { Default, Neutral };

enum class WaveControlPhaseKind { Locator, Puente, Seguir, Invalid };

enum class WaveControlPhaseStatus { Pendiente, EnCurso, Paso, Fallo };

enum class WaveControlPlanModo { Ninguno, Romper, Seguir };

struct WaveControlPhase {
  std::string id;
  WaveControlPhaseKind kind = WaveControlPhaseKind::Invalid;
  std::string consulta;
  std::vector<std::string> need;
  std::vector<std::string> hacia;
  std::vector<std::string> evita;
  WaveControlPhaseStatus status = WaveControlPhaseStatus::Pendiente;
  std::vector<std::string> ancla_visto;
};

struct WaveControlPlan {
  bool committed = false;
  WaveControlPlanModo modo = WaveControlPlanModo::Ninguno;
  std::vector<WaveControlPhase> fases;
};

struct WaveControlLaunch {
  bool ok = false;
  std::string consulta;
  WaveControlPhaseKind kind = WaveControlPhaseKind::Invalid;
  std::string pin_from;
  std::string pin_to;
  std::vector<std::string> pin_loci;
  std::vector<std::string> hacia;
  std::vector<std::string> evita;
  std::string fallos_md;
  int cerca_hops_max = 0;
  int peek_hop_depth = 0;
};

struct WaveControlOla {
  bool ok = false;
  std::string error;
  WaveControlDo do_kind = WaveControlDo::Invalid;
  std::string consulta;
  std::vector<std::string> consultas;
  std::vector<std::string> ids;
  std::vector<std::string> hacia;
  std::vector<std::string> evita;
  WaveControlPlan plan;
  std::string why;
};

inline const char* wave_control_do_name(WaveControlDo d) {
  switch (d) {
    case WaveControlDo::Explorar:
      return "explorar";
    case WaveControlDo::Ampliar:
      return "ampliar";
    case WaveControlDo::Zoom:
      return "zoom";
    case WaveControlDo::Agujas:
      return "agujas";
    case WaveControlDo::Bosquejar:
      return "bosquejar";
    case WaveControlDo::Cerrar:
      return "cerrar";
    case WaveControlDo::Plan:
      return "plan";
    case WaveControlDo::Pasar:
      return "pasar";
    case WaveControlDo::NoPasar:
      return "no_pasar";
    case WaveControlDo::Revisar:
      return "revisar";
    case WaveControlDo::Invalid:
      break;
  }
  return "invalid";
}

inline const char* wave_control_phase_kind_name(WaveControlPhaseKind k) {
  switch (k) {
    case WaveControlPhaseKind::Locator:
      return "locator";
    case WaveControlPhaseKind::Puente:
      return "puente";
    case WaveControlPhaseKind::Seguir:
      return "seguir";
    case WaveControlPhaseKind::Invalid:
      break;
  }
  return "invalid";
}

inline const char* wave_control_phase_status_name(WaveControlPhaseStatus s) {
  switch (s) {
    case WaveControlPhaseStatus::Pendiente:
      return "pendiente";
    case WaveControlPhaseStatus::EnCurso:
      return "en_curso";
    case WaveControlPhaseStatus::Paso:
      return "paso";
    case WaveControlPhaseStatus::Fallo:
      return "fallo";
  }
  return "pendiente";
}

inline const char* wave_control_plan_modo_name(WaveControlPlanModo m) {
  switch (m) {
    case WaveControlPlanModo::Romper:
      return "romper";
    case WaveControlPlanModo::Seguir:
      return "seguir";
    case WaveControlPlanModo::Ninguno:
      break;
  }
  return "";
}

struct WaveState {
  std::string prompt;
  std::string campo;
  std::vector<std::string> papeles;  // guion fijo: preguntas de comprensión; pin en cada ola
  std::vector<int> independiente_done;  // índices 1-based ya lanzados
  bool independiente_leaf = false;      // hijo: no anida
  bool control_worker = false;          // explorador de --control: acumula; no orquesta
  std::string pin_from;
  std::string pin_to;
  std::vector<std::string> pin_loci;
  std::vector<std::string> pin_hacia;
  std::vector<std::string> pin_evita;
  std::string pin_fallo_md;
  int cerca_hops_max = 0;   // 0 = kWaveCercaHopsMax
  int peek_hop_depth = 0;   // 0 = kWavePeekHopDepth
  std::string atlas_md;  // zone map (causal_atlas_v1 if seeded from cards)
  nlohmann::json atlas_cards;  // judge payload de ESTA consulta (no el mazo del padre)
  std::vector<WaveHit> atlas_seed;  // zonas originales; retain no las toca
  std::string opened_md;  // fichas ampliadas tras la ronda cover
  std::vector<std::string> opened_ids;
  std::vector<WaveHit> candidatas;
  std::vector<WaveNeedleLog> needles_log;
  std::vector<WaveCercaLog> cerca_log;
  std::vector<WaveOlaLog> olas_log;
  std::vector<std::string> mencionados;
  std::vector<WaveZone> zonas;
  std::string notas;
  std::string follow_md;
  std::vector<std::string> peeks_done;
  std::vector<std::string> follows_done;
  std::vector<std::string> entres_done;
  std::string last_error;
  int wave_n = 0;
  int propose_n = 0;  // every propose, including failed applies
  bool done = false;
  std::string cierre;
  std::vector<std::string> huecos_claimed;  // from cerrar JSON; not a todo
  std::vector<std::string> circuit_on;
  std::vector<std::string> circuit_off;
  std::vector<std::string> circuit_on_via;
  std::vector<std::string> circuit_off_via;
  std::vector<std::string> circuit_callers_on;
  std::vector<std::string> circuit_callers_off;
  std::string circuit_entre;
  std::vector<WavePeekNeighbors> peek_neighbors;
  std::vector<WaveSketchLink> follow_links;
};

// Tests inject search/peek; the harness binds registry + get_code_of.
struct WaveOps {
  std::function<std::vector<WaveHit>(const std::string& needle, const std::string& campo)>
      search_needle;
  std::function<bool(const std::string& peek, std::string* text, std::string* err)> peek_code;
  // After a function peek: aguas arriba/abajo. Callers become peekable.
  std::function<bool(const std::string& path, const std::string& symbol, const std::string& body,
                     bool truncated, std::string* md, std::vector<WaveHit>* callers,
                     std::string* err)>
      peek_causal;
  // Same payload as worker `causal`: stacks + cond branches + mermaid. Hops peekable.
  std::function<bool(const std::string& path, const std::string& symbol, std::string* md,
                     std::vector<WaveHit>* hops, std::string* err)>
      follow_tree;
  // Directed registry path from → to. Intermediate nodes become peekable.
  std::function<bool(const std::string& from, const std::string& to, std::string* md,
                     std::vector<WaveHit>* hops, std::string* err)>
      path_between;
  // Grep needles inside a function body. hits=0 is evidence. counts parallel to needles.
  std::function<bool(const std::string& path, const std::string& symbol,
                     const std::vector<std::string>& needles, std::string* md,
                     std::vector<int>* hits_per_needle, std::string* err)>
      search_in_body;
  // Semantic scan in a barrio: embed joined concept needles, rank cards, expand hops.
  // seed_fns = {path, symbol}; in_scopes empty = inmediaciones already baked into seeds.
  // note: opcional; el runtime lo pega al cuaderno (despertar dir/archivo).
  std::function<bool(const std::string& query,
                     const std::vector<std::pair<std::string, std::string>>& seed_fns,
                     const std::vector<std::string>& boost_stems,
                     const std::vector<std::string>& in_scopes, int hops,
                     std::vector<WaveCercaHit>* hits, std::string* err, std::string* note)>
      search_cerca;
  // Optional: fill cosine on peek hops from impact-card embeddings vs query.
  std::function<void(const std::string& query, std::vector<WavePeekHop>* hops)> rank_hops;
  // Optional: re-retrieve + judge cards for the child query. Replaces atlas_seed.
  std::function<bool(const std::string& query, WaveState* child, std::string* err)> rebuild_atlas;
  // Ciclo hijo: prompt ya es el papel. Rellena *child (visto/notas/peeks). No anida.
  std::function<bool(const std::string& prompt, WaveState* child, std::string* err)>
      run_independiente;
};

WaveOla wave_parse_ola(const std::string& raw);
bool wave_campo_match(const WaveHit& hit, const std::string& campo);
std::string wave_hit_key(const WaveHit& hit);
bool wave_id_in_hits(const std::vector<WaveHit>& hits, const std::string& id);
const WaveHit* wave_find_hit(const std::vector<WaveHit>& hits, const std::string& id);
void wave_merge_hits(WaveState* st, const std::vector<WaveHit>& incoming);
std::vector<std::string> wave_needle_search_keys(const std::string& needle);
std::string wave_needle_stem_hint(const std::string& needle);
bool wave_cerca_concept_ok(const std::string& needle);
std::string wave_cerca_clip_concept(const std::string& needle);
void wave_control_sanitize_hacia(std::vector<std::string>* hacia);
std::string wave_cerca_query(const std::vector<std::string>& needles);
bool wave_cerca_needles_ok(const std::vector<std::string>& needles, std::string* err);
bool wave_guion_papel_ok(const std::string& papel);
bool wave_guion_papeles_ok(const std::vector<std::string>& papeles, std::string* err);
std::vector<std::string> wave_guion_uncovered(const WaveState& st);
std::string wave_guion_markdown(const WaveState& st);
bool wave_independiente_ok(const WaveState& st, int papel, std::string* err);
std::vector<int> wave_independiente_open(const WaveState& st);
std::string wave_independiente_cue_markdown(const WaveState& st);
void wave_merge_independiente(WaveState* parent, const WaveState& child, int papel);
WaveState wave_independiente_child(const WaveState& parent, const std::string& prompt);
std::string wave_strategy_markdown();
std::vector<std::string> wave_extract_call_names(const std::string& text);

struct WaveOutgoingCall {
  std::string symbol;
  std::string when;
};

std::vector<WaveOutgoingCall> wave_follow_outgoing_calls(const std::string& body);
std::string wave_follow_outgoing_markdown(const std::string& display,
                                          const std::vector<WaveOutgoingCall>& calls);
bool wave_line_has_needle(const std::string& line, const std::string& needle);
int wave_seed_from_atlas(WaveState* st, const nlohmann::json& payload);
int wave_ingest_zone_symbols(WaveState* st, const nlohmann::json& payload,
                             const std::vector<std::string>& zone_ids);
void wave_retain_atlas_ids(WaveState* st, const std::vector<std::string>& ids);
bool wave_needs_guion(const WaveState& st);
bool wave_needs_cover(const WaveState& st);
bool wave_close_audit_accept(const WaveOla& draft, const WaveOla& audit,
                             const WaveState& st);
std::string wave_close_audit_instructions(const WaveState& st);
void wave_attach_cierre_caption(WaveState* st);
std::vector<std::string> wave_salvage_keep(const std::string& raw,
                                           const std::vector<std::string>& allowed);
WaveOla wave_salvage_guion(const std::string& raw);
bool wave_check_barriers(const WaveOla& ola, const WaveState& st, std::string* err);
bool wave_apply(WaveState* st, const WaveOla& ola, const WaveOps& ops, std::string* err);

std::string wave_notebook_markdown(const WaveState& st);
std::string wave_work_markdown(const WaveState& st);
std::string wave_circuit_markdown(const WaveState& st);
std::string wave_sketch_markdown(const WaveState& st);
std::vector<WaveSketchLink> wave_sketch_edges(const WaveState& st);
std::string wave_opened_brief(const std::string& opened_md);
std::string wave_atlas_rest_markdown(const std::string& atlas_md,
                                     const std::vector<std::string>& keep_ids);
std::vector<std::string> wave_cover_peek_targets(const std::string& opened_md);
bool wave_circuit_complete(const WaveState& st);
std::string wave_circuit_cierre(const WaveState& st);
// True on the last loop slot (propose_n / wave_n >= max). Failed olas do not
// reserve the penultimate slot for close.
bool wave_is_last_propose(const WaveState& st);
std::string wave_guion_system_prompt();
std::string wave_guion_user_prompt(const WaveState& st);
std::string wave_cover_system_prompt();
std::string wave_cover_user_prompt(const WaveState& st);
std::string wave_pilot_system_prompt();
std::string wave_explorer_system_prompt();
std::string wave_pilot_user_prompt(const WaveState& st);
struct WaveControlOlor {
  std::string concepto;
  std::string barrio;
  float concentration = 0.f;
  bool twin = false;
  std::string twin_barrio;
  int hits = 0;
};

struct WaveControlBosquejoEdge {
  std::string from;
  std::string to;
};

struct WaveControlBosquejo {
  std::vector<WaveControlOlor> olores;
  std::vector<WaveControlBosquejoEdge> entre;
  std::string nota;
  std::string detalle;  // diagnóstico; no va al prompt
};

WaveControlOla wave_parse_control(const std::string& raw);
bool wave_control_consulta_ok(const std::string& consulta, std::string* err);
bool wave_control_evita_ok(const std::vector<std::string>& evita, std::string* err);
bool wave_control_cerrar_why_ok(const std::string& why, std::string* err);
bool wave_control_bosquejar_ok(const std::vector<std::string>& conceptos, std::string* err);
bool wave_control_aguja_ok(const std::string& tok);
bool wave_control_agujas_ok(const std::vector<std::string>& agujas, std::string* err);
int wave_control_agujas_merge(std::vector<std::string>* acc, const std::vector<std::string>& add);
bool wave_control_zoom_id_ok(const std::string& id);
std::string wave_control_barrio_of_path(const std::string& path);
std::string wave_control_bosquejo_markdown(const WaveControlBosquejo& foto);
bool wave_control_consulta_es_ancla(const std::string& consulta, const std::string& ancla);
bool wave_control_consulta_delta_ok(const std::string& consulta, const std::string& ancla,
                                   const std::vector<std::string>& prev, std::string* err);
std::string wave_control_consulta_misma_caza(const std::string& consulta,
                                            const std::vector<std::string>& prev);
std::vector<WaveControlDo> wave_control_legal(const WaveControlPlan& plan, int jobs_run,
                                             bool last_visto, int agujas_n = 0, int zoom_n = 0,
                                             int ampliar_n = 0);
bool wave_control_do_allowed(const std::vector<WaveControlDo>& legal, WaveControlDo d);
std::string wave_control_legal_markdown(const std::vector<WaveControlDo>& legal);
std::string wave_control_plan_markdown(const WaveControlPlan& plan);
nlohmann::json wave_control_plan_to_json(const WaveControlPlan& plan);
int wave_control_plan_current(const WaveControlPlan& plan);
bool wave_control_plan_commit(WaveControlPlan* plan, const WaveControlOla& ola, std::string* err);
bool wave_control_plan_pasar(WaveControlPlan* plan, const std::vector<std::string>& visto,
                            std::string* err);
bool wave_control_plan_no_pasar(WaveControlPlan* plan, std::string* err);
bool wave_control_plan_revisar(WaveControlPlan* plan, const std::string& consulta,
                              const std::vector<std::string>& hacia, std::string* err,
                              const std::vector<std::string>& evita = {});
WaveControlLaunch wave_control_launch_spec(const WaveControlPlan& plan);
void wave_control_inherit_visto(WaveControlLaunch* spec, const std::vector<std::string>& visto);
void wave_control_seed_launch(WaveState* st, const WaveControlLaunch& spec);
std::string wave_control_brief(const WaveState& st);

// Foto causal entre exploradores (mismo dialecto que pilot_opened_v1).
struct WaveControlJobSnap {
  int n = 0;
  std::string consulta;
  std::string cerrado;
  std::vector<std::string> visto;  // path:symbol consultados
  std::vector<WavePeekNeighbors> peek_neighbors;
  std::string circuit_entre;
};
using WaveControlPathFn =
    std::function<bool(const std::string& from, const std::string& to, std::string* md,
                       std::vector<WaveHit>* hops, std::string* err)>;
WaveControlJobSnap wave_control_job_snap(int n, const WaveState& st);
std::string wave_control_jobs_circuit_pack(const std::vector<WaveControlJobSnap>& jobs,
                                           const WaveControlPathFn& path_between = {});
std::string wave_control_fallos_cue(const std::vector<WaveControlJobSnap>& jobs);
std::string wave_control_slice_exam(const std::string& ancla,
                                   const std::vector<WaveControlJobSnap>& jobs);
std::string wave_control_atlas_brief(const std::string& atlas_md);
std::string wave_control_module_map(const std::string& workspace_root);
std::string wave_control_barrio_brief(const nlohmann::json& cards, const std::string& consulta,
                                      const std::string& workspace_root,
                                      const std::vector<std::string>& opened_ids = {});
std::string wave_control_inspect_brief(const nlohmann::json& cards,
                                       const std::vector<std::string>& ids,
                                       const std::string& consulta);
std::string wave_control_opened_brief(const std::string& opened_md);
std::string wave_control_atlas_pack_brief(const std::string& atlas_md, std::size_t cap = 0);
std::vector<std::string> wave_control_owns_phrases(const nlohmann::json& cards);
std::string wave_control_strip_owns(std::string consulta, const std::vector<std::string>& owns);
std::string wave_control_system_prompt(WaveControlCue cue = WaveControlCue::Default);
std::string wave_control_rama_nudge(const std::string& consulta = {});
std::string wave_control_misma_caza_nudge(const std::string& prev, const std::string& proposed);
std::string wave_control_plantilla_nudge(const std::string& consulta = {});
std::string wave_control_cerrar_nudge();
std::string wave_control_user_prompt(const std::string& user_consulta, const std::string& jobs_md,
                                     const std::string& barrio_brief = {},
                                     const std::string& inspect_brief = {},
                                     const WaveControlPlan& plan = {},
                                     const std::vector<WaveControlDo>& legal = {},
                                     const std::string& circuit_md = {},
                                     const std::string& exam_md = {},
                                     WaveControlCue cue = WaveControlCue::Default,
                                     const std::string& bosquejo_md = {},
                                     const std::string& plano_md = {},
                                     const std::string& zoom_md = {});
nlohmann::json wave_state_to_json(const WaveState& st);
nlohmann::json wave_ola_to_json(const WaveOla& ola);

// Pack handoff for L2: visto = peeks (path:symbol). huecos = named but not read.
struct WavePackHandoff {
  std::vector<std::string> visto;
  std::vector<std::string> seguido;
  std::vector<std::string> in_done;
  std::vector<std::string> huecos;
  std::string why;
};

WavePackHandoff wave_pack_handoff(const WaveState& st);
nlohmann::json wave_pack_to_json(const WaveState& st);
std::string wave_pack_markdown(const WaveState& st);

}  // namespace tuide
