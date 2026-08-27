#include "ai/l2_problem_frame.hpp"
#include "ai/l2_graph_query_profile.hpp"

#include <cctype>
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
  raw_pf.primary_anchor.search_terms.push_back("gradle");         // ungrounded invent — drop
  raw_pf.primary_anchor.search_terms.push_back("npm");
  raw_pf.primary_anchor.search_terms.push_back("build.gradle");  // partial ground — drop
  tuide::problem_frame_refine_from_query(
      &raw_pf, "spinner infinito en chat IA aunque el modelo terminó");
  bool has_nl = false;
  bool injected_ai = false;
  bool kept_spinner = false;
  bool kept_gradle = false;
  for (const auto& t : raw_pf.primary_anchor.search_terms) {
    if (t.find(' ') != std::string::npos) {
      has_nl = true;
    }
    if (t == "ai_controller" || t == "busy_strip" || t == "level2_autonomous_loop") {
      injected_ai = true;
    }
    std::string tl = t;
    for (char& c : tl) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (tl == "spinner") {
      kept_spinner = true;
    }
    if (tl == "gradle" || tl == "npm" || tl == "build.gradle") {
      kept_gradle = true;
    }
  }
  expect(!has_nl, "refine drops spaced NL terms");
  expect(!injected_ai, "refine does not inject domain stems");
  expect(kept_spinner, "refine keeps query-grounded term");
  expect(!kept_gradle, "refine drops ungrounded invented terms");

  // Partial compound: "build" in query must not keep invented "build.gradle".
  tuide::ProblemFrame compound_pf;
  expect(tuide::problem_frame_from_json_string(
             R"({"schema":"problem_frame_v1","problem_kind":"locate",
                 "problem_frame":"where is build launched",
                 "primary_anchor":{"kind":"control","objective":"build launch",
                   "search_terms":["compile","build","build.gradle"]},
                 "secondary_anchors":[{"kind":"module","objective":"config",
                   "search_terms":["build.gradle","package.json"],"deferred":true}]})",
             &compound_pf, &err),
         "compound parse");
  tuide::problem_frame_refine_from_query(
      &compound_pf, "donde se lanza la compilacion o el build del proyecto");
  bool kept_build = false;
  bool kept_build_gradle = false;
  for (const auto& t : compound_pf.primary_anchor.search_terms) {
    std::string tl = t;
    for (char& c : tl) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (tl == "build" || tl == "compile") {
      kept_build = true;
    }
    if (tl.find("gradle") != std::string::npos || tl.find("package") != std::string::npos) {
      kept_build_gradle = true;
    }
  }
  for (const auto& sec : compound_pf.secondary_anchors) {
    for (const auto& t : sec.search_terms) {
      std::string tl = t;
      for (char& c : tl) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (tl.find("gradle") != std::string::npos || tl.find("package") != std::string::npos) {
        kept_build_gradle = true;
      }
    }
  }
  expect(kept_build, "refine keeps build/compile from query");
  expect(!kept_build_gradle, "refine drops partially-grounded invented compounds");
  expect(compound_pf.secondary_anchors.empty(),
         "refine drops secondary anchors with only ungrounded terms");

  // Failure hypotheses: slots + claim; seeds follow active index / anchor_role.
  {
    tuide::ProblemFrame hyp_pf;
    expect(tuide::problem_frame_from_json_string(
               R"({"schema":"problem_frame_v1","problem_kind":"debug",
                   "problem_frame":"thinking indicator stuck",
                   "primary_anchor":{"kind":"control","objective":"find indicator",
                     "search_terms":["thinking","indicator"]},
                   "anchor_confidence":"low",
                   "anchor_hypotheses":[
                     {"claim":"busy never cleared on model done",
                      "slots":{"affected":{"from_map":1,"stem":"busy_strip"},
                               "control":{"from_map":2,"stem":"ai_controller"},
                               "trigger":null,"cleanup":null},
                      "gap":"cleanup","anchor_role":"affected",
                      "falsify_by":"if clear_busy runs on done → dead",
                      "why":"status strip owns busy"},
                     {"claim":"invented",
                      "slots":{"affected":{"stem":"gradle"},"control":null,
                               "trigger":null,"cleanup":null},
                      "gap":"affected","anchor_role":"affected","why":"noise"}
                   ]})",
               &hyp_pf, &err),
           "failure hyp parse");
    expect(hyp_pf.anchor_hypotheses.size() == 2, "two hyps parsed");
    expect(hyp_pf.anchor_hypotheses[0].claim.find("busy") != std::string::npos, "claim set");
    expect(hyp_pf.anchor_hypotheses[0].affected.stem == "busy_strip", "affected stem");
    expect(tuide::problem_frame_wants_anchor_hypotheses(hyp_pf), "wants hyps when low");
    tuide::problem_frame_refine_from_query(&hyp_pf, "thinking indicator stuck");
    expect(hyp_pf.anchor_hypotheses.size() == 2, "refine_from_query does not strip hyps");
    std::vector<std::vector<std::string>> cards = {{"busy_strip", "BusyStrip"},
                                                   {"ai_controller", "AiController"},
                                                   {"status_bar"},
                                                   {"chat_panel"}};
    tuide::problem_frame_refine_hypotheses_to_ranked_cards(&hyp_pf, cards);
    expect(hyp_pf.anchor_hypotheses.size() == 1, "ranked refine drops ungrounded hyp");
    const auto terms = tuide::hypothesis_anchor_terms(hyp_pf.anchor_hypotheses[0]);
    expect(!terms.empty(), "anchor terms from affected");
    bool has_busy = false;
    for (const auto& s : terms) {
      std::string tl = s;
      for (char& c : tl) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (tl.find("busy") != std::string::npos) {
        has_busy = true;
      }
    }
    expect(has_busy, "anchor terms prefer busy_strip");
    hyp_pf.active_hypothesis_index = 0;
    const auto hyp_seeds = tuide::problem_frame_anchor_seeds(hyp_pf);
    expect(!hyp_seeds.empty(), "active hyp seeds");
    const auto roundtrip = tuide::problem_frame_to_json(hyp_pf);
    expect(roundtrip.contains("anchor_hypotheses"), "serialize hyps");
    expect(roundtrip["anchor_hypotheses"][0].contains("slots"), "serialize slots");
    expect(roundtrip.value("active_hypothesis_index", -1) == 0, "serialize active index");
  }

  tuide::RegistryQueryOpts opts;
  const auto profile = tuide::graph_query_profile_default(tuide::GraphQueryPhase::AnchorHunt);
  expect(profile.hops == 0, "anchor hunt hops");
  tuide::graph_query_profile_apply(profile, &opts);
  expect(opts.hops == 0 && opts.top_k == 12, "apply profile");
  expect(opts.match_surface == tuide::RegistryMatchSurface::CardFull, "default match_surface");

  tuide::GraphQueryProfile latch_prof = profile;
  latch_prof.match_surface = tuide::RegistryMatchSurface::Latch;
  tuide::RegistryQueryOpts opts2;
  tuide::graph_query_profile_apply(latch_prof, &opts2);
  expect(opts2.match_surface == tuide::RegistryMatchSurface::Latch, "apply latch surface");
  expect(!opts2.seed_kinds.empty() && opts2.seed_kinds[0] == "latch", "latch seed_kinds");

  expect(std::string(tuide::registry_match_surface_name(tuide::RegistryMatchSurface::CardAttrs)) ==
             "card_attrs",
         "surface name");
  tuide::RegistryMatchSurface parsed = tuide::RegistryMatchSurface::CardFull;
  expect(tuide::registry_match_surface_parse("node_id", &parsed) &&
             parsed == tuide::RegistryMatchSurface::NodeId,
         "parse node_id");
  expect(tuide::registry_embed_model_key("m", tuide::RegistryMatchSurface::Latch).find("latch") !=
             std::string::npos,
         "embed model key tags surface");

  return failures == 0 ? 0 : 1;
}
