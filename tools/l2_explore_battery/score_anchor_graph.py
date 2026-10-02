#!/usr/bin/env python3
"""Score anchor-hunt graph quality: map top-K + registry hop0 seeds."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.l2_explore_battery.pf_battery_lib import (  # noqa: E402
    DEFAULT_CASES,
    gold_stems,
    load_cases,
    load_json,
    map_entry_stems,
    pf_search_terms,
    stem_hit,
    trap_stems,
)


def hop0_stems(data: dict) -> list[str]:
    out: list[str] = []
    for h in data.get("seeds") or []:
        if not isinstance(h, dict):
            continue
        _append_hop_stem(out, h)
    for h in data.get("hits") or []:
        if not isinstance(h, dict):
            continue
        if int(h.get("hop") or 0) != 0:
            continue
        _append_hop_stem(out, h)
    return out


def _append_hop_stem(out: list[str], h: dict) -> None:
    for key in ("stem", "symbol"):
        v = str(h.get(key) or "").strip().lower()
        if v and v not in out:
            out.append(v)
    path = str(h.get("path") or "")
    if path:
        st = Path(path.replace("\\", "/")).stem.lower()
        if st and st not in out:
            out.append(st)


def score_case(case: dict, case_dir: Path) -> dict:
    pf = load_json(case_dir / "problem_frame.json")
    map_text = (case_dir / "map_last.md").read_text(errors="replace") if (case_dir / "map_last.md").exists() else ""
    reg = load_json(case_dir / "registry_hop0.json")

    gold = gold_stems(case)
    traps = trap_stems(case)
    terms = pf_search_terms(pf)

    ranked = map_entry_stems(map_text, top_n=15)
    map_stems = [st for _, _, st in ranked]
    map_paths = [p for _, p, _ in ranked]

    gold_rank = None
    for rank, path, stem in ranked:
        if any(stem_hit(g, [stem, path]) for g in gold):
            gold_rank = rank
            break

    hop0 = hop0_stems(reg)
    hop0_hit = any(stem_hit(g, hop0) for g in gold) if gold else bool(hop0)
    terms_in_map_query = bool(terms)  # map was built with L1 pipeline

    trap_top3 = any(
        t.lower() == stem.lower() or t.lower() == path.lower()
        for t in traps
        for _, path, stem in ranked[:3]
    )

    row = {
        "id": case.get("id"),
        "mandatory": case.get("id") in ("17_ai_spinner_stuck", "20_cancel_ai_generation"),
        "pf_terms_n": len(terms),
        "gold_in_map_top15": gold_rank is not None,
        "gold_map_rank": gold_rank,
        "hop0_gold_hit": hop0_hit,
        "trap_in_map_top3": trap_top3,
        "map_entries": len(ranked),
        "hop0_entries": len(hop0),
        "query_terms": terms,
    }
    trap_blocks = trap_top3 and (gold_rank is None or gold_rank > 3)
    row["pass"] = (
        row["pf_terms_n"] >= 1
        and row["gold_in_map_top15"]
        and row["hop0_gold_hit"]
        and not trap_blocks
    )
    return row


def summarize(rows: list[dict]) -> dict:
    n = len(rows)
    mandatory = [r for r in rows if r.get("mandatory")]
    return {
        "n": n,
        "gold_in_map_top15": sum(bool(r.get("gold_in_map_top15")) for r in rows),
        "hop0_gold_hit": sum(bool(r.get("hop0_gold_hit")) for r in rows),
        "pass": sum(bool(r.get("pass")) for r in rows),
        "mandatory_pass": sum(bool(r.get("pass")) for r in mandatory),
        "mandatory_n": len(mandatory),
        "map_miss": [r["id"] for r in rows if not r.get("gold_in_map_top15")],
        "hop0_miss": [r["id"] for r in rows if not r.get("hop0_gold_hit")],
    }


def gate_2(summary: dict) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    n = int(summary.get("n") or 0)
    if n < 5:
        reasons.append(f"incomplete battery n={n}/5")
    if int(summary.get("gold_in_map_top15") or 0) < max(4, n - 1):
        reasons.append("gold_in_map_top15 < 4")
    if int(summary.get("hop0_gold_hit") or 0) < max(4, n - 1):
        reasons.append("hop0_gold_hit < 4")
    if int(summary.get("mandatory_pass") or 0) < int(summary.get("mandatory_n") or 0):
        reasons.append("mandatory 17/20 failed")
    return (not reasons, reasons)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--round-dir", type=Path, required=True)
    ap.add_argument("--cases", type=Path, default=DEFAULT_CASES)
    ap.add_argument("--check-gate", action="store_true")
    args = ap.parse_args()

    cases = load_cases(args.cases)
    rows = []
    for case in cases:
        cid = case.get("id")
        if not cid:
            continue
        case_dir = args.round_dir / cid
        if not case_dir.exists():
            rows.append({"id": cid, "pass": False, "error": "missing_case_dir"})
            continue
        rows.append(score_case(case, case_dir))

    summary = summarize(rows)
    out = {"rows": rows, "summary": summary}
    gate = gate_2(summary)
    out["gate_2"] = {"pass": gate[0], "reasons": gate[1]}
    out_path = args.round_dir / "anchor_graph_score.json"
    out_path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": summary, "gate_2": out["gate_2"]}, indent=2))
    if args.check_gate and not gate[0]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
