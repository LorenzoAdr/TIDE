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

  tuide::ProblemFrame raw_pf;
  expect(tuide::problem_frame_from_json_string(raw, &raw_pf, &err), "raw for refine");
  raw_pf.primary_anchor.search_terms.push_back("loading state");  // NL — must drop
  tuide::problem_frame_refine_from_query(
      &raw_pf, "spinner infinito en chat IA aunque el modelo terminó");
  bool has_nl = false;
  bool injected_ai = false;
  for (const auto& t : raw_pf.primary_anchor.search_terms) {
    if (t.find(' ') != std::string::npos) {
      has_nl = true;
    }
    if (t == "ai_controller" || t == "busy_strip" || t == "level2_autonomous_loop") {
      injected_ai = true;
    }
  }
  expect(!has_nl, "refine drops spaced NL terms");
  expect(!injected_ai, "refine does not inject domain stems");

  tuide::RegistryQueryOpts opts;
  const auto profile = tuide::graph_query_profile_default(tuide::GraphQueryPhase::AnchorHunt);
  expect(profile.hops == 0, "anchor hunt hops");
  tuide::graph_query_profile_apply(profile, &opts);
  expect(opts.hops == 0 && opts.top_k == 12, "apply profile");

  return failures == 0 ? 0 : 1;
}
