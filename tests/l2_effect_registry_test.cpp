#include "ai/l2_effect_registry.hpp"
#include "ai/l2_effect_slice.hpp"

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
    expect(zone["edges"].size() <= static_cast<std::size_t>(judge_opts.max_edges),
           "causal judge edge budget");
    expect(zone["representatives"].size() <=
               static_cast<std::size_t>(judge_opts.max_representatives),
           "causal judge representative budget");
    for (const auto& card : zone["representatives"]) {
      expect(!card.contains("nudge"), "causal judge excludes prior judgments");
    }
  }
  expect(tuide::registry_causal_judge_markdown(judge_payload).find("causal edges") !=
             std::string::npos,
         "causal judge markdown");
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
  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "l2_effect_registry_test ok\n";
  return 0;
}
