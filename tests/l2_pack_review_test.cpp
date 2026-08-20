#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "ai/l2_pack_review.hpp"

using tuide::all_plan_target_paths_in_watchlist;
using tuide::expand_review_rejects_for_watchlist;
using tuide::target_in_rejected_normalized;
using tuide::target_in_watchlist_normalized;

static int failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++failures;
  }
}

int main() {
  const std::vector<std::string> watchlist = {
      "src/lsp/lsp_client.cpp:CompletionState",
      "src/ui/console_panel.cpp:3090",
      "src/ui/busy_strip.cpp:set_busy_spinner",
  };

  {
    expect(target_in_watchlist_normalized("src/lsp/lsp_client.cpp:CompletionState", watchlist),
           "exact watchlist match");
    expect(target_in_watchlist_normalized("src/lsp/lsp_client.cpp:cancel_completion_fetch",
                                          watchlist),
           "same file different symbol counts as seen");
    expect(!target_in_watchlist_normalized("src/ai/ai_controller.cpp:cancel_all", watchlist),
           "unseen path is new");
  }

  {
    expect(target_in_rejected_normalized("CompletionState", {"CompletionState"}),
           "bare symbol reject match");
    expect(target_in_rejected_normalized("src/lsp/lsp_client.cpp:CompletionState",
                                         {"CompletionState"}),
           "watchlist entry matches bare symbol reject");
  }

  {
    const auto expanded =
        expand_review_rejects_for_watchlist({"CompletionState", "LSP completion"}, watchlist);
    expect(expanded.size() >= 2, "expanded rejects include originals");
    bool has_lsp = false;
    for (const auto& e : expanded) {
      if (e.find("lsp_client.cpp") != std::string::npos) {
        has_lsp = true;
      }
    }
    expect(has_lsp, "symbol reject expands to watchlist path:Symbol");
  }

  {
    const std::vector<std::string> repeat_plan = {
        "src/lsp/lsp_client.cpp:cancel_completion_fetch",
        "src/ui/console_panel.cpp:3090",
    };
    expect(all_plan_target_paths_in_watchlist(repeat_plan, watchlist),
           "repeat plan paths all seen");
    const std::vector<std::string> mixed_plan = {
        "src/ai/ai_controller.cpp:cancel_all",
        "src/ui/console_panel.cpp:3090",
    };
    expect(!all_plan_target_paths_in_watchlist(mixed_plan, watchlist),
           "mixed plan has unseen path");
  }

  {
    // Menu separator is UTF-8 em-dash (" — ", 5 bytes). Parsing must not slice mid-codepoint.
    // Prefer path:line when the menu locus has line > 1 (stable fetch window).
    const std::vector<std::string> menu = {
        "src/ai/ai_controller.cpp:1 — ai_controller (ranked map)",
        "src/ui/busy_strip.cpp:226 — set_busy_spinner (ranked map)",
        "src/ai/model_store.hpp:40 — resolve_l1_model (ranked map)",
    };
    const auto targets = tuide::plan_targets_from_map_hits(menu, 4);
    expect(targets.size() == 3, "parsed 3 plan targets from menu");
    expect(targets[0] == "src/ai/ai_controller.cpp:ai_controller",
           "line=1 keeps path:Symbol got=" +
               (targets.empty() ? std::string("<empty>") : targets[0]));
    expect(targets[1] == "src/ui/busy_strip.cpp:226",
           "line>1 prefers path:line got=" +
               (targets.size() < 2 ? std::string("<missing>") : targets[1]));
    expect(targets[2] == "src/ai/model_store.hpp:40",
           "second line locus path:line got=" +
               (targets.size() < 3 ? std::string("<missing>") : targets[2]));
    for (const auto& t : targets) {
      expect(t.find('\x94') == std::string::npos, "no stray UTF-8 continuation in " + t);
    }
  }

  {
    const std::string map =
        "## Ranked entries\n"
        "1. src/ai/ai_controller.cpp:1  [score=1] — `ai_controller`\n"
        "2. src/ui/busy_strip.cpp:226  [score=1] — `set_busy_spinner`\n"
        "3. src/ui/busy_strip.cpp:289  [score=1] — `clear_busy`\n"
        "4. src/ai/ai_controller.hpp:90  [score=1] — `agent_busy`\n";
    const auto anchors =
        tuide::retrieval_anchor_targets(map, "seeds: ai_controller set_busy_spinner\n", 8);
    expect(!anchors.empty(), "anchors from map+seeds");
    bool has_line_locus = false;
    for (const auto& a : anchors) {
      if (a == "src/ui/busy_strip.cpp:226") {
        has_line_locus = true;
      }
    }
    expect(has_line_locus, "anchors prefer path:line for set_busy_spinner");
    const auto sibs = tuide::expand_anchor_api_siblings(
        {"src/ui/busy_strip.cpp:226", "src/ai/ai_controller.cpp:ai_controller"}, map, 8,
        /*workspace_root=*/"");
    expect(!sibs.empty(), "api siblings non-empty");
    bool has_clear = false;
    bool has_agent = false;
    for (const auto& s : sibs) {
      if (s.find("clear_busy") != std::string::npos || s.find(":289") != std::string::npos) {
        has_clear = true;
      }
      if (s.find("agent_busy") != std::string::npos || s.find(":90") != std::string::npos) {
        has_agent = true;
      }
    }
    expect(has_clear, "siblings include clear_busy (same-file map)");
    expect(has_agent, "siblings include agent_busy (same-file map)");
    // Disk scan (workspace) should find clear_busy / cancel_all even if map omits them.
    {
      const auto disk = tuide::expand_anchor_api_siblings(
          {"src/ui/busy_strip.cpp:226", "src/ai/ai_controller.hpp:90"},
          /*map=*/"", 8, ".");
      bool disk_clear = false;
      bool disk_cancel = false;
      for (const auto& s : disk) {
        if (s.find("clear_busy") != std::string::npos) {
          disk_clear = true;
        }
        if (s.find("cancel_all") != std::string::npos ||
            s.find("cancel_current") != std::string::npos) {
          disk_cancel = true;
        }
      }
      expect(disk_clear, "disk siblings find clear_busy");
      expect(disk_cancel, "disk siblings find cancel_* on controller");
    }
    const auto filtered = tuide::filter_rejects_excluding_anchors(
        {"ai_controller", "CompletionState", "noise_sym"}, anchors);
    expect(std::find(filtered.begin(), filtered.end(), "ai_controller") == filtered.end(),
           "ai_controller reject filtered by anchors");
    expect(std::find(filtered.begin(), filtered.end(), "CompletionState") != filtered.end(),
           "unrelated reject kept");
    const std::string pack =
        "### get_code_of `src/ui/busy_strip.cpp:226`\n\n```\n"
        "void set_busy_spinner(MainLayoutState* layout, BusyActivity activity) {\n"
        "  if (layout->busy_strip->halted) return;\n"
        "}\n```\n"
        "### get_code_of `src/ui/busy_strip.cpp:clear_busy`\n\n```\n"
        "void clear_busy(MainLayoutState* layout) { layout->busy_strip->active = false; }\n```\n";
    expect(tuide::pack_target_has_symbol_body(pack, "src/ui/busy_strip.cpp:226"),
           "path:line fence has code");
    expect(tuide::pack_target_has_symbol_body(pack, "src/ui/busy_strip.cpp:set_busy_spinner"),
           "symbol in fence");
    expect(tuide::pack_target_has_symbol_body(pack, "src/ui/busy_strip.cpp:clear_busy"),
           "clear_busy in fence");
    expect(!tuide::pack_target_has_symbol_body(
               "targets: clear_busy\n- api sibling → clear_busy\n",
               "src/ui/busy_strip.cpp:clear_busy"),
           "metadata mention is not a body");
    expect(tuide::target_is_lifecycle_clear("src/ui/busy_strip.cpp:clear_busy"),
           "clear_busy is clear-side");
    expect(tuide::target_is_lifecycle_set("src/ui/busy_strip.cpp:set_busy_spinner"),
           "set_busy_spinner is set-side");
    expect(tuide::pack_has_lifecycle_pair(pack), "pack has set+clear pair");
    expect(!tuide::pack_has_lifecycle_pair(
               "### get_code_of `src/ui/busy_strip.cpp:226`\n\n```\n"
               "void set_busy_spinner() { halted = true; more code here for length xx }\n```\n"),
           "set-only pack is not a pair");
    expect(tuide::pack_must_anchors_covered(
               pack, {"src/ui/busy_strip.cpp:226", "src/ui/busy_strip.cpp:clear_busy"}, 2),
           "must anchors covered via fences");
    expect(tuide::pack_has_anchor_fragment(pack, anchors), "pack has busy_strip fragment");
    expect(!tuide::pack_has_anchor_fragment("### get_code_of `src/ui/x.cpp:y` (omitido)\n",
                                              anchors),
           "omit-only does not count");
  }

  if (failures == 0) {
    std::cout << "l2_pack_review_test OK\n";
    return 0;
  }
  return 1;
}
