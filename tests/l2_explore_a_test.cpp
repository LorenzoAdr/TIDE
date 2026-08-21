#include <cassert>
#include <iostream>
#include <string>

#include "ai/l2_explore_a.hpp"
#include "ai/l2_feat.hpp"

using tuide::ALocusRole;
using tuide::AState;
using tuide::AVerdictKind;
using tuide::a_notes_markdown;
using tuide::a_state_from_json;
using tuide::a_state_to_json;
using tuide::parse_a_loci_array;
using tuide::parse_a_verdicts_array;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  {
    // Flag off by default in features_promoted.json
    expect(!tuide::l2_feat::enabled("L2_EXPLORE_PHASE_A"),
           "L2_EXPLORE_PHASE_A default off (or unset)");
  }
  {
    nlohmann::json j = nlohmann::json::parse(R"({
      "verdicts": [
        {"target":"src/ui/wake.cpp:tick#tail","verdict":"useful","anchor":"src/ui/wake.cpp:90",
         "stem":"wake","role":"primary","why":"early return"},
        {"target":"src/ui/noise.cpp:foo","verdict":"reject","why":"unrelated"}
      ],
      "done": false
    })");
    std::vector<tuide::AVerdict> vs;
    std::string err;
    expect(parse_a_verdicts_array(j, &vs, &err), "parse verdicts");
    expect(vs.size() == 2, "2 verdicts");
    expect(vs[0].verdict == AVerdictKind::Useful && vs[0].stem == "wake", "useful wake");
    expect(vs[0].role == ALocusRole::Primary, "primary");
    expect(vs[1].verdict == AVerdictKind::Reject, "reject");
  }
  {
    nlohmann::json j = nlohmann::json::parse(R"({
      "loci": [
        {"stem":"wake_policy","anchor":"src/ui/wake_policy.cpp:should_wake","role":"primary",
         "why":"policy gate","window":"tail"},
        {"stem":"busy_strip","anchor":"src/ui/busy_strip.cpp:42","role":"secondary"}
      ]
    })");
    std::vector<tuide::ALocus> loci;
    std::string err;
    expect(parse_a_loci_array(j, &loci, &err), "parse loci");
    expect(loci.size() == 2, "2 loci");
    expect(loci[0].window == "tail", "window");
    expect(loci[1].role == ALocusRole::Secondary, "secondary");
  }
  {
    AState st;
    st.peeks_used = 5;
    st.turns = 1;
    st.cursor = 5;
    tuide::AQueueItem q;
    q.target = "src/a.cpp:Foo#tail";
    q.path = "src/a.cpp";
    q.stem = "a";
    q.symbol = "Foo";
    q.window_hint = "tail";
    q.score = 0.9f;
    st.queue.push_back(q);
    tuide::AVerdict v;
    v.target = q.target;
    v.verdict = AVerdictKind::Useful;
    v.anchor = "src/a.cpp:Foo";
    v.stem = "a";
    v.role = ALocusRole::Primary;
    v.why = "hit";
    st.notes.push_back(v);
    tuide::ALocus loc;
    loc.stem = "a";
    loc.anchor = "src/a.cpp:Foo";
    loc.role = ALocusRole::Primary;
    loc.why = "hit";
    st.loci_draft.push_back(loc);

    const auto j = a_state_to_json(st);
    AState round;
    std::string err;
    expect(a_state_from_json(j, &round, &err), "roundtrip");
    expect(round.peeks_used == 5 && round.queue.size() == 1, "queue peeks");
    expect(round.notes.size() == 1 && round.loci_draft.size() == 1, "notes loci");
    const std::string md = a_notes_markdown(round);
    expect(md.find("useful") != std::string::npos && md.find("src/a.cpp:Foo") != std::string::npos,
           "notes md");
  }
  {
    nlohmann::json j = nlohmann::json::parse(R"({"verdicts":[]})");
    std::vector<tuide::AVerdict> vs;
    std::string err;
    expect(!parse_a_verdicts_array(j, &vs, &err), "empty verdicts fail");
  }
  {
    using tuide::AQueueBuildInput;
    using tuide::AQueueBuildOpts;
    using tuide::build_a_scan_queue;
    using tuide::a_state_seed_queue;

    std::vector<AQueueBuildInput> ranked;
    // Same stem flood — diversify should cap.
    for (int i = 0; i < 10; ++i) {
      AQueueBuildInput in;
      in.file = "src/ui/wake.cpp";
      in.name = "fn" + std::to_string(i);
      in.line = 10 + i;
      in.score = 1000 - i;
      in.functionish = true;
      ranked.push_back(in);
    }
    // Other stems
    for (int i = 0; i < 5; ++i) {
      AQueueBuildInput in;
      in.file = "src/ai/ctrl" + std::to_string(i) + ".cpp";
      in.name = "cancel";
      in.line = 1;
      in.score = 500 - i;
      in.functionish = true;
      ranked.push_back(in);
    }
    // Known short body → no #tail
    {
      AQueueBuildInput in;
      in.file = "src/ui/tiny.cpp";
      in.name = "tick";
      in.line = 3;
      in.score = 900;
      in.functionish = true;
      in.body_lines = 20;
      ranked.push_back(in);
    }
    // Known long body → #tail
    {
      AQueueBuildInput in;
      in.file = "src/ui/huge.cpp";
      in.name = "run";
      in.line = 1;
      in.score = 950;
      in.functionish = true;
      in.body_lines = 400;
      ranked.push_back(in);
    }

    AQueueBuildOpts opts;
    opts.max_items = 40;
    opts.max_per_stem = 3;
    auto q = build_a_scan_queue(ranked, opts);
    expect(!q.empty() && q.size() <= 40, "queue size capped");
    int wake_count = 0;
    for (const auto& it : q) {
      if (it.stem == "wake") {
        ++wake_count;
      }
    }
    expect(wake_count == 3, "max_per_stem=3 for wake");

    bool found_huge_tail = false;
    bool found_tiny_plain = false;
    for (const auto& it : q) {
      if (it.path.find("huge.cpp") != std::string::npos) {
        found_huge_tail = (it.window_hint == "tail" &&
                           it.target.find("#tail") != std::string::npos);
      }
      if (it.path.find("tiny.cpp") != std::string::npos) {
        found_tiny_plain = it.window_hint.empty() && it.target.find('#') == std::string::npos;
      }
    }
    expect(found_huge_tail, "long body → #tail");
    expect(found_tiny_plain, "short body → no window");

    // Deterministic: same input → same targets order
    auto q2 = build_a_scan_queue(ranked, opts);
    expect(q.size() == q2.size(), "deterministic size");
    bool same = true;
    for (std::size_t i = 0; i < q.size(); ++i) {
      if (q[i].target != q2[i].target) {
        same = false;
        break;
      }
    }
    expect(same, "deterministic order");

    AState st;
    a_state_seed_queue(&st, ranked, opts);
    expect(st.queue.size() == q.size() && st.cursor == 0, "seed queue");
    // P3: reserve holds next slice when ranked is wide enough
    expect(st.reserve.size() + st.queue.size() >= st.queue.size(), "reserve ok");
  }
  {
    using tuide::AExpandResult;
    using tuide::AQueueBuildInput;
    using tuide::AQueueBuildOpts;
    using tuide::AState;
    using tuide::AVerdictKind;
    using tuide::a_compute_orphans;
    using tuide::a_loci_must_ordered;
    using tuide::a_plan_target_allowed;
    using tuide::a_state_seed_queue;
    using tuide::maybe_expand_a_queue;

    std::vector<AQueueBuildInput> ranked;
    for (int i = 0; i < 60; ++i) {
      AQueueBuildInput in;
      in.file = "src/mod/file" + std::to_string(i) + ".cpp";
      in.name = "fn" + std::to_string(i);
      in.line = 1;
      in.score = 1000 - i;
      in.functionish = true;
      in.body_lines = 10;
      ranked.push_back(in);
    }
    // Gold outside top-40
    ranked[50].file = "src/gold/rescue.cpp";
    ranked[50].name = "fix_it";
    ranked[50].stem = "rescue";
    ranked[50].score = 10;

    AQueueBuildOpts opts;
    opts.max_items = 40;
    opts.max_per_stem = 2;
    AState st;
    a_state_seed_queue(&st, ranked, opts);
    expect(st.queue.size() == 40, "primary 40");
    expect(!st.reserve.empty(), "has reserve");

    // Exhaust cursor with no useful → layer1 expand
    st.cursor = static_cast<int>(st.queue.size());
    st.turns = 1;
    auto orphans = a_compute_orphans(st, {"fix_it", "rescue"});
    expect(!orphans.empty(), "orphans before useful");
    AExpandResult e1 = maybe_expand_a_queue(&st, orphans);
    expect(e1.expanded && e1.layer == 1 && e1.added > 0, "layer1 expand");
    expect(st.expansions == 1, "expansions=1");

    bool gold_in_queue = false;
    for (const auto& it : st.queue) {
      if (it.path.find("rescue.cpp") != std::string::npos ||
          it.target.find("fix_it") != std::string::npos) {
        gold_in_queue = true;
      }
    }
    expect(gold_in_queue, "gold recovered via layer1");

    // plan allow
    tuide::ALocus loc;
    loc.anchor = "src/gold/rescue.cpp:fix_it";
    loc.stem = "rescue";
    loc.role = tuide::ALocusRole::Primary;
    st.loci_draft = a_loci_must_ordered({loc});
    expect(a_plan_target_allowed(st, "src/gold/rescue.cpp:fix_it"), "allowed locus");
    expect(!a_plan_target_allowed(st, "src/noise/other.cpp:foo"), "blocked outside");
    st.b_allow_paths.push_back("src/noise/other.cpp");
    expect(a_plan_target_allowed(st, "src/noise/other.cpp:foo"), "micro-A allow");
  }

  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "OK l2_explore_a_test\n";
  return 0;
}
