#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "ai/level0_intent_index.hpp"
#include "ai/level0_router.hpp"
#include "ai/vector_math.hpp"

using tuide::AiRouteKind;
using tuide::Level0IntentExample;
using tuide::Level0IntentIndex;
using tuide::Level0IntentMatch;
using tuide::cosine_similarity;
using tuide::route_level0;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

std::vector<float> unit(float x, float y, float z) {
  const float n = std::sqrt(x * x + y * y + z * z);
  return {x / n, y / n, z / n};
}

int main() {
  {
    expect(std::fabs(cosine_similarity(unit(1, 0, 0), unit(1, 0, 0)) - 1.0f) < 1e-5f,
           "cosine identical");
    expect(std::fabs(cosine_similarity(unit(1, 0, 0), unit(0, 1, 0))) < 1e-5f, "cosine orthogonal");
  }

  Level0IntentIndex index;
  std::string err;
  expect(index.load_catalog({}, &err), "load catalog: " + err);
  expect(!index.default_catalog_json().empty(), "embedded catalog non-empty");

  {
    std::vector<Level0IntentExample> rows;
    Level0IntentExample status;
    status.intent_id = "git_status";
    status.name = "git_status";
    status.arg_policy = "path_hint";
    status.example = "archivos modificados";
    status.embedding = unit(1, 0, 0);
    rows.push_back(status);

    Level0IntentExample diff;
    diff.intent_id = "git_diff";
    diff.name = "git_diff";
    diff.arg_policy = "path_hint";
    diff.example = "muéstrame el diff";
    diff.embedding = unit(0, 1, 0);
    rows.push_back(diff);

    Level0IntentExample pull;
    pull.intent_id = "git_pull";
    pull.name = "git_pull";
    pull.arg_policy = "none";
    pull.example = "actualiza el git";
    pull.embedding = unit(0, 0, 1);
    rows.push_back(pull);

    index.set_examples_for_test(std::move(rows));
  }

  {
    const auto m = index.match_precomputed(unit(0.99f, 0.01f, 0), 0.7f, 0.05f);
    expect(m.ok && m.name == "git_status", "match status vector");
  }
  {
    // Ambiguous across *different* intents → margin fail.
    // Same-intent near neighbors must not reject.
    std::vector<Level0IntentExample> rows;
    Level0IntentExample a;
    a.name = "git_status";
    a.arg_policy = "path_hint";
    a.example = "a";
    a.embedding = unit(1, 0.05f, 0);
    rows.push_back(a);
    Level0IntentExample a2 = a;
    a2.example = "a2";
    a2.embedding = unit(1, 0.08f, 0);
    rows.push_back(a2);
    Level0IntentExample b;
    b.name = "git_pull";
    b.arg_policy = "none";
    b.example = "b";
    b.embedding = unit(1, 0.9f, 0);
    rows.push_back(b);
    index.set_examples_for_test(std::move(rows));
    const auto m = index.match_precomputed(unit(1, 0.06f, 0), 0.5f, 0.2f);
    expect(m.ok && m.name == "git_status", "same-intent neighbors ok");
    const auto m2 = index.match_precomputed(unit(1, 0.5f, 0), 0.5f, 0.2f);
    expect(!m2.ok, "cross-intent ambiguous margin");
  }
  {
    const auto m = index.match_precomputed(unit(0.2f, 0.2f, 0.2f), 0.95f, 0.05f);
    expect(!m.ok, "low score escalate");
  }

  // Router with semantic matcher mock.
  {
    auto semantic = [](const std::string& q) {
      Level0IntentMatch m;
      if (q.find("repo") != std::string::npos || q.find("working tree") != std::string::npos) {
        m.ok = true;
        m.name = "git_status";
        m.arg_policy = "path_hint";
        m.score = 0.9f;
        m.margin = 0.2f;
      }
      return m;
    };
    const auto r = route_level0("cómo está el repo hoy", {}, {}, semantic);
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status",
           "semantic paraphrase → git_status");
    const auto r2 = route_level0("working tree dirty?", {}, {}, semantic);
    expect(r2.kind == AiRouteKind::ResolveTool && r2.tool_name == "git_status",
           "semantic EN → git_status");
    const auto r3 = route_level0("explica la arquitectura del parser", {}, {}, semantic);
    expect(r3.kind == AiRouteKind::EscalateLevel1, "semantic miss → escalate");
  }

  // Structural follow-up still wins before semantic.
  {
    auto semantic = [](const std::string&) {
      Level0IntentMatch m;
      m.ok = true;
      m.name = "git_pull";
      m.arg_policy = "none";
      m.score = 0.99f;
      m.margin = 0.5f;
      return m;
    };
    const auto r = route_level0("y dentro de src?", "git_status", "tests", semantic);
    expect(r.kind == AiRouteKind::ResolveTool && r.tool_name == "git_status" && r.arg == "src",
           "follow-up beats semantic");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level0_intent_index_test ok\n";
  return 0;
}
