#include "ai/l2_effect_slice.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

std::string workspace_root() {
  const char* env = std::getenv("TUIDE_ROOT");
  return env != nullptr ? std::string(env) : ".";
}

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

tuide::EffectSliceDeps deps_empty(const std::string& root) {
  tuide::EffectSliceDeps d;
  d.workspace_root = root;
  d.search = [](const std::string&) { return std::vector<tuide::ATrailSearchHit>{}; };
  return d;
}

tuide::EffectSliceDeps deps_rg(const std::string& root) {
  tuide::EffectSliceDeps d;
  d.workspace_root = root;
  d.search = [root](const std::string& symbol) {
    std::vector<tuide::ATrailSearchHit> hits;
    if (symbol.empty()) {
      return hits;
    }
    const std::string cmd = "rg -n --no-heading -F " + symbol + " " + root + "/src 2>/dev/null | head -80";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp == nullptr) {
      return hits;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp) != nullptr) {
      const std::string line(buf);
      const auto colon = line.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const auto colon2 = line.find(':', colon + 1);
      if (colon2 == std::string::npos) {
        continue;
      }
      tuide::ATrailSearchHit h;
      h.path = line.substr(0, colon);
      if (h.path.rfind(root + "/", 0) == 0) {
        h.path = h.path.substr(root.size() + 1);
      }
      try {
        h.line = std::stoi(line.substr(colon + 1, colon2 - colon - 1));
      } catch (...) {
        continue;
      }
      h.preview = line.substr(colon2 + 1);
      hits.push_back(std::move(h));
    }
    pclose(fp);
    return hits;
  };
  return d;
}

bool has_kind(const tuide::EffectSlice& s, tuide::EffectNodeKind k, const std::string& needle) {
  for (const auto& n : s.nodes) {
    if (n.kind != k) {
      continue;
    }
    if (needle.empty() || n.symbol == needle || n.id.find(needle) != std::string::npos ||
        n.cond.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool has_fact_kind(const tuide::EffectSlice& s, tuide::EffectFactKind k) {
  for (const auto& f : s.facts) {
    if (f.kind == k) {
      return true;
    }
  }
  return false;
}

void test_polar_fixture() {
  const std::string root = workspace_root();
  const std::string rel = "tests/fixtures/effect_slice/polar.cpp";
  tuide::EffectSliceSeedIn in;
  in.query = "flag stuck";
  in.add_siblings = false;
  in.map_window.push_back({rel, "set_x", 0, 0.8f});
  in.map_window.push_back({rel, "clear_x", 0, 0.1f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "polar seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(root), &err), "polar build");
  expect(has_kind(sl, tuide::EffectNodeKind::Latch, "flag"), "latch flag");
  expect(has_kind(sl, tuide::EffectNodeKind::Ctrl, "if"), "if node");
  expect(has_fact_kind(sl, tuide::EffectFactKind::Read), "read latch→if");
  bool thread_ok = false;
  for (const auto& t : sl.threads) {
    bool has_latch = false;
    bool has_ctrl = false;
    for (const auto& id : t.node_ids) {
      const auto* n = tuide::effect_slice_find_node(sl, id);
      if (n == nullptr) {
        continue;
      }
      if (n->kind == tuide::EffectNodeKind::Latch) {
        has_latch = true;
      }
      if (n->kind == tuide::EffectNodeKind::Ctrl) {
        has_ctrl = true;
      }
    }
    if (has_latch && has_ctrl) {
      thread_ok = true;
    }
  }
  expect(thread_ok || !sl.threads.empty(), "thread polar or at least some T*");
  expect(!sl.constellations.empty(), "polar constellation");
  if (!sl.constellations.empty()) {
    expect(sl.constellations.front().member == "flag", "constellation centered on flag");
    expect(sl.constellations.front().writer_ids.size() >= 2, "constellation has polar writers");
    expect(!sl.constellations.front().control_ids.empty(), "constellation has control");
  }
}

void test_switch_and_handoff() {
  const std::string root = workspace_root();
  const std::string rel = "tests/fixtures/effect_slice/polar.cpp";
  tuide::EffectSliceSeedIn in;
  in.add_siblings = false;
  in.map_window.push_back({rel, "set_x", 0, 0.5f});
  in.map_window.push_back({rel, "clear_x", 0, 0.5f});
  in.map_window.push_back({rel, "route", 0, 0.2f});
  in.map_window.push_back({rel, "later", 0, 0.2f});
  in.map_window.push_back({rel, "post_later", 0, 0.2f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "switch seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(root), &err), "switch build");
  expect(has_kind(sl, tuide::EffectNodeKind::Ctrl, "switch"), "switch node");
  expect(has_kind(sl, tuide::EffectNodeKind::Ctrl, "case"), "case node");
  expect(has_kind(sl, tuide::EffectNodeKind::Handoff, ""), "handoff from lambda");
}

void test_siblings_expand() {
  const std::string root = workspace_root();
  const std::string rel = "tests/fixtures/effect_slice/polar.cpp";
  tuide::EffectSliceSeedIn in;
  in.add_siblings = true;
  in.map_window.push_back({rel, "set_x", 0, 1.f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "sib seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(root), &err), "sib build");
  expect(has_kind(sl, tuide::EffectNodeKind::Fn, "clear_x"), "sibling clear_x");
  expect(has_kind(sl, tuide::EffectNodeKind::Latch, "flag"), "latch via sibling");
}

void test_json_roundtrip() {
  const std::string root = workspace_root();
  const std::string rel = "tests/fixtures/effect_slice/polar.cpp";
  tuide::EffectSliceSeedIn in;
  in.add_siblings = false;
  in.map_window.push_back({rel, "set_x", 0, 0.5f});
  in.map_window.push_back({rel, "clear_x", 0, 0.5f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "json seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(root), &err), "json build");
  const auto j = tuide::effect_slice_to_json(sl);
  tuide::EffectSlice back;
  expect(tuide::effect_slice_from_json(j, &back, &err), "json parse");
  expect(back.nodes.size() == sl.nodes.size(), "json nodes");
  expect(back.facts.size() == sl.facts.size(), "json facts");
  expect(back.constellations.size() == sl.constellations.size(), "json constellations");
  expect(back.macro_constellations.size() == sl.macro_constellations.size(),
         "json macro constellations");
}

void test_constellation_primary_stems() {
  tuide::EffectSlice sl;
  sl.query = "state remains active";
  tuide::EffectNode on;
  on.id = "fn:src/core/coordinator.cpp:activate";
  on.kind = tuide::EffectNodeKind::Fn;
  on.path = "src/core/coordinator.cpp";
  on.stem = "coordinator";
  on.symbol = "activate";
  on.seed = true;
  on.query_hit = true;
  on.prior_sem = 0.8f;
  tuide::EffectNode off;
  off.id = "fn:src/view/indicator.cpp:deactivate";
  off.kind = tuide::EffectNodeKind::Fn;
  off.path = "src/view/indicator.cpp";
  off.stem = "indicator";
  off.symbol = "deactivate";
  off.prior_sem = 0.4f;
  tuide::EffectNode latch;
  latch.id = "latch:indicator:flag";
  latch.kind = tuide::EffectNodeKind::Latch;
  latch.stem = "indicator";
  latch.symbol = "flag";
  tuide::EffectNode ctrl;
  ctrl.id = "ctrl:src/view/indicator.cpp:10:if";
  ctrl.kind = tuide::EffectNodeKind::Ctrl;
  ctrl.stem = "indicator";
  ctrl.parent_fn = off.id;
  sl.nodes = {on, off, latch, ctrl};
  sl.facts = {
      {"w_on", on.id, latch.id, tuide::EffectFactKind::Write, "flag", 2.f},
      {"w_off", off.id, latch.id, tuide::EffectFactKind::Write, "flag", 2.f},
      {"r_ctrl", latch.id, ctrl.id, tuide::EffectFactKind::Read, "flag", 2.f},
  };

  tuide::effect_slice_rank(&sl);
  expect(sl.constellations.size() == 1, "one manual constellation");
  expect(sl.macro_constellations.size() == 1 &&
             sl.macro_constellations.front().nucleus_ids.size() == 1,
         "atomic nucleus remains inside macro constellation");
  const auto* returned_writer = tuide::effect_slice_find_node(sl, off.id);
  expect(returned_writer != nullptr && returned_writer->mass > 0.045f,
         "polarity latch returns light to relevant writer");
  const auto* projected_hit = tuide::effect_slice_find_node(sl, on.id);
  expect(projected_hit != nullptr && returned_writer != nullptr &&
             projected_hit->mass > returned_writer->mass,
         "top constellation projects light to matching hop0");
  if (sl.constellations.empty()) {
    return;
  }
  const auto& c = sl.constellations.front();
  expect(c.primary_stems.size() == 2, "two complementary primary stems");
  expect(std::find(c.primary_stems.begin(), c.primary_stems.end(), "coordinator") !=
             c.primary_stems.end(),
         "coordinator primary");
  expect(std::find(c.primary_stems.begin(), c.primary_stems.end(), "indicator") !=
             c.primary_stems.end(),
         "indicator primary");
  expect(c.mass_coverage > 0.f, "constellation mass coverage");
}

tuide::EffectNode manual_fn(const std::string& id, const std::string& stem,
                            bool query_hit = false) {
  tuide::EffectNode node;
  node.id = id;
  node.kind = tuide::EffectNodeKind::Fn;
  node.symbol = id;
  node.stem = stem;
  node.seed = query_hit;
  node.query_hit = query_hit;
  node.prior_sem = query_hit ? 0.9f : 0.2f;
  return node;
}

tuide::EffectNode manual_latch(const std::string& id, const std::string& member) {
  tuide::EffectNode node;
  node.id = id;
  node.kind = tuide::EffectNodeKind::Latch;
  node.symbol = member;
  node.stem = "state";
  return node;
}

void test_context_stem_is_not_primary() {
  tuide::EffectSlice sl;
  auto write_a = manual_fn("fn:core:write_a", "core");
  auto write_b = manual_fn("fn:core:write_b", "core");
  auto neighbor = manual_fn("fn:orchestrator:neighbor", "orchestrator", true);
  auto latch = manual_latch("latch:atomic_flag", "atomic_flag");
  sl.nodes = {write_a, write_b, neighbor, latch};
  sl.facts = {
      {"wa", write_a.id, latch.id, tuide::EffectFactKind::Write, "atomic_flag", 2.f},
      {"wb", write_b.id, latch.id, tuide::EffectFactKind::Write, "atomic_flag", 2.f},
      {"call", neighbor.id, write_a.id, tuide::EffectFactKind::Call, "", 1.f},
  };

  tuide::effect_slice_rank(&sl);
  expect(sl.constellations.size() == 1, "atomic context constellation");
  if (sl.constellations.empty()) {
    return;
  }
  const auto& c = sl.constellations.front();
  expect(c.core_stems.size() == 1 && c.core_stems.front() == "core",
         "direct core stem is explicit");
  expect(std::find(c.context_stems.begin(), c.context_stems.end(), "orchestrator") !=
             c.context_stems.end(),
         "call neighbor stem is context");
  expect(std::find(c.primary_stems.begin(), c.primary_stems.end(), "orchestrator") ==
             c.primary_stems.end(),
         "context neighbor never becomes primary");
}

void test_macro_requires_strong_merge_witness() {
  tuide::EffectSlice sl;
  auto a1 = manual_fn("fn:a:a1", "a");
  auto a2 = manual_fn("fn:a:a2", "a");
  auto b1 = manual_fn("fn:b:b1", "b");
  auto b2 = manual_fn("fn:b:b2", "b");
  auto caller = manual_fn("fn:glue:caller", "glue");
  auto la = manual_latch("latch:left_state", "left_state");
  auto lb = manual_latch("latch:right_state", "right_state");
  sl.nodes = {a1, a2, b1, b2, caller, la, lb};
  sl.facts = {
      {"wa1", a1.id, la.id, tuide::EffectFactKind::Write, "left_state", 2.f},
      {"wa2", a2.id, la.id, tuide::EffectFactKind::Write, "left_state", 2.f},
      {"wb1", b1.id, lb.id, tuide::EffectFactKind::Write, "right_state", 2.f},
      {"wb2", b2.id, lb.id, tuide::EffectFactKind::Write, "right_state", 2.f},
      {"ca", caller.id, a1.id, tuide::EffectFactKind::Call, "", 1.f},
      {"cb", b1.id, caller.id, tuide::EffectFactKind::Call, "", 1.f},
  };

  tuide::effect_slice_rank(&sl);
  expect(sl.constellations.size() == 2, "two independent nuclei");
  bool merged = false;
  for (const auto& macro : sl.macro_constellations) {
    merged = merged || macro.nucleus_ids.size() > 1;
  }
  expect(!merged, "caller distance two alone does not merge nuclei");
}

void test_macro_merges_shared_direct_role() {
  tuide::EffectSlice sl;
  auto shared = manual_fn("fn:shared:transition", "shared");
  auto left = manual_fn("fn:left:reset", "left");
  auto right = manual_fn("fn:right:reset", "right");
  auto la = manual_latch("latch:left_mode", "left_mode");
  auto lb = manual_latch("latch:right_mode", "right_mode");
  sl.nodes = {shared, left, right, la, lb};
  sl.facts = {
      {"sla", shared.id, la.id, tuide::EffectFactKind::Write, "left_mode", 2.f},
      {"slb", left.id, la.id, tuide::EffectFactKind::Write, "left_mode", 2.f},
      {"sra", shared.id, lb.id, tuide::EffectFactKind::Write, "right_mode", 2.f},
      {"srb", right.id, lb.id, tuide::EffectFactKind::Write, "right_mode", 2.f},
  };

  tuide::effect_slice_rank(&sl);
  bool witnessed_merge = false;
  for (const auto& macro : sl.macro_constellations) {
    if (macro.nucleus_ids.size() > 1 && macro.merge_strength > 0.f &&
        std::any_of(macro.merge_witnesses.begin(), macro.merge_witnesses.end(),
                    [](const std::string& witness) {
                      return witness.find("shared-direct:") == 0;
                    })) {
      witnessed_merge = true;
    }
  }
  expect(witnessed_merge, "shared direct role merges with exposed witness");
}

void test_no_case17_conditioning_in_ai_pipeline() {
  const std::string root = workspace_root();
  const std::vector<std::string> files = {
      "src/ai/l2_effect_slice.cpp",       "src/ai/l2_effect_summary.cpp",
      "src/ai/l2_explore_a.cpp",         "src/ai/l2_explore_a_trail.cpp",
      "src/ai/l2_pack_review.cpp",        "src/ai/search_needles.cpp",
      "src/ai/coding_embed_rerank.cpp",   "src/ai/level2_session.cpp",
      "src/ai/level2_autonomous_loop.cpp",
  };
  const std::vector<std::string> forbidden = {
      "17_ai_spinner_stuck", "set_busy_spinner", "clear_busy", "agent_busy",
      "AiThinking",          "begin_thinking",   "end_thinking", "busy_strip",
  };
  for (const auto& rel : files) {
    const std::string src = read_file(root + "/" + rel);
    const std::string readable_msg = rel + " readable";
    expect(!src.empty(), readable_msg.c_str());
    for (const auto& token : forbidden) {
      const std::string absent_msg = rel + " has no " + token;
      expect(src.find(token) == std::string::npos, absent_msg.c_str());
    }
  }
}

void test_fixture_state_region() {
  const std::string root = workspace_root();
  tuide::EffectSliceSeedIn in;
  in.query = "flag remains set";
  in.add_siblings = true;
  in.map_window.push_back({"tests/fixtures/effect_slice/polar.cpp", "set_x", 0, 0.9f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "fixture seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(root), &err), "fixture build");
  expect(has_kind(sl, tuide::EffectNodeKind::Fn, "clear_x"), "sibling clear_x");
  expect(has_kind(sl, tuide::EffectNodeKind::Latch, "flag"), "latch flag");
  expect(tuide::effect_slice_count_kind(sl, tuide::EffectNodeKind::Ctrl) >= 1, "at least one ctrl");
  expect(!sl.threads.empty(), "fixture threads");
}

void test_two_file_inventory_call() {
  const std::string root = workspace_root();
  tuide::EffectSliceSeedIn in;
  in.query = "flag";
  in.add_siblings = false;
  in.inventory_paths.push_back("tests/fixtures/effect_slice/box_a.cpp");
  in.map_window.push_back({"tests/fixtures/effect_slice/box_b.cpp", "kick", 0, 0.9f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "twofile seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(root), &err), "twofile build");
  expect(has_kind(sl, tuide::EffectNodeKind::Fn, "set_flag"), "inventory set_flag");
  expect(has_kind(sl, tuide::EffectNodeKind::Fn, "kick"), "seed kick");
  bool cross = false;
  for (const auto& f : sl.facts) {
    if (f.kind != tuide::EffectFactKind::Call) {
      continue;
    }
    const auto* from = tuide::effect_slice_find_node(sl, f.from);
    const auto* to = tuide::effect_slice_find_node(sl, f.to);
    if (from == nullptr || to == nullptr) {
      continue;
    }
    if (from->symbol == "kick" && to->symbol == "set_flag") {
      cross = true;
    }
  }
  expect(cross, "Call kick→set_flag across stems");
  expect(has_kind(sl, tuide::EffectNodeKind::Fn, "clear_flag"), "inventory clear_flag");
}

void test_map_file_level_ingest() {
  const std::string md = R"(# Ranked map
query: flag stuck

## Ranked entries

1. tests/fixtures/effect_slice/box_a.cpp:1  [score=100] — `box_a`
    `file tests/fixtures/effect_slice/box_a.cpp`
2. tests/fixtures/effect_slice/box_b.cpp:8  [score=90] — `kick`
    `void kick(int* p) {`
)";
  tuide::EffectSliceSeedIn in;
  in.add_siblings = false;
  tuide::effect_slice_fill_seed_from_map(&in, md, 10);
  expect(!in.inventory_paths.empty(), "map file-level → inventory");
  expect(in.map_window.size() == 1 && in.map_window[0].symbol == "kick", "map named fn kick");
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "map seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(workspace_root()), &err), "map build");
  expect(has_kind(sl, tuide::EffectNodeKind::Fn, "set_flag"), "file-level expanded set_flag");
}

void test_fail_expand_cap() {
  const std::string root = workspace_root();
  tuide::EffectSliceSeedIn in;
  in.add_siblings = false;
  in.map_window.push_back({"tests/fixtures/effect_slice/box_a.cpp", "set_flag", 0, 1.f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "fail seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(root), &err), "fail build");
  std::vector<std::string> rej;
  if (!sl.threads.empty()) {
    rej.push_back(sl.threads.front().id);
  }
  tuide::effect_slice_fail(&sl, rej, "probe");
  sl.add_siblings = true;
  expect(tuide::effect_slice_expand(&sl, deps_empty(root), &err), "expand");
  expect(has_kind(sl, tuide::EffectNodeKind::Fn, "clear_flag") || sl.exhausted ||
             tuide::effect_slice_count_kind(sl, tuide::EffectNodeKind::Fn) >= 2,
         "expand grew or exhausted");
}

void test_hub_query_unlock() {
  expect(tuide::effect_hub_member("kind"), "kind is hub");
  expect(tuide::effect_hub_member("path"), "path is hub");
  expect(tuide::effect_hub_member("error"), "error is lifecycle hub");
  expect(tuide::effect_hub_member("ready_"), "ready_ is lifecycle hub");
  expect(tuide::effect_hub_member("cache_valid_"), "cache_valid_ is hub");
  expect(!tuide::effect_hub_member("flag"), "flag is not hub");
  expect(!tuide::effect_hub_member("child_pid_"), "pid latch is not hub");
  expect(tuide::effect_hub_member("stdin_fd_"), "stdin_fd_ is resource hub");
  const std::string lsp =
      "si el proceso del servidor de lenguaje LSP se cae o deja de responder";
  expect(!tuide::effect_query_unlocks_member(lsp, "stdin_fd_"), "lsp query does not unlock stdin");
  expect(tuide::effect_query_unlocks_member(lsp, "ready_"), "lsp no-responde unlocks ready_");
  expect(!tuide::effect_query_unlocks_member(lsp, "error"), "lsp query does not unlock error");
  expect(!tuide::effect_query_unlocks_member(lsp, "cache_valid_"), "lsp query does not unlock pty cache");
  expect(tuide::effect_query_unlocks_member("el pipe stdout del proceso", "stdout_fd_"),
         "stdout query unlocks stdout_fd_");
  expect(!tuide::effect_query_unlocks_member(lsp, "path"), "lsp query does not unlock path");
  expect(!tuide::effect_query_unlocks_member(lsp, "kind"), "lsp query does not unlock kind");
  expect(tuide::effect_query_unlocks_member("abrir el archivo por su ruta path", "path"),
         "ruta/path query unlocks path");
  expect(tuide::effect_query_unlocks_member("file path from uri", "path"),
         "english path query unlocks");
}

}  // namespace

int main() {
  test_no_case17_conditioning_in_ai_pipeline();
  test_polar_fixture();
  test_switch_and_handoff();
  test_siblings_expand();
  test_json_roundtrip();
  test_constellation_primary_stems();
  test_context_stem_is_not_primary();
  test_macro_requires_strong_merge_witness();
  test_macro_merges_shared_direct_role();
  test_fixture_state_region();
  test_two_file_inventory_call();
  test_map_file_level_ingest();
  test_fail_expand_cap();
  test_hub_query_unlock();
  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "l2_effect_slice_test ok\n";
  return 0;
}
