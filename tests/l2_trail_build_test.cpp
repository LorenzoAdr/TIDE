#include "ai/l2_explore_a.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
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

bool find_named_leaf(const tuide::ACausalFlowNode& node, const std::string& leaf_name,
                     std::vector<const tuide::ACausalFlowNode*>* path) {
  path->push_back(&node);
  if (node.name == leaf_name && node.children.empty()) {
    return true;
  }
  for (const auto& child : node.children) {
    if (find_named_leaf(child, leaf_name, path)) {
      return true;
    }
  }
  path->pop_back();
  return false;
}

void test_causal_flow_write_caller_chain() {
  char tmpl[] = "/tmp/tuide-causal-XXXXXX";
  char* made = mkdtemp(tmpl);
  expect(made != nullptr, "temp workspace");
  if (made == nullptr) {
    return;
  }
  const std::filesystem::path root = made;
  std::filesystem::create_directories(root / "src");
  {
    std::ofstream out(root / "src" / "writer.cpp");
    out << "void set_ready() {\n"
           "  if (state == Active) {\n"
           "    ready_ = true;\n"
           "  }\n"
           "}\n";
  }
  {
    std::ofstream out(root / "src" / "caller.cpp");
    out << "void tick() {\n"
           "  set_ready();\n"
           "}\n"
           "void tock() {\n"
           "  set_ready();\n"
           "}\n";
  }
  {
    std::ofstream out(root / "src" / "reader.cpp");
    out << "void sink() {\n"
           "}\n"
           "void use_ready() {\n"
           "  sink();\n"
           "}\n"
           "void reader() {\n"
           "  if (ready_) {\n"
           "    use_ready();\n"
           "  }\n"
           "}\n";
  }

  auto search = [](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    std::vector<tuide::ATrailSearchHit> hits;
    auto add = [&](const char* path, int line, const char* preview) {
      tuide::ATrailSearchHit hit;
      hit.path = path;
      hit.line = line;
      hit.preview = preview;
      hits.push_back(std::move(hit));
    };
    if (symbol == "ready_") {
      add("src/writer.cpp", 3, "    ready_ = true;");
      add("src/reader.cpp", 7, "  if (ready_) {");
    } else if (symbol == "set_ready") {
      add("src/caller.cpp", 2, "  set_ready();");
      add("src/caller.cpp", 5, "  set_ready();");
    } else if (symbol == "use_ready") {
      add("src/reader.cpp", 3, "void use_ready() {");
      add("src/reader.cpp", 8, "    use_ready();");
    } else if (symbol == "sink") {
      add("src/reader.cpp", 1, "void sink() {");
      add("src/reader.cpp", 4, "  sink();");
    }
    return hits;
  };

  const tuide::ACausalFlowTree tree = tuide::a_causal_flow_build(
      root.string(), "ready_", "src/writer.cpp", search, tuide::kACausalFlowMaxWrites,
      tuide::kACausalMaxChains, tuide::kACausalUpstreamDepth);

  auto path_has_caller = [&](const char* caller) {
    for (const tuide::ACausalFlowNode& node : tree.root.children) {
      std::vector<const tuide::ACausalFlowNode*> one;
      if (!find_named_leaf(node, "set_ready", &one)) {
        continue;
      }
      for (const tuide::ACausalFlowNode* step : one) {
        if (step->kind == tuide::ACausalNodeKind::Caller && step->name == caller) {
          return true;
        }
      }
    }
    return false;
  };
  expect(path_has_caller("tick"), "caller tick is on a write chain");
  expect(path_has_caller("tock"), "caller tock is on a write chain");

  std::vector<const tuide::ACausalFlowNode*> write_path;
  bool found_write = false;
  for (const tuide::ACausalFlowNode& child : tree.root.children) {
    write_path.clear();
    if (find_named_leaf(child, "set_ready", &write_path)) {
      found_write = true;
      break;
    }
  }
  expect(found_write, "write chain ends at set_ready");
  if (found_write) {
    const tuide::ACausalFlowNode* leaf = write_path.back();
    expect(leaf->kind == tuide::ACausalNodeKind::Write, "leaf is the write");
    expect(leaf->line == 3, "write line");
    expect(leaf->preview.find("ready_") != std::string::npos, "write preview");
    expect(leaf->detail.find("Active") != std::string::npos, "write cond");
  }

  std::vector<const tuide::ACausalFlowNode*> down;
  bool found_sink = false;
  for (const tuide::ACausalFlowNode& child : tree.root.children) {
    down.clear();
    if (find_named_leaf(child, "sink", &down)) {
      found_sink = true;
      break;
    }
  }
  expect(found_sink, "downstream reaches sink");
  if (found_sink) {
    bool saw_reader = false;
    bool saw_use = false;
    for (const tuide::ACausalFlowNode* step : down) {
      if (step->name == "reader") {
        saw_reader = true;
      }
      if (step->kind == tuide::ACausalNodeKind::Callee && step->name == "use_ready") {
        saw_use = true;
      }
    }
    expect(saw_reader, "read sits in reader");
    expect(saw_use, "one downstream hop is use_ready");
    expect(down.back()->kind == tuide::ACausalNodeKind::Callee, "sink is a callee");
  }

  std::filesystem::remove_all(root);
}

void test_causal_flow_call_climbs_condition() {
  char tmpl[] = "/tmp/tuide-causal-call-XXXXXX";
  char* made = mkdtemp(tmpl);
  expect(made != nullptr, "temp call workspace");
  if (made == nullptr) {
    return;
  }
  const std::filesystem::path root = made;
  std::filesystem::create_directories(root / "src");
  {
    std::ofstream out(root / "src" / "poke.cpp");
    out << "void set_flag() {\n"
           "  flag_ = true;\n"
           "}\n"
           "void set_program() {\n"
           "  config_.program = \"bin\";\n"
           "}\n"
           "bool ready() {\n"
           "  return config_.program.empty();\n"
           "}\n"
           "void poke() {\n"
           "  if (flag_ || !ready()) {\n"
           "    go();\n"
           "  }\n"
           "}\n";
  }

  auto search = [](const std::string& symbol) -> std::vector<tuide::ATrailSearchHit> {
    std::vector<tuide::ATrailSearchHit> hits;
    auto add = [&](const char* path, int line, const char* preview) {
      tuide::ATrailSearchHit hit;
      hit.path = path;
      hit.line = line;
      hit.preview = preview;
      hits.push_back(std::move(hit));
    };
    if (symbol == "flag_") {
      add("src/poke.cpp", 2, "  flag_ = true;");
      add("src/poke.cpp", 11, "  if (flag_ || !ready()) {");
    } else if (symbol == "config_.program") {
      add("src/poke.cpp", 5, "  config_.program = \"bin\";");
      add("src/poke.cpp", 8, "  return config_.program.empty();");
    } else if (symbol == "ready") {
      add("src/poke.cpp", 7, "bool ready() {");
      add("src/poke.cpp", 11, "  if (flag_ || !ready()) {");
    } else if (symbol == "set_flag") {
      add("src/poke.cpp", 1, "void set_flag() {");
    } else if (symbol == "set_program") {
      add("src/poke.cpp", 4, "void set_program() {");
    } else if (symbol == "go") {
      add("src/poke.cpp", 12, "    go();");
    }
    return hits;
  };

  const tuide::ACausalFlowTree tree = tuide::a_causal_flow_build(
      root.string(), "go", "src/poke.cpp", search, tuide::kACausalFlowMaxWrites,
      tuide::kACausalMaxChains, tuide::kACausalUpstreamDepth, 12);

  expect(!tree.root.children.empty(), "call site grows a guard");
  bool saw_condition = false;
  bool saw_flag_write = false;
  bool saw_program = false;
  std::vector<const tuide::ACausalFlowNode*> stack;
  const auto walk = [&](auto&& self, const tuide::ACausalFlowNode& node) -> void {
    stack.push_back(&node);
    if (node.name.find("flag_") != std::string::npos &&
        node.name.find("ready") != std::string::npos) {
      saw_condition = true;
    }
    if (node.kind == tuide::ACausalNodeKind::Write && node.name == "set_flag") {
      saw_flag_write = true;
    }
    if (node.name == "config_.program") {
      saw_program = true;
    }
    for (const tuide::ACausalFlowNode& child : node.children) {
      self(self, child);
    }
    stack.pop_back();
  };
  for (const tuide::ACausalFlowNode& child : tree.root.children) {
    walk(walk, child);
  }
  expect(saw_condition, "guard names the if");
  expect(saw_flag_write, "flag_ climbs to set_flag");
  expect(saw_program, "ready climbs to config_.program");
  (void)stack;

  std::filesystem::remove_all(root);
}

}  // namespace

int main() {
  test_generic_trail_real_depth();
  test_no_detached_case_specific_branches();
  test_trap_l0_drops_unlinked_cxl();
  test_causal_flow_write_caller_chain();
  test_causal_flow_call_climbs_condition();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "l2_trail_build_test OK\n";
  return 0;
}
