#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tuide {

// Phase A (locate) types — see docs/plans/l2-explore-phase-a-b.md.
// Feature flag: L2_EXPLORE_PHASE_A (promoted on in features_promoted.json).

enum class AVerdictKind {
  Useful,
  Reject,
  Uncertain,
  Interesting,  // trail: deepen this stack/path (not yet edit-site)
  Unknown,
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
  std::vector<ATrailStack> pending_stacks;  // shown this turn
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

// Flat input for queue builder (avoids pulling RepoMap into every translate unit).
struct AQueueBuildInput {
  std::string file;
  std::string name;
  int line = 0;
  int score = 0;
  bool functionish = true;  // methods/fns → prefer #tail when body length unknown
  int body_lines = 0;       // 0 = unknown; if > long_body_lines → #tail first
  std::string stem;         // optional; derived from file basename if empty
};

struct AQueueBuildOpts {
  std::size_t max_items = static_cast<std::size_t>(kAMaxQueueDefault);
  int max_per_stem = 2;     // diversify so early peeks are not one module
  int long_body_lines = 120;
  bool prefer_tail_for_functions = true;
  bool diversify_path_family = true;  // round-robin ui/ai/lsp/…
  bool prefer_src_paths = true;       // fill queue from src/ first; tests/tools → tail/reserve
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
