#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_effect_slice.hpp"
#include "ai/l2_problem_frame.hpp"

struct sqlite3;

namespace tuide {

// ---------------------------------------------------------------------------
// Contrato del registro persistente (norma; el CLI solo habla por esta API)
//
// Identidad — un hecho, un id:
//   Fn canónica:  fn:{rel_cpp}:{bare_symbol}
//     Gemelo .hpp/.cpp → un nodo (el .cpp si existe). El .hpp es alias.
//   Latch:        latch:{stem}:{member}  (nunca latch:member global)
//   Ctrl:         ctrl:{path}:{line}:{kind}  parent_fn canónico
//   Handoff:      handoff:{path}:{line}
//   Hecho:        UNIQUE(from_id, to_id, kind, member) — merge = no-op
//   seed/prior_sem/mass NO se persisten. Sí: origin, seen_n, fichas.
//
// Admisión — qué puede entrar en una oleada:
//   Solo src/ (tests/fixtures/ si allow_fixtures). Rechazar tests/tools/examples.
//   Calls y latches ruido se tiran. File-level ≠ fn falsa: inventario.
//   Hop solo desde anclas seed de la oleada (lo hace effect_slice_build).
//   Tope kRegistryMaxNewFnPerWave. Inventario grande → files.pending_inventory;
//   no se tiran todas las fns: se admiten seeds + fns ligadas (hechos / parent
//   de ctrl) hasta el tope por archivo. Ctrl sin parent fn admitido no entra.
//   Cosine hop0: items L1 (path+símbolo) primero; cosine rellena hasta top_k. Hops sí expanden a ctrl.
//
// Crecer (ingest): transacción. INSERT OR IGNORE + update ficha si card_hash
//   cambió. Idempotente. Nadie es dueño de un nodo (no se borra “la query 3”).
//
// Borrar / invalidar:
//   refresh --path: reparse; fns desaparecidas → tombstone + borrar hechos.
//   Archivo ausente: tombstone de todos los nodos de ese path.
//   gc: purga tombstones con edad; dry-run primero.
//   Re-ingest del mismo id LEVANTA el tombstone.
//   Rename: v1 el viejo queda tombstone (sin heurística).
//
// Consultar estructural: stats / get / neighbors / path / code (get_code_of).
// Consultar semántico (v2): embed incremental de fichas (cache por card_hash) +
//   1 embed de la query + cosine (puerta fn/latch/handoff) + hops.
//   Trails: PPR (masa por aristas, damp 0.85) + beam de threads. La unidad de
//   respuesta es el camino, no el nodo. T* de oleada siguen efímeros.
// Persistencia: <workspace>/.tuide/effect/registry.sqlite  (WAL)
// ---------------------------------------------------------------------------

inline constexpr int kRegistrySchemaVersion = 2;
inline constexpr const char* kRegistryEmbedModelDefault = "nomic-embed-text-v1.5-q4_k_m";
inline constexpr int kRegistryQueryTopK = 16;
inline constexpr int kRegistryQueryMapStems = 15;
inline constexpr int kRegistryQueryAnchorMapStems = 8;
inline constexpr int kRegistryQueryAnchorSeeds = 8;
inline constexpr int kRegistryQueryHops = 2;
inline constexpr int kRegistryQueryThreads = 5;
inline constexpr int kRegistryMaxNewFnPerWave = 400;
inline constexpr int kRegistryMaxInventoryPerFile = 64;
inline constexpr int kRegistryPathMaxDepth = 8;
inline constexpr int kRegistryGcMinAgeQueries = 3;
inline constexpr int kPilotWorkerMaxSteps = 5;  // tools + informe (cubre: máx. 1 tool)

struct EffectRegistry {
  sqlite3* db = nullptr;
  std::string workspace_root;
  std::string db_path;
};

struct RegistryIngestMeta {
  std::string query;
  std::vector<std::string> seeds;
  std::string map_path;
  bool allow_fixtures = false;
};

struct RegistryGcOpts {
  int min_age_queries = kRegistryGcMinAgeQueries;
  bool dry_run = true;
};

struct RegistryGcReport {
  int tombstones = 0;
  int facts_dropped = 0;
  int applied = 0;
};

struct RegistryStats {
  int queries = 0;
  int files = 0;
  int pending_inventory = 0;
  int nodes = 0;
  int fns = 0;
  int ctrls = 0;
  int latches = 0;
  int handoffs = 0;
  int facts = 0;
  int tombstones = 0;
  int embeddings = 0;
};

struct RegistryNodeRow {
  std::string id;
  std::string kind;
  std::string path;
  std::string symbol;
  std::string stem;
  int line = 0;
  std::string parent_fn;
  std::string ctrl_kind;
  std::string cond;
  std::string origin;
  bool cold = false;
  std::string card_json;
  std::string card_hash;
  int seen_n = 0;
  std::string tombstone_reason;
};

struct RegistryFactRow {
  std::string from_id;
  std::string to_id;
  std::string kind;
  std::string member;
};

struct RegistryNeighbor {
  RegistryFactRow fact;
  RegistryNodeRow node;
  bool outbound = true;
};

// is_query=true → prefijo search_query; false → search_document (nomic).
using RegistryEmbedFn =
    std::function<bool(bool is_query, const std::string& text, std::vector<float>* out)>;
using RegistryEmbedManyFn = std::function<bool(const std::vector<std::string>& texts,
                                               std::vector<std::vector<float>>* out)>;

// What text/kind hop0 cosine (or lexical) matches against.
enum class RegistryMatchSurface {
  CardFull,   // default: Effect Summary markdown card
  Latch,      // latch nodes only; passage = latch stem/member
  CardAttrs,  // fn cards: writes|reads|hot|symbol only
  NodeId,     // lexical match on id/symbol/path (no cosine)
};

const char* registry_match_surface_name(RegistryMatchSurface s);
bool registry_match_surface_parse(const std::string& s, RegistryMatchSurface* out);
// Embeddings PK is (node_id, model); non-CardFull surfaces use model#surface:name.
std::string registry_embed_model_key(const std::string& model, RegistryMatchSurface surface);

struct RegistryEmbedOpts {
  std::string model = kRegistryEmbedModelDefault;
  bool force = false;
  bool skip_glue = true;
  int max_nodes = 4000;
  RegistryMatchSurface match_surface = RegistryMatchSurface::CardFull;
};

struct RegistryEmbedReport {
  int considered = 0;
  int embedded = 0;
  int skipped_cached = 0;
  int skipped_glue = 0;
  int skipped_ctrl = 0;
  int failed = 0;
};

struct RegistryBoostFn {
  std::string path;
  std::string symbol;
};

struct RegistryQueryOpts {
  std::string model = kRegistryEmbedModelDefault;
  int top_k = kRegistryQueryTopK;
  int hops = kRegistryQueryHops;
  std::vector<std::string> hop_kinds;
  // hop0 cosine. Vacío = defaults from match_surface (see registry_query).
  std::vector<std::string> seed_kinds;
  std::vector<std::string> boost_stems;  // fallback por stem si no hay boost_fns
  std::vector<RegistryBoostFn> boost_fns;  // items L1 en orden
  int max_per_stem = 2;
  int threads = kRegistryQueryThreads;
  RegistryMatchSurface match_surface = RegistryMatchSurface::CardFull;
};

struct RegistryQueryHit {
  RegistryNodeRow node;
  float cosine = 0.f;
  int hop = 0;
};

struct RegistryQueryResult {
  std::vector<RegistryQueryHit> hits;
  std::vector<RegistryQueryHit> expanded;
};

struct RegistryTrailHop {
  RegistryNodeRow node;
  float mass = 0.f;
  float cosine = 0.f;
};

struct RegistryTrail {
  std::string id;
  float score = 0.f;
  std::string why;
  std::vector<std::string> latches;
  std::vector<RegistryTrailHop> hops;
};

struct RegistryConstellation {
  std::string id;
  std::string center_id;
  std::string member;
  float score = 0.f;
  float mass_coverage = 0.f;
  std::string why;
  std::vector<std::string> core_stems;
  std::vector<std::string> context_stems;
  std::vector<std::string> primary_stems;
  std::vector<std::string> peripheral_stems;
  std::vector<std::string> writers;
  std::vector<std::string> readers;
  std::vector<std::string> controls;
  std::vector<std::string> handoffs;
  std::vector<RegistryTrailHop> nodes;
};

struct RegistryMacroConstellation {
  std::string id;
  float score = 0.f;
  float mass_coverage = 0.f;
  std::string why;
  std::vector<std::string> nuclei;
  std::vector<std::vector<std::string>> anchor_groups;
  std::vector<std::string> primary_stems;
  std::vector<std::string> merge_witnesses;
  float merge_strength = 0.f;
  std::vector<RegistryTrailHop> nodes;
};

struct RegistryTrailResult {
  RegistryQueryResult query;
  std::vector<RegistryTrailHop> seeds;
  std::vector<RegistryTrail> trails;
  std::vector<RegistryConstellation> constellations;
  std::vector<RegistryMacroConstellation> macro_constellations;
  std::vector<std::string> holes;
  int subgraph_nodes = 0;
  int subgraph_facts = 0;
  float max_cosine = 0.f;
  int map_boosted = 0;
  bool weak_gate = false;  // cosine flojo y sin overlap con mapa → A0
};

struct RegistryCausalJudgeOpts {
  int max_zones = 5;
  int max_representatives = 5;
  int max_edges = 12;
  int max_trails = 1;
  int max_uncovered_seeds = 5;
  // Si no está vacío, emite únicamente estos ids de macrozona.
  std::vector<std::string> zone_filter;
  // Targets canónicos solicitados por el triage para expansión dirigida.
  std::vector<std::string> expand_targets;
  int expand_hops = 0;
  bool outline_all_representatives = false;
  bool promote_uncovered = false;

  // Mechanism pack: skeleton slots + cupo ranking (genérico, sin hardcode por caso).
  bool mechanism_pack = true;
  int skel_trigger_cup = 1;
  int skel_state_cup = 1;
  int skel_effect_cup = 1;
  int port_cup = 2;
  float w_kind_write = 100.f;
  float w_kind_read = 100.f;
  float w_kind_handoff = 90.f;
  float w_kind_ctrl = 80.f;  // then / else / case
  float w_kind_call = 60.f;
  float w_kind_enter_ctrl = 50.f;
  float w_cos = 55.f;
  float w_ppr = 45.f;
  float w_anchor = 70.f;
  float w_direct = 20.f;
  float w_hub = 25.f;
  float w_redundancy = 35.f;
  float semantic_hard_floor = 45.f;
};

// Overlay knobs from JSON object (unknown keys ignored). Returns false on type errors.
bool registry_causal_judge_opts_apply_json(RegistryCausalJudgeOpts* opts, const nlohmann::json& j,
                                           std::string* err);

// Niveles de presentación del pack causal. Ortogonal a GraphQueryPhase (hops).
// atlas: muchas zonas, poco texto. inspect/deep: pocas zonas, más evidencia.
enum class GraphViewLevel {
  Atlas,
  Inspect,
  Deep,
};

struct GraphViewProfile {
  GraphViewLevel level = GraphViewLevel::Atlas;
  int max_zones = 12;
  int max_representatives = 3;
  int max_edges = 4;
  int max_trails = 0;
  int expand_hops = 0;
  bool outline_all_representatives = false;
  bool promote_uncovered = true;
};

const char* graph_view_level_name(GraphViewLevel level);
bool graph_view_level_parse(const std::string& s, GraphViewLevel* out);
GraphViewProfile graph_view_profile_default(GraphViewLevel level);
void graph_view_profile_apply(const GraphViewProfile& profile, RegistryCausalJudgeOpts* opts);

struct RegistryZoneTriage {
  std::string id;
  std::string verdict;  // inspect | reject | anchor
  std::string need;
  std::string explains;
  std::string does_not_explain;
  std::string thread;
  std::string role_guess;
  std::vector<std::string> expand_from;
};

struct RegistryCausalTriageDecision {
  bool ok = false;
  std::string action;  // causal_zone_anchor_v1 | causal_zone_triage_v1 | causal_zone_primary_survey_v1 | causal_atlas_survey_v1
  std::vector<RegistryZoneTriage> zones;
  std::vector<std::string> shortlist;
  std::string hypothesis;
  bool critical_mass = false;
  bool retrieval_needed = false;
  std::string view;  // inspect | deep (siguiente pack; vacío = inspect)
  std::string why;
  std::string raw;
  std::string error;
};

// Survey: cada zona como primary de una hipótesis global (o discard).
struct RegistryPrimarySurveySupporting {
  std::string id;
  std::string role;  // trigger | state_owner | cleanup | consumer | boundary
};

struct RegistryPrimarySurveyEntry {
  std::string id;
  bool discard = false;
  float confidence = 0.f;
  std::string hypothesis;
  std::string discard_reason;
  std::vector<RegistryPrimarySurveySupporting> supporting;
  std::vector<std::string> expand_from;
};

struct RegistryPrimarySurveyDecision {
  bool ok = false;
  std::string action;  // causal_zone_primary_survey_v1
  std::vector<RegistryPrimarySurveyEntry> entries;
  std::string raw;
  std::string error;
};

// Contraste: hasta 2 hyp globales incompatibles (+ discards justificados).
struct RegistryContrastThread {
  std::string primary;
  std::string hypothesis;
  float confidence = 0.f;
  std::vector<RegistryPrimarySurveySupporting> supporting;
  std::vector<std::string> expand_from;
  bool synthetic = false;
};

struct RegistryContrastDiscard {
  std::string id;
  std::string reason;
  std::vector<std::string> expand_from;
};

struct RegistryContrastDecision {
  bool ok = false;
  std::string action;  // causal_zone_contrast_v1
  std::vector<RegistryContrastThread> threads;
  std::vector<RegistryContrastDiscard> discards;
  bool single_viable = false;
  bool injected = false;
  std::string raw;
  std::string error;
};

struct RegistryContrastValidation {
  bool ok = false;
  std::string error;  // incomplete_contrast | duplicate_hypothesis | empty_threads | ...
};

struct RegistryZoneVerdict {
  std::string id;
  std::string verdict;       // select | reject
  std::string role;          // primary | trigger | state_owner | cleanup | consumer | boundary | none
  std::string completeness;  // complete | partial | none
  float confidence = 0.f;
  std::string why;
  std::string contribution;
  std::string missing_link;
  std::vector<std::string> expand_from;
};

struct RegistryCausalJudgeDecision {
  bool ok = false;
  std::vector<RegistryZoneVerdict> zones;
  std::vector<std::string> selected;
  std::string next;  // verify | expand | none | reinvestigate
  std::string hypothesis_status;  // confirmed | partial | falsified
  std::string reinvestigate_need;
  std::string why;
  std::string raw;
  std::string error;
};

std::string registry_db_path(const std::string& workspace_root);

bool registry_path_is_header(const std::string& path);
std::string registry_path_to_cpp(const std::string& path);
std::string registry_stem_of(const std::string& path);
std::string registry_canonical_fn_id(const std::string& workspace_root, const std::string& path,
                                     const std::string& symbol);
std::string registry_canonical_latch_id(const std::string& stem, const std::string& member);
std::string registry_canonical_node_id(const std::string& workspace_root, const EffectNode& n);

bool registry_admit_path(const std::string& rel, bool allow_fixtures);

bool registry_open(const std::string& workspace_root, EffectRegistry* out, std::string* err);
void registry_close(EffectRegistry* r);

bool registry_ingest_slice(EffectRegistry* r, const EffectSlice& slice, const RegistryIngestMeta& meta,
                           std::string* err);
bool registry_refresh_path(EffectRegistry* r, const std::string& rel, std::string* err);
bool registry_gc(EffectRegistry* r, const RegistryGcOpts& opts, RegistryGcReport* report,
                 std::string* err);

bool registry_stats(EffectRegistry* r, RegistryStats* out, std::string* err);
bool registry_get(EffectRegistry* r, const std::string& id, RegistryNodeRow* out, std::string* err);
bool registry_neighbors(EffectRegistry* r, const std::string& id, const std::vector<std::string>& kinds,
                        const std::string& dir, std::vector<RegistryNeighbor>* out, std::string* err);
bool registry_path_between(EffectRegistry* r, const std::string& from, const std::string& to,
                           std::vector<std::string>* node_ids, std::string* err);
bool registry_pending_files(EffectRegistry* r, std::vector<std::string>* out, std::string* err);
bool registry_list_files(EffectRegistry* r, std::vector<std::pair<std::string, bool>>* out,
                         std::string* err);

std::string registry_card_passage(const RegistryNodeRow& n);
std::string registry_card_passage(const RegistryNodeRow& n, RegistryMatchSurface surface);

bool registry_embed_nodes(EffectRegistry* r, const RegistryEmbedFn& embed,
                          const RegistryEmbedManyFn& embed_passages, const RegistryEmbedOpts& opts,
                          RegistryEmbedReport* report, std::string* err);
bool registry_query(EffectRegistry* r, const std::string& query, const RegistryEmbedFn& embed,
                    const RegistryQueryOpts& opts, RegistryQueryResult* out, std::string* err);
bool registry_query_trails(EffectRegistry* r, const std::string& query, const RegistryEmbedFn& embed,
                           const RegistryQueryOpts& opts, RegistryTrailResult* out, std::string* err);
bool registry_causal_judge_payload(EffectRegistry* r, const std::string& query,
                                   const RegistryTrailResult& result,
                                   const RegistryCausalJudgeOpts& opts, nlohmann::json* out,
                                   std::string* err);
bool registry_expand_causal_judge_payload(EffectRegistry* r, const nlohmann::json& base_payload,
                                          const RegistryCausalTriageDecision& triage,
                                          const RegistryCausalJudgeOpts& opts,
                                          nlohmann::json* out, std::string* err);
std::string registry_causal_triage_markdown(const nlohmann::json& payload);
std::string registry_causal_triage_system_prompt();
std::string registry_causal_triage_user_prompt(const std::string& cards_markdown);
std::string registry_causal_anchor_system_prompt();
std::string registry_causal_anchor_user_prompt(const std::string& cards_markdown,
                                              const std::string& reopen_need = {});
RegistryCausalTriageDecision registry_parse_causal_triage_decision(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
RegistryCausalTriageDecision registry_parse_causal_anchor_decision(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
nlohmann::json registry_causal_triage_decision_to_json(
    const RegistryCausalTriageDecision& decision);

std::string registry_causal_primary_survey_system_prompt();
std::string registry_causal_primary_survey_user_prompt(
    const std::string& cards_markdown,
    const std::vector<std::string>& required_zone_ids = {});
RegistryPrimarySurveyDecision registry_parse_causal_primary_survey(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
nlohmann::json registry_primary_survey_to_json(const RegistryPrimarySurveyDecision& decision);
// Top hilos no-discard por confidence (diversidad de primary), convertidos a triage.
std::vector<RegistryCausalTriageDecision> registry_primary_survey_select_threads(
    const RegistryPrimarySurveyDecision& survey, const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets,
    int max_threads = 2);

// Must-compete: zonas rivales deterministas (context∩primary, bridges). Cap 2, excluye top-1.
std::vector<std::string> registry_collect_must_compete_zone_ids(const nlohmann::json& base_payload,
                                                               int max_n = 2);
std::string registry_strip_zone_scores_markdown(const std::string& cards_markdown);

std::string registry_causal_contrast_system_prompt();
std::string registry_causal_contrast_user_prompt(
    const std::string& cards_markdown, const std::vector<std::string>& must_compete,
    const std::string& retry_need = {});
RegistryContrastDecision registry_parse_causal_contrast(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
nlohmann::json registry_contrast_to_json(const RegistryContrastDecision& decision);
RegistryContrastValidation registry_validate_contrast_threads(
    const RegistryContrastDecision& decision, const std::vector<std::string>& must_compete,
    const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
void registry_inject_synthetic_contrast_threads(
    RegistryContrastDecision* decision, const std::vector<std::string>& must_compete,
    const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
std::vector<RegistryCausalTriageDecision> registry_contrast_select_threads(
    const RegistryContrastDecision& contrast, const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets,
    int max_threads = 2);

// Slot hyp-gen: 1 primary por pass (sin mazo), cola determinista, pool 2–3 hyps.
struct RegistrySlotHypothesis {
  std::string primary;
  std::string hypothesis;
  float confidence = 0.f;
  std::vector<RegistryPrimarySurveySupporting> supporting;
  std::vector<std::string> expand_from;
  bool discard = false;
  std::string discard_reason;
  bool synthetic = false;
  bool ok = false;
  std::string error;
  std::string raw;
};

struct RegistrySlotSurveyResult {
  std::vector<std::string> queue;
  std::vector<RegistrySlotHypothesis> slots;     // uno por pass (incl. discards)
  std::vector<RegistrySlotHypothesis> retained;  // 2–3 hyps no-discard
  bool gold_in_hypotheses = false;
};

std::vector<std::string> registry_collect_slot_queue_zone_ids(const nlohmann::json& base_payload,
                                                             int max_n = 8);
std::vector<std::string> registry_slot_supporting_zone_ids(const nlohmann::json& base_payload,
                                                          const std::string& primary_id,
                                                          int max_n = 2);
std::string registry_causal_slot_cards_markdown(const nlohmann::json& base_payload,
                                               const std::string& primary_id,
                                               const std::vector<std::string>& supporting_ids);
std::string registry_causal_slot_system_prompt();
std::string registry_causal_slot_user_prompt(const std::string& cards_markdown,
                                            const std::string& primary_id,
                                            const std::string& retry_need = {});
RegistrySlotHypothesis registry_parse_causal_slot_hypothesis(
    const std::string& raw, const std::string& expected_primary,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
bool registry_validate_slot_hypothesis(
    const RegistrySlotHypothesis& hyp, const std::string& expected_primary,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets,
    std::string* err);
void registry_inject_synthetic_slot_hypothesis(
    RegistrySlotHypothesis* hyp, const std::string& primary_id,
    const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);
std::vector<RegistrySlotHypothesis> registry_slot_retain_hypotheses(
    const std::vector<RegistrySlotHypothesis>& slots, const nlohmann::json& base_payload,
    int max_keep = 3);
nlohmann::json registry_slot_survey_to_json(const RegistrySlotSurveyResult& result);
bool registry_slot_gold_in_hypotheses(const std::vector<RegistrySlotHypothesis>& retained,
                                     const nlohmann::json& base_payload,
                                     const std::vector<std::string>& expected_stems);
std::vector<RegistryCausalTriageDecision> registry_slot_hyps_to_threads(
    const std::vector<RegistrySlotHypothesis>& retained, const nlohmann::json& base_payload,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);

void registry_apply_deterministic_co_shortlist(const nlohmann::json& base_payload,
                                               RegistryCausalTriageDecision* triage);

// Tras parsear synth: si hay ≥2 zonas en el thin slice y la selección tiene peor
// overlap hypothesis↔primary_stems (o empate en 0 con menor mass_coverage),
// preferir la zona shortlisteada con mejor overlap.
void registry_apply_synth_hypothesis_tiebreak(const nlohmann::json& expanded_payload,
                                              const std::string& hypothesis,
                                              const std::string& anchor_why,
                                              RegistryCausalJudgeDecision* decision);

std::string registry_causal_judge_markdown(const nlohmann::json& payload);
std::string registry_causal_atlas_markdown(const nlohmann::json& payload,
                                          const std::string& consulta = {});
std::string registry_causal_pack_markdown(const nlohmann::json& payload, GraphViewLevel level);
std::string registry_causal_zone_kind(const nlohmann::json& zone);
// Solape consulta↔owns/peek/stems (genérico; no hay lista de barrios).
int registry_causal_query_zone_overlap(const std::string& query, const nlohmann::json& zone);
int registry_causal_query_hay_overlap(const std::string& query, const std::string& hay);
nlohmann::json registry_atlas_overlap_add_ids(const nlohmann::json& payload,
                                            const std::string& query,
                                            const std::vector<std::string>& opened_ids);
std::string registry_causal_zone_id_for_stem(const nlohmann::json& payload,
                                            const std::string& stem);
void registry_atlas_fill_expand_from(const nlohmann::json& payload,
                                     RegistryCausalTriageDecision* decision);
std::string registry_causal_atlas_survey_system_prompt();
std::string registry_causal_atlas_survey_user_prompt(const std::string& atlas_markdown);
RegistryCausalTriageDecision registry_parse_causal_atlas_survey(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& allowed_targets);

// Segunda pasada: ¿el pack inspect cubre el objeto de la consulta? Si no, añadir M*.
struct RegistryCausalAtlasCoverDecision {
  bool ok = false;
  bool covers = false;
  std::vector<std::string> add;
  std::string why;
  std::string raw;
  std::string error;
};

std::string registry_causal_atlas_cover_system_prompt();
std::string registry_causal_atlas_cover_user_prompt(const std::string& atlas_markdown,
                                                    const std::vector<std::string>& opened,
                                                    const std::vector<std::string>& remaining,
                                                    const std::string& inspect_markdown);
RegistryCausalAtlasCoverDecision registry_parse_causal_atlas_cover(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids,
    const std::vector<std::string>& already_open);
// Añade ids inspect al shortlist (sin duplicar). Devuelve cuántos se insertaron.
int registry_atlas_merge_inspect_ids(RegistryCausalTriageDecision* decision,
                                     const std::vector<std::string>& add_ids,
                                     const std::vector<std::string>& allowed_zone_ids,
                                     int max_total);

// Masa determinista de una hyp (no es autoevaluación del LLM).
struct RegistryCausalHypMass {
  float score = 0.f;
  std::string band;  // high | medium | low
  int grounded_slots = 0;
  bool owns_ok = false;
  bool neighbor_fill = false;
  bool honest_gap = false;
  std::string why;
};

// Hipótesis de fallo (slots affected/control/trigger/cleanup) sobre fichas inspect.
// ok = parseada Y al menos una hyp de masa alta (candidata). need_more = ampliar fichas.
struct RegistryCausalHypDecision {
  bool ok = false;
  bool parsed = false;
  bool need_more = false;
  std::string action;  // causal_zone_hyp_v1
  std::string view;    // inspect | deep (si need_more)
  std::vector<std::string> add;
  std::vector<std::string> expand_from;
  std::vector<AnchorHypothesis> hypotheses;
  std::vector<RegistryCausalHypMass> masses;  // paralelo a hypotheses
  float mass = 0.f;
  std::string mass_band;  // high | medium | low
  std::string why;
  std::string raw;
  std::string error;
};

std::string registry_causal_zone_hyp_system_prompt();
std::string registry_causal_zone_hyp_user_prompt(const std::string& inspect_markdown,
                                                 const std::vector<std::string>& remaining_ids = {});
RegistryCausalHypDecision registry_parse_causal_zone_hyp(
    const std::string& raw, const nlohmann::json& inspect_payload,
    const std::vector<std::string>& atlas_zone_ids = {});
void registry_score_causal_zone_hyp(RegistryCausalHypDecision* decision,
                                    const nlohmann::json& inspect_payload);
std::vector<std::string> registry_atlas_suggest_cover_ids(const nlohmann::json& atlas_payload,
                                                         const std::vector<std::string>& opened,
                                                         int max_n = 2);
nlohmann::json registry_causal_hyp_decision_to_json(const RegistryCausalHypDecision& decision);

// PoC piloto: plan de 3–4 encargos (cubre|como|gap) sobre barrios. No es hyp.
struct RegistryCausalPilotTask {
  std::string kind;  // cubre | como | gap
  std::string zone;
  std::string stem;
  std::string question;
};

struct RegistryCausalPilotPlan {
  bool ok = false;
  bool need_more = false;
  std::string action;  // causal_pilot_plan_v1 | causal_pilot_need_more
  std::vector<RegistryCausalPilotTask> tasks;
  std::vector<std::string> add;
  int unique_stems = 0;
  int n_cubre = 0;
  bool has_como = false;
  bool has_gap = false;
  std::string why;
  std::string raw;
  std::string error;
};

std::string registry_causal_pilot_plan_system_prompt(bool allow_need_more = true);
std::string registry_causal_pilot_plan_user_prompt(
    const std::string& atlas_markdown, const std::string& opened_pack,
    const std::vector<std::string>& remaining_ids,
    const std::vector<std::string>& overlap_suggest = {}, bool allow_need_more = true);
RegistryCausalPilotPlan registry_parse_causal_pilot_plan(
    const std::string& raw, const nlohmann::json& atlas_payload,
    const std::vector<std::string>& atlas_zone_ids = {},
    const std::vector<std::string>& opened_ids = {}, const std::string& query = {},
    bool allow_need_more = false);
std::string registry_causal_pilot_opened_pack(const nlohmann::json& payload,
                                             const std::vector<std::string>& ids,
                                             const std::string& query = {});
std::string registry_causal_pilot_barrio_menu(const nlohmann::json& payload,
                                              const std::vector<std::string>& ids,
                                              const std::string& query = {});
nlohmann::json registry_causal_payload_filter_zones(const nlohmann::json& payload,
                                                    const std::vector<std::string>& ids);
bool registry_causal_pilot_target_in_stem(const std::string& target, const std::string& stem);
nlohmann::json registry_causal_pilot_plan_to_json(const RegistryCausalPilotPlan& plan);
std::string registry_causal_pilot_plan_markdown(const RegistryCausalPilotPlan& plan);

// Notebook del worker: símbolos de la ficha + lo que vayan devolviendo las tools.
struct RegistryCausalPilotWorkerNotebook {
  std::vector<std::string> allowed_targets;
  std::vector<std::string> allowed_paths;
  std::string notes;
  int n_need_code = 0;
  int n_outline = 0;
  int n_follow = 0;
  int n_query_nudge = 0;
  bool used_tool() const { return n_need_code + n_outline + n_follow > 0; }
  bool used_code_or_follow() const { return n_need_code + n_follow > 0; }
};

// Trabajador sordo: una ficha, un stem, catálogo need_code|outline|follow. Máx. kPilotWorkerMaxSteps.
struct RegistryCausalPilotWorkerReport {
  bool ok = false;
  bool need_code = false;
  bool is_tool = false;
  std::string tool;    // need_code | outline | follow
  std::string action;  // causal_pilot_worker_v1 | causal_pilot_need_code | …_outline | …_follow
  std::string kind;    // cubre | como | gap
  std::string zone;
  std::string stem;
  std::string verdict;  // cubre | no_cubre | chain | missing | found
  bool covers = false;
  std::string owns;
  std::string chain;
  std::string path_symbol;
  std::string port_to;
  std::string target;
  std::string direction;  // incoming | outgoing (follow)
  std::string why;
  std::string brief;  // 2 frases para el piloto: qué hace el barrio / qué falta
  std::string raw;
  std::string error;
  int steps = 0;
};

struct RegistryCausalPilotPlenary {
  bool ok = false;
  std::string action;   // causal_pilot_plenary_v1
  std::string verdict;  // entiendo | no_entiendo | abandono
  std::vector<std::string> keep;
  std::vector<std::string> drop;
  std::string why;
  std::string raw;
  std::string error;
};

void registry_causal_pilot_notebook_from_payload(const nlohmann::json& payload,
                                                 const std::string& stem,
                                                 RegistryCausalPilotWorkerNotebook* nb);
bool registry_causal_pilot_target_in_notebook(const std::string& target,
                                              const RegistryCausalPilotWorkerNotebook& nb);
void registry_causal_pilot_notebook_add_target(RegistryCausalPilotWorkerNotebook* nb,
                                               const std::string& target);
std::string registry_causal_pilot_allowed_markdown(const RegistryCausalPilotWorkerNotebook& nb);
std::string registry_causal_pilot_follow_markdown(const nlohmann::json& payload,
                                                  const std::string& target,
                                                  const std::string& direction,
                                                  const std::string& stem,
                                                  std::vector<std::string>* new_targets,
                                                  std::string* port_to);
std::string registry_causal_pilot_worker_system_prompt(const std::string& kind,
                                                       const std::string& stem);
std::string registry_causal_pilot_worker_user_prompt(
    const std::string& kind, const std::string& question, const std::string& zone_markdown,
    const std::string& notes = {}, const std::string& allowed_markdown = {});
RegistryCausalPilotWorkerReport registry_parse_causal_pilot_worker(
    const std::string& raw, const std::string& expected_kind, const std::string& expected_stem,
    const std::string& query = {}, const RegistryCausalPilotWorkerNotebook* notebook = nullptr);
nlohmann::json registry_causal_pilot_worker_to_json(const RegistryCausalPilotWorkerReport& r);
std::string registry_causal_pilot_worker_markdown(const RegistryCausalPilotWorkerReport& r);

std::string registry_causal_pilot_plenary_system_prompt();
std::string registry_causal_pilot_plenary_user_prompt(const std::string& reports_markdown);
RegistryCausalPilotPlenary registry_parse_causal_pilot_plenary(
    const std::string& raw, const std::vector<std::string>& allowed_stems);
void registry_tally_pilot_plenary(RegistryCausalPilotPlenary* plenary,
                                  const std::vector<RegistryCausalPilotWorkerReport>& reports);
nlohmann::json registry_causal_pilot_plenary_to_json(const RegistryCausalPilotPlenary& p);
std::string registry_causal_pilot_plenary_markdown(const RegistryCausalPilotPlenary& p);

std::string registry_causal_judge_system_prompt();
std::string registry_causal_judge_user_prompt(const std::string& cards_markdown);
std::string registry_causal_synth_system_prompt();
std::string registry_causal_synth_user_prompt(const std::string& cards_markdown,
                                             const std::string& hypothesis,
                                             const std::string& anchor_why = {});
std::vector<std::string> registry_causal_judge_zone_ids(const std::string& cards_markdown);
RegistryCausalJudgeDecision registry_parse_causal_judge_decision(
    const std::string& raw, const std::vector<std::string>& allowed_zone_ids);
nlohmann::json registry_causal_judge_decision_to_json(
    const RegistryCausalJudgeDecision& decision);

nlohmann::json registry_node_to_json(const RegistryNodeRow& n);
nlohmann::json registry_stats_to_json(const RegistryStats& s);

}  // namespace tuide
