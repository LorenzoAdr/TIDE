#!/usr/bin/env python3
"""Score causal-card recall before any LLM decision."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.score_zone_judge import PROMPTS, score_judge_cards  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cards-root", required=True)
    parser.add_argument("--out", default="")
    args = parser.parse_args()
    cards_root = Path(args.cards_root)
    if not cards_root.is_absolute():
        cards_root = ROOT / cards_root
    rows = []
    for case in json.loads(PROMPTS.read_text(encoding="utf-8")):
        case_id = str(case["id"])
        try:
            cards = (cards_root / case_id / "judge_cards.md").read_text(encoding="utf-8")
        except OSError:
            rows.append({"id": case_id, "ok": False, "error": "missing_cards"})
            continue
        layers = score_judge_cards(
            cards,
            [str(value) for value in case.get("expected_stems") or []],
            [str(value) for value in case.get("trap_stems") or []],
            [],
        )
        rows.append(
            {
                "id": case_id,
                "ok": True,
                "declared": bool(layers["gold_declared_zone_ids"]),
                "evidence": bool(layers["gold_evidence_zone_ids"]),
                "uncovered": layers["gold_in_uncovered"],
                "only_uncovered": layers["gold_only_uncovered"],
                "first_rank": layers["first_gold_zone_rank"],
                "gold_zones": layers["gold_zone_ids"],
                "mixed": layers["mixed_zone_count"],
            }
        )
    summary = {
        "total": len(rows),
        "valid": sum(bool(row.get("ok")) for row in rows),
        "declared_recall": sum(bool(row.get("declared")) for row in rows),
        "evidence_recall": sum(bool(row.get("evidence")) for row in rows),
        "uncovered_recall": sum(bool(row.get("uncovered")) for row in rows),
        "only_uncovered": sum(bool(row.get("only_uncovered")) for row in rows),
        "mixed_zone_total": sum(int(row.get("mixed") or 0) for row in rows),
        "missing_declared": [row["id"] for row in rows if row.get("ok") and not row.get("declared")],
    }
    output = {"summary": summary, "rows": rows}
    text = json.dumps(output, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        out = Path(args.out)
        if not out.is_absolute():
            out = ROOT / out
        out.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
