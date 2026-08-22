#include "ai/l2_explore_a.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
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

bool stack_has_duplicate_symbols(const tuide::ATrailStack& s) {
  std::unordered_set<std::string> seen;
  for (const auto& h : s.hops) {
    if (h.symbol.empty()) {
      continue;
    }
    if (!seen.insert(h.symbol).second) {
      return true;
    }
  }
  return false;
}

void test_set_busy_spinner_real_depth() {
  const std::string root = workspace_root();
  auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    std::vector<tuide::ATrailSearchHit> hits;
    const std::string cmd =
        "rg -n --no-heading -F " + symbol + " " + root + "/src 2>/dev/null | head -80";
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
      while (!h.preview.empty() && (h.preview.back() == '\n' || h.preview.back() == '\r')) {
        h.preview.pop_back();
      }
      hits.push_back(std::move(h));
    }
    pclose(fp);
    return hits;
  };

  const auto stacks = tuide::a_trail_build_full_stacks(
      root, "set_busy_spinner", "src/ui/busy_strip.cpp", search_fn, tuide::kATrailMaxStacks,
      tuide::kATrailMaxDepth);
  expect(!stacks.empty(), "set_busy_spinner produces stacks");

  bool found_ai_stack = false;
  for (const auto& s : stacks) {
    expect(!stack_has_duplicate_symbols(s), "stack has no duplicate symbols");
    expect(s.hops.size() >= 2, "stack has L0 + caller");
    if (s.hops.size() >= 3) {
      expect(s.hops[0].symbol != s.hops[1].symbol, "climb adds distinct parent");
    }
    for (std::size_t i = 0; i + 1 < s.hops.size(); ++i) {
      if (s.hops[i].symbol != "begin_thinking") {
        continue;
      }
      found_ai_stack = true;
      expect(s.hops[i].call_line != 834,
             "begin_thinking hop is not the definition line");
      if (i > 0) {
        expect(s.hops[i - 1].symbol != "begin_thinking",
               "no begin_thinking → begin_thinking chain");
      }
      break;
    }
  }
  expect(found_ai_stack, "found begin_thinking caller stack");
}

void test_set_busy_spinner_cond_branches() {
  const std::string root = workspace_root();
  auto search_fn = [&](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    std::vector<tuide::ATrailSearchHit> hits;
    const std::string cmd =
        "rg -n --no-heading -F " + symbol + " " + root + "/src 2>/dev/null | head -80";
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
      while (!h.preview.empty() && (h.preview.back() == '\n' || h.preview.back() == '\r')) {
        h.preview.pop_back();
      }
      hits.push_back(std::move(h));
    }
    pclose(fp);
    return hits;
  };

  const std::vector<std::string> seeds = {"spinner", "busy", "cancel", "agent_busy"};
  const auto stacks = tuide::a_trail_build_full_stacks(
      root, "set_busy_spinner", "src/ui/busy_strip.cpp", search_fn, tuide::kATrailMaxStacks,
      tuide::kATrailMaxDepth);
  const auto branches = tuide::a_trail_build_cond_branches(
      root, "set_busy_spinner", "src/ui/busy_strip.cpp", seeds, search_fn, stacks);

  auto has_id = [&](const char* id) {
    for (const auto& b : branches) {
      if (b.id == id) {
        return true;
      }
    }
    return false;
  };
  expect(has_id("ON"), "cond ON branch");
  expect(has_id("CXL"), "cond CXL branch");
  expect(has_id("OFF"), "cond OFF branch");
  expect(has_id("LINK"), "cond LINK handoff when CXL+OFF");

  for (const auto& b : branches) {
    if (b.id == "CXL") {
      expect(b.snippet.find("cancel") != std::string::npos ||
                 b.then_text.find("cancel") != std::string::npos ||
                 b.snippet.find("agent_cancel_") != std::string::npos,
             "CXL mentions cancel path");
    }
    if (b.id == "OFF") {
      expect(b.then_text.find("end_thinking") != std::string::npos ||
                 b.then_text.find("clear_busy") != std::string::npos,
             "OFF mentions cleanup");
    }
  }
}

}  // namespace

int main() {
  test_set_busy_spinner_real_depth();
  test_set_busy_spinner_cond_branches();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "l2_trail_build_test OK\n";
  return 0;
}
