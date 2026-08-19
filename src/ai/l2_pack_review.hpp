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

// path:Symbol plan targets from ranked map menu lines.
std::vector<std::string> plan_targets_from_map_hits(const std::vector<std::string>& hit_menu,
                                                     int max_targets = 3);

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
