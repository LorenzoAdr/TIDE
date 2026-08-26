#include "ai/l2_problem_frame.hpp"
#include "ai/l2_graph_query_profile.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool ok, const char* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  tuide::ProblemFrame pf;
  std::string err;
  const char* raw = R"({
    "schema": "problem_frame_v1",
    "problem_kind": "debug",
    "problem_frame": "spinner stuck after model done",
    "primary_anchor": {
      "kind": "symptom_control",
      "objective": "control busy/spinner in chat panel",
      "search_terms": ["spinner", "busy", "set_busy", "clear_busy"],
      "edge_hints": ["set_", "clear_"]
    },
    "mechanism_gaps": [{"slot": "cleanup", "question": "who clears busy?"}],
    "reject_noise": ["infinito"],
    "anchor_confidence": "high"
  })";
  expect(tuide::problem_frame_from_json_string(raw, &pf, &err), "parse v1");
  expect(pf.primary_anchor.kind == "symptom_control", "anchor kind");
  expect(tuide::problem_frame_minimally_valid(pf), "minimally valid");
  const auto seeds = tuide::problem_frame_anchor_seeds(pf);
  expect(seeds.size() >= 4, "anchor seeds");

  tuide::ProblemFrame legacy;
  expect(tuide::problem_frame_from_json_string(
             R"({"intent":"x","primary_goal":"find busy","search_terms":["busy"]})", &legacy,
             &err),
         "legacy parse");
  expect(!legacy.primary_anchor.objective.empty(), "legacy objective");

  tuide::ProblemFrame fb = tuide::problem_frame_fallback_from_query(
      "spinner infinito en el chat aunque el modelo terminó");
  expect(fb.provenance == "deterministic_fallback", "fallback provenance");
  expect(!fb.primary_anchor.search_terms.empty(), "fallback terms");

  tuide::RegistryQueryOpts opts;
  const auto profile = tuide::graph_query_profile_default(tuide::GraphQueryPhase::AnchorHunt);
  expect(profile.hops == 0, "anchor hunt hops");
  tuide::graph_query_profile_apply(profile, &opts);
  expect(opts.hops == 0 && opts.top_k == 12, "apply profile");

  return failures == 0 ? 0 : 1;
}
