#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_explore_a.hpp"

namespace tuide {

struct SymbolIndexSnapshot;

// Effect Summary (Phase A0) — deterministic TS card, not LLM summary.
// See docs/plans/l2-explore-effect-summary.md.

inline constexpr int kEffectSummaryMaxLines = 16;
inline constexpr int kEffectSummaryMaxChars = 900;
inline constexpr int kEffectSummaryMaxList = 8;

struct EffectSummary {
  std::string path;         // workspace-relative
  std::string symbol;
  std::string anchor;       // path:Symbol
  std::string window_hint;  // head|tail|mid|hit|empty
  int start_line = 0;
  int end_line = 0;
  std::string sig;
  std::string kind;                     // fn|method|dtor|inline|file|fallback
  std::string path_fam;                 // ui|ai|lsp|…
  std::vector<std::string> roles;   // multi-tag ≤3
  std::vector<std::string> calls;
  std::vector<std::string> calls_seed;
  std::vector<std::string> writes;
  std::vector<std::string> reads;
  std::string ctrl;
  std::string guard;                    // outer if/return guard (seed overlap)
  std::vector<std::string> hot;
  std::vector<std::string> seed_match;
  std::vector<std::string> orphan_match;
  std::vector<std::string> callers;     // inbound ref files ×N (index)
  int body_lines = 0;
  int map_score = 0;
  std::string stem;
  std::string map_related;
  int refs_in = 0;
  int body_sem_permille = 0;
  int file_rank = 0;
  int file_count = 0;
  bool dup_stem = false;
  std::string nudge;
  std::string card_text;
};

struct EffectSummaryOpts {
  std::vector<std::string> seeds;
  int max_list = kEffectSummaryMaxList;
  int max_chars = kEffectSummaryMaxChars;
  int hint_line = 0;   // map line hint — desambiguar símbolos repetidos
  int map_score = 0;   // L1 score for rank: line
  std::string stem;
  std::string map_related;
  int refs_in = 0;
  int body_sem_permille = 0;
  int file_rank = 0;
  int file_count = 0;
  bool dup_stem = false;
  std::vector<std::string> orphans;
  std::string query;  // intent for lexical rerank / nudge
  const SymbolIndexSnapshot* symbol_snapshot = nullptr;
};

// Build card from absolute path + symbol name. Returns minimal card on TS miss.
EffectSummary effect_summary_build(const std::string& abs_path, const std::string& rel_path,
                                   const std::string& symbol, const std::string& window_hint,
                                   const EffectSummaryOpts& opts = {});

// Resolve queue item → card (workspace_root + AQueueItem).
EffectSummary effect_summary_for_queue_item(const std::string& workspace_root,
                                            const AQueueItem& item,
                                            const EffectSummaryOpts& opts = {});

// Quality signals for no-LLM batteries (seed overlap, budget, TS fallback).
struct EffectSummaryQuality {
  int card_chars = 0;
  int line_count = 0;
  bool within_budget = true;
  bool ts_fallback = false;
  int seed_hits = 0;  // calls + writes + hot
};

EffectSummaryQuality effect_summary_quality(const EffectSummary& es,
                                            const std::vector<std::string>& seeds);

// Seed-first truncation (§2.6 of plan).
std::vector<std::string> effect_summary_truncate_list(const std::vector<std::string>& items,
                                                      const std::vector<std::string>& seeds,
                                                      int max_n);

std::string effect_summary_render_card(const EffectSummary& es);
nlohmann::json effect_summary_to_json(const EffectSummary& es);

// Lexical rerank score for A0 tranche ordering (seeds + orphans + L1 meta + card body).
int effect_summary_lexical_rerank_score(const EffectSummary& es, const EffectSummaryOpts& opts);

// One row of card-based A0 rerank (for batteries / debug).
struct A0CardRankRow {
  AQueueItem item;
  EffectSummary es;
  int score = 0;
  int slice_rank = 0;  // 1-based position in map-order slice before rerank
};

// Reorder slice by Effect Summary card body (writes/hot/nudge/…), not queue metadata needles.
std::vector<AQueueItem> a_order_a0_tranche_by_card(
    const std::string& workspace_root, const std::vector<AQueueItem>& slice, const AState& st,
    int max_n, const A0TrancheBuildOpts* opts = nullptr,
    std::vector<A0CardRankRow>* rank_debug = nullptr);

}  // namespace tuide
