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

void test_generic_trail_real_depth() {
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
      root, "update_hierarchy_status", "src/ui/call_hierarchy_view.cpp", search_fn,
      tuide::kATrailMaxStacks, tuide::kATrailMaxDepth);
  expect(!stacks.empty(), "generic helper produces stacks");

  bool found_caller_stack = false;
  for (const auto& s : stacks) {
    expect(!stack_has_duplicate_symbols(s), "stack has no duplicate symbols");
    expect(s.hops.size() >= 2, "stack has L0 + caller");
    if (s.hops.size() >= 3) {
      expect(s.hops[0].symbol != s.hops[1].symbol, "climb adds distinct parent");
    }
    for (std::size_t i = 0; i + 1 < s.hops.size(); ++i) {
      if (s.hops[i].symbol != "expand_hierarchy_tree") {
        continue;
      }
      found_caller_stack = true;
      if (i > 0) {
        expect(s.hops[i - 1].symbol != "expand_hierarchy_tree", "no duplicate caller chain");
      }
      break;
    }
  }
  expect(found_caller_stack, "found generic caller stack");
}

void test_no_detached_case_specific_branches() {
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

  const std::vector<std::string> seeds = {"update_hierarchy_status"};
  const auto stacks = tuide::a_trail_build_full_stacks(
      root, "update_hierarchy_status", "src/ui/call_hierarchy_view.cpp", search_fn,
      tuide::kATrailMaxStacks, tuide::kATrailMaxDepth);
  const auto branches = tuide::a_trail_build_cond_branches(
      root, "update_hierarchy_status", "src/ui/call_hierarchy_view.cpp", seeds, search_fn, stacks);

  auto has_id = [&](const char* id) {
    for (const auto& b : branches) {
      if (b.id == id) {
        return true;
      }
    }
    return false;
  };
  expect(has_id("ON"), "cond ON branch");
  expect(!has_id("CXL"), "no detached CXL injection");
  expect(!has_id("OFF"), "no detached OFF injection");
  expect(!has_id("LINK"), "no synthetic LINK without linked branches");
}

void test_trap_l0_drops_unlinked_cxl() {
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

  const std::vector<std::string> seeds = {"activate_console_input"};
  const auto stacks = tuide::a_trail_build_full_stacks(
      root, "activate_console_input", "src/ui/console_panel.cpp", search_fn,
      tuide::kATrailMaxStacks, tuide::kATrailMaxDepth);
  const auto branches = tuide::a_trail_build_cond_branches(
      root, "activate_console_input", "src/ui/console_panel.cpp", seeds, search_fn, stacks);
  for (const auto& b : branches) {
    if (b.id == "CXL" || b.id == "OFF" || b.id == "LINK") {
      expect(false, "unlinked CXL/OFF/LINK is not injected");
    }
  }
}

}  // namespace

int main() {
  test_generic_trail_real_depth();
  test_no_detached_case_specific_branches();
  test_trap_l0_drops_unlinked_cxl();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "l2_trail_build_test OK\n";
  return 0;
}
