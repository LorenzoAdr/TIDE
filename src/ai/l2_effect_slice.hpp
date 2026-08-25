#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_explore_a.hpp"

namespace tuide {

// Bounded effect-slice registry (no LLM). Query unit is a ranked thread, not a
// "critical" function. Control (if/switch) is a node: it imports a latch.

inline constexpr int kEffectSliceMaxNodes = 800;
inline constexpr int kEffectSliceMaxFacts = 12000;
inline constexpr int kEffectSliceMaxSeedFn = 80;
inline constexpr int kEffectSliceMaxCtrlPerFn = 6;
inline constexpr int kEffectSliceMaxCallersPerFn = 12;
inline constexpr int kEffectSliceMaxSiblingsPerFile = 256;
inline constexpr int kEffectSliceMaxExpand = 3;
inline constexpr int kEffectSliceMaxThreads = 5;
inline constexpr int kEffectSliceMaxConstellations = 8;
inline constexpr int kEffectSlicePprIters = 20;
inline constexpr float kEffectSlicePprDamp = 0.85f;
inline constexpr int kEffectSliceMapTopDefault = 40;

enum class EffectNodeKind { Fn, Ctrl, Latch, Handoff };
enum class EffectCtrlKind { None, If, Switch, Case, Loop, Guard };
enum class EffectFactKind {
  Call,
  EnterCtrl,
  Then,
  Else,
  Case,
  Default,
  Fallthrough,
  Wrap,
  Write,
  Read,
  Handoff,
  Contains
};

const char* effect_node_kind_name(EffectNodeKind k);
const char* effect_ctrl_kind_name(EffectCtrlKind k);
const char* effect_fact_kind_name(EffectFactKind k);
EffectNodeKind parse_effect_node_kind(const std::string& s);
EffectCtrlKind parse_effect_ctrl_kind(const std::string& s);
EffectFactKind parse_effect_fact_kind(const std::string& s);

struct EffectNode {
  std::string id;
  EffectNodeKind kind = EffectNodeKind::Fn;
  EffectCtrlKind ctrl_kind = EffectCtrlKind::None;
  std::string path;
  std::string symbol;
  std::string anchor;
  std::string stem;
  std::string parent_fn;
  std::string parent_switch;
  std::string cond;
  int line = 0;
  float prior_sem = 0.f;
  float mass = 0.f;
  bool seed = false;
  bool query_hit = false;
  int query_rank = -1;
  bool cold = false;
};

struct EffectFact {
  std::string id;
  std::string from;
  std::string to;
  EffectFactKind kind = EffectFactKind::Call;
  std::string member;
  float w_edge = 1.f;
};

struct EffectThread {
  std::string id;
  std::vector<std::string> node_ids;
  std::vector<std::string> fact_ids;
  std::vector<std::string> latches;
  float score = 0.f;
  std::string why;
};

// Query-conditioned region around a shared-state latch. Unlike EffectThread,
// this is a small subgraph rather than one ordered path.
struct EffectConstellation {
  std::string id;
  std::string center_id;
  std::string member;
  std::vector<std::string> node_ids;
  std::vector<std::string> fact_ids;
  std::vector<std::string> writer_ids;
  std::vector<std::string> reader_ids;
  std::vector<std::string> control_ids;
  std::vector<std::string> handoff_ids;
  // Stems owning direct writer/reader roles. primary_stems remains the ranked
  // compatibility view of this atomic core; call/handoff neighbors stay context.
  std::vector<std::string> core_stems;
  std::vector<std::string> context_stems;
  std::vector<std::string> primary_stems;
  std::vector<std::string> peripheral_stems;
  float score = 0.f;
  float mass_coverage = 0.f;
  std::string why;
};

// Query-conditioned causal zone made of one or more atomic constellations plus
// short, witnessed call/handoff arms. Atomic nuclei remain available as facets.
struct EffectMacroConstellation {
  std::string id;
  std::vector<std::string> nucleus_ids;
  std::vector<std::string> node_ids;
  std::vector<std::vector<std::string>> anchor_groups;
  std::vector<std::string> primary_stems;
  std::vector<std::string> merge_witnesses;
  float merge_strength = 0.f;
  float score = 0.f;
  float mass_coverage = 0.f;
  std::string why;
};

struct EffectFnMentions {
  std::vector<std::string> writes;
  std::vector<std::string> reads;
  std::vector<std::string> calls;
};

struct EffectSliceSeedFn {
  std::string path;
  std::string symbol;
  int line = 0;
  float prior_sem = 0.f;
  bool file_level = false;  // map row is a file hit → inventory, not a fake fn
};

struct EffectSliceSeedIn {
  std::string query;
  std::vector<std::string> seeds;
  std::vector<EffectSliceSeedFn> map_window;
  std::vector<std::string> inventory_paths;  // file-level map hits
  std::string extra_anchor;
  bool add_siblings = true;
  int window_n = kEffectSliceMaxSeedFn;
};

struct EffectSliceDeps {
  std::string workspace_root;
  std::function<std::vector<ATrailSearchHit>(const std::string& symbol)> search;
};

struct EffectSlice {
  std::string query;
  std::vector<std::string> seeds;
  std::vector<EffectNode> nodes;
  std::vector<EffectFact> facts;
  std::vector<EffectThread> threads;
  std::vector<EffectConstellation> constellations;
  std::vector<EffectMacroConstellation> macro_constellations;
  std::vector<std::string> pending_symbols;
  std::vector<std::string> holes;
  std::vector<std::string> rejected_thread_ids;
  int generation = 0;
  bool exhausted = false;
  bool add_siblings = true;
  std::unordered_set<std::string> summarized;
  std::unordered_set<std::string> searched;
  std::unordered_set<std::string> hop_origin;
  std::unordered_set<std::string> inventory_paths;
  std::unordered_map<std::string, std::string> file_source;
  std::unordered_map<std::string, EffectFnMentions> mentions;
  // Query-time: hub latches (kind/path/…) desbloqueados por cosine.
  std::unordered_set<std::string> unlocked_members;
};

// Parse ranked map markdown (including file-level rows). Does not use A0 skip_file_level.
void effect_slice_fill_seed_from_map(EffectSliceSeedIn* in, const std::string& map_md, int top_n);

bool effect_slice_seed(EffectSlice* s, const EffectSliceSeedIn& in, std::string* err);
bool effect_slice_build(EffectSlice* s, const EffectSliceDeps& deps, std::string* err);
void effect_slice_rank(EffectSlice* s, int k = kEffectSliceMaxThreads);
bool effect_hub_member(const std::string& member);
bool effect_query_unlocks_member(const std::string& query, const std::string& member);
std::string effect_latch_member_key(const std::string& symbol_or_id);
std::vector<EffectThread> effect_slice_threads(const EffectSlice& s, int k = kEffectSliceMaxThreads);
std::vector<EffectConstellation> effect_slice_constellations(
    const EffectSlice& s, int k = kEffectSliceMaxConstellations);
std::string effect_slice_view_markdown(const EffectSlice& s);
std::string effect_slice_stats_markdown(const EffectSlice& s);
void effect_slice_fail(EffectSlice* s, const std::vector<std::string>& rejected_ids,
                       const std::string& why);
bool effect_slice_expand(EffectSlice* s, const EffectSliceDeps& deps, std::string* err);

const EffectNode* effect_slice_find_node(const EffectSlice& s, const std::string& id);
int effect_slice_count_kind(const EffectSlice& s, EffectNodeKind k);

nlohmann::json effect_slice_to_json(const EffectSlice& s);
bool effect_slice_from_json(const nlohmann::json& j, EffectSlice* out, std::string* err);

}  // namespace tuide
