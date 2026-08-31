#include <iostream>
#include <string>

#include "ai/l2_think.hpp"

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  using tuide::L2ThinkLevel;
  using tuide::apply_think_profile;
  using tuide::l2_think_level_name;
  using tuide::think_profile;
  using tuide::think_profile_for;

  expect(think_profile_for("explore_a", false, false).level == L2ThinkLevel::Low, "explore_a low");
  expect(think_profile_for("explore_a", true, false).level == L2ThinkLevel::Low,
         "explore_a low even with pack");
  expect(think_profile_for("edit", false, false).level == L2ThinkLevel::Off, "edit off");
  expect(think_profile_for("edit", true, false).budget == 0, "edit budget 0");
  expect(!think_profile_for("edit", true, false).enable_thinking, "edit thinking off");

  expect(think_profile_for("explore", false, false).level == L2ThinkLevel::High,
         "explore !pack high");
  expect(think_profile_for("explore_b", false, false).level == L2ThinkLevel::High,
         "explore_b !pack high");
  expect(think_profile_for("explore", true, false).level == L2ThinkLevel::Medium,
         "explore with pack medium");
  expect(think_profile_for("explore_b", true, false).level == L2ThinkLevel::Medium,
         "explore_b with pack medium");

  expect(think_profile_for("explore", false, true).level == L2ThinkLevel::Low, "pack review low");
  expect(think_profile_for("explore", true, true).level == L2ThinkLevel::Low,
         "pack review low even with pack");

  expect(think_profile_for("compile", false, false).level == L2ThinkLevel::Medium,
         "unknown defaults medium");

  expect(think_profile_for("causal_pilot_plan", false, false).level == L2ThinkLevel::High,
         "pilot plan high");
  expect(think_profile_for("causal_pilot_plan_more", false, false).level == L2ThinkLevel::Medium,
         "pilot plan_more medium");
  expect(think_profile_for("causal_pilot_worker", false, false).level == L2ThinkLevel::Low,
         "pilot worker low");
  expect(think_profile_for("causal_pilot_plenary", false, false).level == L2ThinkLevel::Medium,
         "pilot plenary medium");
  expect(think_profile_for("causal_zone_hyp", false, false).level == L2ThinkLevel::High,
         "zone hyp high");
  expect(think_profile_for("causal_zone_hyp_more", false, false).level == L2ThinkLevel::Medium,
         "zone hyp_more medium");
  expect(think_profile_for("causal_zone_judge", false, false).level == L2ThinkLevel::Low,
         "zone judge low");
  expect(think_profile_for("causal_atlas_survey", false, false).level == L2ThinkLevel::Low,
         "atlas survey low");
  expect(think_profile_for("causal_zone_anchor", false, false).level == L2ThinkLevel::High,
         "zone anchor high");
  expect(think_profile_for("causal_zone_synth", false, false).level == L2ThinkLevel::Medium,
         "zone synth medium");
  expect(think_profile_for("causal_zone_contrast", false, false).level == L2ThinkLevel::Medium,
         "zone contrast medium");
  expect(think_profile_for("causal_zone_triage", false, false).level == L2ThinkLevel::Low,
         "zone triage low");
  expect(think_profile_for("causal_zone_slot_hyp", false, false).level == L2ThinkLevel::Low,
         "slot hyp low");
  expect(think_profile_for("causal_atlas_cover", false, false).level == L2ThinkLevel::Low,
         "atlas cover low");

  {
    tuide::L2BrainRequest p;
    p.phase = "causal_pilot_plan";
    p.max_tokens = 512;
    tuide::apply_think_for_request(&p);
    expect(p.reasoning_budget == 1536, "apply_think_for_request plan budget");
    expect(p.max_tokens == 512 + 1536, "apply_think_for_request plan max_tokens");
  }
  {
    tuide::L2BrainRequest w;
    w.phase = "causal_pilot_worker";
    w.max_tokens = 640;
    tuide::apply_think_for_request(&w);
    expect(w.reasoning_budget == 64, "worker low budget");
    expect(w.max_tokens == 640 + 64, "worker max_tokens bump");
  }

  const auto high = think_profile(L2ThinkLevel::High);
  expect(high.budget == 1536, "high budget");
  expect(std::string(l2_think_level_name(high.level)) == "high", "high name");

  tuide::LlamaCompletionRequest req;
  req.max_tokens = 512;
  req.grammar_file = "/tmp/x.gbnf";
  apply_think_profile(&req, high);
  expect(req.max_tokens == 512 + 1536, "distill max_tokens += high budget");
  expect(req.max_tokens >= 512 + 1536, "distill max_tokens >= 512+1536");
  expect(req.grammar_file.empty(), "grammar cleared when budget > 0");
  expect(req.enable_thinking.has_value() && *req.enable_thinking, "high thinking on");
  expect(req.reasoning_budget == 1536, "high budget field");

  tuide::L2BrainRequest edit;
  edit.max_tokens = 2048;
  edit.grammar_file = "/tmp/edit.gbnf";
  apply_think_profile(&edit, think_profile_for("edit", true, false));
  expect(edit.max_tokens == 2048, "edit does not bump max_tokens");
  expect(edit.grammar_file == "/tmp/edit.gbnf", "edit keeps grammar");
  expect(edit.enable_thinking.has_value() && !*edit.enable_thinking, "edit thinking false");
  expect(edit.reasoning_budget == 0, "edit budget 0");

  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "l2_think_test ok\n";
  return 0;
}
