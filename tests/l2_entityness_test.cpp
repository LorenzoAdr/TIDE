#include "ai/l2_entityness.hpp"
#include "ai/l2_effect_registry.hpp"
#include "ai/l2_graph_query_profile.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

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
  tuide::RegistryMatchSurface s = tuide::RegistryMatchSurface::CardFull;
  expect(tuide::registry_match_surface_parse("latch", &s) &&
             s == tuide::RegistryMatchSurface::Latch,
         "parse latch");

  const auto terms = tuide::entityness_prompt_terms(
      "spinner infinito en el panel del chat aunque el modelo terminó");
  expect(!terms.empty(), "prompt terms non-empty");
  bool has_spinner = false;
  for (const auto& t : terms) {
    if (t == "spinner") {
      has_spinner = true;
    }
  }
  expect(has_spinner, "spinner extracted as term");

  expect(std::abs(tuide::entityness_hit_score(1) - (1.f / 3.f)) < 1e-5f, "hit_score 1");
  expect(std::abs(tuide::entityness_hit_score(2) - 0.5f) < 1e-5f, "hit_score half-life");
  expect(tuide::entityness_combine(1.f, 1) < 0.35f, "single-hit concentration penalized");
  expect(tuide::entityness_combine(0.73f, 4) > tuide::entityness_combine(1.f, 1),
         "many hits beat lone perfect conc");
  expect(tuide::entityness_combine(0.73f, 4) >= 0.45f, "spinner-like stays F1-eligible");

  tuide::ProblemFrame pf;
  pf.primary_anchor.objective = "control spinner lifecycle";
  pf.primary_anchor.search_terms = {"spinner"};
  pf.secondary_anchors.push_back({"module", "chat ambient", {"chat_panel"}, true, "later"});
  pf.anchor_hypotheses.push_back(
      {"busy strip", {"busy_strip"}, "effect", "menu candidate for thinking indicator"});

  tuide::EntitynessReport empty_rep;
  empty_rep.query = "x";
  const std::string block = tuide::entityness_prompt_block(empty_rep);
  expect(block.find("ENTITYNESS:") != std::string::npos, "prompt block header");

  // Optional: live registry if present (smoke, not required).
  const char* root_env = std::getenv("TUIDE_ROOT");
  const fs::path root = root_env ? fs::path(root_env) : fs::current_path();
  tuide::EffectRegistry reg;
  std::string err;
  if (tuide::registry_open(root.string(), &reg, &err)) {
    tuide::EntitynessOpts opts;
    tuide::EntitynessLinkReport links;
    const bool ok = tuide::entityness_score_problem_frame(
        &reg, pf, "spinner en chat", {}, opts, &links, &err);
    expect(ok, "entityness_score_problem_frame runs");
    if (ok) {
      expect(links.links.size() >= 2, "primary + at least one more link");
      bool has_hyp = false;
      for (const auto& L : links.links) {
        if (L.role.size() >= 4 && L.role.compare(0, 4, "hyp_") == 0) {
          has_hyp = true;
        }
      }
      expect(has_hyp, "hyp link scored");
      expect(links.explore_mode == "f1_anchor" || links.explore_mode == "classic_scan",
             "explore_mode set");
      expect(links.to_json().contains("links"), "json links");
    }
    tuide::registry_close(&reg);
  }

  return failures == 0 ? 0 : 1;
}
