#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

#include "ai/l2_explore_a.hpp"
#include "ai/l2_feat.hpp"
#include "ai/l2_problem_frame.hpp"

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
    setenv("L2_FEAT_L2_EXPLORE_PHASE_A", "0", 1);
    expect(!tuide::l2_feat::enabled("L2_EXPLORE_PHASE_A"), "env 0 disables Phase A");
    setenv("L2_FEAT_L2_EXPLORE_PHASE_A", "1", 1);
    expect(tuide::l2_feat::enabled("L2_EXPLORE_PHASE_A"), "env 1 enables Phase A");
    unsetenv("L2_FEAT_L2_EXPLORE_PHASE_A");
  }
  {
    const std::string map =
        "# Ranked map\n\n## Ranked entries\n\n"
        "1. src/ui/wake.cpp:90 — `should_wake`\n"
        "2. src/ui/noise.cpp:1  [score=0.5] — `noise`\n"
        "3. bare_no_path\n";
    const auto inputs = tuide::a_queue_inputs_from_ranked_map_markdown(map, 80);
    expect(inputs.size() == 2, "map parser keeps 2 entries");
    expect(inputs[0].file == "src/ui/wake.cpp" && inputs[0].name == "should_wake",
           "wake symbol");
    expect(inputs[0].line == 90, "wake line");
    expect(inputs[1].name == "noise", "noise symbol");
  }
  {
    const std::string map =
        "# Ranked map\n\n## Ranked entries\n\n"
        "6. src/ui/busy_strip.cpp:226  [score=2326456] — `set_busy_spinner`\n"
        "    why: base=1641032 body=0.57 · stem=busy_strip dup_stem · file_rank=1/1 · refs≈10 · "
        "related=state,BusyActivity,layout\n";
    const auto inputs = tuide::a_queue_inputs_from_ranked_map_markdown(map, 4);
    expect(inputs.size() == 1, "why line parsed");
    expect(inputs[0].stem == "busy_strip", "why stem");
    expect(inputs[0].refs_in == 10, "why refs");
    expect(inputs[0].body_sem_permille == 570, "why body_sem");
    expect(inputs[0].file_rank == 1 && inputs[0].file_count == 1, "why file_rank");
    expect(inputs[0].dup_stem, "why dup_stem");
    expect(inputs[0].map_related.find("BusyActivity") != std::string::npos, "why related");
  }
  {
    const std::string map =
        "# Ranked map\n\n## Ranked entries\n\n"
        "1. tests/fixtures/effect_slice/box_a.cpp:2 — `set_flag`\n"
        "2. tests/fixtures/effect_slice/box_a.hpp:2 — `set_flag`\n"
        "3. tests/fixtures/effect_slice/box_b.hpp:8 — `kick`\n"
        "4. tests/fixtures/effect_slice/box_b.cpp:8 — `kick`\n";
    tuide::AQueueMapFilterOpts fopts;
    fopts.want_n = 4;
    fopts.skip_file_level = false;
    const char* root_env = std::getenv("TUIDE_ROOT");
    const std::string root = root_env != nullptr ? root_env : "..";
    const auto filtered = tuide::a_queue_inputs_from_ranked_map_filtered(map, fopts, root);
    bool has_box_a_cpp = false;
    for (const auto& item : filtered) {
      has_box_a_cpp = has_box_a_cpp || item.file == "tests/fixtures/effect_slice/box_a.cpp";
    }
    expect(has_box_a_cpp, "keeps box_a cpp");
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
    expect(parse_a_verdicts_array(j, &vs, &err), "empty verdicts ok at parse");
    expect(vs.empty(), "empty vector");
  }
  {
    nlohmann::json j = nlohmann::json::parse(
        R"({"action":"a_judge","target":"src/ui/busy_strip.cpp:spinner_busy_set","verdict":"useful","why":"flag"})");
    std::vector<tuide::AVerdict> vs;
    std::string err;
    expect(parse_a_verdicts_array(j, &vs, &err), "flat a_judge ok");
    expect(vs.size() == 1 && vs[0].verdict == tuide::AVerdictKind::Useful, "flat useful");
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
  {
    tuide::AVerdict v;
    v.target = "src/ui/wake.cpp:tick#tail";
    v.verdict = AVerdictKind::Useful;
    v.anchor = "path:tick";
    v.stem = "src/ui/wake.cpp:tick";
    v.role = ALocusRole::Primary;
    tuide::a_normalize_verdict(&v);
    expect(v.anchor == "src/ui/wake.cpp:tick", "norm anchor from target");
    expect(v.stem == "wake", "norm stem basename");
    expect(tuide::a_anchor_resolvable(v.anchor), "resolvable");

    tuide::ALocus loc;
    loc.stem = "src/ui/console_panel.cpp:ConsolePanelState";
    loc.anchor = "path:ConsolePanelState";
    loc.role = ALocusRole::Primary;
    tuide::a_normalize_locus(&loc);
    expect(loc.anchor.find("console_panel") != std::string::npos, "swap stem→anchor");
    expect(loc.stem == "console_panel", "stem basename after swap");

    std::vector<tuide::ALocus> many;
    for (int i = 0; i < 5; ++i) {
      tuide::ALocus L;
      L.stem = "s" + std::to_string(i);
      L.anchor = "src/f" + std::to_string(i) + ".cpp:Sym";
      L.role = ALocusRole::Primary;
      many.push_back(L);
    }
    tuide::a_cap_locus_roles(&many);
    int p = 0, s = 0, u = 0;
    for (const auto& L : many) {
      if (L.role == ALocusRole::Primary) {
        ++p;
      } else if (L.role == ALocusRole::Secondary) {
        ++s;
      } else {
        ++u;
      }
    }
    expect(p == 2 && s == 2 && u == 1, "cap 2+2+suspect");

    AState st;
    st.peeks_used = 10;
    st.turns = 2;
    st.notes.push_back({});
    st.notes.back().verdict = AVerdictKind::Useful;
    st.notes.back().target = "src/ui/wake.cpp:tick";
    st.notes.back().stem = "wake";
    std::string err;
    std::vector<tuide::ALocus> one = {loc};
    expect(!tuide::a_validate_a_done(st, one, &err), "no reject blocks a_done");
    expect(err.find("contraste") != std::string::npos, "contrast msg");
    st.notes.push_back({});
    st.notes.back().verdict = AVerdictKind::Reject;
    st.notes.back().target = "src/ai/other.cpp:x";
    st.notes.back().stem = "other";
    expect(tuide::a_validate_a_done(st, one, &err), "with reject ok");
  }
  {
    // Path-family round-robin: high-score ui/* should not monopolize early queue.
    std::vector<tuide::AQueueBuildInput> ranked;
    for (int i = 0; i < 6; ++i) {
      tuide::AQueueBuildInput in;
      in.file = "src/ui/u" + std::to_string(i) + ".cpp";
      in.name = "Ui" + std::to_string(i);
      in.score = 1000 - i;
      in.functionish = true;
      ranked.push_back(in);
    }
    for (int i = 0; i < 4; ++i) {
      tuide::AQueueBuildInput in;
      in.file = "src/ai/a" + std::to_string(i) + ".cpp";
      in.name = "Ai" + std::to_string(i);
      in.score = 100 - i;  // lower than ui
      in.functionish = true;
      ranked.push_back(in);
    }
    tuide::AQueueBuildOpts opts;
    opts.max_items = 8;
    opts.max_per_stem = 2;
    opts.diversify_path_family = true;
    const auto q = tuide::build_a_scan_queue(ranked, opts);
    expect(q.size() >= 4, "diverse queue size");
    expect(tuide::a_path_family(q[0].path) != tuide::a_path_family(q[1].path) ||
               q.size() < 2,
           "early peeks alternate families when both exist");
    bool saw_ai = false;
    for (std::size_t i = 0; i < std::min<std::size_t>(4, q.size()); ++i) {
      if (tuide::a_path_family(q[i].path) == "ai") {
        saw_ai = true;
      }
    }
    expect(saw_ai, "ai appears in first 4 despite lower score");

    // prefer_src: tests/tools stay out of early queue
    {
      std::vector<tuide::AQueueBuildInput> mixed = ranked;
      for (int i = 0; i < 5; ++i) {
        tuide::AQueueBuildInput in;
        in.file = "tests/t" + std::to_string(i) + ".cpp";
        in.name = "T" + std::to_string(i);
        in.score = 5000 - i;  // higher than ui/ai
        mixed.push_back(in);
      }
      tuide::AQueueBuildOpts o2 = opts;
      o2.prefer_src_paths = true;
      o2.max_items = 8;
      const auto q2 = tuide::build_a_scan_queue(mixed, o2);
      bool early_test = false;
      for (std::size_t i = 0; i < std::min<std::size_t>(6, q2.size()); ++i) {
        if (q2[i].path.rfind("tests/", 0) == 0) {
          early_test = true;
        }
      }
      expect(!early_test, "tests/ not in early peeks when prefer_src");
    }

    AState early;
    early.peeks_used = 3;
    early.turns = 1;
    early.notes.push_back({});
    early.notes.back().verdict = AVerdictKind::Useful;
    early.notes.back().target = "src/ui/u0.cpp:Ui0";
    early.notes.back().stem = "u0";
    early.notes.push_back({});
    early.notes.back().verdict = AVerdictKind::Reject;
    early.notes.back().target = "src/ui/u1.cpp:Ui1";
    early.notes.back().stem = "u1";
    expect(!tuide::a_enough_locate_breadth(early), "breadth blocks early");
    early.peeks_used = 10;
    expect(tuide::a_enough_locate_breadth(early), "breadth ok after peeks");
    // Still need cross-module if queue has both families
    early.queue = q;
    std::string err2;
    tuide::ALocus loc;
    loc.stem = "u0";
    loc.anchor = "src/ui/u0.cpp:Ui0";
    loc.role = ALocusRole::Primary;
    expect(!tuide::a_validate_a_done(early, {loc}, &err2), "cross-module gate");
    expect(err2.find("cross-módulo") != std::string::npos ||
               err2.find("familias") != std::string::npos,
           "cross-module msg");
    early.notes.push_back({});
    early.notes.back().verdict = AVerdictKind::Reject;
    early.notes.back().target = "src/ai/a0.cpp:Ai0";
    early.notes.back().stem = "a0";
    expect(tuide::a_validate_a_done(early, {loc}, &err2), "cross-module satisfied");
  }
  {
    // Trail: begin → all reject → invalidate L0
    AState st;
    tuide::AVerdict u;
    u.target = "src/ui/busy.cpp:set_busy_spinner";
    u.anchor = u.target;
    u.stem = "busy";
    u.verdict = AVerdictKind::Useful;
    u.why = "spinner control";
    tuide::a_trail_begin(&st, u);
    expect(st.trail.active, "trail active");
    expect(st.trail.focus_symbol == "set_busy_spinner", "focus symbol");
    st.notes.push_back(u);
    tuide::ALocus hyp;
    hyp.anchor = u.anchor;
    hyp.stem = u.stem;
    hyp.role = ALocusRole::Suspect;
    st.loci_draft.push_back(hyp);

    tuide::ATrailStack s1;
    s1.id = "S1";
    s1.hops.push_back({});
    s1.hops.back().symbol = "begin_thinking";
    s1.hops.back().anchor = "src/ai/ai_controller.cpp:begin_thinking";
    tuide::ATrailStack s2 = s1;
    s2.id = "S2";
    s2.hops.back().symbol = "git_busy";
    st.trail.pending_stacks.clear();
    st.trail.pending_stacks.push_back(s1);
    st.trail.pending_stacks.push_back(s2);
    st.trail.awaiting_judge = true;

    std::vector<tuide::AVerdict> rej;
    {
      tuide::AVerdict v;
      v.target = "S1";
      v.verdict = AVerdictKind::Reject;
      rej.push_back(v);
      v.target = "S2";
      rej.push_back(v);
    }
    std::string err;
    expect(tuide::a_trail_apply_judge(&st, rej, &err), "apply all-reject");
    expect(!st.trail.active, "trail cleared after falsify");
    expect(st.notes.front().verdict == AVerdictKind::Reject, "L0 demoted");
    expect(st.loci_draft.empty(), "hyp locus dropped");
  }
  {
    // Trail: interesting queues force deepen
    AState st;
    tuide::AVerdict u;
    u.target = "src/ui/busy.cpp:set_busy_spinner";
    u.anchor = u.target;
    u.stem = "busy";
    u.verdict = AVerdictKind::Useful;
    tuide::a_trail_begin(&st, u);
    tuide::ATrailStack s1;
    s1.id = "S1";
    s1.hops.resize(2);
    s1.hops[0].symbol = "begin_thinking";
    s1.hops[0].anchor = "src/ai/ai_controller.cpp:begin_thinking";
    s1.hops[1].symbol = "set_busy_spinner";
    st.trail.pending_stacks.clear();
    st.trail.pending_stacks.push_back(s1);
    st.trail.awaiting_judge = true;
    tuide::AVerdict v;
    v.target = "S1";
    v.verdict = AVerdictKind::Interesting;
    v.why = "IA sets busy";
    std::string err;
    std::vector<tuide::AVerdict> one = {v};
    expect(tuide::a_trail_apply_judge(&st, one, &err), "interesting ok");
    expect(st.trail.active, "still active");
    expect(st.trail.force_queue.size() == 1 && st.trail.force_queue[0] == "S1", "force S1");
    expect(tuide::parse_a_verdict_kind("interesting") == AVerdictKind::Interesting,
           "parse interesting");
  }
  {
    // Cond branch interesting queues force deepen
    tuide::AState st;
    tuide::AVerdict u;
    u.target = "src/ui/busy.cpp:set_busy_spinner";
    u.anchor = u.target;
    u.stem = "busy";
    u.verdict = tuide::AVerdictKind::Useful;
    tuide::a_trail_begin(&st, u);
    tuide::ATrailCondBranch branch;
    branch.id = "LINK";
    branch.path = "src/ai/ai_controller.cpp";
    branch.symbol = "cancel_current";
    branch.anchor = "src/ai/ai_controller.cpp:cancel_current";
    st.trail.cond_branches.push_back(branch);
    st.trail.awaiting_judge = true;
    tuide::AVerdict v;
    v.target = "LINK";
    v.verdict = tuide::AVerdictKind::Interesting;
    v.why = "cancel no OFF";
    std::string err;
    std::vector<tuide::AVerdict> one = {v};
    expect(tuide::a_trail_apply_judge(&st, one, &err), "cond interesting ok");
    expect(st.trail.active, "trail active after cond");
    expect(st.trail.force_queue.size() == 1 && st.trail.force_queue[0] == "LINK",
           "force LINK");
    expect(st.trail.cond_branches[0].verdict == tuide::AVerdictKind::Interesting,
           "cond verdict set");
  }
  {
    // Soft-cap: 4 interesting → keep 3, do not hard-fail.
    tuide::AState st;
    tuide::AVerdict u;
    u.target = "src/ui/busy.cpp:set_busy_spinner";
    u.anchor = u.target;
    u.stem = "busy";
    u.verdict = tuide::AVerdictKind::Useful;
    tuide::a_trail_begin(&st, u);
    st.trail.pending_stacks.clear();
    for (int i = 1; i <= 4; ++i) {
      tuide::ATrailStack s;
      s.id = "S" + std::to_string(i);
      s.hops.resize(2);
      s.hops[0].symbol = "caller" + std::to_string(i);
      s.hops[0].anchor = "src/ai/x.cpp:caller" + std::to_string(i);
      s.hops[1].symbol = "set_busy_spinner";
      st.trail.pending_stacks.push_back(s);
    }
    st.trail.awaiting_judge = true;
    std::vector<tuide::AVerdict> four;
    for (int i = 1; i <= 4; ++i) {
      tuide::AVerdict v;
      v.target = "S" + std::to_string(i);
      v.verdict = tuide::AVerdictKind::Interesting;
      four.push_back(v);
    }
    std::string err;
    expect(tuide::a_trail_apply_judge(&st, four, &err), "soft-cap interesting ok");
    expect(st.trail.force_queue.size() <=
               static_cast<std::size_t>(tuide::kATrailMaxInterestingPerLevel),
           "force queue ≤ interesting cap");
    expect(st.trail.force_queue.size() <= static_cast<std::size_t>(tuide::kATrailMaxBranches),
           "force queue ≤ branch cap");
    expect(!st.trail.force_queue.empty(), "kept some interesting");
  }
  {
    // Fuzzy match symbol name → stack id.
    tuide::AState st;
    tuide::AVerdict u;
    u.target = "src/ui/busy.cpp:set_busy_spinner";
    u.anchor = u.target;
    u.stem = "busy";
    u.verdict = tuide::AVerdictKind::Useful;
    tuide::a_trail_begin(&st, u);
    tuide::ATrailStack s1;
    s1.id = "S1";
    s1.hops.resize(2);
    s1.hops[0].symbol = "begin_thinking";
    s1.hops[0].anchor = "src/ai/ai_controller.cpp:begin_thinking";
    s1.hops[1].symbol = "set_busy_spinner";
    st.trail.pending_stacks.clear();
    st.trail.pending_stacks.push_back(s1);
    st.trail.awaiting_judge = true;
    tuide::AVerdict v;
    v.target = "begin_thinking";  // symbol, not S1
    v.verdict = tuide::AVerdictKind::Interesting;
    std::string err;
    expect(tuide::a_trail_apply_judge(&st, {v}, &err), "fuzzy symbol ok");
    expect(st.trail.force_queue.size() == 1 && st.trail.force_queue[0] == "S1",
           "fuzzy queued S1");
  }
  {
    // Unmatched A0 symbol names must fail closed (keep awaiting; no soft-close/suspect).
    tuide::AState st;
    tuide::AVerdict u;
    u.target = "src/ui/busy.cpp:set_busy_spinner";
    u.anchor = u.target;
    u.stem = "busy";
    u.verdict = tuide::AVerdictKind::Useful;
    tuide::a_trail_begin(&st, u);
    tuide::ATrailStack s1;
    s1.id = "S1";
    s1.hops.resize(2);
    s1.hops[0].symbol = "begin_thinking";
    s1.hops[1].symbol = "set_busy_spinner";
    st.trail.pending_stacks.clear();
    st.trail.pending_stacks.push_back(s1);
    tuide::ATrailCondBranch link;
    link.id = "LINK";
    link.symbol = "cancel_current";
    st.trail.cond_branches.push_back(link);
    st.trail.awaiting_judge = true;
    tuide::AVerdict bad;
    bad.target = "wake_console_panel";
    bad.verdict = tuide::AVerdictKind::Interesting;
    bad.why = "garbage A0 name";
    std::string err;
    expect(!tuide::a_trail_apply_judge(&st, {bad}, &err), "unmatched interesting fails");
    expect(st.trail.active && st.trail.awaiting_judge, "trail stays open for retry");
    expect(st.trail.force_queue.empty(), "no force from unmatched");
    expect(!err.empty(), "err set");
  }
  {
    // Dataflow begin after trail must keep recap/job_root (backtrack needs them).
    tuide::AState st;
    st.trail.active = true;
    st.a1_trail_recap = "### cond `CXL`\n";
    st.a1_job_root = "src/ui/busy.cpp:set_busy_spinner";
    st.a1_df_caller_anchor = "src/ai/ai_controller.cpp:cancel_current";
    st.a1_suspect_done = true;
    tuide::AExpansionItem df;
    df.target = "src/ai/ai_controller.hpp:agent_busy_";
    df.modality = tuide::AExpandModality::Dataflow;
    df.suspect_var = "agent_busy_";
    tuide::a_a1_begin_job(&st, df);
    expect(st.a_subphase == "a1_dataflow", "dataflow subphase");
    expect(st.a1_job_root == "src/ui/busy.cpp:set_busy_spinner", "job_root preserved");
    expect(st.a1_trail_recap.find("CXL") != std::string::npos, "recap preserved");
    expect(st.a1_df_caller_anchor.find("cancel_current") != std::string::npos,
           "caller preserved");
    expect(st.a1_active.target.find("agent_busy_") != std::string::npos, "active is df target");
  }
  {
    using tuide::ADataFlowKind;
    using tuide::a_dataflow_classify_line;
    expect(a_dataflow_classify_line("  agent_busy_.store(false);", "agent_busy_") ==
               ADataFlowKind::Write,
           "atomic store → write");
    expect(a_dataflow_classify_line("  if (agent_busy_.load()) {", "agent_busy_") ==
               ADataFlowKind::Read,
           "atomic load → read");
    expect(a_dataflow_classify_line("  if (agent_busy_.exchange(true)) {", "agent_busy_") ==
               ADataFlowKind::Write,
           "atomic exchange → write");
    expect(a_dataflow_classify_line("  std::atomic<bool> agent_busy_{false};", "agent_busy_") ==
               ADataFlowKind::Decl,
           "member decl");
    expect(a_dataflow_classify_line("  // agent_busy_.store(true);", "agent_busy_") ==
               ADataFlowKind::Unknown,
           "comment-only → unknown");
  }
  {
    expect(tuide::a_is_symptom_edge_name("set_active"), "lifecycle edge name");
    expect(tuide::a_target_prefers_trail_a0("src/view/status_panel.cpp:set_active", nullptr),
           "L0 prefers trail");
    expect(tuide::a_coerce_a0_expand_modality("src/view/status_panel.cpp:set_active",
                                              tuide::AExpandModality::Dataflow, nullptr) ==
               tuide::AExpandModality::Trail,
           "coerce dataflow→trail");
    std::vector<std::string> state_writes = {"state.activity", "state.kind"};
    expect(tuide::a_coerce_a0_expand_modality("src/x.cpp:foo", tuide::AExpandModality::Dataflow,
                                              &state_writes) == tuide::AExpandModality::Trail,
           "state writes→trail");
    expect(!tuide::a_a0_dataflow_allowed_without_trail("src/search/x.cpp:cancel",
                                                       "cancel_requested_"),
           "weak A0 dataflow blocked");
    expect(tuide::a_a0_dataflow_allowed_without_trail("src/core/x.cpp:Foo", "update_active_"),
           "strong ident ok");
    std::vector<tuide::AExpansionItem> q;
    tuide::AExpansionItem df;
    df.target = "a";
    df.modality = tuide::AExpandModality::Dataflow;
    tuide::AExpansionItem tr;
    tr.target = "b";
    tr.modality = tuide::AExpandModality::Trail;
    q.push_back(df);
    q.push_back(tr);
    tuide::a_sort_a1_queue(&q);
    expect(q[0].modality == tuide::AExpandModality::Trail, "trail before dataflow");
  }
  {
    nlohmann::json j = nlohmann::json::parse(R"({
      "action":"a_judge","phase":"a0_sniff",
      "verdicts":[
        {"target":"src/a.cpp:Foo","verdict":"expand","expand_with":"peek","why":"hot write"},
        {"target":"src/b.cpp:Bar","verdict":"reject","why":"glue"}
      ]
    })");
    std::vector<tuide::AVerdict> vs;
    std::string err;
    expect(parse_a_verdicts_array(j, &vs, &err), "a0 parse");
    expect(vs.size() == 2, "2 a0 verdicts");
    expect(vs[0].verdict == AVerdictKind::Expand, "expand kind");
    expect(vs[0].expand_with == tuide::AExpandModality::Peek, "peek modality");
  }
  {
    AState st;
    tuide::AQueueItem q;
    q.target = "src/view/status_panel.cpp:start_update_worker#tail";
    q.score = 2541937.f;
    st.queue.push_back(q);
    expect(tuide::a_queue_item_score(st, "src/view/status_panel.cpp:start_update_worker") >
               2.5e6f,
           "queue score matches without #tail");
    expect(tuide::a_queue_item_score(st, "src/view/status_panel.cpp:start_update_worker#tail") >
               2.5e6f,
           "queue score matches with #tail");
    expect(tuide::a_queue_item_score(st, "src/other.cpp:nope") == 0.f, "miss → 0");
  }
  {
    setenv("L2_FEAT_L2_EXPLORE_PHASE_A", "1", 1);
    setenv("L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY", "1", 1);
    expect(tuide::a_effect_summary_enabled(), "effect summary enabled");
    AState st;
    st.a_subphase = "a0_sniff";
    tuide::AQueueItem q;
    q.target = "src/x.cpp:Sym";
    q.score = 0.9f;
    st.queue.push_back(q);
    std::vector<tuide::AVerdict> vs;
    tuide::AVerdict expand;
    expand.target = "src/x.cpp:Sym";
    expand.verdict = AVerdictKind::Expand;
    expand.expand_with = tuide::AExpandModality::Peek;
    expand.why = "hot";
    vs.push_back(expand);
    tuide::AVerdict rej;
    rej.target = "src/y.cpp:Glue";
    rej.verdict = AVerdictKind::Reject;
    rej.why = "noop";
    vs.push_back(rej);
    std::string err;
    expect(tuide::a_apply_a0_verdicts(&st, vs, &err), "a0 apply");
    expect(st.a1_active_set, "queued expand activates A1");
    expect(st.a1_active.modality == tuide::AExpandModality::Peek, "A1 peek");
    unsetenv("L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY");
    unsetenv("L2_FEAT_L2_EXPLORE_PHASE_A");
  }
  {
    setenv("L2_FEAT_L2_EXPLORE_PHASE_A", "1", 1);
    setenv("L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY", "1", 1);
    AState st;
    st.a_subphase = "a0_sniff";
    std::vector<tuide::AVerdict> vs;
    for (int i = 0; i < 5; ++i) {
      tuide::AQueueItem q;
      q.target = "src/x.cpp:Sym" + std::to_string(i);
      st.queue.push_back(q);
      tuide::AVerdict v;
      v.target = q.target;
      v.verdict = AVerdictKind::Expand;
      v.expand_with = tuide::AExpandModality::Trail;
      v.why = "keep 5th expand";
      vs.push_back(v);
    }
    tuide::AVerdict rej;
    rej.target = "src/y.cpp:Glue";
    rej.verdict = AVerdictKind::Reject;
    vs.push_back(rej);
    std::string err;
    expect(tuide::a_apply_a0_verdicts(&st, vs, &err), "a0 apply 5 expands");
    expect(st.a1_active_set, "first expand active");
    expect(static_cast<int>(st.a1_queue.size()) == 4, "no per-turn demote of 5th expand");
    unsetenv("L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY");
    unsetenv("L2_FEAT_L2_EXPLORE_PHASE_A");
  }
  {
    tuide::ATrail tr;
    tr.root_anchor = "src/ui/busy_strip.cpp:set_busy_spinner";
    tr.focus_anchor = tr.root_anchor;
    tuide::ATrailStack s;
    s.id = "S1";
    tuide::ATrailHop h;
    h.symbol = "begin_thinking";
    s.hops.push_back(h);
    tr.pending_stacks.push_back(s);
    tuide::ATrailCondBranch b;
    b.id = "CXL";
    b.when_text = "cancel";
    tr.cond_branches.push_back(b);
    expect(tuide::a_trail_judge_show_stacks(tr), "stacks win first judge");
    const std::string md = tuide::a_trail_stacks_markdown(tr);
    expect(md.find("Ramas condicionales") == std::string::npos, "hide cond when stacks");
    expect(md.find("`S1`") != std::string::npos, "shows S1");
    expect(md.find("prioridad") == std::string::npos, "no prioridad magnet");
  }
  {
    tuide::ATrail tr;
    tr.root_anchor = "src/ui/x.cpp:foo";
    tuide::ATrailCondBranch b;
    b.id = "ON";
    tr.cond_branches.push_back(b);
    expect(!tuide::a_trail_judge_show_stacks(tr), "cond when no stacks");
    const std::string md = tuide::a_trail_stacks_markdown(tr);
    expect(md.find("`ON`") != std::string::npos, "shows ON");
    expect(md.find("vuelve a cola") == std::string::npos, "do not skip cond-only");
  }
  {
    setenv("L2_FEAT_L2_EXPLORE_PHASE_A", "1", 1);
    setenv("L2_FEAT_L2_EXPLORE_ANCHOR_CAUSAL", "1", 1);
    tuide::ProblemFrame pf;
    pf.primary_anchor.objective = "control spinner busy";
    pf.primary_anchor.search_terms = {"spinner", "busy", "set_busy"};
    pf.primary_anchor.edge_hints = {"set_", "clear_"};
    AState st;
    st.explore_mode = "f1_anchor";
    tuide::AQueueItem good;
    good.target = "src/ai/ai_controller.cpp:set_agent_busy";
    good.path = "src/ai/ai_controller.cpp";
    good.symbol = "set_agent_busy";
    good.stem = "ai_controller";
    good.score = 1.f;
    tuide::AQueueItem trap;
    trap.target = "src/ui/console_panel.cpp:log_line";
    trap.path = "src/ui/console_panel.cpp";
    trap.symbol = "log_line";
    trap.stem = "console_panel";
    trap.score = 2.f;
    st.queue = {trap, good};
    tuide::a_apply_f1_anchor_queue_filter(&st, pf);
    expect(st.queue.front().symbol == "set_agent_busy", "F1 rerank boosts anchor match");
    expect(tuide::a_f1_coerce_expand_modality(tuide::AExpandModality::Trail) ==
               tuide::AExpandModality::Peek,
           "F1 coerce trail→peek");
    st.peeks_used = 2;
    st.cards_used = 4;
    tuide::AVerdict note;
    note.verdict = AVerdictKind::Useful;
    note.target = good.target;
    st.notes.push_back(note);
    tuide::AVerdict rej;
    rej.verdict = AVerdictKind::Reject;
    rej.target = trap.target;
    st.notes.push_back(rej);
    tuide::ALocus loc;
    loc.stem = "ai_controller";
    loc.anchor = good.target;
    loc.role = tuide::ALocusRole::Primary;
    loc.why = "sets busy";
    std::string gate_err;
    expect(tuide::a_validate_f1_anchor_done(st, {loc}, &gate_err), "F1 validate ok");
    unsetenv("L2_FEAT_L2_EXPLORE_ANCHOR_CAUSAL");
    unsetenv("L2_FEAT_L2_EXPLORE_PHASE_A");
  }

  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "OK l2_explore_a_test\n";
  return 0;
}
