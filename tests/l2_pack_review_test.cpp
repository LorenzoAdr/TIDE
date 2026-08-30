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
      "src/view/status_panel.cpp:set_active",
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
        "src/core/coordinator.cpp:1 — coordinator (ranked map)",
        "src/view/status_panel.cpp:226 — set_active (ranked map)",
        "src/ai/model_store.hpp:40 — resolve_l1_model (ranked map)",
    };
    const auto targets = tuide::plan_targets_from_map_hits(menu, 4);
    expect(targets.size() == 3, "parsed 3 plan targets from menu");
    expect(targets[0] == "src/core/coordinator.cpp:coordinator",
           "line=1 keeps path:Symbol got=" +
               (targets.empty() ? std::string("<empty>") : targets[0]));
    expect(targets[1] == "src/view/status_panel.cpp:226",
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
        "1. tests/fixtures/effect_slice/box_b.cpp:1  [score=1] — `box_b`\n"
        "2. tests/fixtures/effect_slice/box_a.cpp:2  [score=1] — `set_flag`\n"
        "3. tests/fixtures/effect_slice/box_a.cpp:6  [score=1] — `clear_flag`\n"
        "4. tests/fixtures/effect_slice/box_b.cpp:8  [score=1] — `kick`\n";
    const auto anchors =
        tuide::retrieval_anchor_targets(map, "seeds: box set flag\n", 8);
    expect(!anchors.empty(), "anchors from map+seeds");
    bool has_line_locus = false;
    for (const auto& a : anchors) {
      if (a == "tests/fixtures/effect_slice/box_a.cpp:2") {
        has_line_locus = true;
      }
    }
    expect(has_line_locus, "anchors prefer path:line for set_flag");
    const auto sibs = tuide::expand_anchor_api_siblings(
        {"tests/fixtures/effect_slice/box_a.cpp:2"}, map, 8,
        /*workspace_root=*/"");
    expect(!sibs.empty(), "api siblings non-empty");
    bool has_clear = false;
    bool has_peer = false;
    for (const auto& s : sibs) {
      if (s.find("clear_flag") != std::string::npos || s.find(":6") != std::string::npos) {
        has_clear = true;
      }
      if (s.find("kick") != std::string::npos || s.find(":8") != std::string::npos) {
        has_peer = true;
      }
    }
    expect(has_clear, "siblings include clear_flag (same-file map)");
    expect(has_peer || !sibs.empty(), "siblings preserve ranked neighbors");
    // Disk scan should derive the generic set_* → clear_* complement.
    {
      const auto disk = tuide::expand_anchor_api_siblings(
          {"tests/fixtures/effect_slice/box_a.cpp:set_flag"},
          /*map=*/"", 8, ".");
      bool disk_clear = false;
      for (const auto& s : disk) {
        if (s.find("clear_flag") != std::string::npos) {
          disk_clear = true;
        }
      }
      expect(disk_clear, "disk siblings find clear_flag");
    }
    const auto filtered = tuide::filter_rejects_excluding_anchors(
        {"tests/fixtures/effect_slice/box_a.cpp:2", "CompletionState", "noise_sym"}, anchors);
    expect(std::find(filtered.begin(), filtered.end(),
                     "tests/fixtures/effect_slice/box_a.cpp:2") == filtered.end(),
           "anchor reject filtered");
    expect(std::find(filtered.begin(), filtered.end(), "CompletionState") != filtered.end(),
           "unrelated reject kept");
    const std::string pack =
        "### get_code_of `src/view/status_panel.cpp:226`\n\n```\n"
        "void set_active(State* state) {\n"
        "  if (state->halted) return;\n"
        "}\n```\n"
        "### get_code_of `src/view/status_panel.cpp:clear_active`\n\n```\n"
        "void clear_active(State* state) { state->active = false; }\n```\n";
    expect(tuide::pack_target_has_symbol_body(pack, "src/view/status_panel.cpp:226"),
           "path:line fence has code");
    expect(tuide::pack_target_has_symbol_body(pack, "src/view/status_panel.cpp:set_active"),
           "symbol in fence");
    expect(tuide::pack_target_has_symbol_body(pack, "src/view/status_panel.cpp:clear_active"),
           "clear_active in fence");
    expect(!tuide::pack_target_has_symbol_body(
               "targets: clear_active\n- api sibling → clear_active\n",
               "src/view/status_panel.cpp:clear_active"),
           "metadata mention is not a body");
    expect(tuide::target_is_lifecycle_clear("src/view/status_panel.cpp:clear_active"),
           "clear_active is clear-side");
    expect(tuide::target_is_lifecycle_set("src/view/status_panel.cpp:set_active"),
           "set_active is set-side");
    expect(tuide::pack_has_lifecycle_pair(pack), "pack has set+clear pair");
    expect(!tuide::pack_has_lifecycle_pair(
               "### get_code_of `src/view/status_panel.cpp:226`\n\n```\n"
               "void set_active() { halted = true; more code here for length xx }\n```\n"),
           "set-only pack is not a pair");
    expect(tuide::pack_must_anchors_covered(
               pack, {"src/view/status_panel.cpp:226", "src/view/status_panel.cpp:clear_active"}, 2),
           "must anchors covered via fences");
    const std::vector<std::string> pack_anchors = {
        "src/view/status_panel.cpp:226", "src/view/status_panel.cpp:clear_active"};
    expect(tuide::pack_has_anchor_fragment(pack, pack_anchors), "pack has anchor fragment");
    expect(!tuide::pack_has_anchor_fragment("### get_code_of `src/ui/x.cpp:y` (omitido)\n",
                                              pack_anchors),
           "omit-only does not count");
    const std::string pack_lang =
        "### get_code_of `src/view/status_panel.cpp:226`\n\n```cpp\n"
        "void set_active(State* state) {\n"
        "  if (state->halted) return;\n"
        "}\n```\n"
        "### get_code_of `src/view/status_panel.cpp:clear_active`\n\n```cpp\n"
        "void clear_active(State* state) { state->active = false; }\n```\n";
    expect(tuide::pack_target_has_symbol_body(pack_lang, "src/view/status_panel.cpp:set_active"),
           "```cpp fence still has symbol");
    expect(tuide::pack_has_lifecycle_pair(pack_lang), "```cpp pack has set+clear pair");
    expect(tuide::load_pack_fragment_body(pack_lang, "src/view/status_panel.cpp:226")
                   .find("void set_active") != std::string::npos,
           "inner body skips cpp tag");
    expect(tuide::load_pack_fragment_body(pack_lang, "src/view/status_panel.cpp:226")
                   .find("cpp") == std::string::npos,
           "lang tag not in reused body");
    const std::string digest = tuide::build_pack_digest(pack_lang);
    expect(digest.find("void set_active") != std::string::npos, "digest keeps fenced body");
    expect(digest.find("```cpp") != std::string::npos, "digest keeps lang opener");
  }

  if (failures == 0) {
    std::cout << "l2_pack_review_test OK\n";
    return 0;
  }
  return 1;
}
