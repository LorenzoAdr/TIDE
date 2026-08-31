#include "ai/l2_effect_registry.hpp"
#include "ai/l2_effect_slice.hpp"
#include "ai/l2_explore_a.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
int tmp_seq = 0;

void expect(bool ok, const char* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

std::string tide_root() {
  if (const char* env = std::getenv("TUIDE_ROOT")) {
    if (env[0]) {
      return env;
    }
  }
#ifdef TUIDE_SOURCE_DIR
  return TUIDE_SOURCE_DIR;
#else
  return ".";
#endif
}

std::string make_tmp() {
  fs::path p = fs::temp_directory_path() /
               ("tuide_reg_" + std::to_string(getpid()) + "_" + std::to_string(++tmp_seq));
  fs::create_directories(p);
  return p.string();
}

void write_file(const std::string& abs, const std::string& body) {
  fs::create_directories(fs::path(abs).parent_path());
  std::ofstream out(abs);
  out << body;
}

void copy_rel(const std::string& tide, const std::string& tmp, const std::string& rel) {
  const fs::path dst = fs::path(tmp) / rel;
  fs::create_directories(dst.parent_path());
  fs::copy_file(fs::path(tide) / rel, dst, fs::copy_options::overwrite_existing);
}

std::string worker_read_notes(const std::string& target, const std::string& body,
                              const std::string& var) {
  return "\n----- need_code " + target + " -----\n```cpp\n" + body +
         (body.empty() || body.back() == '\n' ? "" : "\n") + "```\n" +
         "----- dataflow " + var + " -----\nwrites of `" + var + "` in " + target + "\n";
}

void stamp_worker_read(tuide::RegistryCausalPilotWorkerNotebook* nb, const std::string& target,
                       const std::string& body = "void fn() { busy = true; }\n",
                       const std::string& var = "busy") {
  nb->n_need_code = std::max(nb->n_need_code, 1);
  nb->n_dataflow = std::max(nb->n_dataflow, 1);
  nb->notes += worker_read_notes(target, body, var);
}

tuide::EffectSliceDeps deps_empty(const std::string& root) {
  tuide::EffectSliceDeps d;
  d.workspace_root = root;
  d.search = [](const std::string&) { return std::vector<tuide::ATrailSearchHit>{}; };
  return d;
}

tuide::EffectNode mk_fn(const std::string& path, const std::string& symbol, bool seed = false) {
  tuide::EffectNode n;
  n.id = "fn:" + path + ":" + symbol;
  n.kind = tuide::EffectNodeKind::Fn;
  n.path = path;
  n.symbol = symbol;
  n.stem = tuide::registry_stem_of(path);
  n.seed = seed;
  n.line = 1;
  return n;
}

bool ingest(tuide::EffectRegistry* r, const tuide::EffectSlice& sl, bool fixtures, const char* q) {
  tuide::RegistryIngestMeta meta;
  meta.query = q ? q : "test";
  meta.allow_fixtures = fixtures;
  std::string err;
  const bool ok = tuide::registry_ingest_slice(r, sl, meta, &err);
  if (!ok) {
    std::cerr << "ingest err: " << err << '\n';
  }
  return ok;
}

void test_idempotent_polar() {
  const std::string tide = tide_root();
  const std::string tmp = make_tmp();
  copy_rel(tide, tmp, "tests/fixtures/effect_slice/polar.cpp");
  const std::string rel = "tests/fixtures/effect_slice/polar.cpp";
  tuide::EffectSliceSeedIn in;
  in.query = "flag stuck";
  in.add_siblings = false;
  in.map_window.push_back({rel, "set_x", 0, 0.8f});
  in.map_window.push_back({rel, "clear_x", 0, 0.1f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "polar seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(tmp), &err), "polar build");

  tuide::EffectRegistry r;
  expect(tuide::registry_open(tmp, &r, &err), "open polar");
  expect(ingest(&r, sl, true, "polar1"), "ingest 1");
  tuide::RegistryStats a;
  expect(tuide::registry_stats(&r, &a, &err), "stats 1");
  expect(ingest(&r, sl, true, "polar2"), "ingest 2");
  tuide::RegistryStats b;
  expect(tuide::registry_stats(&r, &b, &err), "stats 2");
  expect(a.nodes == b.nodes, "idempotent nodes");
  expect(a.facts == b.facts, "idempotent facts");
  expect(a.fns == b.fns, "idempotent fns");
  expect(b.queries == a.queries + 1, "second query row");
  tuide::RegistryNodeRow row;
  const std::string latch = tuide::registry_canonical_latch_id("polar", "flag");
  expect(tuide::registry_get(&r, latch, &row, &err), "latch polar:flag");
  expect(row.tombstone_reason.empty(), "latch live");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_two_file_call() {
  const std::string tide = tide_root();
  const std::string tmp = make_tmp();
  copy_rel(tide, tmp, "tests/fixtures/effect_slice/box_a.cpp");
  copy_rel(tide, tmp, "tests/fixtures/effect_slice/box_b.cpp");
  tuide::EffectSliceSeedIn in;
  in.query = "flag";
  in.add_siblings = false;
  in.inventory_paths.push_back("tests/fixtures/effect_slice/box_a.cpp");
  in.map_window.push_back({"tests/fixtures/effect_slice/box_b.cpp", "kick", 0, 0.9f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "twofile seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(tmp), &err), "twofile build");

  tuide::EffectRegistry r;
  expect(tuide::registry_open(tmp, &r, &err), "open twofile");
  expect(ingest(&r, sl, true, "twofile"), "ingest twofile");

  tuide::RegistryNodeRow set_flag;
  expect(tuide::registry_get(&r, "fn:tests/fixtures/effect_slice/box_a.cpp:set_flag", &set_flag,
                             &err),
         "one set_flag");
  tuide::RegistryNodeRow kick;
  expect(tuide::registry_get(&r, "fn:tests/fixtures/effect_slice/box_b.cpp:kick", &kick, &err),
         "kick");
  std::vector<tuide::RegistryNeighbor> nbs;
  expect(tuide::registry_neighbors(&r, kick.id, {"call"}, "out", &nbs, &err), "neighbors kick");
  int calls = 0;
  for (const auto& nb : nbs) {
    if (nb.fact.to_id.find("set_flag") != std::string::npos) {
      ++calls;
    }
  }
  expect(calls == 1, "one Call kick→set_flag");
  tuide::RegistryStats st;
  tuide::registry_stats(&r, &st, &err);
  expect(st.fns >= 2, "at least kick+set_flag");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_hpp_cpp_twin() {
  const std::string tmp = make_tmp();
  write_file(tmp + "/src/widget.hpp", "void paint();\n");
  write_file(tmp + "/src/widget.cpp", "void paint() { int x = 1; (void)x; }\n");
  expect(tuide::registry_canonical_fn_id(tmp, "src/widget.hpp", "paint") ==
             "fn:src/widget.cpp:paint",
         "canonical cpp twin");

  tuide::EffectSlice sl;
  sl.nodes.push_back(mk_fn("src/widget.hpp", "paint", true));
  tuide::EffectRegistry r;
  std::string err;
  expect(tuide::registry_open(tmp, &r, &err), "open twin");
  expect(ingest(&r, sl, false, "twin"), "ingest twin");
  tuide::RegistryNodeRow row;
  expect(tuide::registry_get(&r, "fn:src/widget.hpp:paint", &row, &err), "alias resolves");
  expect(row.id == "fn:src/widget.cpp:paint", "canonical id cpp");
  expect(row.path == "src/widget.cpp", "stored path cpp");
  expect(!row.card_json.empty() || !row.card_hash.empty() || true, "card optional");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_latches_per_stem() {
  const std::string tmp = make_tmp();
  write_file(tmp + "/src/box_a.cpp", "void set_flag() { flag = 1; }\n");
  write_file(tmp + "/src/box_b.cpp", "void set_flag() { flag = 1; }\n");
  tuide::EffectSlice sl;
  auto a = mk_fn("src/box_a.cpp", "set_flag", true);
  auto b = mk_fn("src/box_b.cpp", "set_flag", true);
  tuide::EffectNode latch;
  latch.id = "latch:flag";
  latch.kind = tuide::EffectNodeKind::Latch;
  latch.symbol = "latch:flag";
  sl.nodes.push_back(a);
  sl.nodes.push_back(b);
  sl.nodes.push_back(latch);
  tuide::EffectFact w1;
  w1.from = a.id;
  w1.to = latch.id;
  w1.kind = tuide::EffectFactKind::Write;
  w1.member = "flag";
  tuide::EffectFact w2;
  w2.from = b.id;
  w2.to = latch.id;
  w2.kind = tuide::EffectFactKind::Write;
  w2.member = "flag";
  sl.facts.push_back(w1);
  sl.facts.push_back(w2);

  tuide::EffectRegistry r;
  std::string err;
  expect(tuide::registry_open(tmp, &r, &err), "open latch");
  expect(ingest(&r, sl, false, "latch"), "ingest latch");
  tuide::RegistryNodeRow la;
  tuide::RegistryNodeRow lb;
  expect(tuide::registry_get(&r, "latch:box_a:flag", &la, &err), "latch box_a");
  expect(tuide::registry_get(&r, "latch:box_b:flag", &lb, &err), "latch box_b");
  tuide::RegistryNodeRow glob;
  expect(!tuide::registry_get(&r, "latch:flag", &glob, &err), "no global latch:flag");
  tuide::RegistryStats st;
  tuide::registry_stats(&r, &st, &err);
  expect(st.latches == 2, "two latches");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_refresh_tombstone_and_gc() {
  const std::string tmp = make_tmp();
  const std::string rel = "tests/fixtures/effect_slice/box_a.cpp";
  write_file(tmp + "/" + rel,
             "void set_flag(int* p) { if (p) *p = 1; }\n"
             "void clear_flag(int* p) { if (p) *p = 0; }\n");
  tuide::EffectSliceSeedIn in;
  in.add_siblings = false;
  in.inventory_paths.push_back(rel);
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "refresh seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(tmp), &err), "refresh build");

  tuide::EffectRegistry r;
  expect(tuide::registry_open(tmp, &r, &err), "open refresh");
  expect(ingest(&r, sl, true, "refresh"), "ingest refresh");
  tuide::RegistryNodeRow gone;
  expect(tuide::registry_get(&r, "fn:tests/fixtures/effect_slice/box_a.cpp:clear_flag", &gone,
                             &err),
         "clear_flag before refresh");

  write_file(tmp + "/" + rel, "void set_flag(int* p) { if (p) *p = 1; }\n");
  expect(tuide::registry_refresh_path(&r, rel, &err), "refresh path");
  expect(tuide::registry_get(&r, "fn:tests/fixtures/effect_slice/box_a.cpp:clear_flag", &gone,
                             &err),
         "tombstone still gettable");
  expect(gone.tombstone_reason == "gone", "tombstone reason gone");
  tuide::RegistryNodeRow live;
  expect(tuide::registry_get(&r, "fn:tests/fixtures/effect_slice/box_a.cpp:set_flag", &live, &err),
         "set_flag lives");
  expect(live.tombstone_reason.empty(), "set_flag not tombstoned");

  std::vector<tuide::RegistryNeighbor> nbs;
  tuide::registry_neighbors(&r, gone.id, {}, "", &nbs, &err);
  expect(nbs.empty(), "facts touching tombstone dropped");

  tuide::RegistryGcOpts dry;
  dry.dry_run = true;
  dry.min_age_queries = 0;
  tuide::RegistryGcReport report;
  expect(tuide::registry_gc(&r, dry, &report, &err), "gc dry-run");
  expect(report.tombstones >= 1, "dry-run sees tombstone");
  expect(report.applied == 0, "dry-run does not delete");
  expect(tuide::registry_get(&r, gone.id, &gone, &err), "still there after dry-run");

  tuide::RegistryGcOpts apply;
  apply.dry_run = false;
  apply.min_age_queries = 0;
  expect(tuide::registry_gc(&r, apply, &report, &err), "gc apply");
  expect(report.applied >= 1, "apply deletes");
  expect(!tuide::registry_get(&r, gone.id, &gone, &err), "gone after apply");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_reject_tests_product() {
  const std::string tmp = make_tmp();
  tuide::EffectSlice sl;
  sl.nodes.push_back(mk_fn("tests/secret.cpp", "nope", true));
  sl.nodes.push_back(mk_fn("src/ok.cpp", "yes", true));
  write_file(tmp + "/src/ok.cpp", "void yes() {}\n");
  tuide::EffectRegistry r;
  std::string err;
  expect(tuide::registry_open(tmp, &r, &err), "open reject");
  expect(ingest(&r, sl, false, "reject"), "ingest product");
  tuide::RegistryNodeRow row;
  expect(!tuide::registry_get(&r, "fn:tests/secret.cpp:nope", &row, &err), "tests/ rejected");
  expect(tuide::registry_get(&r, "fn:src/ok.cpp:yes", &row, &err), "src/ admitted");
  expect(!tuide::registry_admit_path("tests/foo.cpp", false), "admit tests false");
  expect(tuide::registry_admit_path("tests/fixtures/x.cpp", true), "admit fixtures when allowed");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_canonical_helpers() {
  expect(tuide::registry_canonical_latch_id("polar", "flag") == "latch:polar:flag",
         "latch id");
  expect(tuide::registry_canonical_latch_id("polar", "x").empty(), "noise member rejected");
  expect(tuide::registry_path_to_cpp("src/a.hpp") == "src/a.cpp", "hpp→cpp");
  expect(tuide::registry_stem_of("src/view/status_panel.cpp") == "status_panel", "stem");
}

bool fake_embed(bool /*is_query*/, const std::string& text, std::vector<float>* out) {
  if (out == nullptr) {
    return false;
  }
  out->assign(32, 0.f);
  std::string tok;
  auto flush = [&]() {
    if (tok.size() < 3) {
      tok.clear();
      return;
    }
    std::uint32_t h = 2166136261u;
    for (unsigned char c : tok) {
      h ^= c;
      h *= 16777619u;
    }
    (*out)[h % 32] += 1.f;
    tok.clear();
  };
  for (unsigned char c : text) {
    if (std::isalnum(c)) {
      tok.push_back(static_cast<char>(std::tolower(c)));
    } else {
      flush();
    }
  }
  flush();
  return true;
}

void test_embed_cosine_and_hops() {
  const std::string tmp = make_tmp();
  write_file(tmp + "/src/marker.cpp",
             "void update_marker() { int marker_phase = 1; (void)marker_phase; }\n");
  write_file(tmp + "/src/session.cpp",
             "void compact_session() { int session_cookie = 1; (void)session_cookie; }\n");
  tuide::EffectSlice sl;
  sl.nodes.push_back(mk_fn("src/marker.cpp", "update_marker", true));
  sl.nodes.push_back(mk_fn("src/session.cpp", "compact_session", true));
  tuide::EffectRegistry r;
  std::string err;
  expect(tuide::registry_open(tmp, &r, &err), "open embed");
  expect(ingest(&r, sl, false, "embed"), "ingest embed");

  tuide::RegistryEmbedOpts eopts;
  eopts.skip_glue = false;
  eopts.model = "test-bag";
  tuide::RegistryEmbedReport rep;
  expect(tuide::registry_embed_nodes(&r, fake_embed, {}, eopts, &rep, &err), "embed 1");
  expect(rep.embedded >= 2, "embedded two fns");
  tuide::RegistryStats st;
  tuide::registry_stats(&r, &st, &err);
  expect(st.embeddings >= 2, "stats embeddings");

  expect(tuide::registry_embed_nodes(&r, fake_embed, {}, eopts, &rep, &err), "embed 2");
  expect(rep.skipped_cached >= 2, "second embed is cache hit");
  expect(rep.embedded == 0, "no re-embed if hash equal");

  tuide::RegistryQueryOpts qopts;
  qopts.model = "test-bag";
  qopts.top_k = 2;
  qopts.hops = 0;
  tuide::RegistryQueryResult qr;
  expect(tuide::registry_query(&r, "marker_phase stale", fake_embed, qopts, &qr, &err),
         "query marker");
  expect(!qr.hits.empty(), "query hits");
  expect(qr.hits.front().node.symbol == "update_marker", "marker ranks first");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_query_hops_cross_file() {
  const std::string tide = tide_root();
  const std::string tmp = make_tmp();
  copy_rel(tide, tmp, "tests/fixtures/effect_slice/box_a.cpp");
  copy_rel(tide, tmp, "tests/fixtures/effect_slice/box_b.cpp");
  tuide::EffectSliceSeedIn in;
  in.query = "flag";
  in.add_siblings = false;
  in.inventory_paths.push_back("tests/fixtures/effect_slice/box_a.cpp");
  in.map_window.push_back({"tests/fixtures/effect_slice/box_b.cpp", "kick", 0, 0.9f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "hops seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(tmp), &err), "hops build");
  tuide::EffectRegistry r;
  expect(tuide::registry_open(tmp, &r, &err), "open hops");
  expect(ingest(&r, sl, true, "hops"), "ingest hops");
  tuide::RegistryEmbedOpts eopts;
  eopts.skip_glue = false;
  eopts.model = "test-bag";
  tuide::RegistryEmbedReport rep;
  expect(tuide::registry_embed_nodes(&r, fake_embed, {}, eopts, &rep, &err), "embed hops");
  tuide::RegistryQueryOpts qopts;
  qopts.model = "test-bag";
  qopts.top_k = 4;
  qopts.hops = 1;
  qopts.hop_kinds = {"call"};
  tuide::RegistryQueryResult qr;
  expect(tuide::registry_query(&r, "kick", fake_embed, qopts, &qr, &err), "query kick");
  bool saw_kick = false;
  for (const auto& h : qr.hits) {
    if (h.node.symbol == "kick") {
      saw_kick = true;
    }
  }
  bool saw_set = false;
  for (const auto& h : qr.hits) {
    if (h.node.symbol == "set_flag") {
      saw_set = true;
    }
  }
  for (const auto& h : qr.expanded) {
    if (h.node.symbol == "set_flag") {
      saw_set = true;
    }
  }
  if (!saw_kick || !saw_set) {
    std::cerr << "hops debug hits=" << qr.hits.size() << " expanded=" << qr.expanded.size()
              << "\n";
    for (const auto& h : qr.hits) {
      std::cerr << "  hit " << h.node.id << " cos=" << h.cosine << "\n";
    }
    for (const auto& h : qr.expanded) {
      std::cerr << "  exp hop=" << h.hop << " " << h.node.id << "\n";
    }
  }
  expect(saw_kick, "kick in cosine hits");
  expect(saw_set, "hop call reaches set_flag");

  tuide::RegistryTrailResult tr;
  expect(tuide::registry_query_trails(&r, "kick", fake_embed, qopts, &tr, &err), "trails kick");
  bool trail_kick = false;
  bool trail_set = false;
  for (const auto& t : tr.trails) {
    for (const auto& h : t.hops) {
      if (h.node.symbol == "kick") {
        trail_kick = true;
      }
      if (h.node.symbol == "set_flag") {
        trail_set = true;
      }
    }
  }
  if (!trail_kick || !trail_set) {
    std::cerr << "trails debug n=" << tr.trails.size() << " sub=" << tr.subgraph_nodes << "\n";
    for (const auto& t : tr.trails) {
      std::cerr << "  " << t.id << " " << t.why << " score=" << t.score << "\n";
      for (const auto& h : t.hops) {
        std::cerr << "    " << h.node.id << "\n";
      }
    }
  }
  expect(!tr.trails.empty(), "at least one trail");
  expect(trail_kick && trail_set, "trail kick → set_flag");

  tuide::RegistryQueryResult qr_map;
  qopts.top_k = 1;
  qopts.hops = 0;
  qopts.boost_stems = {"box_b"};
  qopts.boost_fns.push_back({"tests/fixtures/effect_slice/box_b.cpp", "kick"});
  expect(tuide::registry_query(&r, "set_flag", fake_embed, qopts, &qr_map, &err),
         "query map-first");
  expect(!qr_map.hits.empty(), "map-first hop0");
  expect(qr_map.hits.front().node.symbol == "kick", "L1 item occupies hop0 before cosine");

  qopts.top_k = 2;
  tuide::RegistryQueryResult qr_fill;
  expect(tuide::registry_query(&r, "set_flag", fake_embed, qopts, &qr_fill, &err),
         "query map then cosine fill");
  bool saw_flag = false;
  bool saw_kick_hop0 = false;
  for (const auto& h : qr_fill.hits) {
    if (h.node.symbol == "set_flag") {
      saw_flag = true;
    }
    if (h.node.symbol == "kick") {
      saw_kick_hop0 = true;
    }
  }
  expect(saw_kick_hop0, "map stem in hop0");
  expect(saw_flag, "cosine fills remaining hop0 slots");

  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_query_constellations() {
  const std::string tide = tide_root();
  const std::string tmp = make_tmp();
  copy_rel(tide, tmp, "tests/fixtures/effect_slice/polar.cpp");
  const std::string rel = "tests/fixtures/effect_slice/polar.cpp";
  tuide::EffectSliceSeedIn in;
  in.query = "flag stuck";
  in.add_siblings = false;
  in.map_window.push_back({rel, "set_x", 0, 0.8f});
  in.map_window.push_back({rel, "clear_x", 0, 0.4f});
  tuide::EffectSlice sl;
  std::string err;
  expect(tuide::effect_slice_seed(&sl, in, &err), "constellation seed");
  expect(tuide::effect_slice_build(&sl, deps_empty(tmp), &err), "constellation build");

  tuide::EffectRegistry r;
  expect(tuide::registry_open(tmp, &r, &err), "open constellation registry");
  expect(ingest(&r, sl, true, "constellation"), "ingest constellation");
  tuide::RegistryEmbedOpts eopts;
  eopts.skip_glue = false;
  eopts.model = "test-bag";
  tuide::RegistryEmbedReport rep;
  expect(tuide::registry_embed_nodes(&r, fake_embed, {}, eopts, &rep, &err),
         "embed constellation");
  tuide::RegistryQueryOpts qopts;
  qopts.model = "test-bag";
  qopts.top_k = 8;
  qopts.hops = 2;
  qopts.boost_stems = {"polar"};
  tuide::RegistryTrailResult result;
  expect(tuide::registry_query_trails(&r, "flag stuck", fake_embed, qopts, &result, &err),
         "query constellation");
  expect(!result.seeds.empty() && result.seeds.front().mass > 0.f,
         "registry exposes projected seed mass");
  expect(!result.constellations.empty(), "registry returns constellation");
  expect(!result.macro_constellations.empty() &&
             !result.macro_constellations.front().nuclei.empty(),
         "registry returns hierarchical macro constellation");
  if (!result.constellations.empty()) {
    const auto& c = result.constellations.front();
    expect(c.member == "flag", "registry constellation member");
    expect(!c.nodes.empty(), "registry constellation nodes");
    expect(std::find(c.primary_stems.begin(), c.primary_stems.end(), "polar") !=
               c.primary_stems.end(),
           "registry constellation primary stem");
  }
  nlohmann::json judge_payload;
  tuide::RegistryCausalJudgeOpts judge_opts;
  expect(tuide::registry_causal_judge_payload(&r, "flag stuck", result, judge_opts,
                                              &judge_payload, &err),
         "build causal judge payload");
  expect(judge_payload.value("schema", "") == "causal_judge_v1",
         "causal judge schema");
  expect(judge_payload.contains("zones") && !judge_payload["zones"].empty(),
         "causal judge zones");
  if (judge_payload.contains("zones") && !judge_payload["zones"].empty()) {
    const auto& zone = judge_payload["zones"][0];
    expect(zone.contains("edges") && !zone["edges"].empty(),
           "causal judge typed edges");
    expect(zone.contains("representatives") && !zone["representatives"].empty(),
           "causal judge mini cards");
    expect(zone.contains("core_stems") && zone.contains("context_stems") &&
               zone.contains("merge_witnesses"),
           "causal judge exposes core context and merge evidence");
    expect(zone.contains("mechanism") && zone.contains("ports") &&
               zone.contains("support_edges") && zone.contains("pack_meta"),
           "causal judge mechanism pack fields");
    expect(zone["pack_meta"].value("mechanism_pack", false),
           "mechanism pack enabled by default");
    // edges == concat(mechanism slots + ports + support)
    std::size_t concat_n = zone["ports"].size() + zone["support_edges"].size();
    for (const char* slot : {"trigger", "state", "effect"}) {
      if (zone["mechanism"].contains(slot)) {
        ++concat_n;
      }
    }
    expect(zone["edges"].size() == concat_n, "edges equals mechanism∪ports∪support");
    expect(zone["edges"].size() <= static_cast<std::size_t>(judge_opts.max_edges),
           "causal judge edge budget");
    expect(zone["representatives"].size() <=
               static_cast<std::size_t>(judge_opts.max_representatives),
           "causal judge representative budget");
    for (const auto& card : zone["representatives"]) {
      expect(!card.contains("nudge"), "causal judge excludes prior judgments");
    }
    // Skeleton must not invent slots: missing listed or filled from real facts.
    const auto missing = zone["pack_meta"].value("skeleton_missing", nlohmann::json::array());
    for (const char* slot : {"trigger", "state", "effect"}) {
      const bool filled = zone["mechanism"].contains(slot);
      bool listed_missing = false;
      for (const auto& m : missing) {
        listed_missing = listed_missing || (m.is_string() && m.get<std::string>() == slot);
      }
      expect(filled || listed_missing, "skeleton slot filled or explicitly missing");
    }
  }
  expect(tuide::registry_causal_judge_markdown(judge_payload).find("causal edges") !=
             std::string::npos,
         "causal judge markdown");
  expect(tuide::registry_causal_judge_markdown(judge_payload).find("mechanism:") !=
                 std::string::npos ||
             judge_payload["zones"][0]["mechanism"].empty(),
         "causal judge markdown mechanism section when filled");

  {
    nlohmann::json atlas_payload = {
        {"query", "spinner busy"},
        {"gate", {{"max_cosine", 0.6}, {"map_boosted", 1}, {"weak", false}}},
        {"zones",
         nlohmann::json::array(
             {nlohmann::json{{"id", "M1"},
                             {"primary_stems", {"console_panel", "clickable"}},
                             {"risks", {"no_state_nucleus"}},
                             {"pack_meta", {{"skeleton_missing", {"state", "effect"}}}},
                             {"edges",
                              nlohmann::json::array({nlohmann::json{
                                  {"from", "src/ui/console_panel.cpp:handle_console_tab_hover"},
                                  {"kind", "call"},
                                  {"to", "src/ui/clickable.cpp:update_panel_hover"}}})},
                             {"representatives",
                              nlohmann::json::array({nlohmann::json{
                                  {"target",
                                   "src/ui/console_panel.cpp:handle_console_tab_hover"}}})}},
              nlohmann::json{
                  {"id", "M6"},
                  {"primary_stems", {"busy_strip"}},
                  {"nuclei", nlohmann::json::array({nlohmann::json{{"id", "C1"},
                                                                  {"state", "spinner_frame"}}})},
                  {"roles",
                   {{"writers",
                     nlohmann::json::array(
                         {nlohmann::json{{"target", "src/ui/busy_strip.cpp:set_busy_spinner"}},
                          nlohmann::json{{"target", "src/ui/busy_strip.cpp:clear_busy"}}})}}},
                  {"ports",
                   nlohmann::json::array({nlohmann::json{
                       {"from_zone", "M5"},
                       {"to_zone", "M6"},
                       {"from", "src/ai/ai_controller.cpp:begin_thinking"},
                       {"kind", "call"},
                       {"to", "src/ui/busy_strip.cpp:set_busy_spinner"}}})}}})}};
    const std::string atlas_md = tuide::registry_causal_atlas_markdown(atlas_payload);
    expect(atlas_md.find("causal_atlas_v1") != std::string::npos, "atlas schema header");
    expect(atlas_md.find("score=") == std::string::npos, "atlas omits scores");
    expect(atlas_md.find("mini-cards") == std::string::npos, "atlas omits mini-cards");
    expect(atlas_md.find("kind=chrome") != std::string::npos, "atlas chrome kind");
    expect(atlas_md.find("kind=latch") != std::string::npos, "atlas latch kind");
    expect(atlas_md.find("search:") != std::string::npos, "atlas labels retrieval as search");
    expect(atlas_md.find("owns:") != std::string::npos, "atlas owns caption");
    expect(atlas_md.size() < 1800, "atlas stays compact");
    nlohmann::json settings_zone = nlohmann::json{
        {"id", "M4"},
        {"primary_stems", nlohmann::json::array({"settings_modal"})},
        {"representatives",
         nlohmann::json::array(
             {nlohmann::json{{"target", "src/ui/settings_modal.cpp:cancel_shortcut_recording"}},
              nlohmann::json{
                  {"target", "src/ui/settings_modal.cpp:append_top_level_tabs_header"}}})},
        {"roles",
         {{"writers",
           nlohmann::json::array({nlohmann::json{
               {"target", "src/ui/settings_modal.cpp:cancel_shortcut_recording"}}})}}}};
    expect(tuide::registry_causal_zone_kind(settings_zone) == "object",
           "settings_modal is object not cancel");
    nlohmann::json settings_payload = {{"zones", nlohmann::json::array({settings_zone})}};
    const std::string settings_md = tuide::registry_causal_atlas_markdown(settings_payload);
    expect(settings_md.find("kind=object") != std::string::npos, "atlas object kind");
    expect(settings_md.find("no es solo cancel") != std::string::npos, "atlas not caption");
    expect(settings_md.find("append_top_level_tabs_header") != std::string::npos,
           "atlas prefers entry peek");
    const std::string inspect_md = tuide::registry_causal_judge_markdown(atlas_payload);
    expect(inspect_md.find("owns:") != std::string::npos, "inspect captions owns");
    expect(inspect_md.find("kind=latch") != std::string::npos, "inspect captions kind");
    tuide::RegistryCausalTriageDecision settings_survey;
    tuide::RegistryZoneTriage z4;
    z4.id = "M4";
    settings_survey.zones.push_back(z4);
    tuide::registry_atlas_fill_expand_from(settings_payload, &settings_survey);
    expect(!settings_survey.zones.front().expand_from.empty() &&
               settings_survey.zones.front().expand_from.front().find("append_top_level") !=
                   std::string::npos,
           "object expand_from prefers entry over cancel");
    nlohmann::json hole_zone = nlohmann::json{
        {"id", "M3"},
        {"primary_stems", nlohmann::json::array({"editor_panel"})},
        {"risks", nlohmann::json::array({"promoted_from_uncovered"})},
        {"representatives",
         nlohmann::json::array(
             {nlohmann::json{{"target", "src/ui/editor_panel.cpp:set_primary"}}})},
        {"edges",
         nlohmann::json::array({nlohmann::json{
             {"from", "src/ui/editor_panel.cpp:MakeEditorPanel"},
             {"kind", "call"},
             {"to", "src/ui/editor_panel.cpp:set_primary"}}})}};
    tuide::RegistryCausalTriageDecision hole_survey;
    tuide::RegistryZoneTriage z3;
    z3.id = "M3";
    hole_survey.zones.push_back(z3);
    tuide::registry_atlas_fill_expand_from(
        nlohmann::json{{"zones", nlohmann::json::array({hole_zone})}}, &hole_survey);
    expect(!hole_survey.zones.front().expand_from.empty() &&
               hole_survey.zones.front().expand_from.front().find("MakeEditorPanel") !=
                   std::string::npos,
           "hole expand_from prefers Make* from edges");
    nlohmann::json twin_a = {{"id", "M1"},
                             {"primary_stems", nlohmann::json::array({"visual_highlight"})},
                             {"representatives", nlohmann::json::array({nlohmann::json{
                                 {"target", "src/ui/visual_highlight.cpp:compute"}}})}};
    nlohmann::json twin_b = {{"id", "M6"},
                             {"primary_stems", nlohmann::json::array({"visual_highlight"})},
                             {"representatives", nlohmann::json::array({nlohmann::json{
                                 {"target", "src/ui/visual_highlight.cpp:drain"}}})}};
    nlohmann::json twins = {{"zones", nlohmann::json::array({twin_a, twin_b})}};
    expect(tuide::registry_causal_atlas_markdown(twins).find("same=M1") != std::string::npos,
           "atlas collapses duplicate stems");
    expect(tuide::registry_causal_pack_markdown(atlas_payload, tuide::GraphViewLevel::Atlas)
                   .find("causal_atlas_v1") != std::string::npos,
           "pack markdown atlas");
    tuide::RegistryCausalTriageDecision survey = tuide::registry_parse_causal_atlas_survey(
        R"({"action":"causal_atlas_survey_v1","inspect":["M6","M1"],"view":"inspect",)"
        R"("why":"latch del spinner vs chrome de hover"})",
        {"M1", "M6"},
        {{"M6", {"src/ui/busy_strip.cpp:set_busy_spinner"}},
         {"M1", {"src/ui/console_panel.cpp:handle_console_tab_hover"}}});
    expect(survey.ok && survey.shortlist.size() == 2, "atlas survey parses id list");
    expect(survey.view == "inspect", "atlas survey default view");
    tuide::registry_atlas_fill_expand_from(atlas_payload, &survey);
    expect(!survey.zones.empty() && !survey.zones.front().expand_from.empty(),
           "atlas fill expand_from from representatives");
    const auto cover = tuide::registry_parse_causal_atlas_cover(
        R"({"action":"causal_atlas_cover_v1","covers":false,"add":["M6","M1"],)"
        R"("why":"el pack es hover; el latch del spinner está en M6"})",
        {"M1", "M6"}, {"M1"});
    expect(cover.ok && !cover.covers && cover.add.size() == 1 && cover.add[0] == "M6",
           "cover parse skips already-open ids");
    tuide::RegistryCausalTriageDecision only_chrome = tuide::registry_parse_causal_atlas_survey(
        R"({"action":"causal_atlas_survey_v1","inspect":["M1"],"view":"inspect",)"
        R"("why":"chrome de hover para contrastar el síntoma"})",
        {"M1", "M6"},
        {{"M1", {"src/ui/console_panel.cpp:handle_console_tab_hover"}},
         {"M6", {"src/ui/busy_strip.cpp:set_busy_spinner"}}});
    expect(only_chrome.ok && only_chrome.shortlist.size() == 1, "survey single zone");
    const int nadd =
        tuide::registry_atlas_merge_inspect_ids(&only_chrome, cover.add, {"M1", "M6"}, 4);
    expect(nadd == 1 && only_chrome.shortlist.size() == 2 && only_chrome.shortlist.back() == "M6",
           "cover merge appends unused zone");
    const auto cover_yes = tuide::registry_parse_causal_atlas_cover(
        R"({"action":"causal_atlas_cover_v1","covers":true,"add":["M1"],)"
        R"("why":"M6 ya tiene el latch del spinner y writers set/clear"})",
        {"M1", "M6"}, {"M6"});
    expect(cover_yes.ok && cover_yes.covers && cover_yes.add.empty(),
           "cover true clears add list");
    tuide::GraphViewLevel parsed = tuide::GraphViewLevel::Inspect;
    expect(tuide::graph_view_level_parse("atlas", &parsed) &&
               parsed == tuide::GraphViewLevel::Atlas,
           "parse view atlas");
    const auto vp = tuide::graph_view_profile_default(tuide::GraphViewLevel::Atlas);
    expect(vp.max_zones >= 10 && vp.expand_hops == 0, "atlas view is wide and shallow");

    const auto hyp = tuide::registry_parse_causal_zone_hyp(
        R"({"action":"causal_zone_hyp_v1","hypotheses":[{)"
        R"("claim":"el latch spinner no se limpia al terminar",)"
        R"("slots":{"affected":{"stem":"busy_strip","path_symbol":"busy_strip::clear_busy"},)"
        R"("control":{"stem":"ai_controller"},"trigger":null,)"
        R"("cleanup":{"stem":"busy_strip","path_symbol":"busy_strip::clear_busy"}},)"
        R"("gap":"cleanup","anchor_role":"affected",)"
        R"("falsify_by":"si clear_busy corre al done, hyp muere",)"
        R"("why":"M6 posee spinner_frame"}],"why":"latch y caller visibles en fichas"})",
        atlas_payload);
    expect(hyp.ok && hyp.parsed && hyp.mass_band == "high", "zone hyp parses slot schema");
    expect(hyp.hypotheses[0].affected.stem == "busy_strip", "hyp affected stem");
    expect(hyp.hypotheses[0].affected.path_symbol.find("clear_busy") != std::string::npos,
           "hyp canonicalizes short path_symbol");
    expect(hyp.hypotheses[0].gap == "cleanup", "hyp gap");
    const auto with_null = tuide::registry_parse_causal_zone_hyp(
        R"({"action":"causal_zone_hyp_v1","hypotheses":[{)"
        R"("claim":"el latch spinner no se limpia al terminar",)"
        R"("slots":{"affected":{"stem":"busy_strip","path_symbol":"busy_strip::clear_busy"},)"
        R"("control":{"stem":"ai_controller","path_symbol":null},"trigger":null,)"
        R"("cleanup":{"stem":"busy_strip"}},)"
        R"("gap":"cleanup","anchor_role":"affected",)"
        R"("falsify_by":"si clear_busy corre al done, hyp muere","why":"M6 latch"}],)"
        R"("why":"null path_symbol debe aceptarse"})",
        atlas_payload);
    expect(with_null.ok && with_null.hypotheses.size() == 1, "hyp allows null path_symbol");
    const auto invented = tuide::registry_parse_causal_zone_hyp(
        R"({"action":"causal_zone_hyp_v1","hypotheses":[{)"
        R"("claim":"inventa un toolchain que no está en las fichas",)"
        R"("slots":{"affected":{"stem":"gradle"},"control":null,"trigger":null,"cleanup":null},)"
        R"("gap":"affected","anchor_role":"affected","why":"ruido"}],)"
        R"("why":"stem ajeno se anula, la hyp de ausencia vive"})",
        atlas_payload);
    expect(invented.parsed && !invented.ok && invented.need_more,
           "ungrounded stem is not accepted mass");
    expect(invented.hypotheses.size() == 1, "ungrounded stem does not drop hyp");
    expect(invented.hypotheses[0].affected.stem.empty(), "ungrounded stem is nulled");
    expect(invented.mass_band == "low", "honest gap is low mass");
    const auto mixed = tuide::registry_parse_causal_zone_hyp(
        R"({"action":"causal_zone_hyp_v1","hypotheses":[{)"
        R"("claim":"el latch spinner no se limpia al terminar",)"
        R"("slots":{"affected":{"stem":"busy_strip"},"control":{"stem":"gradle"},)"
        R"("trigger":null,"cleanup":null},)"
        R"("gap":"cleanup","anchor_role":"affected",)"
        R"("falsify_by":"si clear_busy corre al done, hyp muere","why":"M6 latch"}],)"
        R"("why":"glosa en un slot no tira la hyp grounded"})",
        atlas_payload);
    expect(mixed.ok && mixed.hypotheses.size() == 1, "mixed hyp keeps grounded slot");
    expect(mixed.hypotheses[0].affected.stem == "busy_strip", "mixed keeps busy_strip");
    expect(mixed.hypotheses[0].control.stem.empty(), "mixed nulls ungrounded control");
    const auto need_more = tuide::registry_parse_causal_zone_hyp(
        R"({"action":"causal_zone_hyp_v1","need_more":true,"add":["M6","M99"],)"
        R"("view":"deep","hypotheses":[],"why":"las fichas no cubren el latch del spinner"})",
        atlas_payload, {"M1", "M6"});
    expect(need_more.parsed && !need_more.ok && need_more.need_more, "need_more is not accepted");
    expect(need_more.add.size() == 1 && need_more.add[0] == "M6", "need_more add filters ids");
    expect(need_more.view == "deep", "need_more view deep");
    nlohmann::json neighbor_payload = nlohmann::json{
        {"zones",
         nlohmann::json::array(
             {nlohmann::json{{"id", "M1"},
                             {"primary_stems", nlohmann::json::array({"visual_highlight"})},
                             {"representatives",
                              nlohmann::json::array({nlohmann::json{
                                  {"target", "src/editor/visual_highlight.cpp:drain"}}})}},
              nlohmann::json{{"id", "M5"},
                             {"primary_stems", nlohmann::json::array({"editor_panel"})},
                             {"representatives",
                              nlohmann::json::array({nlohmann::json{
                                  {"target", "src/ui/editor_panel.cpp:MakeEditorPanel"}}})}}})}};
    const auto neighbor = tuide::registry_parse_causal_zone_hyp(
        R"({"action":"causal_zone_hyp_v1","hypotheses":[{)"
        R"("claim":"el resaltado no se actualiza tras el replace",)"
        R"("slots":{"affected":{"stem":"visual_highlight"},"control":null,)"
        R"("trigger":null,"cleanup":{"stem":"visual_highlight"}},)"
        R"("gap":"cleanup","anchor_role":"affected",)"
        R"("falsify_by":"si drain corre al replace, hyp muere","why":"M1 mechanism"}],)"
        R"("why":"vecino con mechanism no debe coronar"})",
        neighbor_payload);
    expect(neighbor.parsed && !neighbor.ok && neighbor.need_more, "neighbor fill is not accepted");
    expect(!neighbor.masses.empty() && neighbor.masses[0].neighbor_fill,
           "neighbor_fill flag on highlight vs editor_panel");
    const auto suggested =
        tuide::registry_atlas_suggest_cover_ids(atlas_payload, {"M1"}, 2);
    expect(!suggested.empty() && suggested[0] == "M6", "suggest remaining latch not chrome");
    const auto plan = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"este barrio es el latch del spinner pedido?"},)"
        R"({"kind":"como","zone":"M1","question":"como se gestiona el hover en este chrome?"}],)"
        R"("why":"latch del spinner vs chrome rival"})",
        atlas_payload, {"M1", "M6"});
    expect(plan.ok && plan.tasks.size() == 2, "pilot plan parses two diverse tasks");
    expect(plan.tasks[0].kind == "cubre" && plan.tasks[0].stem == "busy_strip",
           "pilot fills stem from zone");
    expect(plan.tasks[1].kind == "como" && plan.tasks[1].stem == "busy_strip",
           "como on chrome retargets to latch");
    expect(plan.n_cubre == 1 && plan.has_como, "pilot score fields");
    const auto plan_dup = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"este barrio es el latch del spinner pedido?"},)"
        R"({"kind":"gap","zone":"M6","question":"quien limpia el spinner en el mismo barrio?"}],)"
        R"("why":"cubre y gap en el mismo barrio son encargos distintos"})",
        atlas_payload, {"M1", "M6"}, {"M6"});
    expect(plan_dup.ok && plan_dup.tasks.size() == 2, "cubre+gap same stem allowed");
    expect(plan_dup.n_cubre == 1 && plan_dup.has_gap, "cubre+gap score");
    const auto plan_two_cubre = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"este barrio es el latch del spinner pedido?"},)"
        R"({"kind":"cubre","zone":"M6","question":"este mismo barrio cubre el spinner otra vez?"}],)"
        R"("why":"dos cubre al mismo owns se colapsan"})",
        atlas_payload, {"M1", "M6"}, {"M6"});
    expect(!plan_two_cubre.ok && plan_two_cubre.tasks.size() == 1,
           "two cubre same stem collapse");
    const auto plan_como_remaining = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"este barrio es el latch del spinner pedido?"},)"
        R"({"kind":"como","zone":"M1","question":"como se escribe el hover en un id no abierto?"}],)"
        R"("why":"como en restantes debe caer"})",
        atlas_payload, {"M1", "M6"}, {"M6"});
    expect(plan_como_remaining.tasks.size() == 1 && !plan_como_remaining.has_como,
           "como on remaining is dropped");
    const auto plan_template_q = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"¿Es ESTE owns el objeto de la consulta?"},)"
        R"({"kind":"cubre","zone":"M1","question":"¿Es ESTE owns el objeto de la consulta?"},)"
        R"({"kind":"como","zone":"M6","question":"¿Quién pone/limpia/dispara X EN este stem?"}],)"
        R"("why":"plantilla clonada se especializa por owns"})",
        atlas_payload, {"M1", "M6"}, {"M1", "M6"});
    expect(plan_template_q.ok && plan_template_q.tasks.size() == 3,
           "template questions salvage two cubre");
    expect(plan_template_q.n_cubre == 2 && plan_template_q.has_como, "template score two cubre");
    expect(plan_template_q.tasks[0].question.find("busy_strip") != std::string::npos &&
               plan_template_q.tasks[1].question.find("console_panel") != std::string::npos,
           "template cubre names each owns");
    const auto plan_como_q = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"¿Cómo se llama el objeto que representa el spinner?"},)"
        R"({"kind":"como","zone":"M1","question":"¿es console_panel el objeto de la consulta?"}],)"
        R"("why":"cubre cómo y como yes/no se reescriben"})",
        atlas_payload, {"M1", "M6"}, {"M1", "M6"});
    expect(plan_como_q.ok && plan_como_q.tasks.size() == 2, "rewritten verbs still parse");
    expect(plan_como_q.tasks[0].question.find("¿es busy_strip") != std::string::npos,
           "cubre cómo becomes yes/no");
    expect(plan_como_q.tasks[1].kind == "como" &&
               plan_como_q.tasks[1].stem == "busy_strip",
           "como on chrome retargets to latch cubre");
    const auto wr_pipe = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain|no_cubre",)"
        R"("path_symbol":"src/ui/busy_strip.cpp:clear_busy",)"
        R"("chain":"done→clear_busy→spinner_frame","why":"limpia el latch al terminar"})",
        "como", "busy_strip");
    expect(wr_pipe.ok && wr_pipe.verdict == "chain" && wr_pipe.covers, "como accepts chain|alt");
    const auto wr = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"cubre","verdict":"no_cubre",)"
        R"("owns":"console_panel","why":"esto es chrome de hover, no el spinner"})",
        "cubre", "console_panel");
    expect(wr.ok && !wr.covers && wr.verdict == "no_cubre", "worker cubre no_cubre");
    const auto wr_code = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_need_code","target":"src/ui/busy_strip.cpp:clear_busy"})",
        "como", "busy_strip");
    expect(wr_code.ok && wr_code.need_code, "worker need_code in stem");
    const auto wr_out = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_need_code","target":"src/ai/ai_controller.cpp:begin_thinking"})",
        "como", "busy_strip");
    expect(!wr_out.ok && !wr_out.need_code, "worker need_code fenced by stem");
    const auto plen = tuide::registry_parse_causal_pilot_plenary(
        R"({"action":"causal_pilot_plenary_v1","verdict":"entiendo",)"
        R"("keep":["busy_strip"],"drop":["console_panel"],"why":"el latch explica el spinner"})",
        {"busy_strip", "console_panel"});
    expect(plen.ok && plen.verdict == "entiendo" && plen.keep.size() == 1, "plenary keep");
    tuide::RegistryCausalPilotPlenary empty_plen;
    empty_plen.verdict = "no_entiendo";
    empty_plen.ok = true;
    std::vector<tuide::RegistryCausalPilotWorkerReport> reps(2);
    reps[0].ok = true;
    reps[0].kind = "cubre";
    reps[0].stem = "settings_modal";
    reps[0].covers = true;
    reps[0].verdict = "cubre";
    reps[1].ok = true;
    reps[1].stem = "application";
    reps[1].verdict = "no_cubre";
    tuide::registry_tally_pilot_plenary(&empty_plen, reps);
    expect(empty_plen.verdict == "entiendo" && empty_plen.keep.size() == 1 &&
               empty_plen.keep[0] == "settings_modal",
           "tally fills keep from cubre");
    nlohmann::json one = tuide::registry_causal_payload_filter_zones(atlas_payload, {"M6"});
    expect(one["zones"].size() == 1 && one["zones"][0]["id"] == "M6", "filter one zone");
    const auto menu = tuide::registry_causal_pilot_barrio_menu(twins, {"M1", "M6"});
    expect(menu.find("owns=visual_highlight") != std::string::npos, "barrio menu owns");
    expect(menu.find("same=M1") != std::string::npos, "barrio menu marks clone");
    nlohmann::json quit_z = {{"id", "M9"},
                             {"primary_stems", nlohmann::json::array({"quit_confirm"})},
                             {"representatives", nlohmann::json::array({nlohmann::json{
                                 {"target", "src/ui/quit_confirm.cpp:show_dialog"}}})}};
    nlohmann::json shut_z = {{"id", "M2"},
                             {"primary_stems", nlohmann::json::array({"shutdown_overlay"})},
                             {"representatives", nlohmann::json::array({nlohmann::json{
                                 {"target", "src/ui/shutdown_overlay.cpp:draw"}}})}};
    const std::string q_quit = "dialogo de confirmacion al cerrar la aplicacion";
    expect(tuide::registry_causal_query_zone_overlap(q_quit, quit_z) >
               tuide::registry_causal_query_zone_overlap(q_quit, shut_z),
           "confirmacion overlaps quit_confirm more than shutdown");
    nlohmann::json compile_z = {
        {"id", "M3"},
        {"primary_stems", nlohmann::json::array({"compile_commands_remap"})},
        {"representatives", nlohmann::json::array({nlohmann::json{
            {"target", "src/util/compile_commands_remap.cpp:remap"}}})}};
    expect(tuide::registry_causal_query_zone_overlap("compile o ejecute el build", compile_z) > 0,
           "compile overlaps compile_commands");
    expect(tuide::registry_causal_zone_id_for_stem(atlas_payload, "busy_strip") == "M6",
           "zone id from primary stem");
    nlohmann::json ov_payload = {{"zones", nlohmann::json::array({shut_z, quit_z})}};
    const auto ov_add =
        tuide::registry_atlas_overlap_add_ids(ov_payload, q_quit, std::vector<std::string>{"M2"});
    expect(ov_add.size() == 1 && ov_add[0] == "M9", "overlap add remaining with higher score");
    const auto opened_pack = tuide::registry_causal_pilot_opened_pack(atlas_payload, {"M6"}, "");
    expect(opened_pack.find("pilot_opened_v1") != std::string::npos, "opened pack header");
    expect(opened_pack.find("owns:") != std::string::npos &&
               opened_pack.find("busy_strip") != std::string::npos,
           "opened pack names latch owns");
    expect(opened_pack.find("nucleus:") != std::string::npos &&
               opened_pack.find("spinner_frame") != std::string::npos,
           "opened pack keeps nucleus");
    expect(opened_pack.find("mini-cards") == std::string::npos, "opened pack omits mini-cards");
    const auto plan_need = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_need_more","add":["M6","M99"],)"
        R"("why":"el latch del spinner está en restantes no en abiertos"})",
        atlas_payload, {"M1", "M6"}, {"M1"}, "", true);
    expect(plan_need.ok && plan_need.need_more && plan_need.add.size() == 1 &&
               plan_need.add[0] == "M6",
           "need_more filters remaining ids");
    const auto need_blocked = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_need_more","add":["M6"],)"
        R"("why":"segunda ampliación no está permitida en este pase"})",
        atlas_payload, {"M1", "M6"}, {"M1"}, "", false);
    expect(!need_blocked.ok && need_blocked.need_more, "need_more rejected on second pass");
    const auto need_open = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_need_more","add":["M1"],)"
        R"("why":"no se puede pedir un id ya abierto"})",
        atlas_payload, {"M1", "M6"}, {"M1"}, "", true);
    expect(!need_open.ok, "need_more drops already-open ids");
    const auto sys_more = tuide::registry_causal_pilot_plan_system_prompt(true);
    const auto sys_plan = tuide::registry_causal_pilot_plan_system_prompt(false);
    expect(sys_more.find("causal_pilot_need_more") != std::string::npos,
           "first-pass prompt offers need_more");
    expect(sys_plan.find("causal_pilot_need_more") == std::string::npos &&
               sys_plan.find("PROHIBIDO ampliar") != std::string::npos,
           "second-pass prompt forbids need_more");
    const auto user_plan = tuide::registry_causal_pilot_plan_user_prompt("atlas", "pack", {"M6"},
                                                                        {}, false);
    expect(user_plan.find("PROHIBIDO add") != std::string::npos,
           "second-pass user forbids add");
    const auto plan_rem_zero = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"este barrio es el latch del spinner pedido?"},)"
        R"({"kind":"cubre","zone":"M1","question":"este barrio cubre el hover de chrome?"}],)"
        R"("why":"restante sin solape no entra en plaza 4"})",
        atlas_payload, {"M1", "M6"}, {"M6"},
        "dialogo de confirmacion al cerrar");
    expect(plan_rem_zero.tasks.size() == 1, "remaining cubre without overlap is dropped");
    const auto plan_rem_hit = tuide::registry_parse_causal_pilot_plan(
        R"({"action":"causal_pilot_plan_v1","tasks":[)"
        R"({"kind":"cubre","zone":"M6","question":"este barrio es el latch del spinner pedido?"},)"
        R"({"kind":"cubre","zone":"M1","question":"este barrio cubre el texto de la consola?"}],)"
        R"("why":"restante con solape de console si entra"})",
        atlas_payload, {"M1", "M6"}, {"M6"}, "pinta el texto del terminal console");
    expect(plan_rem_hit.ok && plan_rem_hit.n_cubre == 2, "remaining cubre with overlap kept");
    const auto wr_flip = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"cubre","verdict":"cubre",)"
        R"("owns":"shutdown_overlay","why":"owns es shutdown_overlay de la ficha"})",
        "cubre", "shutdown_overlay", q_quit);
    expect(wr_flip.ok && wr_flip.covers && wr_flip.verdict == "cubre",
           "cubre without query overlap keeps worker verdict");
    const auto wr_keep = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"cubre","verdict":"cubre",)"
        R"("owns":"quit_confirm","why":"este barrio es el dialogo de confirmacion al salir"})",
        "cubre", "quit_confirm", q_quit);
    expect(wr_keep.ok && wr_keep.covers && wr_keep.verdict == "cubre",
           "cubre with query overlap stays cubre");
    const auto wr_port = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"gap","verdict":"missing",)"
        R"("port_to":"stem_vecino","why":"el call sale a un vecino que no veo"})",
        "gap", "busy_strip");
    expect(wr_port.ok && wr_port.port_to.empty(), "placeholder port_to is cleared");
    const auto wr_outline = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_outline","target":"src/ui/busy_strip.cpp"})",
        "cubre", "busy_strip");
    expect(wr_outline.ok && wr_outline.is_tool && wr_outline.tool == "outline",
           "worker outline in stem");
    const auto wr_ol_out = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_outline","target":"src/ai/ai_controller.cpp"})",
        "cubre", "busy_strip");
    expect(!wr_ol_out.ok && !wr_ol_out.is_tool, "outline fenced by stem");
    const auto wr_follow = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_follow","target":"src/ui/busy_strip.cpp:set_busy_spinner",)"
        R"("direction":"outgoing"})",
        "como", "busy_strip");
    expect(wr_follow.ok && wr_follow.is_tool && wr_follow.tool == "follow" &&
               wr_follow.direction == "outgoing",
           "worker follow outgoing");
    const auto wr_alias = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"need_code","target":"src/ui/busy_strip.cpp:set_busy_spinner"})",
        "como", "busy_strip");
    expect(wr_alias.ok && wr_alias.is_tool && wr_alias.tool == "need_code",
           "short action need_code aliases to catalog");
    const auto wr_cubre_follow = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_follow","target":"src/ui/busy_strip.cpp:set_busy_spinner",)"
        R"("direction":"outgoing"})",
        "cubre", "busy_strip");
    expect(!wr_cubre_follow.ok, "cubre rejects follow");
    tuide::RegistryCausalPilotWorkerNotebook nb_card;
    nlohmann::json zone_busy = {
        {"id", "M1"},
        {"primary_stems", nlohmann::json::array({"busy_strip"})},
        {"representatives",
         nlohmann::json::array({nlohmann::json{
             {"target", "src/ui/busy_strip.cpp:set_busy_spinner"}}})},
        {"edges", nlohmann::json::array({nlohmann::json{
                      {"from", "src/ui/busy_strip.cpp:set_busy_spinner"},
                      {"to", "src/ui/busy_strip.cpp:ensure_spinner_thread"},
                      {"kind", "call"}}})}};
    tuide::registry_causal_pilot_notebook_from_payload(nlohmann::json{{"zones", {zone_busy}}},
                                                       "busy_strip", &nb_card);
    expect(tuide::registry_causal_pilot_target_in_notebook(
               "src/ui/busy_strip.cpp:set_busy_spinner", nb_card),
           "notebook has spinner symbol");
    const auto wr_invent = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_need_code","target":"src/ui/busy_strip.cpp:handle_load_state"})",
        "como", "busy_strip", "", &nb_card);
    expect(!wr_invent.ok && !wr_invent.need_code, "need_code must be on the card");
    const auto wr_follow_invent = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_follow","target":"src/ui/busy_strip.cpp:handle_load_state",)"
        R"("direction":"outgoing"})",
        "como", "busy_strip", "", &nb_card);
    expect(!wr_follow_invent.ok && !wr_follow_invent.is_tool,
           "follow must be on the card");
    const auto wr_follow_card = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_follow","target":"src/ui/busy_strip.cpp:set_busy_spinner",)"
        R"("direction":"outgoing"})",
        "como", "busy_strip", "", &nb_card);
    expect(wr_follow_card.ok && wr_follow_card.is_tool && wr_follow_card.tool == "follow",
           "follow of a card symbol is allowed");
    const auto wr_df = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_dataflow","target":"busy","path":"src/ui/busy_strip.cpp"})",
        "como", "busy_strip", "", &nb_card);
    expect(wr_df.ok && wr_df.is_tool && wr_df.tool == "dataflow" && wr_df.target == "busy",
           "dataflow of a field is allowed");
    const auto wr_df_out = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"dataflow","target":"busy","path":"src/ai/ai_controller.cpp"})",
        "como", "busy_strip", "", &nb_card);
    expect(!wr_df_out.ok && !wr_df_out.is_tool, "dataflow path fenced by stem");
    const auto wr_causal = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_causal","target":"src/ui/busy_strip.cpp:set_busy_spinner"})",
        "como", "busy_strip", "", &nb_card);
    expect(wr_causal.ok && wr_causal.is_tool && wr_causal.tool == "causal",
           "causal of a card symbol is allowed");
    tuide::RegistryCausalPilotWorkerNotebook nb_chrome;
    nlohmann::json zone_chrome = {
        {"id", "M2"},
        {"owns", "console_panel"},
        {"primary_stems", nlohmann::json::array({"console_panel"})},
        {"representatives",
         nlohmann::json::array({nlohmann::json{
             {"target", "src/ui/console_panel.cpp:handle_console_tab_hover"}}})}};
    nlohmann::json chrome_payload = {
        {"zones", nlohmann::json::array({zone_chrome})},
        {"uncovered_seeds", nlohmann::json::array({nlohmann::json{
                                {"target", "src/ui/console_panel.cpp:try_open_ai_result_at"},
                                {"stem", "console_panel"}}})}};
    tuide::registry_causal_pilot_notebook_from_payload(chrome_payload, "console_panel",
                                                       &nb_chrome);
    expect(tuide::registry_causal_pilot_target_in_notebook(
               "src/ui/console_panel.cpp:handle_console_tab_hover", nb_chrome),
           "chrome notebook has representative");
    expect(!tuide::registry_causal_pilot_target_in_notebook(
               "src/ui/console_panel.cpp:try_open_ai_result_at", nb_chrome),
           "uncovered seeds are not ficha symbols");
    const auto wr_seed_follow = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_follow","target":"src/ui/console_panel.cpp:try_open_ai_result_at",)"
        R"("direction":"outgoing"})",
        "como", "console_panel", "", &nb_chrome);
    expect(!wr_seed_follow.ok && !wr_seed_follow.is_tool,
           "follow of an uncovered seed is rejected");
    nb_chrome.n_follow = 1;
    nb_chrome.notes =
        "\n----- follow src/ui/console_panel.cpp:handle_console_tab_hover outgoing -----\n"
        "(sin hops en la ficha para ese símbolo)\n";
    const auto wr_empty_nc = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"no_cubre",)"
        R"("path_symbol":"src/ui/console_panel.cpp:handle_console_tab_hover",)"
        R"("why":"handle_console_tab_hover no tiene hops para el acto de carga",)"
        R"("brief":"Este barrio solo registra hover del tab. No controla el spinner de carga."})",
        "como", "console_panel", "", &nb_chrome);
    expect(!wr_empty_nc.ok && wr_empty_nc.error.find("sin lectura") != std::string::npos,
           "empty follow of a rep is not a report");
    stamp_worker_read(&nb_chrome, "src/ui/console_panel.cpp:handle_console_tab_hover",
                      "void handle_console_tab_hover() {}\n", "hover");
    const auto wr_empty_nc_flow = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"no_cubre",)"
        R"("path_symbol":"src/ui/console_panel.cpp:handle_console_tab_hover",)"
        R"("why":"handle_console_tab_hover no tiene hops para el acto de carga",)"
        R"("brief":"Este barrio solo registra hover del tab. No controla el spinner de carga."})",
        "como", "console_panel", "", &nb_chrome);
    expect(!wr_empty_nc_flow.ok && wr_empty_nc_flow.error.find("follow vacío") != std::string::npos,
           "empty follow of a rep is not no_cubre even after a read");
    const auto wr_empty_chain = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
        R"("path_symbol":"src/ui/console_panel.cpp:handle_console_tab_hover",)"
        R"("chain":"handle_console_tab_hover registra el hover del tab de consola",)"
        R"("why":"handle_console_tab_hover es el acto de hover en este stem",)"
        R"("brief":"El hover del tab vive aquí. No hay hops; el símbolo mismo es el acto."})",
        "como", "console_panel", "", &nb_chrome);
    expect(wr_empty_chain.ok && wr_empty_chain.verdict == "chain",
           "empty follow may still chain if the symbol is the act after a read");
    nb_chrome.notes +=
        "\n----- follow src/ui/console_panel.cpp:handle_console_tab_click outgoing -----\n"
        "(sin hops en la ficha para ese símbolo)\n";
    tuide::registry_causal_pilot_notebook_add_target(
        &nb_chrome, "src/ui/console_panel.cpp:handle_console_tab_click");
    const auto wr_two_nc = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"no_cubre",)"
        R"("path_symbol":"src/ui/console_panel.cpp:handle_console_tab_hover",)"
        R"("why":"handle_console_tab_hover y handle_console_tab_click no cubren la carga",)"
        R"("brief":"Ni hover ni click controlan el spinner. Este barrio es chrome de consola."})",
        "como", "console_panel", "", &nb_chrome);
    expect(wr_two_nc.ok && wr_two_nc.verdict == "no_cubre",
           "no_cubre after two distinct empty follows is allowed");
    const auto wr_como_notool = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
        R"("path_symbol":"src/ui/busy_strip.cpp:set_busy_spinner",)"
        R"("chain":"set_busy_spinner pinta el latch","why":"set_busy_spinner escribe spinner_frame"})",
        "como", "busy_strip", "", &nb_card);
    expect(!wr_como_notool.ok, "como chain without need_code is invalid");
    stamp_worker_read(&nb_card, "src/ui/busy_strip.cpp:set_busy_spinner",
                      "void set_busy_spinner() { spinner_frame = 1; }\n", "spinner_frame");
    const auto wr_como_tool = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
        R"("path_symbol":"src/ui/busy_strip.cpp:set_busy_spinner",)"
        R"("chain":"set_busy_spinner pinta el latch","why":"set_busy_spinner escribe spinner_frame"})",
        "como", "busy_strip", "", &nb_card);
    expect(wr_como_tool.ok && wr_como_tool.covers && !wr_como_tool.walk.empty(),
           "como chain after need_code aliases walk");
    const auto wr_template = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"cubre","verdict":"cubre",)"
        R"("owns":"busy_strip","why":"la ficha muestra el UI pedido"})",
        "cubre", "busy_strip", "spinner del chat", &nb_card);
    expect(!wr_template.ok, "cubre template why is rejected when notebook has symbols");
    const auto wr_cite = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"cubre","verdict":"cubre",)"
        R"("owns":"busy_strip","why":"set_busy_spinner es el latch del spinner pedido"})",
        "cubre", "busy_strip", "spinner del chat", &nb_card);
    expect(wr_cite.ok && wr_cite.covers && wr_cite.brief.find("latch") != std::string::npos,
           "cubre why that cites a card symbol");
    const auto wr_gap_prose = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"gap","verdict":"missing",)"
        R"("why":"falta el control del estado de carga del chat",)"
        R"("brief":"El spinner se queda. No hay cancelacion limpia en este barrio."})",
        "gap", "busy_strip", "", &nb_card);
    expect(!wr_gap_prose.ok && wr_gap_prose.error.find("why no cita") != std::string::npos,
           "gap prose without a notebook symbol is rejected");
    const auto wr_gap_cite = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"gap","verdict":"missing",)"
        R"("why":"set_busy_spinner pinta el latch pero el cancel sale del stem",)"
        R"("brief":"busy_strip tiene set_busy_spinner. El eslabon de cancel no vive aqui."})",
        "gap", "busy_strip", "", &nb_card);
    expect(wr_gap_cite.ok && wr_gap_cite.verdict == "missing",
           "gap missing that cites a card symbol");
    const auto wr_symbol_only = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"cubre","verdict":"cubre",)"
        R"("owns":"busy_strip","why":"set_busy_spinner"})",
        "cubre", "busy_strip", "spinner del chat", &nb_card);
    expect(!wr_symbol_only.ok, "cubre symbol-only why is not a piloto brief");
    const auto como_nudge_only = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n", "\n----- nota -----\nInforme rechazado\n");
    expect(como_nudge_only.find("PROHIBIDO causal_pilot_worker_v1") != std::string::npos &&
               como_nudge_only.find("need_code") != std::string::npos,
           "without a body the next JSON must be a tool");
    const auto como_follow_notes = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n",
        "\n----- follow src/ui/busy_strip.cpp:set_busy_spinner outgoing -----\nhop\n");
    expect(como_follow_notes.find("PROHIBIDO causal_pilot_worker_v1") != std::string::npos &&
               como_follow_notes.find("need_code") != std::string::npos,
           "follow chunk does not unlock informe");
    const auto como_empty_hops = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n",
        "\n----- follow src/ui/console_panel.cpp:handle_console_tab_hover outgoing -----\n"
        "(sin hops en la ficha para ese símbolo)\n");
    expect(como_empty_hops.find("PROHIBIDO causal_pilot_worker_v1") != std::string::npos,
           "empty follow still requires a body read");
    const auto como_body_only = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n",
        "\n----- need_code src/ui/busy_strip.cpp:set_busy_spinner -----\nvoid set_busy_spinner(){}\n");
    expect(como_body_only.find("sin dudas") != std::string::npos &&
               como_body_only.find("PROHIBIDO causal_pilot_worker_v1") == std::string::npos,
           "need_code without dataflow does not forbid informe");
    const auto como_ready = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n",
        "\n----- need_code src/ui/busy_strip.cpp:set_busy_spinner -----\nvoid fn(){}\n"
        "\n----- dataflow busy -----\nwrites\n");
    expect(como_ready.find("sin dudas") != std::string::npos &&
               como_ready.find("cuando baste") == std::string::npos &&
               como_ready.find("Puedes emitir causal_pilot_worker_v1") == std::string::npos,
           "body+flow does not invite an early report");
    const auto como_unread = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n",
        "\n----- need_code src/ui/busy_strip.cpp:set_busy_spinner -----\nvoid fn(){}\n"
        "\n----- nota -----\nInforme rechazado: walk nombra un símbolo no leído: "
        "src/ui/busy_strip.cpp:halt_busy_strip\n");
    expect(como_unread.find("PROHIBIDO causal_pilot_worker_v1") != std::string::npos,
           "unread walk step forces a tool, not another report");
    const auto como_unread_resolved = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n",
        "\n----- need_code src/ui/busy_strip.cpp:set_busy_spinner -----\nvoid set_busy_spinner(){}\n"
        "\n----- nota -----\nInforme rechazado: walk nombra un símbolo no leído: "
        "src/ui/busy_strip.cpp:halt_busy_strip\n"
        "\n----- need_code src/ui/busy_strip.cpp:halt_busy_strip -----\nvoid halt_busy_strip(){}\n");
    expect(como_unread_resolved.find("PROHIBIDO causal_pilot_worker_v1") == std::string::npos,
           "after reading the unread symbol the report is allowed again");
    const auto como_last = tuide::registry_causal_pilot_worker_user_prompt(
        "como", "q", "# ficha\n", "\n----- nota -----\n", "", true);
    expect(como_last.find("ÚLTIMO TURNO") != std::string::npos &&
               como_last.find("PROHIBIDO pedir tool") != std::string::npos,
           "last turn forces the report");
    std::vector<std::string> hops;
    std::string port;
    const auto follow_md = tuide::registry_causal_pilot_follow_markdown(
        nlohmann::json{{"zones", {zone_busy}}}, "src/ui/busy_strip.cpp:set_busy_spinner",
        "outgoing", "busy_strip", &hops, &port);
    expect(follow_md.find("ensure_spinner_thread") != std::string::npos && !hops.empty(),
           "follow markdown lists outgoing hop");
    tuide::RegistryCausalPilotWorkerNotebook nb_empty;
    const auto wr_empty = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"cubre","verdict":"no_cubre",)"
        R"("owns":"console_panel","why":"esto es chrome de hover, no el spinner"})",
        "cubre", "console_panel", "", &nb_empty);
    expect(!wr_empty.ok, "empty card cubre must outline or need_code first");
    tuide::RegistryCausalPilotWorkerNotebook nb_close;
    nb_close.notes = "\n----- cierre -----\núltimo turno\n";
    const auto wr_close = tuide::registry_parse_causal_pilot_worker(
        R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"no_cubre",)"
        R"("why":"no llegue a leer el cuerpo del latch",)"
        R"("brief":"Se acabo el presupuesto. No pude explicar quien limpia busy."})",
        "como", "busy_strip", "", &nb_close);
    expect(wr_close.ok && wr_close.verdict == "no_cubre",
           "closing turn allows honest no_cubre without body");
    tuide::RegistryCausalPilotPlenary trap_plen;
    trap_plen.verdict = "entiendo";
    trap_plen.ok = true;
    trap_plen.keep = {"application"};
    trap_plen.why = "el plenario copio un como de trampa";
    std::vector<tuide::RegistryCausalPilotWorkerReport> mix(2);
    mix[0].ok = true;
    mix[0].kind = "cubre";
    mix[0].stem = "quit_confirm";
    mix[0].covers = true;
    mix[0].verdict = "cubre";
    mix[1].ok = true;
    mix[1].kind = "como";
    mix[1].stem = "application";
    mix[1].covers = true;
    mix[1].verdict = "chain";
    tuide::registry_tally_pilot_plenary(&trap_plen, mix);
    expect(trap_plen.keep.size() == 1 && trap_plen.keep[0] == "quit_confirm",
           "tally keep is only cubre not como chain");
    tuide::RegistryCausalPilotPlenary como_only;
    como_only.verdict = "entiendo";
    como_only.ok = true;
    como_only.keep = {"application"};
    como_only.why = "como chain no debe coronar";
    std::vector<tuide::RegistryCausalPilotWorkerReport> only_como(1);
    only_como[0].ok = true;
    only_como[0].kind = "como";
    only_como[0].stem = "application";
    only_como[0].covers = true;
    only_como[0].verdict = "chain";
    tuide::registry_tally_pilot_plenary(&como_only, only_como);
    expect(como_only.keep.empty() && como_only.verdict == "no_entiendo",
           "tally drops como-only keep");
  }

  // Pack-off path keeps edges without requiring skeleton slots.
  tuide::RegistryCausalJudgeOpts pack_off = judge_opts;
  pack_off.mechanism_pack = false;
  nlohmann::json pack_off_payload;
  expect(tuide::registry_causal_judge_payload(&r, "flag stuck", result, pack_off,
                                              &pack_off_payload, &err),
         "build pack-off judge payload");
  if (pack_off_payload.contains("zones") && !pack_off_payload["zones"].empty()) {
    const auto& z = pack_off_payload["zones"][0];
    expect(z.contains("edges") && !z["edges"].empty(), "pack-off still emits edges");
    expect(!z["pack_meta"].value("mechanism_pack", true), "pack-off meta flag");
  }

  tuide::RegistryCausalJudgeOpts knobs = judge_opts;
  expect(tuide::registry_causal_judge_opts_apply_json(
             &knobs, nlohmann::json{{"w_cos", 10.5}, {"port_cup", 1}, {"skel_state_cup", 1}},
             &err),
         "apply judge knobs json");
  expect(knobs.w_cos == 10.5f && knobs.port_cup == 1, "knobs overlay applied");
  expect(!tuide::registry_causal_judge_opts_apply_json(
             &knobs, nlohmann::json{{"w_cos", "nope"}}, &err),
         "reject bad knob type");
  auto result_with_uncovered = result;
  tuide::RegistryTrailHop uncovered_seed;
  uncovered_seed.node.id = "fn:src/polar.cpp:uncovered_probe";
  uncovered_seed.node.kind = "fn";
  uncovered_seed.node.path = "src/polar.cpp";
  uncovered_seed.node.symbol = "uncovered_probe";
  uncovered_seed.node.stem = "polar";
  uncovered_seed.cosine = 0.7f;
  result_with_uncovered.seeds.push_back(uncovered_seed);
  tuide::RegistryCausalJudgeOpts candidate_opts;
  candidate_opts.max_zones = 8;
  candidate_opts.promote_uncovered = true;
  nlohmann::json candidate_payload;
  expect(tuide::registry_causal_judge_payload(&r, "flag stuck", result_with_uncovered,
                                              candidate_opts, &candidate_payload, &err),
         "build uncovered candidate payload");
  bool promoted_uncovered = false;
  for (const auto& zone : candidate_payload["zones"]) {
    for (const auto& risk : zone.value("risks", nlohmann::json::array())) {
      promoted_uncovered =
          promoted_uncovered ||
          (risk.is_string() &&
           (risk.get<std::string>() == "uncovered_candidate" ||
            risk.get<std::string>() == "promoted_from_uncovered"));
    }
  }
  expect(promoted_uncovered, "uncovered query seed becomes bounded candidate zone");
  nlohmann::json bridge_payload = judge_payload;
  bridge_payload["zone_bridges"] = nlohmann::json::array({
      {{"trail", "T1"},
       {"zones", nlohmann::json::array({"M1", "M2"})},
       {"stems", nlohmann::json::array({"polar"})},
       {"why", "synthetic bridge"}}});
  tuide::RegistryCausalTriageDecision co_triage;
  co_triage.ok = true;
  co_triage.shortlist = {"M1"};
  co_triage.critical_mass = true;
  tuide::RegistryZoneTriage anchor_zone;
  anchor_zone.id = "M1";
  co_triage.zones.push_back(anchor_zone);
  tuide::registry_apply_deterministic_co_shortlist(bridge_payload, &co_triage);
  expect(co_triage.shortlist.size() >= 2, "co-shortlist adds bridge-linked zone");
  expect(std::find(co_triage.shortlist.begin(), co_triage.shortlist.end(), "M2") !=
             co_triage.shortlist.end(),
         "co-shortlist includes bridge zone");

  // Floor top-2: shortlist lleno sin M2 → eviction del último no-top2 (caso 02).
  nlohmann::json floor_payload = {
      {"zones",
       nlohmann::json::array(
           {{{"id", "M1"},
             {"primary_stems", nlohmann::json::array({"scroll_bar"})},
             {"context_stems", nlohmann::json::array()}},
            {{"id", "M2"},
             {"primary_stems", nlohmann::json::array({"main_layout"})},
             {"context_stems", nlohmann::json::array()}},
            {{"id", "M4"},
             {"primary_stems", nlohmann::json::array({"csv_viewer"})},
             {"context_stems", nlohmann::json::array()}},
            {{"id", "M6"},
             {"primary_stems", nlohmann::json::array({"csv_viewer"})},
             {"context_stems", nlohmann::json::array()}}})}};
  tuide::RegistryCausalTriageDecision floor_triage;
  floor_triage.ok = true;
  floor_triage.critical_mass = true;
  floor_triage.shortlist = {"M1", "M4", "M6"};
  for (const char* zid : {"M1", "M4", "M6"}) {
    tuide::RegistryZoneTriage z;
    z.id = zid;
    floor_triage.zones.push_back(z);
  }
  tuide::registry_apply_deterministic_co_shortlist(floor_payload, &floor_triage);
  expect(floor_triage.shortlist.size() == 3, "floor top-2 keeps cap 3");
  expect(std::find(floor_triage.shortlist.begin(), floor_triage.shortlist.end(), "M2") !=
             floor_triage.shortlist.end(),
         "floor top-2 force-includes registry runner-up");
  expect(std::find(floor_triage.shortlist.begin(), floor_triage.shortlist.end(), "M1") !=
             floor_triage.shortlist.end(),
         "floor top-2 keeps registry top");

  // Context overlap: M1 context ai_controller → añade M7 primary (casos 07/20).
  nlohmann::json ctx_payload = {
      {"zones",
       nlohmann::json::array(
           {{{"id", "M1"},
             {"primary_stems", nlohmann::json::array({"busy_strip"})},
             {"context_stems", nlohmann::json::array({"ai_controller"})}},
            {{"id", "M2"},
             {"primary_stems", nlohmann::json::array({"level2_session"})},
             {"context_stems", nlohmann::json::array()}},
            {{"id", "M7"},
             {"primary_stems", nlohmann::json::array({"ai_controller", "level2_autonomous_loop"})},
             {"context_stems", nlohmann::json::array()}}})}};
  tuide::RegistryCausalTriageDecision ctx_triage;
  ctx_triage.ok = true;
  ctx_triage.critical_mass = true;
  ctx_triage.shortlist = {"M1"};
  tuide::RegistryZoneTriage ctx_anchor;
  ctx_anchor.id = "M1";
  ctx_triage.zones.push_back(ctx_anchor);
  tuide::registry_apply_deterministic_co_shortlist(ctx_payload, &ctx_triage);
  expect(ctx_triage.shortlist.size() <= 3, "context complement respects cap 3");
  expect(std::find(ctx_triage.shortlist.begin(), ctx_triage.shortlist.end(), "M2") !=
             ctx_triage.shortlist.end(),
         "top-2 floor adds M2 alongside single-zone anchor");
  expect(std::find(ctx_triage.shortlist.begin(), ctx_triage.shortlist.end(), "M7") !=
             ctx_triage.shortlist.end(),
         "context overlap adds primary-owner of shortlist context stem");

  // Synth tie-break: M1 sin overlap / baja mass → M2 con editor_panel en hypothesis.
  nlohmann::json synth_payload = {
      {"zones",
       nlohmann::json::array(
           {{{"id", "M1"},
             {"primary_stems", nlohmann::json::array({"status_language_popover"})},
             {"mass_coverage", 0.002f}},
            {{"id", "M2"},
             {"primary_stems", nlohmann::json::array({"editor_panel"})},
             {"mass_coverage", 0.09f}}})}};
  tuide::RegistryCausalJudgeDecision synth_decision;
  synth_decision.ok = true;
  synth_decision.selected = {"M1"};
  tuide::RegistryZoneVerdict synth_m1;
  synth_m1.id = "M1";
  synth_m1.verdict = "select";
  synth_m1.role = "primary";
  synth_m1.completeness = "complete";
  synth_m1.confidence = 0.8f;
  synth_m1.why = "elige popover por error";
  synth_decision.zones.push_back(synth_m1);
  tuide::RegistryZoneVerdict synth_m2;
  synth_m2.id = "M2";
  synth_m2.verdict = "reject";
  synth_m2.role = "none";
  synth_m2.completeness = "none";
  synth_m2.why = "descartada incorrectamente";
  synth_decision.zones.push_back(synth_m2);
  tuide::registry_apply_synth_hypothesis_tiebreak(
      synth_payload, "El estado del editor maneja eventos de mouse", "", &synth_decision);
  expect(synth_decision.selected == std::vector<std::string>({"M2"}),
         "synth tie-break prefers hypothesis stem overlap");

  // v2: symbolish token matches representative → prefer that zone over high-mass distractor.
  nlohmann::json rep_payload = {
      {"zones",
       nlohmann::json::array(
           {{{"id", "M1"},
             {"primary_stems", nlohmann::json::array({"settings_modal"})},
             {"mass_coverage", 0.16f},
             {"representatives",
              nlohmann::json::array(
                  {{{"target", "src/ui/settings_modal.cpp:handle_shortcuts_settings_keys"}}})}},
            {{"id", "M2"},
             {"primary_stems", nlohmann::json::array({"level1_agent"})},
             {"mass_coverage", 0.29f},
             {"representatives",
              nlohmann::json::array(
                  {{{"target", "src/ai/level1_agent.cpp:propose_investigate_needles"}}})}}})}};
  tuide::RegistryCausalJudgeDecision rep_decision;
  rep_decision.ok = true;
  rep_decision.selected = {"M2"};
  tuide::RegistryZoneVerdict rep_m2;
  rep_m2.id = "M2";
  rep_m2.verdict = "select";
  rep_m2.why = "distractora de mayor mass";
  rep_decision.zones.push_back(rep_m2);
  tuide::registry_apply_synth_hypothesis_tiebreak(
      rep_payload, "La funcion handle_shortcuts_settings_keys maneja los atajos", "",
      &rep_decision);
  expect(rep_decision.selected == std::vector<std::string>({"M1"}),
         "tie-break v2 prefers symbolish↔representative match");

  // v2: equal overlap → prefer lower mass (specificity).
  nlohmann::json mass_payload = {
      {"zones",
       nlohmann::json::array(
           {{{"id", "M1"},
             {"primary_stems", nlohmann::json::array({"quit_confirm"})},
             {"mass_coverage", 0.003f},
             {"representatives", nlohmann::json::array()}},
            {{"id", "M4"},
             {"primary_stems", nlohmann::json::array({"open_file_confirm"})},
             {"mass_coverage", 0.05f},
             {"representatives", nlohmann::json::array()}}})}};
  tuide::RegistryCausalJudgeDecision mass_decision;
  mass_decision.ok = true;
  mass_decision.selected = {"M4"};
  tuide::RegistryZoneVerdict mass_m4;
  mass_m4.id = "M4";
  mass_m4.verdict = "select";
  mass_m4.why = "distractora abierta por mass";
  mass_decision.zones.push_back(mass_m4);
  tuide::registry_apply_synth_hypothesis_tiebreak(
      mass_payload, "confirmar archivos sin guardar antes de salir", "", &mass_decision);
  expect(mass_decision.selected == std::vector<std::string>({"M1"}),
         "tie-break v2 prefers lower mass on equal overlap");

  // v2: negation cue degrades matched representative (post-falsify).
  nlohmann::json neg_payload = {
      {"zones",
       nlohmann::json::array(
           {{{"id", "M4"},
             {"primary_stems", nlohmann::json::array({"application"})},
             {"mass_coverage", 0.002f},
             {"representatives",
              nlohmann::json::array(
                  {{{"target", "src/app/application.cpp:restore_workspace_session"}}})}},
            {{"id", "M8"},
             {"primary_stems", nlohmann::json::array({"workspace_config", "app_settings"})},
             {"mass_coverage", 0.0008f},
             {"representatives", nlohmann::json::array()}},
            {{"id", "M2"},
             {"primary_stems", nlohmann::json::array({"level2_session"})},
             {"mass_coverage", 0.12f},
             {"representatives", nlohmann::json::array()}}})}};
  tuide::RegistryCausalJudgeDecision neg_decision;
  neg_decision.ok = true;
  neg_decision.selected = {"M2"};
  tuide::RegistryZoneVerdict neg_m2;
  neg_m2.id = "M2";
  neg_m2.verdict = "select";
  neg_m2.why = "conservada tras reopen vacio";
  neg_decision.zones.push_back(neg_m2);
  tuide::registry_apply_synth_hypothesis_tiebreak(
      neg_payload,
      "La funcion restore_workspace_session no maneja el cursor y los paneles laterales", "",
      &neg_decision);
  expect(neg_decision.selected == std::vector<std::string>({"M8"}),
         "tie-break v2 negation prefers specific gold over falsified symbol zone");

  // Primary survey parse + thread selection (legacy path still used by tests).
  {
    const std::string survey_raw =
        R"({"action":"causal_zone_primary_survey_v1","zones":[)"
        R"({"id":"M1","discard":false,"confidence":0.8,"hypothesis":"run_compile lanza la compilacion desde level2_session","supporting":[{"id":"M7","role":"state_owner"}],"expand_from":["src/ai/level2_session.cpp:run_compile"]},)"
        R"({"id":"M7","discard":false,"confidence":0.65,"hypothesis":"ai_controller posee el estado de tarea y dispara el compile","supporting":[{"id":"M1","role":"trigger"}],"expand_from":["src/app/application.cpp:fail_debug_launch"]},)"
        R"({"id":"M2","discard":true,"discard_reason":"remap de compile commands no explica el sintoma","confidence":0.1}]})";
    const std::unordered_map<std::string, std::vector<std::string>> survey_targets{
        {"M1", {"src/ai/level2_session.cpp:run_compile"}},
        {"M2", {"src/util/compile_commands_remap.cpp:list_running_docker_containers"}},
        {"M7", {"src/app/application.cpp:fail_debug_launch"}},
    };
    const auto survey = tuide::registry_parse_causal_primary_survey(
        survey_raw, {"M1", "M2", "M7"}, survey_targets);
    expect(survey.ok && survey.entries.size() == 3, "primary survey parses three zones");
    nlohmann::json survey_payload = {
        {"zones", nlohmann::json::array({{{"id", "M1"}}, {{"id", "M2"}}, {{"id", "M7"}}})}};
    auto threads =
        tuide::registry_primary_survey_select_threads(survey, survey_payload, survey_targets, 2);
    expect(threads.size() == 2, "primary survey selects top-2 threads");
    expect(threads[0].shortlist.front() == "M1", "highest confidence primary first");
    expect(threads[1].shortlist.front() == "M7", "second thread is competing primary");
  }

  // Must-compete + contrast validate/inject (caso 07).
  {
    nlohmann::json payload07 = {
        {"zones",
         nlohmann::json::array(
             {{{"id", "M1"},
               {"primary_stems", nlohmann::json::array({"level2_session"})},
               {"context_stems", nlohmann::json::array({"raw_pty_screen", "ai_controller"})},
               {"representatives",
                nlohmann::json::array(
                    {{{"target", "src/ai/level2_session.cpp:run_compile"}}})}},
              {{"id", "M2"},
               {"primary_stems", nlohmann::json::array({"compile_commands_remap"})},
               {"context_stems", nlohmann::json::array()},
               {"representatives", nlohmann::json::array()}},
              {{"id", "M7"},
               {"primary_stems",
                nlohmann::json::array({"ui_panel_render_cache", "ai_controller", "application"})},
               {"context_stems", nlohmann::json::array()},
               {"representatives",
                nlohmann::json::array(
                    {{{"target", "src/app/application.cpp:fail_debug_launch"}}})}}})},
        {"zone_bridges", nlohmann::json::array()}};
    const auto must = tuide::registry_collect_must_compete_zone_ids(payload07, 2);
    expect(std::find(must.begin(), must.end(), "M7") != must.end(),
           "must-compete detects M7 via context∩primary ai_controller");
    expect(std::find(must.begin(), must.end(), "M1") == must.end(),
           "must-compete excludes top-1");

    const auto queue = tuide::registry_collect_slot_queue_zone_ids(payload07, 8);
    expect(!queue.empty() && queue.front() == "M1", "slot queue starts with top-1");
    expect(std::find(queue.begin(), queue.end(), "M7") != queue.end(),
           "slot queue includes must-compete M7");

    tuide::RegistrySlotHypothesis slot_m7;
    slot_m7.ok = true;
    slot_m7.primary = "M7";
    slot_m7.confidence = 0.7f;
    slot_m7.hypothesis =
        "ai_controller posee el estado de tarea y dispara la compilacion del proyecto";
    slot_m7.expand_from = {"src/app/application.cpp:fail_debug_launch"};
    tuide::RegistrySlotHypothesis slot_m1;
    slot_m1.ok = true;
    slot_m1.primary = "M1";
    slot_m1.confidence = 0.8f;
    slot_m1.hypothesis = "run_compile lanza la compilacion desde level2_session";
    slot_m1.expand_from = {"src/ai/level2_session.cpp:run_compile"};
    auto retained =
        tuide::registry_slot_retain_hypotheses({slot_m1, slot_m7}, payload07, 3);
    expect(retained.size() == 2, "slot retain keeps two diverse primaries");
    expect(tuide::registry_slot_gold_in_hypotheses(retained, payload07, {"ai_controller"}),
           "gold_in_hypotheses true when M7 retained with ai_controller stem");

    const std::unordered_map<std::string, std::vector<std::string>> targets07{
        {"M1", {"src/ai/level2_session.cpp:run_compile"}},
        {"M2", {"src/util/x.cpp:y"}},
        {"M7", {"src/app/application.cpp:fail_debug_launch"}},
    };

    // Incomplete: discard M7 without expand_from
    tuide::RegistryContrastDecision bad;
    bad.ok = true;
    bad.threads.push_back(
        {"M1", "La compilacion se lanza en run_compile de level2_session", 0.7f, {}, {}, false});
    bad.discards.push_back({"M7", "no se relaciona", {}});
    auto vbad = tuide::registry_validate_contrast_threads(bad, must, payload07, targets07);
    expect(!vbad.ok && vbad.error == "incomplete_contrast",
           "validate rejects must-compete discard without targets");

    // Duplicate hyp
    tuide::RegistryContrastDecision dup;
    dup.ok = true;
    dup.threads.push_back(
        {"M1", "run_compile lanza la compilacion del proyecto", 0.7f, {}, {}, false});
    dup.threads.push_back(
        {"M7", "run_compile lanza la compilacion del proyecto otra vez", 0.6f, {}, {}, false});
    auto vdup = tuide::registry_validate_contrast_threads(dup, must, payload07, targets07);
    expect(!vdup.ok && vdup.error == "duplicate_hypothesis",
           "validate rejects duplicated run_compile hypotheses");

    // single_viable OK when must empty
    tuide::RegistryContrastDecision single;
    single.ok = true;
    single.single_viable = true;
    single.threads.push_back(
        {"M1", "solo un mecanismo viable en este mazo pobre", 0.6f, {}, {}, false});
    auto vsingle =
        tuide::registry_validate_contrast_threads(single, {}, payload07, targets07);
    expect(vsingle.ok, "validate accepts single_viable without must-compete");

    // Inject synthetic M7 when only one thread
    tuide::RegistryContrastDecision inject = bad;
    tuide::registry_inject_synthetic_contrast_threads(&inject, must, payload07, targets07);
    expect(inject.injected, "inject marks synthetic");
    expect(std::any_of(inject.threads.begin(), inject.threads.end(),
                       [](const tuide::RegistryContrastThread& th) {
                         return th.primary == "M7" && th.synthetic;
                       }),
           "inject adds synthetic M7 thread");
    auto threads =
        tuide::registry_contrast_select_threads(inject, payload07, targets07, 2);
    expect(threads.size() == 2, "contrast select_threads returns two after inject");
    expect(std::any_of(threads.begin(), threads.end(),
                       [](const tuide::RegistryCausalTriageDecision& t) {
                         return !t.shortlist.empty() && t.shortlist.front() == "M7";
                       }),
           "select_threads includes M7 primary");

    // Inject replaces weakest non-must when already 2 threads (M1+M5, no M7)
    tuide::RegistryContrastDecision full;
    full.ok = true;
    full.threads.push_back(
        {"M1", "La compilacion se lanza en run_compile de level2_session", 0.8f, {}, {}, false});
    full.threads.push_back(
        {"M5", "copia debil del ancla en otra zona sin mecanismo propio", 0.4f, {}, {}, false});
    tuide::registry_inject_synthetic_contrast_threads(&full, must, payload07, targets07);
    expect(full.injected && full.threads.size() == 2, "inject keeps cap 2");
    expect(std::any_of(full.threads.begin(), full.threads.end(),
                       [](const tuide::RegistryContrastThread& th) {
                         return th.primary == "M7" && th.synthetic;
                       }),
           "inject replaces M5 with synthetic M7");
    expect(std::any_of(full.threads.begin(), full.threads.end(),
                       [](const tuide::RegistryContrastThread& th) { return th.primary == "M1"; }),
           "inject keeps stronger M1 thread");

    const std::string stripped = tuide::registry_strip_zone_scores_markdown(
        "## M1 score=0.91 margin=0.1 coverage=0.1\nstems: level2_session\n");
    expect(stripped.find("score=") == std::string::npos && stripped.find("## M1\n") != std::string::npos,
           "strip_zone_scores removes score line noise");
  }

  const std::string triage_md = tuide::registry_causal_triage_markdown(judge_payload);
  expect(triage_md.find("causal_zone_triage_v1") != std::string::npos,
         "causal triage markdown");
  if (!judge_payload["zones"].empty() &&
      !judge_payload["zones"][0]["representatives"].empty()) {
    const std::string triage_zone = judge_payload["zones"][0].value("id", "");
    const std::string triage_target =
        judge_payload["zones"][0]["representatives"][0].value("target", "");
    const std::unordered_map<std::string, std::vector<std::string>> allowed_targets{
        {triage_zone, {triage_target}}};
    const std::string triage_raw =
        std::string("{\"action\":\"causal_zone_triage_v1\",\"zones\":{\"") +
        triage_zone +
        "\":{\"verdict\":\"inspect\",\"need\":\"comprobar writer y reader\","
        "\"expand_from\":[\"" +
        triage_target +
        "\"]}},\"shortlist\":[\"" + triage_zone +
        "\"],\"retrieval_needed\":false,\"why\":\"zona con estado compartido\"}";
    const auto triage = tuide::registry_parse_causal_triage_decision(
        triage_raw, {triage_zone}, allowed_targets);
    expect(triage.ok && triage.shortlist == std::vector<std::string>({triage_zone}),
           "causal triage parses strict decision");
    const auto rehomed = tuide::registry_parse_causal_triage_decision(
        R"({"action":"causal_zone_triage_v1","inspect":[{"id":"M1",)"
        R"("need":"seguir el writer correcto","expand_from":["src/right.cpp:writer"]}],)"
        R"("retrieval_needed":false,"why":"el target identifica su zona"})",
        {"M1", "M2"}, {{"M1", {"src/left.cpp:reader"}}, {"M2", {"src/right.cpp:writer"}}});
    expect(rehomed.ok && rehomed.shortlist == std::vector<std::string>({"M2"}),
           "causal triage canonical targets repair a misplaced zone id");
    const auto anchor = tuide::registry_parse_causal_anchor_decision(
        R"({"action":"causal_zone_anchor_v1","anchors":[{"id":"M2","role_guess":"state_owner",)"
        R"("explains":"posee el flag compartido","does_not_explain":"falta el trigger de escritura",)"
        R"("expand_from":["src/right.cpp:writer"],"thread":"seguir writer hacia el latch"}],)"
        R"("hypothesis":"el writer no limpia el flag al cancelar","critical_mass":true,)"
        R"("retrieval_needed":false,"why":"M2 concentra estado y writer del síntoma"})",
        {"M1", "M2"}, {{"M1", {"src/left.cpp:reader"}}, {"M2", {"src/right.cpp:writer"}}});
    expect(anchor.ok && anchor.shortlist == std::vector<std::string>({"M2"}) &&
               anchor.critical_mass && !anchor.hypothesis.empty(),
           "causal anchor parses epistemic decision");
    const auto too_many = tuide::registry_parse_causal_anchor_decision(
        R"({"action":"causal_zone_anchor_v1","anchors":[)"
        R"({"id":"M1","role_guess":"trigger","explains":"dispara el flujo A","does_not_explain":"no limpia",)"
        R"("expand_from":["src/left.cpp:reader"],"thread":"seguir reader"},)"
        R"({"id":"M2","role_guess":"cleanup","explains":"limpia el flag","does_not_explain":"no dispara",)"
        R"("expand_from":["src/right.cpp:writer"],"thread":"seguir writer"},)"
        R"({"id":"M3","role_guess":"consumer","explains":"consume el flag X","does_not_explain":"no escribe",)"
        R"("expand_from":["src/left.cpp:reader"],"thread":"seguir consumer"}],)"
        R"("hypothesis":"tres anclas no deben pasar","critical_mass":true,)"
        R"("retrieval_needed":false,"why":"demasiadas anclas apiladas"})",
        {"M1", "M2", "M3"},
        {{"M1", {"src/left.cpp:reader"}},
         {"M2", {"src/right.cpp:writer"}},
         {"M3", {"src/left.cpp:reader"}}});
    expect(!too_many.ok, "causal anchor rejects more than two anchors");
    const auto truncated = tuide::registry_parse_causal_anchor_decision(
        std::string("```json\n{\"action\":\"causal_zone_anchor_v1\",\"anchors\":[") +
            "{\"id\":\"M2\",\"role_guess\":\"state_owner\","
            "\"explains\":\"posee el flag compartido\","
            "\"does_not_explain\":\"falta el trigger de escritura\","
            "\"expand_from\":[\"src/right.cpp:writer\"],"
            "\"thread\":\"seguir writer hacia el latch\"}],"
            "\"hypothesis\":\"el writer no limpia el flag al cancelar\","
            "\"critical_mass\":true,\"retrieval_needed\":false,\"why\":\"La M2 gestiona el esta",
        {"M1", "M2"}, {{"M1", {"src/left.cpp:reader"}}, {"M2", {"src/right.cpp:writer"}}});
    expect(truncated.ok && truncated.shortlist == std::vector<std::string>({"M2"}),
           "causal anchor salvages truncated fenced JSON");
    tuide::RegistryCausalJudgeOpts expanded_opts;
    expanded_opts.max_representatives = 10;
    expanded_opts.max_edges = 24;
    expanded_opts.expand_hops = 1;
    nlohmann::json expanded_payload;
    expect(tuide::registry_expand_causal_judge_payload(
               &r, judge_payload, triage, expanded_opts, &expanded_payload, &err),
           "causal triage expands selected zone");
    expect(expanded_payload.value("schema", "") == "causal_judge_v1_expanded" &&
               expanded_payload["zones"].size() == 1,
           "expanded payload keeps shortlist");
  }
  const std::string judge_cards =
      "# causal_judge_v1\n## M1 score=1\n## M2 score=.5\n";
  const auto judge_ids = tuide::registry_causal_judge_zone_ids(judge_cards);
  expect(judge_ids == std::vector<std::string>({"M1", "M2"}),
         "causal judge extracts zone ids");
  const auto decision = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","zones":[)"
      R"({"id":"M1","verdict":"select","role":"primary","completeness":"complete",)"
      R"("confidence":0.9,"why":"flag conecta writer y reader","contribution":"control del flag",)"
      R"("missing_link":"","expand_from":[]},)"
      R"({"id":"M2","verdict":"reject","role":"none","completeness":"none",)"
      R"("confidence":0.8,"why":"otro flujo sin flag","contribution":"","missing_link":"",)"
      R"("expand_from":[]})"
      R"(],"selected":["M1"],"next":"verify","why":"M1 cubre el flujo"})",
      judge_ids);
  expect(decision.ok && decision.selected == std::vector<std::string>({"M1"}),
         "causal judge parses strict decision");
  const auto incomplete = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","zones":[)"
      R"({"id":"M1","verdict":"select","role":"primary","completeness":"complete",)"
      R"("confidence":0.9,"why":"mecanismo del flag","contribution":"control del flag",)"
      R"("missing_link":"","expand_from":[]})"
      R"(],"selected":["M1"],"next":"verify","why":"incompleto"})",
      judge_ids);
  expect(!incomplete.ok, "causal judge rejects missing zone verdict");
  const auto multi_zone = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","zones":[)"
      R"({"id":"M1","verdict":"select","role":"primary","completeness":"complete",)"
      R"("confidence":0.9,"why":"writer controla flag","contribution":"estado principal",)"
      R"("missing_link":"","expand_from":[]},)"
      R"({"id":"M2","verdict":"select","role":"cleanup","completeness":"partial",)"
      R"("confidence":0.7,"why":"cleanup toca flag","contribution":"limpieza al cancelar",)"
      R"("missing_link":"falta llamada desde cancel","expand_from":["box::cancel"]})"
      R"(],"selected":["M1","M2"],"next":"expand","why":"dos piezas causales distintas"})",
      judge_ids);
  expect(multi_zone.ok && multi_zone.selected.size() == 2,
         "causal judge accepts complementary selected zones");
  const auto compact = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","zones":["M1","M2"],"selected":["M1"],)"
      R"("next":"verify","why":{"M1":{"verdict":"select","role":"primary",)"
      R"("completeness":"complete","confidence":0.9,"contribution":"control del flag"},)"
      R"("M2":{"verdict":"reject","confidence":0.8,"why":"otro flujo sin flag"}}})",
      judge_ids);
  expect(compact.ok && compact.selected == std::vector<std::string>({"M1"}),
         "causal judge accepts compact indexed decision");
  const auto scalar_compact = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","zones":{"M1":"select"},)"
      R"("selected":["M1"],"next":"verify",)"
      R"("why":"M1 contiene el punto de entrada causal solicitado"})",
      {"M1"});
  expect(scalar_compact.ok && scalar_compact.selected == std::vector<std::string>({"M1"}),
         "causal judge accepts scalar compact verdict");
  const auto promoted = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","zones":{"M1":{"verdict":"select",)"
      R"("role":"trigger","completeness":"complete","confidence":0.9,)"
      R"("why":"controla el flag","contribution":"control del flag"},)"
      R"("M2":{"verdict":"reject","confidence":0.8,"why":"otro flujo sin flag"}},)"
      R"("selected":["M1"],"next":"verify","why":"M1 cubre el flujo"})",
      judge_ids);
  expect(promoted.ok && promoted.zones.front().role == "primary",
         "causal judge promotes sole selection to primary");
  const auto falsified = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","hypothesis_status":"falsified",)"
      R"("reinvestigate_need":"buscar el cleanup que cancela el flag stuck",)"
      R"("zones":{"M1":{"verdict":"reject","confidence":0.8,"why":"otro flujo sin cleanup"},)"
      R"("M2":{"verdict":"reject","confidence":0.7,"why":"solo infraestructura genérica"}},)"
      R"("selected":[],"next":"reinvestigate","why":"la hipótesis del writer no explica el cancel"})",
      judge_ids);
  expect(falsified.ok && falsified.next == "reinvestigate" &&
             falsified.hypothesis_status == "falsified",
         "causal synth accepts falsified reinvestigate");
  const auto sloppy = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","hypothesis_status":"confirmed",)"
      R"("reinvestigate_need":"","zones":{"M1":{"select":"box::writer","role":"mutator",)"
      R"("completeness":"complete","confidence":"high",)"
      R"("why":"writer controla el flag stuck en el núcleo"},)"
      R"("M2":{"verdict":"reject","confidence":0.7,"why":"otro flujo sin el flag"}},)"
      R"("selected":["M1"],"next":"verify","why":"síntesis"})",
      judge_ids);
  expect(sloppy.ok && sloppy.selected == std::vector<std::string>({"M1"}) &&
             sloppy.zones.front().role == "primary",
         "causal synth tolerates sloppy 7B confidence/role/select fields");
  const auto placeholder = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","hypothesis_status":"confirmed",)"
      R"("zones":{"M1":{"verdict":"select","role":"primary","completeness":"complete",)"
      R"("confidence":0.8,"why":"evidencia concreta","contribution":"aporte"},)"
      R"("M2":{"verdict":"reject","confidence":0.7,"why":"otro flujo sin el flag"}},)"
      R"("selected":["M1"],"next":"verify","why":"síntesis con evidencia"})",
      judge_ids);
  expect(!placeholder.ok, "causal synth rejects copied prompt placeholders");
  const auto partial_no_expand = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","hypothesis_status":"partial",)"
      R"("zones":{"M1":{"verdict":"select","role":"primary","completeness":"partial",)"
      R"("confidence":0.75,"why":"M1 escribe el flag stuck vía writer","contribution":"control del flag"},)"
      R"("M2":{"verdict":"reject","confidence":0.7,"why":"otro flujo sin el flag"}},)"
      R"("selected":["M1"],"next":"verify","why":"M1 cubre el mecanismo pedido"})",
      judge_ids);
  expect(partial_no_expand.ok && partial_no_expand.zones.front().completeness == "complete" &&
             partial_no_expand.next == "verify",
         "causal synth coerces partial without expand_from to complete");
  const auto veredict_typo = tuide::registry_parse_causal_judge_decision(
      R"({"action":"causal_zone_judge","hypothesis_status":"partial",)"
      R"("reinvestigate_need":"falta el builder del contenido de la pestaña about",)"
      R"("zones":{"M1":{"veredict":"reject","confidence":0.9,)"
      R"("why":"append_tabs solo crea el header sin contenido"},)"
      R"("M2":{"verdict":"reject","confidence":0.7,"why":"otro flujo sin settings"}},)"
      R"("selected":[],"next":"expand","why":"el header no cubre el contenido about"})",
      judge_ids);
  expect(veredict_typo.ok && veredict_typo.next == "reinvestigate" &&
             veredict_typo.hypothesis_status == "falsified",
         "causal synth maps veredict typo and empty partial to reinvestigate");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_large_inventory_keeps_linked_fn() {
  const std::string tmp = make_tmp();
  std::string body;
  for (int i = 0; i < 70; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "void fn_%02d() {}\n", i);
    body += buf;
  }
  write_file(tmp + "/src/big.cpp", body);

  tuide::EffectSlice sl;
  sl.inventory_paths.insert("src/big.cpp");
  for (int i = 0; i < 70; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "fn_%02d", i);
    sl.nodes.push_back(mk_fn("src/big.cpp", name, i == 0));
    sl.nodes.back().line = i + 1;
  }
  tuide::EffectNode ctrl;
  ctrl.id = "ctrl:src/big.cpp:70:if";
  ctrl.kind = tuide::EffectNodeKind::Ctrl;
  ctrl.ctrl_kind = tuide::EffectCtrlKind::If;
  ctrl.path = "src/big.cpp";
  ctrl.line = 70;
  ctrl.parent_fn = "fn:src/big.cpp:fn_69";
  ctrl.cond = "!ready_.load()";
  sl.nodes.push_back(ctrl);

  tuide::EffectNode orphan;
  orphan.id = "ctrl:src/big.cpp:99:if";
  orphan.kind = tuide::EffectNodeKind::Ctrl;
  orphan.ctrl_kind = tuide::EffectCtrlKind::If;
  orphan.path = "src/big.cpp";
  orphan.line = 99;
  orphan.parent_fn = "fn:src/big.cpp:missing_parent";
  orphan.cond = "orphan";
  sl.nodes.push_back(orphan);

  tuide::EffectRegistry r;
  std::string err;
  expect(tuide::registry_open(tmp, &r, &err), "open big");
  expect(ingest(&r, sl, false, "big"), "ingest big");
  tuide::RegistryNodeRow row;
  expect(tuide::registry_get(&r, "fn:src/big.cpp:fn_00", &row, &err), "seed fn_00");
  expect(tuide::registry_get(&r, "fn:src/big.cpp:fn_69", &row, &err), "linked parent fn_69");
  expect(tuide::registry_get(&r, "ctrl:src/big.cpp:70:if", &row, &err), "ctrl of fn_69");
  expect(!tuide::registry_get(&r, "ctrl:src/big.cpp:99:if", &row, &err), "orphan ctrl dropped");
  std::vector<std::string> pending;
  expect(tuide::registry_pending_files(&r, &pending, &err), "pending list");
  expect(!pending.empty(), "big.cpp stays pending");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_query_hop0_skips_ctrl() {
  const std::string tmp = make_tmp();
  write_file(tmp + "/src/lsp.cpp",
             "void restart_lsp() { int restart = 1; (void)restart; }\n"
             "void other() {}\n");
  tuide::EffectSlice sl;
  sl.nodes.push_back(mk_fn("src/lsp.cpp", "restart_lsp", true));
  tuide::EffectNode ctrl;
  ctrl.id = "ctrl:src/lsp.cpp:1:if";
  ctrl.kind = tuide::EffectNodeKind::Ctrl;
  ctrl.ctrl_kind = tuide::EffectCtrlKind::If;
  ctrl.path = "src/lsp.cpp";
  ctrl.line = 1;
  ctrl.parent_fn = "fn:src/lsp.cpp:restart_lsp";
  ctrl.cond = "language server process crash restart detect";
  sl.nodes.push_back(ctrl);
  tuide::EffectFact contains;
  contains.from = "fn:src/lsp.cpp:restart_lsp";
  contains.to = ctrl.id;
  contains.kind = tuide::EffectFactKind::Contains;
  sl.facts.push_back(contains);

  tuide::EffectRegistry r;
  std::string err;
  expect(tuide::registry_open(tmp, &r, &err), "open qctrl");
  expect(ingest(&r, sl, false, "qctrl"), "ingest qctrl");
  tuide::RegistryEmbedOpts eopts;
  eopts.skip_glue = false;
  eopts.model = "test-bag";
  tuide::RegistryEmbedReport rep;
  expect(tuide::registry_embed_nodes(&r, fake_embed, {}, eopts, &rep, &err), "embed qctrl");
  tuide::RegistryQueryOpts qopts;
  qopts.model = "test-bag";
  qopts.top_k = 4;
  qopts.hops = 1;
  tuide::RegistryQueryResult qr;
  expect(tuide::registry_query(&r, "language server process crash restart", fake_embed, qopts,
                              &qr, &err),
         "query qctrl");
  expect(!qr.hits.empty(), "qctrl hits");
  for (const auto& h : qr.hits) {
    expect(h.node.kind != "ctrl", "hop0 is not ctrl");
  }
  expect(qr.hits.front().node.symbol == "restart_lsp", "fn ranks hop0");
  bool saw_ctrl_hop = false;
  for (const auto& h : qr.expanded) {
    if (h.node.kind == "ctrl") {
      saw_ctrl_hop = true;
    }
  }
  expect(saw_ctrl_hop, "hops still reach ctrl");
  tuide::registry_close(&r);
  fs::remove_all(tmp);
}

void test_pilot_worker_read_gate_a() {
  const std::string root =
      (fs::path(tide_root()) / "tests" / "fixtures" / "pilot_worker_read").string();
  expect(fs::exists(fs::path(root) / "src" / "latch.cpp"), "latch fixture exists");
  expect(fs::exists(fs::path(root) / "src" / "chrome.cpp"), "chrome fixture exists");

  tuide::RegistryCausalPilotWorkerNotebook nb;
  nlohmann::json zone = {
      {"id", "L1"},
      {"owns", "latch"},
      {"primary_stems", nlohmann::json::array({"latch"})},
      {"representatives",
       nlohmann::json::array({nlohmann::json{{"target", "src/latch.cpp:start_busy"}}})}};
  tuide::registry_causal_pilot_notebook_from_payload(nlohmann::json{{"zones", {zone}}}, "latch",
                                                     &nb);

  tuide::RegistryCausalPilotWorkerNotebook nb_hops = nb;
  nb_hops.n_follow = 1;
  nb_hops.notes =
      "\n----- follow src/latch.cpp:start_busy outgoing -----\nhop to tick\n";
  const auto hops_only = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/latch.cpp:start_busy",)"
      R"("chain":"start_busy dispara tick por una arista de ficha",)"
      R"("why":"start_busy es el acto segun los hops",)"
      R"("brief":"Las aristas dicen que start_busy cubre el latch. No hace falta leer el cuerpo."})",
      "como", "latch", "", &nb_hops);
  expect(!hops_only.ok && hops_only.error.find("sin lectura") != std::string::npos,
         "follow hops are not evidence");

  const auto ask_nc = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_need_code","target":"src/latch.cpp:start_busy"})", "como",
      "latch", "", &nb);
  expect(ask_nc.ok && ask_nc.tool == "need_code", "gate A need_code is accepted");

  std::string latch_src;
  {
    std::ifstream in(fs::path(root) / "src" / "latch.cpp");
    std::string line;
    while (std::getline(in, line)) {
      latch_src += line;
      latch_src += '\n';
    }
  }
  expect(latch_src.find("start_busy") != std::string::npos, "latch source has start_busy");
  nb.n_need_code = 1;
  nb.notes += "\n----- need_code src/latch.cpp:start_busy -----\n" + latch_src;

  const auto ask_nc_again = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_need_code","target":"src/latch.cpp:start_busy"})", "cubre",
      "latch", "", &nb);
  expect(!ask_nc_again.ok && ask_nc_again.error.find("repetido") != std::string::npos,
         "repeat need_code is rejected");
  const auto ask_nc_tail = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_need_code","target":"src/latch.cpp:start_busy#tail"})", "cubre",
      "latch", "", &nb);
  expect(ask_nc_tail.ok && ask_nc_tail.tool == "need_code", "windowed need_code is a refetch");
  const auto cubre_md = tuide::registry_causal_pilot_worker_user_prompt(
      "cubre", "q", "# ficha\n", nb.notes, "", false);
  expect(cubre_md.find("PROHIBIDO repetir need_code") != std::string::npos,
         "cubre with body forbids repeat need_code");

  const auto ask_df = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_dataflow","target":"busy","path":"src/latch.cpp"})", "como",
      "latch", "", &nb);
  expect(ask_df.ok && ask_df.tool == "dataflow" && ask_df.target == "busy",
         "gate A dataflow is accepted");

  auto search = [&](const std::string& name) {
    std::vector<tuide::ATrailSearchHit> hits;
    for (const char* rel : {"src/latch.cpp", "src/chrome.cpp"}) {
      std::ifstream f(fs::path(root) / rel);
      std::string line;
      int n = 0;
      while (std::getline(f, line)) {
        ++n;
        if (line.find(name) == std::string::npos) {
          continue;
        }
        tuide::ATrailSearchHit h;
        h.path = rel;
        h.line = n;
        h.preview = line;
        hits.push_back(h);
      }
    }
    return hits;
  };
  const auto df = tuide::a_dataflow_build_with_search(root, "busy", "src/latch.cpp", search);
  const auto md = tuide::a_dataflow_markdown(df);
  expect(!df.writes.empty(), "busy has writes in latch");
  expect(md.find("src/latch.cpp") != std::string::npos, "dataflow gold is latch source");
  bool write_true = false;
  bool write_false = false;
  bool chrome_write = false;
  for (const auto& w : df.writes) {
    if (w.path.find("chrome") != std::string::npos) {
      chrome_write = true;
    }
    if (w.preview.find("= true") != std::string::npos) {
      write_true = true;
    }
    if (w.preview.find("= false") != std::string::npos) {
      write_false = true;
    }
  }
  expect(write_true && write_false && !chrome_write, "busy writers are start/halt not hover");
  for (const auto& w : df.writes) {
    fs::path abs = fs::path(root) / w.path;
    const auto hop =
        tuide::a_trail_enrich_hop(abs.lexically_normal().string(), w.path, w.line, "");
    if (!hop.symbol.empty()) {
      tuide::registry_causal_pilot_notebook_add_target(&nb, w.path + ":" + hop.symbol);
    }
  }
  expect(tuide::registry_causal_pilot_target_in_notebook("src/latch.cpp:halt_busy", nb),
         "dataflow names halt_busy into the notebook");

  nb.n_dataflow = 1;
  nb.notes += "\n----- dataflow busy -----\n" + md;

  const auto ask_df_again = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_dataflow","target":"busy"})", "como", "latch", "", &nb);
  expect(!ask_df_again.ok && ask_df_again.error.find("repetido") != std::string::npos,
         "repeat dataflow is rejected");

  nb.n_need_code = 2;
  nb.notes += "\n----- need_code src/latch.cpp:halt_busy -----\n"
              "void halt_busy(Session* s) { if (s == nullptr) { return; } s->busy = false; }\n";

  const auto canned = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/latch.cpp:start_busy",)"
      R"("chain":"start_busy escribe busy; halt_busy lo limpia",)"
      R"("why":"start_busy pone busy=true en este stem",)"
      R"("brief":"Este barrio es el latch busy. start_busy y halt_busy escriben el flag."})",
      "como", "latch", "", &nb);
  expect(canned.ok && canned.covers, "canned chain anchored to latch source");

  const auto trap = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/chrome.cpp:handle_tab_hover",)"
      R"("chain":"handle_tab_hover escribe busy al pasar el raton",)"
      R"("why":"handle_tab_hover es el writer del latch",)"
      R"("brief":"El hover del chrome pone busy. Este barrio no es un latch."})",
      "como", "latch", "", &nb);
  const auto last_md = tuide::registry_causal_pilot_worker_user_prompt(
      "como", "q", "# ficha\n", nb.notes, "", true);
  expect(last_md.find("ÚLTIMO TURNO") != std::string::npos, "gate A last-turn prompt");
  expect(last_md.find("duda:Symbol") != std::string::npos, "last turn asks for walk doubts");

  tuide::RegistryCausalPilotWorkerNotebook nb_lie = nb;
  nb_lie.notes = "\n----- need_code src/latch.cpp:start_busy -----\n"
                 "void start_busy(Session* s) { s->busy = true; }\n";
  nb_lie.n_need_code = 1;
  tuide::registry_causal_pilot_notebook_add_target(&nb_lie, "src/latch.cpp:halt_busy");
  const auto wr_lie = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/latch.cpp:start_busy",)"
      R"("walk":"start_busy: escribe busy -> halt_busy: limpia busy",)"
      R"("why":"start_busy y halt_busy son el latch",)"
      R"("brief":"Este barrio escribe y limpia busy. Halt cierra el flag."})",
      "como", "latch", "", &nb_lie);
  expect(!wr_lie.ok && wr_lie.error.find("no leído") != std::string::npos &&
             wr_lie.error.find("halt_busy") != std::string::npos,
         "walk that lists unread halt_busy is a lie");
  const auto wr_short = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/latch.cpp:start_busy",)"
      R"("walk":"start_busy: escribe busy",)"
      R"("why":"start_busy pone busy=true en este stem",)"
      R"("brief":"Este barrio escribe busy. No abri el clear todavia."})",
      "como", "latch", "", &nb_lie);
  expect(wr_short.ok && wr_short.verdict == "chain",
         "one-step walk of a read symbol is the model deciding");
  const auto wr_duda = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"no_cubre",)"
      R"("path_symbol":"src/latch.cpp:start_busy",)"
      R"("walk":"start_busy: escribe busy -> duda:halt_busy",)"
      R"("why":"start_busy escribe; duda halt_busy",)"
      R"("brief":"Vi quien escribe busy. No lei el clear halt_busy."})",
      "como", "latch", "", &nb_lie);
  expect(wr_duda.ok && wr_duda.verdict == "no_cubre",
         "duda:halt_busy with no_cubre is honest");
  tuide::RegistryCausalPilotWorkerNotebook nb_tick = nb_lie;
  nb_tick.notes = "\n----- need_code src/latch.cpp:start_busy -----\n"
                  "void start_busy(Session* s) { s->busy = true; s->ticks = 0; }\n";
  tuide::registry_causal_pilot_notebook_add_target(&nb_tick, "src/latch.cpp:tick");
  const auto wr_tick = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"start_busy: escribe busy -> tick: limpia busy",)"
      R"("why":"start_busy escribe; tick limpia",)"
      R"("brief":"El hop de la ficha dice que tick limpia. No lei el cuerpo de tick."})",
      "como", "latch", "", &nb_tick);
  expect(!wr_tick.ok && wr_tick.error.find("no leído") != std::string::npos,
         "ticks in a body is not a read of tick");
  tuide::RegistryCausalPilotWorkerNotebook nb_close_lie = nb_tick;
  nb_close_lie.notes += "\n----- cierre -----\núltimo turno\n";
  const auto wr_close_lie = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"start_busy: escribe busy -> tick: limpia busy",)"
      R"("why":"tick limpia segun la ficha",)"
      R"("brief":"Cierro con el hop de la ficha. Tick seria el clear."})",
      "como", "latch", "", &nb_close_lie);
  expect(!wr_close_lie.ok && wr_close_lie.error.find("no leído") != std::string::npos,
         "last turn still cannot claim an unread walk step");
  const auto sys_md = tuide::registry_causal_pilot_worker_system_prompt("como", "latch");
  expect(sys_md.find("SIN DUDAS") != std::string::npos &&
             sys_md.find("cuando baste") == std::string::npos,
         "system prompt enumerates without a close semaphore");
  expect(sys_md.find("aguas_abajo") != std::string::npos, "system prompt names aguas_abajo");
  expect(sys_md.find("FnA: escribe el campo -> FnB") == std::string::npos,
         "como system prompt has no A->B walk template");
  expect(sys_md.find("árbol") != std::string::npos, "como system prompt names the tree");
}

void test_pilot_worker_tree_walk() {
  tuide::RegistryCausalPilotWorkerNotebook nb;
  tuide::registry_causal_pilot_notebook_add_target(&nb, "src/mess.cpp:set_busy");
  tuide::registry_causal_pilot_notebook_add_target(&nb, "src/mess.cpp:clear_busy");
  tuide::registry_causal_pilot_notebook_add_target(&nb, "src/mess.cpp:halt");
  tuide::registry_causal_pilot_notebook_add_target(&nb, "src/mess.cpp:arm");
  tuide::registry_causal_pilot_notebook_add_target(&nb, "src/mess.cpp:tick");
  tuide::registry_causal_pilot_notebook_add_target(&nb, "src/mess.cpp:keep_alive");
  nb.n_need_code = 3;
  nb.n_dataflow = 1;
  nb.notes =
      "\n----- need_code src/mess.cpp:set_busy -----\n"
      "void set_busy(State* s) { s->busy = true; s->halted = false; keep_alive(s); }\n"
      "----- aguas_abajo src/mess.cpp:set_busy -----\n"
      "busy:  escribe true\n"
      "halted:  escribe false\n"
      "\n----- need_code src/mess.cpp:clear_busy -----\n"
      "void clear_busy(State* s) { s->busy = false; }\n"
      "----- aguas_abajo src/mess.cpp:clear_busy -----\n"
      "busy:  escribe false\n"
      "\n----- need_code src/mess.cpp:halt -----\n"
      "void halt(State* s) { s->halted = true; s->busy = false; }\n"
      "----- aguas_abajo src/mess.cpp:halt -----\n"
      "halted:  escribe true\n"
      "busy:  escribe false\n"
      "\n----- dataflow busy -----\n"
      "fn=`set_busy` write busy\n"
      "fn=`clear_busy` write busy\n"
      "fn=`halt` write busy\n";

  const auto wr_tree = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/mess.cpp:set_busy",)"
      R"("walk":"set_busy: escribe busy -> clear_busy: limpia busy ; )"
      R"(halt: escribe halted y baja busy ; duda:arm ; port_to:chrome",)"
      R"("port_to":"chrome",)"
      R"("chain":"set_busy escribe busy; clear_busy y halt lo limpian; halted en halt",)"
      R"("why":"set_busy pone busy; clear_busy y halt lo bajan; halted sale de halt",)"
      R"("brief":"En este barrio busy lo enciende set_busy y lo apagan clear_busy o halt. )"
      R"(halted lo escribe halt; arm entra de chrome y no se abre."})",
      "como", "mess", "", &nb);
  expect(wr_tree.ok && wr_tree.verdict == "chain" && wr_tree.port_to == "chrome",
         wr_tree.ok ? "ramified walk with two fields, duda, and port_to is a legal chain"
                    : wr_tree.error.c_str());

  const auto wr_unread = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/mess.cpp:set_busy",)"
      R"("walk":"set_busy: escribe busy -> keep_alive: limpia busy",)"
      R"("why":"set_busy escribe; keep_alive limpia",)"
      R"("brief":"Keep_alive seria el clear. No abri el cuerpo de keep_alive."})",
      "como", "mess", "", &nb);
  expect(!wr_unread.ok && wr_unread.error.find("no leído") != std::string::npos,
         wr_unread.ok ? "unread keep_alive should fail"
                      : (wr_unread.error.find("no leído") != std::string::npos
                             ? "unread keep_alive in the walk is still a lie"
                             : wr_unread.error.c_str()));

  tuide::RegistryCausalPilotWorkerNotebook nb_latch;
  tuide::registry_causal_pilot_notebook_add_target(&nb_latch, "src/latch.cpp:start_busy");
  tuide::registry_causal_pilot_notebook_add_target(&nb_latch, "src/latch.cpp:halt_busy");
  nb_latch.n_need_code = 2;
  nb_latch.n_dataflow = 1;
  nb_latch.notes =
      "\n----- need_code src/latch.cpp:start_busy -----\n"
      "void start_busy(Session* s) { s->busy = true; }\n"
      "----- aguas_abajo src/latch.cpp:start_busy -----\n"
      "busy:  escribe true\n"
      "\n----- need_code src/latch.cpp:halt_busy -----\n"
      "void halt_busy(Session* s) { s->busy = false; }\n"
      "----- aguas_abajo src/latch.cpp:halt_busy -----\n"
      "busy:  escribe false\n"
      "\n----- dataflow busy -----\n"
      "fn=`start_busy` write busy\n"
      "fn=`halt_busy` write busy\n";
  const auto wr_latch = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("path_symbol":"src/latch.cpp:start_busy",)"
      R"("walk":"start_busy: escribe busy -> halt_busy: limpia busy",)"
      R"("chain":"start_busy escribe busy; halt_busy lo limpia",)"
      R"("why":"start_busy pone busy=true; halt_busy lo baja",)"
      R"("brief":"Este barrio es el latch busy. start_busy y halt_busy escriben el flag."})",
      "como", "latch", "", &nb_latch);
  expect(wr_latch.ok && wr_latch.verdict == "chain",
         "two-hop latch walk remains a legal chain");

  const auto sys_mess = tuide::registry_causal_pilot_worker_system_prompt("como", "mess");
  expect(sys_mess.find("FnA: escribe el campo -> FnB") == std::string::npos &&
             sys_mess.find("NombreFn: escribe el campo") == std::string::npos,
         "como prompt for mess has no two-hop template");
  const auto user_md = tuide::registry_causal_pilot_worker_user_prompt(
      "como", "q", "# ficha\n", nb.notes, "", false, "mess");
  expect(user_md.find("dudas locales") != std::string::npos,
         "como user prompt closes on local doubts of this stem");
}

void test_pilot_worker_aguas() {
  const std::string root =
      (fs::path(tide_root()) / "tests" / "fixtures" / "pilot_worker_read").string();
  auto search = [&](const std::string& name) {
    std::vector<tuide::ATrailSearchHit> hits;
    for (const char* rel : {"src/latch.cpp", "src/chrome.cpp"}) {
      std::ifstream f(fs::path(root) / rel);
      std::string line;
      int n = 0;
      while (std::getline(f, line)) {
        ++n;
        if (line.find(name) == std::string::npos) {
          continue;
        }
        tuide::ATrailSearchHit h;
        h.path = rel;
        h.line = n;
        h.preview = line;
        hits.push_back(h);
      }
    }
    return hits;
  };

  const std::string start_body =
      "void start_busy(Session* s) {\n"
      "  if (s == nullptr) { return; }\n"
      "  s->busy = true;\n"
      "  s->ticks = 0;\n"
      "}\n";
  const std::string start_down = tuide::registry_causal_pilot_aguas_abajo_markdown(
      "src/latch.cpp:start_busy", start_body, false);
  expect(start_down.find("----- aguas_abajo src/latch.cpp:start_busy -----") != std::string::npos,
         "aguas_abajo fence for start_busy");
  expect(start_down.find("busy:") != std::string::npos &&
             start_down.find("escribe true") != std::string::npos,
         "start_busy writes busy true");
  expect(start_down.find("ticks:") != std::string::npos &&
             start_down.find("escribe 0") != std::string::npos,
         "start_busy writes ticks 0");
  expect(start_down.find("no hay clear de busy") != std::string::npos,
         "start_busy notes missing busy clear");
  expect(start_down.find("causal_pilot_dataflow") != std::string::npos &&
             start_down.find("\"target\":\"busy\"") != std::string::npos,
         "missing clear points at dataflow of busy");
  expect(start_down.find("no es el clear") != std::string::npos,
         "aguas_abajo says a call is not the clear");
  expect(start_down.find("halt_busy") == std::string::npos &&
             start_down.find("request_work") == std::string::npos &&
             start_down.find("on_idle") == std::string::npos,
         "aguas_abajo does not name other functions");

  const std::string halt_body =
      "void halt_busy(Session* s) {\n"
      "  if (s == nullptr) { return; }\n"
      "  s->busy = false;\n"
      "}\n";
  const std::string halt_down = tuide::registry_causal_pilot_aguas_abajo_markdown(
      "src/latch.cpp:halt_busy", halt_body, false);
  expect(halt_down.find("escribe false") != std::string::npos, "halt_busy writes busy false");
  expect(halt_down.find("no hay clear de busy") == std::string::npos,
         "halt_busy is the clear");

  const std::string tick_body =
      "void tick(Session* s) {\n"
      "  if (s == nullptr || !s->busy) { return; }\n"
      "  s->ticks += 1;\n"
      "}\n";
  const std::string tick_down = tuide::registry_causal_pilot_aguas_abajo_markdown(
      "src/latch.cpp:tick", tick_body, false);
  expect(tick_down.find("incrementa") != std::string::npos, "tick increments ticks");
  expect(tick_down.find("busy:") != std::string::npos && tick_down.find("lee") != std::string::npos,
         "tick reads busy");
  expect(tick_down.find("no hay clear de busy") != std::string::npos,
         "tick does not clear busy");
  expect(tick_down.find("halt_busy") == std::string::npos, "tick aguas_abajo has no halt_busy");

  const std::string cxl_body =
      "void cancel_level1() {\n"
      "  agent_cancel_.store(true);\n"
      "  if (agent_busy_.load()) { append(\"cxl\"); }\n"
      "}\n";
  const std::string cxl_down = tuide::registry_causal_pilot_aguas_abajo_markdown(
      "src/ai/ai_controller.cpp:cancel_level1", cxl_body, false);
  expect(cxl_down.find("agent_cancel_") != std::string::npos &&
             cxl_down.find("escribe true") != std::string::npos,
         "cancel_level1 writes agent_cancel_ true");
  expect(cxl_down.find("no hay clear de agent_cancel_") == std::string::npos,
         "a cancel flag store(true) is the act, not a missing latch clear");

  const std::string trunc = tuide::registry_causal_pilot_aguas_abajo_markdown(
      "src/latch.cpp:start_busy", start_body, true);
  expect(trunc.find("solo el recorte enviado") != std::string::npos, "truncated flag");

  const std::string start_up = tuide::registry_causal_pilot_aguas_arriba_markdown(
      root, "src/latch.cpp", "start_busy", search, nullptr);
  expect(start_up.find("----- aguas_arriba src/latch.cpp:start_busy -----") != std::string::npos,
         "aguas_arriba fence");
  expect(start_up.find("(sin caller en este stem)") != std::string::npos,
         "latch fixture has no caller of start_busy");
  expect(start_up.find("halt_busy") == std::string::npos, "incoming of start_busy is not halt_busy");

  const std::string tmp = make_tmp();
  write_file(tmp + "/src/latch.cpp",
             "namespace demo {\n"
             "struct Session { bool busy = false; int ticks = 0; };\n"
             "void start_busy(Session* s) {\n"
             "  if (s == nullptr) { return; }\n"
             "  s->busy = true;\n"
             "}\n"
             "void halt_busy(Session* s) {\n"
             "  if (s == nullptr) { return; }\n"
             "  s->busy = false;\n"
             "}\n"
             "void request_work(Session* s) {\n"
             "  if (s != nullptr && !s->busy) { start_busy(s); }\n"
             "}\n"
             "void on_idle(Session* s) {\n"
             "  if (s != nullptr && s->ticks > 8) { halt_busy(s); }\n"
             "}\n"
             "}\n");
  auto search_tmp = [&](const std::string& name) {
    std::vector<tuide::ATrailSearchHit> hits;
    std::ifstream f(tmp + "/src/latch.cpp");
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
      ++n;
      if (line.find(name) == std::string::npos) {
        continue;
      }
      tuide::ATrailSearchHit h;
      h.path = "src/latch.cpp";
      h.line = n;
      h.preview = line;
      hits.push_back(h);
    }
    return hits;
  };
  std::vector<std::string> incoming;
  const std::string start_up_call = tuide::registry_causal_pilot_aguas_arriba_markdown(
      tmp, "src/latch.cpp", "start_busy", search_tmp, &incoming);
  expect(start_up_call.find("request_work") != std::string::npos,
         "temp caller of start_busy is request_work");
  expect(start_up_call.find("quien:") != std::string::npos, "aguas_arriba has quien");
  expect(start_up_call.find("cuando:") != std::string::npos, "aguas_arriba has cuando");
  bool saw_request = false;
  for (const auto& t : incoming) {
    if (t.find("request_work") != std::string::npos) {
      saw_request = true;
    }
  }
  expect(saw_request, "incoming targets include request_work");
  const std::string halt_up = tuide::registry_causal_pilot_aguas_arriba_markdown(
      tmp, "src/latch.cpp", "halt_busy", search_tmp, nullptr);
  expect(halt_up.find("on_idle") != std::string::npos, "halt_busy caller is on_idle");
  const std::string tick_up = tuide::registry_causal_pilot_aguas_arriba_markdown(
      root, "src/latch.cpp", "tick", search, nullptr);
  expect(tick_up.find("(sin caller en este stem)") != std::string::npos,
         "tick has no caller in the fixture");

  const std::string atomic_body =
      "void cancel_level1() {\n"
      "  agent_cancel_.store(true);\n"
      "  if (agent_busy_.load()) { append(\"x\"); }\n"
      "}\n";
  const std::string atomic_down = tuide::registry_causal_pilot_aguas_abajo_markdown(
      "src/ai/ai_controller.cpp:cancel_level1", atomic_body, false);
  expect(atomic_down.find("agent_cancel_") != std::string::npos &&
             atomic_down.find("escribe true") != std::string::npos,
         "aguas_abajo sees atomic store");
  expect(atomic_down.find("agent_busy_") != std::string::npos &&
             atomic_down.find("lee") != std::string::npos,
         "aguas_abajo sees atomic load");

  const std::string other = make_tmp();
  write_file(other + "/src/latch.cpp",
             "namespace demo {\n"
             "struct Session { bool busy = false; };\n"
             "void start_busy(Session* s) { if (s) s->busy = true; }\n"
             "}\n");
  write_file(other + "/src/chrome.cpp",
             "namespace demo {\n"
             "void request_work(Session* s) {\n"
             "  if (s != nullptr && !s->busy) { start_busy(s); }\n"
             "}\n"
             "}\n");
  auto search_other = [&](const std::string& name) {
    std::vector<tuide::ATrailSearchHit> hits;
    for (const char* rel : {"src/latch.cpp", "src/chrome.cpp"}) {
      std::ifstream f(other + "/" + rel);
      std::string line;
      int n = 0;
      while (std::getline(f, line)) {
        ++n;
        if (line.find(name) == std::string::npos) {
          continue;
        }
        tuide::ATrailSearchHit h;
        h.path = rel;
        h.line = n;
        h.preview = line;
        hits.push_back(h);
      }
    }
    return hits;
  };
  const std::string up_other = tuide::registry_causal_pilot_aguas_arriba_markdown(
      other, "src/latch.cpp", "start_busy", search_other, nullptr);
  expect(up_other.find("(sin caller en este stem)") != std::string::npos,
         "other-stem caller is not aguas_arriba");
  expect(up_other.find("request_work") == std::string::npos,
         "aguas_arriba does not leak chrome caller");
  fs::remove_all(other);
  fs::remove_all(tmp);

  tuide::RegistryCausalPilotWorkerNotebook nb;
  nlohmann::json zone = {
      {"id", "L1"},
      {"owns", "latch"},
      {"primary_stems", nlohmann::json::array({"latch"})},
      {"representatives",
       nlohmann::json::array({nlohmann::json{{"target", "src/latch.cpp:start_busy"}}})}};
  tuide::registry_causal_pilot_notebook_from_payload(nlohmann::json{{"zones", {zone}}}, "latch",
                                                     &nb);
  tuide::registry_causal_pilot_notebook_add_target(&nb, "src/latch.cpp:halt_busy");
  nb.n_need_code = 1;
  nb.notes = "\n----- need_code src/latch.cpp:start_busy -----\n" + start_body + start_down +
             start_up +
             "----- aguas_abajo src/latch.cpp:start_busy -----\n"
             "no hay clear; halt_busy seria el writer\n"
             "\n----- dataflow busy -----\n(sin fn= write)\n";
  const auto lie = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"start_busy: escribe busy -> halt_busy: limpia busy",)"
      R"("why":"aguas_abajo nombra halt_busy",)"
      R"("brief":"El fence de atributos dice que halt_busy limpia. No abri el cuerpo."})",
      "como", "latch", "", &nb);
  expect(!lie.ok && lie.error.find("no leído") != std::string::npos,
         "aguas fences are not a need_code read of halt_busy");

  tuide::RegistryCausalPilotWorkerNotebook nb_call = nb;
  tuide::registry_causal_pilot_notebook_add_target(&nb_call, "src/ui/busy_strip.cpp:ensure_spinner_thread");
  nb_call.n_need_code = 1;
  nb_call.notes =
      "\n----- need_code src/ui/busy_strip.cpp:set_busy_spinner -----\n"
      "void set_busy_spinner() { state.halted.load(); ensure_spinner_thread(&state); }\n"
      "----- aguas_abajo src/ui/busy_strip.cpp:set_busy_spinner -----\n"
      "halted:  lee\nno hay clear de halted en este cuerpo\n";
  const auto hop_call = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"set_busy_spinner: escribe -> ensure_spinner_thread: mantiene",)"
      R"("why":"set_busy_spinner llama ensure_spinner_thread",)"
      R"("brief":"El call del cuerpo mantiene el spinner. No pedi dataflow."})",
      "como", "busy_strip", "", &nb_call);
  expect(!hop_call.ok && hop_call.error.find("falta dataflow") != std::string::npos &&
             hop_call.error.find("halted") != std::string::npos,
         "outgoing call in the body is not a substitute for dataflow");
  nb_call.n_dataflow = 1;
  nb_call.notes +=
      "\n----- dataflow halted -----\nfn=`src/ui/busy_strip.cpp:halt_busy_strip` @329 write\n";
  tuide::registry_causal_pilot_notebook_add_target(&nb_call, "src/ui/busy_strip.cpp:halt_busy_strip");
  const auto still_call = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"set_busy_spinner: escribe -> ensure_spinner_thread: mantiene",)"
      R"("why":"set_busy_spinner llama ensure_spinner_thread",)"
      R"("brief":"Tras dataflow sigo copiando el hop del cuerpo. Halt no esta."})",
      "como", "busy_strip", "", &nb_call);
  expect(!still_call.ok && still_call.error.find("writer") != std::string::npos &&
             still_call.error.find("halt_busy_strip") != std::string::npos,
         "dataflow write fn= requires need_code of that writer");
  const auto md_writer = tuide::registry_causal_pilot_worker_user_prompt(
      "como", "q", "# ficha\n", nb_call.notes, "", false, "busy_strip");
  expect(md_writer.find("PROHIBIDO causal_pilot_worker_v1") != std::string::npos &&
             md_writer.find("halt_busy_strip") != std::string::npos &&
             md_writer.find("causal_pilot_need_code") != std::string::npos,
         "como prompt after dataflow forces need_code of the write fn=");
  const auto duda_writer = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"no_cubre",)"
      R"("walk":"set_busy_spinner: escribe -> duda:halt_busy_strip",)"
      R"("why":"set_busy_spinner escribe; duda halt_busy_strip",)"
      R"("brief":"Vi quien escribe halted. No abri el writer halt_busy_strip."})",
      "como", "busy_strip", "", &nb_call);
  expect(duda_writer.ok && duda_writer.verdict == "no_cubre",
         "duda:writer with no_cubre is honest without opening it");
  tuide::RegistryCausalPilotWorkerNotebook nb_opened = nb_call;
  nb_opened.n_need_code = 2;
  nb_opened.notes +=
      "\n----- need_code src/ui/busy_strip.cpp:halt_busy_strip -----\n"
      "void halt_busy_strip() { state.halted.store(true); }\n";
  const auto opened = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"set_busy_spinner: escribe halted -> halt_busy_strip: limpia halted",)"
      R"("why":"halt_busy_strip escribe halted",)"
      R"("brief":"set_busy_spinner pone el spinner. halt_busy_strip lo para."})",
      "como", "busy_strip", "", &nb_opened);
  expect(opened.ok && opened.verdict == "chain", "need_code of the write fn= unlocks chain");
  const auto callee_lie = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"set_busy_spinner: escribe -> ensure_spinner_thread: mantiene",)"
      R"("why":"set_busy_spinner llama ensure_spinner_thread",)"
      R"("brief":"Abri halt_busy_strip pero el walk sigue siendo la llamada del recorte."})",
      "como", "busy_strip", "", &nb_opened);
  expect(!callee_lie.ok && callee_lie.error.find("no leído") != std::string::npos,
         "a callee in another body is not a need_code of that callee");

  tuide::RegistryCausalPilotWorkerNotebook nb_two = nb_opened;
  nb_two.notes +=
      "\n----- dataflow agent_cancel_ -----\n"
      "fn=`src/ai/ai_controller.cpp:cancel_level1` @737 write\n"
      "fn=`src/ai/ai_controller.cpp:run_level1_async` @937 write\n"
      "fn=`src/ai/ai_controller.cpp:run_insert_async` @1038 write\n";
  nb_two.notes +=
      "\n----- need_code src/ai/ai_controller.cpp:cancel_level1 -----\n"
      "void cancel_level1() { agent_cancel_.store(true); }\n"
      "\n----- need_code src/ai/ai_controller.cpp:run_level1_async -----\n"
      "void run_level1_async() { agent_cancel_.store(false); }\n";
  tuide::registry_causal_pilot_notebook_add_target(&nb_two, "src/ai/ai_controller.cpp:cancel_level1");
  tuide::registry_causal_pilot_notebook_add_target(&nb_two,
                                                   "src/ai/ai_controller.cpp:run_level1_async");
  tuide::registry_causal_pilot_notebook_add_target(&nb_two,
                                                   "src/ai/ai_controller.cpp:run_insert_async");
  const auto two_writers = tuide::registry_parse_causal_pilot_worker(
      R"({"action":"causal_pilot_worker_v1","kind":"como","verdict":"chain",)"
      R"("walk":"cancel_level1: escribe agent_cancel_ -> run_level1_async: limpia agent_cancel_",)"
      R"("why":"cancel_level1 escribe; run_level1_async limpia",)"
      R"("brief":"Abri un write extra del flag. No recorro todos los run_async."})",
      "como", "ai_controller", "", &nb_two);
  expect(two_writers.ok && two_writers.verdict == "chain",
         "two fenced write fn= do not force a third");
  const auto md_two = tuide::registry_causal_pilot_worker_user_prompt(
      "como", "q", "# ficha\n", nb_two.notes, "", false, "ai_controller");
  expect(md_two.find("run_insert_async") == std::string::npos ||
             md_two.find("PROHIBIDO causal_pilot_worker_v1") == std::string::npos,
         "prompt does not serialize every write fn= after two are open");

  const auto md_clear = tuide::registry_causal_pilot_worker_user_prompt(
      "como", "q", "# ficha\n",
      "\n----- need_code src/latch.cpp:start_busy -----\n" + start_body + start_down, "", false);
  expect(md_clear.find("PROHIBIDO causal_pilot_worker_v1") != std::string::npos &&
             md_clear.find("causal_pilot_dataflow") != std::string::npos &&
             md_clear.find("busy") != std::string::npos,
         "como user prompt forces dataflow after missing clear");
  const auto md_cubre = tuide::registry_causal_pilot_worker_user_prompt(
      "cubre", "q", "# ficha\n",
      "\n----- need_code src/latch.cpp:start_busy -----\n" + start_body + start_down, "", false);
  expect(md_cubre.find("PROHIBIDO causal_pilot_worker_v1") == std::string::npos,
         "cubre does not force dataflow after missing clear");
}

}  // namespace

int main() {
  test_canonical_helpers();
  test_idempotent_polar();
  test_two_file_call();
  test_hpp_cpp_twin();
  test_latches_per_stem();
  test_refresh_tombstone_and_gc();
  test_reject_tests_product();
  test_embed_cosine_and_hops();
  test_query_hops_cross_file();
  test_query_constellations();
  test_large_inventory_keeps_linked_fn();
  test_query_hop0_skips_ctrl();
  test_pilot_worker_read_gate_a();
  test_pilot_worker_tree_walk();
  test_pilot_worker_aguas();
  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "l2_effect_registry_test ok\n";
  return 0;
}
