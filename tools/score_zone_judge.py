#!/usr/bin/env python3
"""Score persisted zone-judge decisions; golds are used only after LLM inference."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
OPERATIONAL_EXTRA = {
    "13_lsp_auto_restart": ["lsp_symbol_provider"],
    "20_cancel_ai_generation": ["busy_strip"],
}


ZONE_HEADER_RE = re.compile(r"^##\s+(M\d+)\b")
TARGET_STEM_RE = re.compile(r"\b([A-Za-z0-9_]+)::")


def parse_judge_cards(cards: str) -> dict[str, Any]:
    """Parse card structure without consulting benchmark golds or traps."""
    zones: list[dict[str, Any]] = []
    zones_by_id: dict[str, dict[str, Any]] = {}
    uncovered_stems: set[str] = set()
    current_zone: dict[str, Any] | None = None
    in_uncovered = False

    for line in cards.splitlines():
        header = ZONE_HEADER_RE.match(line)
        if header:
            zone_id = header.group(1)
            current_zone = {
                "id": zone_id,
                "rank": len(zones) + 1,
                "stems_declared": set(),
                "stems_evidence": set(),
            }
            zones.append(current_zone)
            zones_by_id[zone_id] = current_zone
            in_uncovered = False
            continue
        if line.startswith("## "):
            # Any non-zone section closes the preceding zone. In particular,
            # uncovered seeds must never become evidence for the final zone.
            current_zone = None
            in_uncovered = line.strip().lower() == "## uncovered seeds"
            continue
        if in_uncovered:
            uncovered_stems.update(TARGET_STEM_RE.findall(line))
            continue
        if current_zone is None:
            continue
        if line.startswith("stems:"):
            current_zone["stems_declared"].update(line[6:].split())
        else:
            current_zone["stems_evidence"].update(TARGET_STEM_RE.findall(line))

    return {
        "zones": zones,
        "zones_by_id": zones_by_id,
        "uncovered_stems": uncovered_stems,
    }


def stems_by_zone(cards: str) -> dict[str, set[str]]:
    """Compatibility view: all declared and evidenced stems by zone."""
    parsed = parse_judge_cards(cards)
    return {
        zone["id"]: set(zone["stems_declared"]) | set(zone["stems_evidence"])
        for zone in parsed["zones"]
    }


def score_judge_cards(
    cards: str,
    gold_stems: list[str],
    trap_stems: list[str],
    selected: list[str],
) -> dict[str, Any]:
    """Compute post-inference card/selection metrics by evidence layer."""
    parsed = parse_judge_cards(cards)
    gold = {stem.lower() for stem in gold_stems if stem}
    traps = {stem.lower() for stem in trap_stems if stem}

    def normalized(values: set[str]) -> set[str]:
        return {value.lower() for value in values}

    zone_rows: list[dict[str, Any]] = []
    for zone in parsed["zones"]:
        declared = normalized(zone["stems_declared"])
        evidence = normalized(zone["stems_evidence"])
        all_stems = declared | evidence
        gold_declared = declared & gold
        gold_evidence = evidence & gold
        gold_all = all_stems & gold
        trap_all = all_stems & traps
        declared_purity = len(gold_declared) / len(declared) if declared else None
        zone_rows.append(
            {
                "id": zone["id"],
                "rank": zone["rank"],
                "stems_declared": sorted(declared),
                "stems_evidence": sorted(evidence),
                "gold_declared": sorted(gold_declared),
                "gold_evidence": sorted(gold_evidence),
                "gold": sorted(gold_all),
                "traps": sorted(trap_all),
                "declared_stem_count": len(declared),
                "mixed_declared": len(declared) > 1,
                "approx_gold_purity": declared_purity,
            }
        )

    uncovered = normalized(parsed["uncovered_stems"])
    gold_uncovered = uncovered & gold
    gold_zone_rows = [zone for zone in zone_rows if zone["gold"]]
    first_gold = gold_zone_rows[0] if gold_zone_rows else None
    selected_ids = set(selected)
    selected_rows = [zone for zone in zone_rows if zone["id"] in selected_ids]
    selected_stems = {
        stem
        for zone in selected_rows
        for stem in zone["stems_declared"] + zone["stems_evidence"]
    }
    selected_declared = {
        stem for zone in selected_rows for stem in zone["stems_declared"]
    }
    selected_gold = selected_stems & gold

    return {
        "zones": zone_rows,
        "uncovered_stems": sorted(uncovered),
        "gold_in_zones": bool(gold_zone_rows),
        "gold_zone_ids": [zone["id"] for zone in gold_zone_rows],
        "gold_declared_zone_ids": [
            zone["id"] for zone in zone_rows if zone["gold_declared"]
        ],
        "gold_evidence_zone_ids": [
            zone["id"] for zone in zone_rows if zone["gold_evidence"]
        ],
        "gold_in_uncovered": bool(gold_uncovered),
        "gold_uncovered": sorted(gold_uncovered),
        "gold_only_uncovered": bool(gold_uncovered) and not gold_zone_rows,
        "first_gold_zone": first_gold["id"] if first_gold else None,
        "first_gold_zone_rank": first_gold["rank"] if first_gold else None,
        "mixed_zone_count": sum(zone["mixed_declared"] for zone in zone_rows),
        "mean_declared_stems_per_zone": (
            sum(zone["declared_stem_count"] for zone in zone_rows) / len(zone_rows)
            if zone_rows
            else 0.0
        ),
        "first_gold_zone_approx_purity": (
            first_gold["approx_gold_purity"] if first_gold else None
        ),
        "selected_stems": sorted(selected_stems),
        "selected_declared_stems": sorted(selected_declared),
        "selected_gold": sorted(selected_gold),
        "selected_gold_any": bool(selected_gold),
        "selected_gold_full": bool(gold) and gold <= selected_stems,
        "selected_trap": bool(selected_stems & traps),
        "selected_approx_purity": (
            len(selected_declared & gold) / len(selected_declared)
            if selected_declared
            else None
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--round-dir", required=True)
    args = parser.parse_args()
    round_dir = Path(args.round_dir)
    if not round_dir.is_absolute():
        round_dir = ROOT / round_dir
    cases = json.loads(PROMPTS.read_text(encoding="utf-8"))
    rows = []
    for case in cases:
        case_id = case["id"]
        case_dir = round_dir / case_id
        try:
            decision = json.loads((case_dir / "decision.json").read_text(encoding="utf-8"))
            cards = (case_dir / "cards.md").read_text(encoding="utf-8")
        except (OSError, json.JSONDecodeError):
            rows.append({"id": case_id, "ok": False, "error": "missing_artifact"})
            continue
        try:
            triage = json.loads((case_dir / "triage.json").read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            triage = {}
        triage_shortlist = [str(value) for value in triage.get("shortlist") or []]
        selected = [str(value) for value in decision.get("selected") or []]
        repaired_primary = bool(selected) and decision.get("error") == (
            "debe haber una única primary si hay selección"
        )
        ok = bool(decision.get("ok")) or repaired_primary
        expected = [str(value) for value in case.get("expected_stems") or []]
        operational = list(dict.fromkeys(expected + OPERATIONAL_EXTRA.get(case_id, [])))
        traps = [str(value) for value in case.get("trap_stems") or []]
        layers = score_judge_cards(cards, expected, traps, selected)
        operational_layers = score_judge_cards(cards, operational, traps, selected)
        selected_stems = set(layers["selected_declared_stems"])
        available_stems = {
            stem
            for zone in layers["zones"]
            for stem in zone["stems_declared"]
        }
        expected_zones = layers["gold_declared_zone_ids"]
        triage_gold = bool(set(triage_shortlist) & set(expected_zones))
        any_hit = ok and any(stem in selected_stems for stem in expected)
        full_hit = ok and bool(expected) and all(stem in selected_stems for stem in expected)
        available_any = any(stem in available_stems for stem in expected)
        available_full = bool(expected) and all(stem in available_stems for stem in expected)
        op_hit = ok and any(stem in selected_stems for stem in operational)
        trap = ok and any(stem in selected_stems for stem in traps)
        rows.append(
            {
                "id": case_id,
                "ok": ok,
                "repaired_primary": repaired_primary,
                "selected": selected,
                "triage_shortlist": triage_shortlist,
                "triage_gold": triage_gold,
                "selected_stems": sorted(selected_stems),
                "expected_zones": expected_zones,
                "any_hit": any_hit,
                "full_hit": full_hit,
                "available_any": available_any,
                "available_full": available_full,
                "operational_hit": op_hit,
                "trap": trap,
                "card_layers": layers,
                "operational_card_layers": operational_layers,
                "empty": ok and not selected,
                "error": "" if ok else decision.get("error", ""),
            }
        )
    layer_rows = [row["card_layers"] for row in rows if row.get("card_layers")]
    first_gold_purities = [
        layer["first_gold_zone_approx_purity"]
        for layer in layer_rows
        if layer.get("first_gold_zone_approx_purity") is not None
    ]
    selected_purities = [
        layer["selected_approx_purity"]
        for layer in layer_rows
        if layer.get("selected_approx_purity") is not None
    ]
    summary = {
        "total": len(rows),
        "valid": sum(bool(row.get("ok")) for row in rows),
        "repaired_primary": sum(bool(row.get("repaired_primary")) for row in rows),
        "any_hit": sum(bool(row.get("any_hit")) for row in rows),
        "full_hit": sum(bool(row.get("full_hit")) for row in rows),
        "available_any": sum(bool(row.get("available_any")) for row in rows),
        "available_full": sum(bool(row.get("available_full")) for row in rows),
        "operational_hit": sum(bool(row.get("operational_hit")) for row in rows),
        "triage_cases": sum(bool(row.get("triage_shortlist")) for row in rows),
        "triage_recall": sum(bool(row.get("triage_gold")) for row in rows),
        "trap": sum(bool(row.get("trap")) for row in rows),
        "empty": sum(bool(row.get("empty")) for row in rows),
        "gold_in_zones": sum(
            bool((row.get("card_layers") or {}).get("gold_in_zones")) for row in rows
        ),
        "gold_declared_in_zones": sum(
            bool((row.get("card_layers") or {}).get("gold_declared_zone_ids"))
            for row in rows
        ),
        "gold_evidence_in_zones": sum(
            bool((row.get("card_layers") or {}).get("gold_evidence_zone_ids"))
            for row in rows
        ),
        "gold_in_uncovered": sum(
            bool((row.get("card_layers") or {}).get("gold_in_uncovered"))
            for row in rows
        ),
        "gold_only_uncovered": sum(
            bool((row.get("card_layers") or {}).get("gold_only_uncovered")) for row in rows
        ),
        "selected_gold_any": sum(
            bool(row.get("ok"))
            and bool((row.get("card_layers") or {}).get("selected_gold_any"))
            for row in rows
        ),
        "selected_gold_full": sum(
            bool(row.get("ok"))
            and bool((row.get("card_layers") or {}).get("selected_gold_full"))
            for row in rows
        ),
        "selected_trap": sum(
            bool(row.get("ok"))
            and bool((row.get("card_layers") or {}).get("selected_trap"))
            for row in rows
        ),
        "mixed_zone_total": sum(
            int(layer.get("mixed_zone_count") or 0) for layer in layer_rows
        ),
        "first_gold_zone_approx_purity_mean": (
            sum(first_gold_purities) / len(first_gold_purities)
            if first_gold_purities
            else None
        ),
        "selected_approx_purity_mean": (
            sum(selected_purities) / len(selected_purities)
            if selected_purities
            else None
        ),
        "first_gold_zone_ranks": {
            row["id"]: row["card_layers"]["first_gold_zone_rank"]
            for row in rows
            if (row.get("card_layers") or {}).get("first_gold_zone_rank") is not None
        },
        "miss_available_ids": [
            row["id"] for row in rows if row.get("available_any") and not row.get("any_hit")
        ],
        "unavailable_ids": [row["id"] for row in rows if not row.get("available_any")],
        "trap_ids": [row["id"] for row in rows if row.get("trap")],
        "empty_ids": [row["id"] for row in rows if row.get("empty")],
    }
    (round_dir / "rescored.json").write_text(
        json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (round_dir / "rescored_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
