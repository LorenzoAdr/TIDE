#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tuide {

// Phase A (locate) types — see docs/plans/l2-explore-phase-a-b.md.
// Feature flag: L2_EXPLORE_PHASE_A (off by default).

enum class AVerdictKind {
  Useful,
  Reject,
  Uncertain,
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

struct AState {
  std::vector<AQueueItem> queue;
  int cursor = 0;           // next index into queue
  int peeks_used = 0;
  int turns = 0;
  int expansions = 0;       // ranking-miss expansions used
  std::vector<ALocus> loci_draft;
  std::vector<AVerdict> notes;  // durable verdicts (bodies discarded)
  std::vector<std::string> orphans;       // Instruction facets/idents still uncovered
  std::vector<std::string> rejected_stems;
  bool done = false;
};

inline constexpr int kAMaxPeeksPerTurn = 5;
inline constexpr int kAMaxPeeksTotal = 32;
inline constexpr int kAMaxTurns = 8;
inline constexpr int kAMaxQueueDefault = 40;
inline constexpr int kAMaxExpansions = 2;
inline constexpr int kAMaxEscapeHatches = 2;

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
  int max_per_stem = 4;     // diversify so turn-1 is not one module
  int long_body_lines = 120;
  bool prefer_tail_for_functions = true;
};

std::vector<AQueueItem> build_a_scan_queue(const std::vector<AQueueBuildInput>& ranked,
                                           const AQueueBuildOpts& opts = {});

// Seed AState.queue from ranked map-like inputs (resets cursor; keeps notes/loci).
void a_state_seed_queue(AState* st, const std::vector<AQueueBuildInput>& ranked,
                        const AQueueBuildOpts& opts = {});

const char* a_verdict_kind_name(AVerdictKind kind);
AVerdictKind parse_a_verdict_kind(const std::string& s);

const char* a_locus_role_name(ALocusRole role);
ALocusRole parse_a_locus_role(const std::string& s);

AVerdict parse_a_verdict_json(const nlohmann::json& j);
ALocus parse_a_locus_json(const nlohmann::json& j);

bool parse_a_verdicts_array(const nlohmann::json& j, std::vector<AVerdict>* out, std::string* err);
bool parse_a_loci_array(const nlohmann::json& j, std::vector<ALocus>* out, std::string* err);

nlohmann::json a_state_to_json(const AState& st);
bool a_state_from_json(const nlohmann::json& j, AState* out, std::string* err);

std::string a_notes_markdown(const AState& st);

}  // namespace tuide
