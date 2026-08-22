#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_feat.hpp"

namespace tuide {

struct SymbolIndexSnapshot;

// Phase A (locate) types — see docs/plans/l2-explore-phase-a-b.md.
// Feature flag: L2_EXPLORE_PHASE_A (promoted on in features_promoted.json).

enum class AVerdictKind {
  Useful,
  Reject,
  Uncertain,
  Interesting,  // trail: deepen this stack/path (not yet edit-site)
  Expand,       // A0 sniff: merits A1 peek/trail/dataflow (not locus)
  Unknown,
};

enum class AExpandModality {
  None,
  Peek,
  Trail,
  Dataflow,
};

enum class ALocusRole {
  Primary,
  Secondary,
  Suspect,
  Unknown,
};

struct AVerdict {
  std::string target;   // path:Symbol[#window] as shown in the peek tranche
  AVerdictKind verdict = AVerdictKind::Unknown;
  std::string anchor;   // path:Symbol | path:line | path:A-B
  std::string stem;
  ALocusRole role = ALocusRole::Unknown;
  std::string why;
  AExpandModality expand_with = AExpandModality::None;  // A0 expand
  std::string suspect_var;                              // dataflow hint
};

struct AExpansionItem {
  std::string target;
  AExpandModality modality = AExpandModality::Peek;
  std::string suspect_var;
  float score = 0.f;
};

struct ALocus {
  std::string stem;
  std::string anchor;
  ALocusRole role = ALocusRole::Unknown;
  std::string why;
  std::string window;  // head | tail | mid | hit (optional)
};

// One ranked window for the A scan queue (not necessarily 1:1 with a symbol).
struct AQueueItem {
  std::string target;  // path:Symbol or path:line, optionally #head|#mid|#tail|#hit
  std::string path;
  std::string stem;
  std::string symbol;
  int line = 0;
  std::string window_hint;  // empty | head | mid | tail | hit
  float score = 0.f;
  std::string map_related;  // L1 related= tokens (compact)
  int refs_in = 0;          // L1 refs≈N
  int body_sem_permille = 0;  // L1 body=0.57 → 570
  int file_rank = 0;          // L1 file_rank numerador
  int file_count = 0;         // L1 file_rank denominador
  bool dup_stem = false;
  int stem_sem_rank = 0;
};

// One hop on a call-path: call site + Tree-sitter scopes + control + local snippet.
struct ATrailHop {
  std::string path;
  std::string symbol;         // enclosing function/method (bare)
  std::string scope_chain;    // ns → type → fn (outer → inner)
  std::string signature;      // e.g. void AiController::begin_thinking()
  std::string control_kind;   // innermost if|switch|for|while|do|try|""
  std::string control_chain;  // nested controls call→…→fn, e.g. "if → switch"
  std::string control_cond;   // condition text(s), e.g. if (activity == AiThinking)
  int call_line = 0;          // 1-based call site
  std::string snippet;        // control block(s) and/or ±N lines around call
  std::string summary;        // 1–2 lines when compacted as parent
  std::string anchor;         // path:Symbol
  bool is_call_site = false;  // TS/heuristic: real call, not comment/string/def
};

// Full path entry → … → L0 (complete stack for one branch; not the whole graph).
struct ATrailStack {
  std::string id;  // S1, S2, …
  std::vector<ATrailHop> hops;
  AVerdictKind verdict = AVerdictKind::Unknown;
  std::string why;
};

// Conditional branch (ON / cancel / OFF / async handoff) — primary A1 map geometry.
struct ATrailCondBranch {
  std::string id;         // ON | CXL | OFF | LINK
  std::string when_text;  // guard / trigger
  std::string then_text;  // effect (calls, flag writes)
  std::string note;       // async gap, flag hints
  std::string anchor;
  std::string path;
  std::string symbol;
  int line = 0;
  std::string snippet;
  AVerdictKind verdict = AVerdictKind::Unknown;
  std::string why;
};

// Active call-hierarchy trail while Phase A deepens a useful hypothesis.
struct ATrail {
  bool active = false;
  bool awaiting_judge = false;
  std::string root_anchor;  // L0 hypothesis (peek useful)
  std::string root_stem;
  std::string root_why;
  std::string focus_anchor;  // current expansion focus (starts = root)
  std::string focus_symbol;
  int depth = 0;             // levels deepened from L0
  int branches_open = 0;
  int invalidations = 0;
  std::vector<ATrailHop> trail;           // confirmed parents (L0 → … → focus), summaries
  std::vector<ATrailStack> pending_stacks;  // shown this turn (supporting call-stacks)
  std::vector<ATrailCondBranch> cond_branches;  // ON/CXL/OFF/LINK conditional map
  std::vector<std::string> force_queue;     // interesting stack ids still to traverse
};

struct AState {
  std::vector<AQueueItem> queue;
  std::vector<AQueueItem> reserve;  // ranked candidates beyond initial top-K (P3)
  int cursor = 0;                   // next index into queue
  int peeks_used = 0;
  int turns = 0;
  int expansions = 0;               // ranking-miss expansions used (cap kAMaxExpansions)
  int last_expand_layer = 0;        // 0 none, 1–3 last successful layer
  std::vector<ALocus> loci_draft;
  std::vector<AVerdict> notes;  // durable verdicts (bodies discarded)
  std::vector<std::string> orphans;       // Instruction facets/idents still uncovered
  std::vector<std::string> rejected_stems;
  std::vector<std::string> b_allow_paths;  // micro-A allowlist from Phase B miss (capa 4)
  bool done = false;
  bool expand_exhausted = false;  // hit expansion cap with still-empty useful
  ATrail trail;
  // Effect Summary A0/A1 (L2_EXPLORE_EFFECT_SUMMARY).
  std::string a_subphase;  // a0_sniff | a1_peek | a1_trail | a1_dataflow | empty=classic
  int cards_used = 0;
  int a0_turns = 0;
  std::vector<std::string> a0_shown_targets;  // cards in current A0 tranche (awaiting verdict)
  std::vector<std::string> seeds;
  std::vector<AExpansionItem> a1_queue;
  AExpansionItem a1_active;
  bool a1_active_set = false;
  std::string a1_suspect_context;  // interesting stacks md for a1_suspect_vars
  bool a1_suspect_done = false;    // one suspect-vars pass per A1 trail expand
  std::string a1_job_root;         // A0 expand target for current A1 job chain
  std::string a1_trail_recap;      // trail frame kept through suspect+dataflow
  std::string a1_df_scope_path;    // rg scope: interesting caller file (not L0)
  std::string a1_df_caller_anchor; // interesting hop anchor for coherence check
};

inline constexpr int kAMaxPeeksPerTurn = 5;
inline constexpr int kAMaxPeeksTotal = 32;
inline constexpr int kAMaxTurns = 8;
inline constexpr int kAMaxQueueDefault = 40;
inline constexpr int kAMaxExpansions = 2;
inline constexpr int kAMaxEscapeHatches = 2;
inline constexpr int kAMaxPrimaryLoci = 2;
inline constexpr int kAMaxSecondaryLoci = 2;
// Don't crown first useful: need breadth before a_done (unless budget exhausted).
inline constexpr int kAMinPeeksBeforeDone = 10;
inline constexpr int kAMinStemsBeforeDone = 4;
inline constexpr int kAMinPathFamiliesBeforeDone = 2;
// Call-hierarchy trail (human-style deepen from an interesting peek).
inline constexpr int kATrailMaxStacks = 3;  // fewer, better-ranked for 7B
inline constexpr int kATrailMaxDepth = 4;
inline constexpr int kATrailMaxInterestingPerLevel = 3;
inline constexpr int kATrailMaxBranches = 2;
inline constexpr int kATrailCallSitePad = 6;       // ± lines around call
inline constexpr int kATrailMaxHopSnippetChars = 900;
inline constexpr int kATrailMinCallersToFalsify = 2;  // need ≥N stacks before auto-invalidate

// A0 Effect Summary (docs/plans/l2-explore-effect-summary.md)
inline constexpr int kA0MaxCardsPerTurn = 5;
// Rerank considers this many queue items from cursor, then shows ≤ kA0MaxCardsPerTurn.
inline constexpr int kA0RerankWindow = 15;
inline constexpr int kA0MaxCardsTotal = 64;
inline constexpr int kA0MaxTurns = 4;
inline constexpr int kA0MaxCharsPerTurn = 8000;
inline constexpr int kA0MaxNotesChars = 1500;
inline constexpr int kA0MaxExpandPerTurn = 4;
inline constexpr int kA0MaxExpandTotal = 12;
inline constexpr int kA1MaxPeeks = 16;
inline constexpr int kA1MaxTrails = 4;
inline constexpr int kA1MaxDataflows = 4;

inline bool a_effect_summary_enabled() {
  return l2_feat::enabled("L2_EXPLORE_PHASE_A") && l2_feat::enabled("L2_EXPLORE_EFFECT_SUMMARY");
}

const char* a_expand_modality_name(AExpandModality m);
AExpandModality parse_a_expand_modality(const std::string& s);

// L0 symptom APIs (busy/spinner/thinking) — A0 prefers trail over dataflow.
std::string a_target_symbol_name(const std::string& target);
bool a_is_symptom_edge_name(const std::string& name);
bool a_writes_suggest_trail_a0(const std::vector<std::string>& writes);
bool a_target_prefers_trail_a0(const std::string& target,
                               const std::vector<std::string>* writes = nullptr);
AExpandModality a_coerce_a0_expand_modality(const std::string& target, AExpandModality m,
                                            const std::vector<std::string>* writes = nullptr);

// True when explore_a should sniff fichas instead of body peeks.
bool a_in_a0_sniff(const AState& st);

// Compact a_notes for A0 prompt budget (§3.4).
std::string a_notes_markdown_compact(const AState& st, int max_chars = kA0MaxNotesChars);

// Queue score for anti-false-reject (top-15 or hot∩seeds).
float a_queue_item_score(const AState& st, const std::string& target);

// Items actually shown in one A0 tranche (reorder + char budget), mirroring session markdown.
struct A0TrancheShown {
  std::vector<AQueueItem> items;
  int slice_n = 0;
  int card_chars = 0;
  bool char_truncated = false;
};

struct A0TrancheBuildOpts {
  const SymbolIndexSnapshot* symbol_snapshot = nullptr;
};

A0TrancheShown a_build_a0_tranche_shown(const std::string& workspace_root, const AState& st,
                                        int max_cards,
                                        const A0TrancheBuildOpts* opts = nullptr);

bool a_target_matches_verdict_anchor(const std::string& queue_target,
                                     const std::string& verdict_target);

// Apply A0 sniff verdicts (expand/reject/uncertain); returns err if batch invalid.
// When workspace_root is set, every shown card must receive a matching verdict.
bool a_apply_a0_verdicts(AState* st, const std::vector<AVerdict>& verdicts, std::string* err,
                         const std::string* workspace_root = nullptr);

// Post-trail A1: queue dataflow from suspect vars (max 2); advances a_subphase.
bool a_apply_a1_suspect_verdicts(AState* st, const std::vector<AVerdict>& verdicts,
                                 std::string* err);
std::string a_build_a1_suspect_context(const AState& st, const std::vector<AVerdict>& verdicts);

// Path portion of path:Symbol[#window] anchor.
std::string a_path_from_anchor(const std::string& anchor);

// A0 dataflow without prior trail — only strong member-like idents, not L0/symptom paths.
bool a_a0_dataflow_allowed_without_trail(const std::string& target,
                                         const std::string& suspect_var);

// Stable sort: trail → peek → dataflow.
void a_sort_a1_queue(std::vector<AExpansionItem>* queue);

// Capture interesting-hop scope + recap after a_trail_judge (A1).
void a_fill_a1_trail_frame(AState* st, const std::vector<AVerdict>& verdicts);

// Reset trail-frame fields when starting a fresh A1 job from the queue.
void a_a1_begin_job(AState* st, const AExpansionItem& item);

// Dataflow incoherent → reopen trail deepen (keeps recap + trail.active).
void a_a1_backtrack_to_trail(AState* st);

// Clear trail-frame after successful A1 confirm or job discard.
void a_a1_clear_trail_frame(AState* st);

// Resolve path_hint for A1 dataflow (scoped caller file when trail frame set).
std::string a_a1_dataflow_path_hint(const AState& st, const AExpansionItem& item);

// Flat input for queue builder (avoids pulling RepoMap into every translate unit).
struct AQueueBuildInput {
  std::string file;
  std::string name;
  int line = 0;
  int score = 0;
  bool functionish = true;  // methods/fns → prefer #tail when body length unknown
  int body_lines = 0;       // 0 = unknown; if > long_body_lines → #tail first
  std::string stem;         // optional; derived from file basename if empty
  std::string map_related;  // L1 related=… (compact)
  int refs_in = 0;          // L1 refs≈N
  int body_sem_permille = 0;
  int file_rank = 0;
  int file_count = 0;
  bool dup_stem = false;
  int stem_sem_rank = 0;
};

struct AQueueBuildOpts {
  std::size_t max_items = static_cast<std::size_t>(kAMaxQueueDefault);
  int max_per_stem = 3;  // light cap; trail deepens, no need for harsh stem starve
  int long_body_lines = 120;
  bool prefer_tail_for_functions = true;
  // Family RR was for pre-trail “force ai/ into early peeks”; with call-hierarchy,
  // prefer map score order in the primary queue (see a_state_seed_queue).
  bool diversify_path_family = false;
  bool prefer_src_paths = true;  // fill queue from src/ first; tests/tools → tail/reserve
};

std::vector<AQueueItem> build_a_scan_queue(const std::vector<AQueueBuildInput>& ranked,
                                           const AQueueBuildOpts& opts = {});

// Seed queue (top max_items) + reserve (next slice) for P3 expansion.
void a_state_seed_queue(AState* st, const std::vector<AQueueBuildInput>& ranked,
                        const AQueueBuildOpts& opts = {});

// Instruction needles/idents not yet covered by useful anchors/notes.
std::vector<std::string> a_compute_orphans(const AState& st,
                                           const std::vector<std::string>& needles);

struct AExpandResult {
  bool expanded = false;
  int layer = 0;       // 1 = extend reserve, 2 = re-rank by orphans, 3 = recall extras
  int added = 0;
  bool exhausted = false;
  std::string reason;
};

// Expand when A is empty/weak at queue end or orphans remain. Cap: kAMaxExpansions.
// Layer 3 may inject `extra_recall` (stem/search hits from the index).
AExpandResult maybe_expand_a_queue(AState* st, const std::vector<std::string>& orphans,
                                   const std::vector<AQueueBuildInput>& extra_recall = {},
                                   const AQueueBuildOpts& opts = {});

// True if plan target path/stem is allowed in explore_b (loci or micro-A allowlist).
bool a_plan_target_allowed(const AState& st, const std::string& target);

// Sort loci primary → secondary → suspect for must-tier watchlist.
std::vector<ALocus> a_loci_must_ordered(std::vector<ALocus> loci);

// Fix model sloppiness: anchor from target if needed; stem = file basename.
void a_normalize_verdict(AVerdict* v);
void a_normalize_locus(ALocus* loc);
bool a_anchor_resolvable(const std::string& anchor);

// Cap roles in-place (primary ≤ kAMaxPrimaryLoci, then secondary ≤ kAMaxSecondaryLoci).
void a_cap_locus_roles(std::vector<ALocus>* loci);

// True when A budgets are exhausted (allows a_done without reject contrast).
bool a_budget_relaxed(const AState& st);

// Path family for diversity/contrast: src/ui/foo.cpp → "ui", src/ai/x → "ai".
std::string a_path_family(const std::string& path_or_target);

int a_notes_distinct_stems(const AState& st);
int a_notes_path_families(const AState& st);
int a_queue_path_families(const AState& st);

// True if peeks/stems breadth is enough to allow a_done (item 4).
bool a_enough_locate_breadth(const AState& st);

// Validate a_done loci against notes/state. On failure writes err and returns false.
bool a_validate_a_done(const AState& st, const std::vector<ALocus>& loci, std::string* err);

// Parse L1/L2 ranked-map markdown lines into queue inputs (best-effort).
// Accepts: "1. src/foo.cpp:42 — `Sym`" / "1. src/foo.cpp:1  [score=…] — `name`"
std::vector<AQueueBuildInput> a_queue_inputs_from_ranked_map_markdown(const std::string& map_md,
                                                                      std::size_t max_n = 80);

struct AQueueMapFilterOpts {
  std::size_t want_n = 20;
  bool skip_header_with_cpp = true;
  bool deprioritize_tests = true;
  bool skip_file_level = true;       // file src/foo.cpp:1 — `foo`
  bool skip_examples = true;
  int max_generic_cancel = 3;        // cap bare cancel()/Class::cancel noise
  int max_per_stem = 2;
  int orphan_rescue_slots = 2;
  std::vector<std::string> orphans;
};

// Skip hpp when .cpp sibling exists, dedup path:Symbol, backfill beyond top-N.
std::vector<AQueueBuildInput> a_queue_inputs_from_ranked_map_filtered(
    const std::string& map_md, const AQueueMapFilterOpts& opts,
    const std::string& workspace_root);

// Rerank + stem-cap within an A0 tranche slice (does not advance cursor).
std::vector<AQueueItem> a_order_a0_tranche(const std::vector<AQueueItem>& slice,
                                           const AState& st, int max_n);

const char* a_verdict_kind_name(AVerdictKind kind);
AVerdictKind parse_a_verdict_kind(const std::string& s);

const char* a_locus_role_name(ALocusRole role);
ALocusRole parse_a_locus_role(const std::string& s);

AVerdict parse_a_verdict_json(const nlohmann::json& j);
ALocus parse_a_locus_json(const nlohmann::json& j);

bool parse_a_verdicts_array(const nlohmann::json& j, std::vector<AVerdict>* out, std::string* err);
bool parse_a_loci_array(const nlohmann::json& j, std::vector<ALocus>* out, std::string* err);

// Trail stack verdicts: target = S1|S2|… (or stack id).
bool parse_a_trail_verdicts_array(const nlohmann::json& j, std::vector<AVerdict>* out,
                                  std::string* err);
bool a_validate_a_trail_judge(const AState& st, const std::vector<AVerdict>& verdicts,
                              std::string* err);

// Start trail from a useful peek hypothesis (does not crown primary).
void a_trail_begin(AState* st, const AVerdict& useful);

// Invalidate L0 after all stacks rejected; clears trail and demotes useful→reject.
void a_trail_invalidate_root(AState* st, const std::string& why);

// Apply interesting/reject on pending stacks; returns true if trail still active.
// On interesting: fills force_queue (runtime must deepen those before next queue peeks).
bool a_trail_apply_judge(AState* st, const std::vector<AVerdict>& verdicts, std::string* err);

nlohmann::json a_trail_to_json(const ATrail& tr);
bool a_trail_from_json(const nlohmann::json& j, ATrail* out);

nlohmann::json a_state_to_json(const AState& st);
bool a_state_from_json(const nlohmann::json& j, AState* out, std::string* err);

std::string a_notes_markdown(const AState& st);
std::string a_trail_stacks_markdown(const ATrail& tr);

// Enrich one call-site hop with TS scope chain + control + ±pad snippet.
// abs_path = absolute file; rel_path for anchors; call_line 1-based.
ATrailHop a_trail_enrich_hop(const std::string& abs_path, const std::string& rel_path,
                             int call_line, const std::string& called_symbol);

struct ATrailSearchHit {
  std::string path;  // workspace-relative preferred
  int line = 0;      // 1-based
  std::string preview;
};

// Enrich one call-site hop with TS scope chain + control + ±pad snippet.
ATrailHop a_trail_enrich_hop(const std::string& abs_path, const std::string& rel_path,
                             int call_line, const std::string& called_symbol);

// Direct-caller stacks (1 hop + L0). Prefer a_trail_build_full_stacks with a searcher.
std::vector<ATrailStack> a_trail_build_stacks(const std::string& workspace_root,
                                              const std::string& focus_symbol,
                                              const std::string& focus_path_hint,
                                              const std::vector<ATrailSearchHit>& search_hits,
                                              int max_stacks = kATrailMaxStacks,
                                              int max_depth = kATrailMaxDepth);

// Build complete paths: for each ranked direct caller, climb parents via `search`
// until max_depth. `search(symbol)` returns call-site hits (no LSP).
std::vector<ATrailStack> a_trail_build_full_stacks(
    const std::string& workspace_root, const std::string& focus_symbol,
    const std::string& focus_path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    int max_stacks = kATrailMaxStacks, int max_depth = kATrailMaxDepth);

// Conditional branches ON/CXL/OFF/LINK from seeds + stacks (async/state-aware).
std::vector<ATrailCondBranch> a_trail_build_cond_branches(
    const std::string& workspace_root, const std::string& focus_symbol,
    const std::string& focus_path_hint, const std::vector<std::string>& seeds,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    const std::vector<ATrailStack>& stacks);

// --- Data-flow (rg-only, no LSP) ------------------------------------------------
// Heuristic write/read/decl sites for a variable/field name. Not SSA/taint;
// good enough to reserve suspect_vars for Phase B and to probe by hand.

inline constexpr int kADataFlowMaxWrites = 12;
inline constexpr int kADataFlowMaxReads = 12;
inline constexpr int kADataFlowMaxDecls = 6;
inline constexpr int kADataFlowSnippetPad = 2;

enum class ADataFlowKind { Unknown = 0, Decl, Write, Read };

inline const char* a_dataflow_kind_name(ADataFlowKind k) {
  switch (k) {
    case ADataFlowKind::Decl:
      return "decl";
    case ADataFlowKind::Write:
      return "write";
    case ADataFlowKind::Read:
      return "read";
    default:
      return "?";
  }
}

struct ADataFlowSite {
  std::string path;
  int line = 0;
  ADataFlowKind kind = ADataFlowKind::Unknown;
  std::string preview;
  std::string snippet;
};

struct ADataFlowReport {
  std::string name;
  std::string path_hint;
  std::vector<ADataFlowSite> decls;
  std::vector<ADataFlowSite> writes;
  std::vector<ADataFlowSite> reads;
  int raw_hits = 0;
  int dropped = 0;  // non-srcish / no word-boundary / truncated buckets
};

// Classify a source line mentioning `name` (word-ish). Pure string heuristics.
ADataFlowKind a_dataflow_classify_line(const std::string& line, const std::string& name);

// Build report from search hits (caller supplies rg via search tool).
ADataFlowReport a_dataflow_build(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::vector<ATrailSearchHit>& hits, int max_writes = kADataFlowMaxWrites,
    int max_reads = kADataFlowMaxReads, int max_decls = kADataFlowMaxDecls);

// Convenience: search(name) then build.
ADataFlowReport a_dataflow_build_with_search(
    const std::string& workspace_root, const std::string& name, const std::string& path_hint,
    const std::function<std::vector<ATrailSearchHit>(const std::string& symbol)>& search,
    int max_writes = kADataFlowMaxWrites, int max_reads = kADataFlowMaxReads,
    int max_decls = kADataFlowMaxDecls);

std::string a_dataflow_markdown(const ADataFlowReport& r);
nlohmann::json a_dataflow_to_json(const ADataFlowReport& r);

}  // namespace tuide
