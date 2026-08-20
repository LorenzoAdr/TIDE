#pragma once

#include <string>
#include <vector>

namespace tuide {

struct PackReviewVerdict {
  bool ok = false;
  std::string verdict;  // covered | partial | miss
  std::string reason;
  std::vector<std::string> present;
  std::vector<std::string> missing;
  std::vector<std::string> reject;  // optional path:Symbol to evict from watchlist/pack
  float confidence = 0.f;
  std::string raw;
  std::string error;
};

// Compact digest of pack.md for semantic review (targets + snippet heads).
std::string build_pack_digest(const std::string& pack_body, std::size_t max_chars = 4200);

// Read query (+ optional ## Distilled intent) from session.md.
std::string extract_session_instruction_block(const std::string& session_body);

std::string extract_distilled_intent_block(const std::string& session_body);

PackReviewVerdict parse_pack_review_json(const std::string& raw);

std::string pack_review_system_prompt();

std::string pack_review_user_prompt(const std::string& instruction_block,
                                    const std::string& distilled_block,
                                    const std::string& pack_digest,
                                    const std::vector<std::string>& watchlist);

// English search terms for runtime expand (from review missing + distilled intent).
std::vector<std::string> review_search_terms(const PackReviewVerdict& verdict,
                                             const std::string& distilled_block, int max_terms);

// Parse rg search output into short menu lines for L2 replan.
std::vector<std::string> parse_search_hits_menu(const std::string& search_text, int max_hits);

// Drop hits whose file path already appears in watchlist targets.
std::vector<std::string> filter_search_hits_excluding_watchlist(
    const std::vector<std::string>& hits, const std::vector<std::string>& watchlist);

// Skip review search terms already executed this session.
std::vector<std::string> filter_unused_review_search_terms(
    const std::vector<std::string>& terms, const std::vector<std::string>& already_used);

// Reuse an existing pack fragment body for delta fetch (empty if missing).
std::string load_pack_fragment_body(const std::string& pack_body, const std::string& target);

bool target_in_watchlist_normalized(const std::string& target,
                                    const std::vector<std::string>& watchlist);

bool target_in_rejected_normalized(const std::string& target,
                                   const std::vector<std::string>& rejected);

// Map free-text pack_review reject tokens to watchlist path:Symbol entries for prune.
std::vector<std::string> expand_review_rejects_for_watchlist(
    const std::vector<std::string>& rejects, const std::vector<std::string>& watchlist);

// True when every non-empty plan target path is already represented on the watchlist.
bool all_plan_target_paths_in_watchlist(const std::vector<std::string>& targets,
                                        const std::vector<std::string>& watchlist);

// When review grep returns no hits: suggest path:Symbol lines from ## Ranked map.
std::vector<std::string> ranked_map_fallback_hits(const std::string& session_body,
                                                  const std::vector<std::string>& watchlist,
                                                  const PackReviewVerdict& verdict,
                                                  const std::string& distilled_block,
                                                  int max_hits = 10);

// Replan menu from full map_last.md (not compacted session map).
std::vector<std::string> ranked_map_replan_hits(const std::string& map_last_body,
                                                 const std::vector<std::string>& watchlist,
                                                 const std::vector<std::string>& rejected,
                                                 const PackReviewVerdict& verdict,
                                                 const std::string& distilled_block,
                                                 int max_hits = 12);

// Top ranked map entries whose file path is not yet in watchlist (pushback / escalación).
std::vector<std::string> ranked_map_unseen_hits(const std::string& map_last_body,
                                                 const std::vector<std::string>& watchlist,
                                                 const std::vector<std::string>& rejected,
                                                 int max_hits = 8);

// Prefer path:line (map locus) over path:Symbol when the menu has a numeric line.
std::vector<std::string> plan_targets_from_map_hits(const std::vector<std::string>& hit_menu,
                                                     int max_targets = 3);

// Top ranked-map / session-seed anchors that must not be denylisted.
std::vector<std::string> retrieval_anchor_targets(const std::string& map_last_body,
                                                  const std::string& session_body,
                                                  int max_map = 12);

// API siblings: same-file map neighbors + complementary verbs found in twin
// headers/sources under workspace_root (set_X→clear_X, busy→clear_/cancel_).
std::vector<std::string> expand_anchor_api_siblings(const std::vector<std::string>& seeds,
                                                     const std::string& map_last_body = {},
                                                     int max_extra = 8,
                                                     const std::string& workspace_root = {});

// True if pack.md has a real fragment body for any anchor path (not omit-only).
bool pack_has_anchor_fragment(const std::string& pack_body,
                              const std::vector<std::string>& anchors);

// Code fences only (ignore targets header / sibling notes / normalize lines).
std::string pack_code_fences_only(const std::string& pack_body);

// True when a get_code_of fragment matching `target` has a fence containing the
// requested symbol (path:Symbol) or non-trivial code (path:line).
bool pack_target_has_symbol_body(const std::string& pack_body, const std::string& target);

// Path-bearing anchors/targets that lack a real symbol body inside fences.
std::vector<std::string> pack_targets_missing_bodies(const std::string& pack_body,
                                                      const std::vector<std::string>& targets);

// Generic covered gate: at least min_ok path-bearing anchors have symbol bodies in fences.
bool pack_must_anchors_covered(const std::string& pack_body,
                               const std::vector<std::string>& anchors, int min_ok = 3);

// True when pack fences include both an open/set-side and a clear/cancel-side body.
bool pack_has_lifecycle_pair(const std::string& pack_body);

// Heuristic: target symbol looks like clear_/cancel_/reset_/stop_ side.
bool target_is_lifecycle_clear(const std::string& target);
// Heuristic: target symbol looks like set_/start_/enable_/busy side.
bool target_is_lifecycle_set(const std::string& target);

// Drop rejects that hit protected anchors (map top / seeds).
std::vector<std::string> filter_rejects_excluding_anchors(
    const std::vector<std::string>& rejects, const std::vector<std::string>& anchors);

// Invented missing symbols (no path/, not in map) → reject tokens for denylist.
std::vector<std::string> infer_invented_rejects(const PackReviewVerdict& verdict,
                                                 const std::string& map_last_body);

// Human-readable replan menu for L2 (MAP HITS / SEARCH HITS + rejected/missing).
std::string build_pack_replan_menu(const std::string& session_body,
                                   const std::string& map_last_body,
                                   const std::vector<std::string>& watchlist,
                                   const std::vector<std::string>& rejected,
                                   const PackReviewVerdict& verdict,
                                   const std::string& distilled_block,
                                   const std::vector<std::string>* hits_override = nullptr,
                                   const std::vector<std::string>* search_terms = nullptr,
                                   bool pushback = false);

}  // namespace tuide
