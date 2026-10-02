#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "ai/l2_effect_registry.hpp"
#include "ai/l2_problem_frame.hpp"

namespace tuide {

struct EntityCandidate {
  std::string term;
  std::vector<std::string> aliases;
  // Combined score: concentration * hit_score (prefer high concentration AND many hits).
  float entityness = 0.f;
  float concentration = 0.f;  // stem mass share among hits (0..1)
  float hit_score = 0.f;      // saturating transform of hit_count (0..1)
  int hit_count = 0;          // total registry hits used in concentration
  std::string owner_stem;
  int owner_files = 0;
  bool twin = false;
  std::vector<std::string> evidence_ids;
};

struct EntitynessReport {
  std::string query;
  std::vector<EntityCandidate> candidates;
  nlohmann::json to_json() const;
};

// One chain link from ProblemFrame (primary or secondary), scored for entityness.
struct EntityLinkScore {
  std::string role;  // primary | secondary_0 | …
  std::string kind;
  std::string objective;
  std::vector<std::string> search_terms;
  float entityness = 0.f;
  float concentration = 0.f;
  float hit_score = 0.f;
  int hit_count = 0;
  std::string owner_stem;
  int owner_files = 0;
  bool twin = false;
  std::vector<std::string> evidence_ids;
};

struct EntitynessLinkReport {
  std::string query;
  std::vector<EntityLinkScore> links;
  float best_entityness = 0.f;
  std::string best_role;
  std::string explore_mode;  // f1_anchor | classic_scan
  float threshold = 0.45f;
  nlohmann::json to_json() const;
};

struct EntitynessOpts {
  // term -> aliases (must include the term itself if desired).
  std::unordered_map<std::string, std::vector<std::string>> aliases_by_term;
  int top_k = 24;
  // If true, also run card_attrs cosine (needs embedder + embeddings).
  bool use_card_attrs = false;
  // Min entityness on a link to choose F1 anchor hunt (else classic scan).
  float f1_threshold = 0.45f;
};

// Extract content nouns/tokens from a user prompt (generic stopword filter).
// Legacy helper for prompt-only probes; prefer entityness_score_problem_frame.
std::vector<std::string> entityness_prompt_terms(const std::string& query);

// Score a single bag of search terms (max over aliases). Used by link + legacy probes.
bool entityness_score_terms(EffectRegistry* r, const std::vector<std::string>& terms,
                            const RegistryEmbedFn& embed, const EntitynessOpts& opts,
                            EntityCandidate* out, std::string* err);

// Legacy: tokenize query → score each token. Prefer link scoring for production.
bool entityness_probe(EffectRegistry* r, const std::string& query, const RegistryEmbedFn& embed,
                      const EntitynessOpts& opts, EntitynessReport* out, std::string* err);

// Production path: score ProblemFrame primary + secondary + anchor_hypotheses (roles
// primary | secondary_N | hyp_N), then choose explore_mode.
bool entityness_score_problem_frame(EffectRegistry* r, const ProblemFrame& pf,
                                    const std::string& query, const RegistryEmbedFn& embed,
                                    const EntitynessOpts& opts, EntitynessLinkReport* out,
                                    std::string* err);

// Format compact block (debug / notes).
std::string entityness_prompt_block(const EntitynessReport& report, int max_rows = 8);
std::string entityness_links_prompt_block(const EntitynessLinkReport& report, int max_rows = 8);

// hit_score = hits / (hits + half_life). Combined entityness = concentration * hit_score.
inline constexpr float kEntitynessHitHalfLife = 2.f;
inline constexpr float kEntitynessF1ThresholdDefault = 0.45f;

inline float entityness_hit_score(int hits, float half_life = kEntitynessHitHalfLife) {
  if (hits <= 0 || half_life <= 0.f) {
    return 0.f;
  }
  return static_cast<float>(hits) / (static_cast<float>(hits) + half_life);
}

inline float entityness_combine(float concentration, int hits,
                                float half_life = kEntitynessHitHalfLife) {
  const float c = std::max(0.f, std::min(1.f, concentration));
  return std::max(0.f, std::min(1.f, c * entityness_hit_score(hits, half_life)));
}

}  // namespace tuide
