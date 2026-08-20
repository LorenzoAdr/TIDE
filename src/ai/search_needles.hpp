#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tuide {

// Split "a|b|c" / "a, b" / multiline into unique trimmed needles.
std::vector<std::string> split_search_needles(const std::string& arg);

// Project-agnostic identifier variants: snake both orders + CamelCase.
// Example: panel_performance → performance_panel, PanelPerformance, PerformancePanel.
std::vector<std::string> expand_identifier_variants(const std::string& needle);

// split + expand + dedupe (preserves first-seen order). Cap at max_n.
std::vector<std::string> expand_search_needles(const std::string& arg, std::size_t max_n = 12);

// Bonus when relative path / basename / stem contains any tried seed (case-insensitive).
// Longer seeds win. Tiny needles (<3) ignored for name matching.
int filename_seed_match_score(const std::string& relative_path,
                              const std::vector<std::string>& needles);

// Rank a workspace hit for the AI search tool (src/ first + filename seed bonus).
int score_search_hit(const std::string& relative_path, const std::string& matching_needle,
                     const std::vector<std::string>& all_needles);

// Intent hints shared by L0/L1 (avoid git tools on "dónde está el código…").
bool query_asks_code_location(const std::string& text);
// "dame contexto" / "give me context" → dump top matches with code bodies.
bool query_asks_context_dump(const std::string& text);
// "añade un tab/pestaña", "cambia el label", implement UI → L2 edit (not git_*).
bool query_asks_code_edit(const std::string& text);
bool query_asks_git_repo(const std::string& text);
bool is_git_repo_tool_name(const std::string& name);

// Pull snake_case / CamelCase tokens from NL (fallback when the model emits bad JSON).
std::vector<std::string> extract_code_tokens(const std::string& text, std::size_t max_n = 8);

// Expand NL tokens with common bilingual code equivalents for index retrieval
// (cierre→quit/close, buscador→search, …). Generic vocabulary only — no project file stems.
std::vector<std::string> expand_nl_retrieval_tokens(const std::vector<std::string>& tokens,
                                                   std::size_t max_n = 32,
                                                   std::string_view query_folded = {});

// Content facets from NL (folded, stopword-filtered). Used for multi-facet ranking.
std::vector<std::string> extract_query_facets(const std::string& text, std::size_t max_n = 12);

// How many query facets hit file/name/signature (expansions allowed per facet).
// Returns covered_count * 1000 + secondary hit strength.
int facet_coverage_score(const std::string& file, const std::string& name,
                         const std::string& signature, const std::vector<std::string>& facets);

// Split compound identifier into lowercase parts (snake/CamelCase); strips path:Symbol prefix.
std::vector<std::string> decompose_identifier_parts(const std::string& identifier);

// Drop structural/UI noise tokens unsuitable for body semantic query (update, ensure, hover, …).
bool is_semantic_token_noise(const std::string& token);

// Filter + dedupe semantic token list.
std::vector<std::string> filter_semantic_tokens(std::vector<std::string> tokens);

// L2 loose tokens + decomposed compound seeds + distilled terms → body embed query tokens.
std::vector<std::string> merge_semantic_tokens(const std::vector<std::string>& l2_tokens,
                                               const std::vector<std::string>& compound_seeds,
                                               const std::vector<std::string>& distilled_terms);

}  // namespace tuide
