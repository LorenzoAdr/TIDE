#!/usr/bin/env python3
"""Tests for deterministic causal-judge card parsing and scoring."""

from __future__ import annotations

import unittest

from tools.score_zone_judge import parse_judge_cards, score_judge_cards


CARDS = """\
# causal_judge_v1
query: add a temp tab

## M1 score=1
stems: console_panel raw_pty_screen
anchors:
- G1: console_panel::handle_mouse[q1]
causal edges:
- console_panel::handle_mouse -call-> ui_wake::wake_console

## M5 score=0.5
stems: binary_symbols_panel
mini-cards:
- raw_pty_screen::text | text()

## uncovered seeds
- q4 main_layout::ConsolePanelTabs
- q5 diagnostics_panel::terminal_lines

<!-- budget -->
"""


class JudgeCardsParserTest(unittest.TestCase):
    def test_uncovered_section_is_not_attributed_to_last_zone(self) -> None:
        parsed = parse_judge_cards(CARDS)

        self.assertEqual(["M1", "M5"], [zone["id"] for zone in parsed["zones"]])
        m1 = parsed["zones_by_id"]["M1"]
        m5 = parsed["zones_by_id"]["M5"]
        self.assertEqual({"console_panel", "raw_pty_screen"}, m1["stems_declared"])
        self.assertEqual({"console_panel", "ui_wake"}, m1["stems_evidence"])
        self.assertEqual({"binary_symbols_panel"}, m5["stems_declared"])
        self.assertEqual({"raw_pty_screen"}, m5["stems_evidence"])
        self.assertNotIn("main_layout", m5["stems_evidence"])
        self.assertEqual(
            {"main_layout", "diagnostics_panel"}, parsed["uncovered_stems"]
        )

    def test_scores_layers_and_selection_without_mutating_parse(self) -> None:
        score = score_judge_cards(
            CARDS,
            gold_stems=["main_layout"],
            trap_stems=["binary_symbols_panel"],
            selected=["M5"],
        )

        self.assertFalse(score["gold_in_zones"])
        self.assertTrue(score["gold_in_uncovered"])
        self.assertTrue(score["gold_only_uncovered"])
        self.assertIsNone(score["first_gold_zone"])
        self.assertFalse(score["selected_gold_any"])
        self.assertTrue(score["selected_trap"])

    def test_first_gold_zone_mixing_and_approximate_purity(self) -> None:
        score = score_judge_cards(
            CARDS,
            gold_stems=["console_panel"],
            trap_stems=[],
            selected=["M1"],
        )

        self.assertEqual("M1", score["first_gold_zone"])
        self.assertEqual(1, score["first_gold_zone_rank"])
        self.assertEqual(1, score["mixed_zone_count"])
        self.assertEqual(0.5, score["first_gold_zone_approx_purity"])
        self.assertEqual(0.5, score["selected_approx_purity"])
        self.assertTrue(score["selected_gold_any"])


    def test_core_context_layers_when_requested(self) -> None:
        cards = """\
# causal_judge_v1
## M1 score=1
stems: trap_stem
core stems: gold_core
context stems: gold_ctx
mini-cards:
- gold_core::writer | void writer()
"""
        primary_only = score_judge_cards(
            cards, gold_stems=["gold_core"], trap_stems=[], selected=[]
        )
        extended = score_judge_cards(
            cards,
            gold_stems=["gold_core"],
            trap_stems=[],
            selected=[],
            include_core_context=True,
        )
        self.assertFalse(primary_only["gold_declared_zone_ids"])
        self.assertEqual(["M1"], extended["gold_declared_zone_ids"])


if __name__ == "__main__":
    unittest.main()
