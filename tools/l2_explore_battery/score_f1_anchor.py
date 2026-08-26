#!/usr/bin/env python3
"""Score F1 anchor-hunt battery (explore_f1_ok / anchor_miss_v1)."""
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
    stem_hit,
)


def score_case(case: dict, case_dir: Path) -> dict:
    st = load_json(case_dir / "state.json")
    ast = load_json(case_dir / "a_state.json")
    log = (case_dir / "run.log").read_text(errors="replace") if (case_dir / "run.log").exists() else ""
    pf = load_json(case_dir / "problem_frame.json")

    phase = st.get("phase") or ""
    anchor = ast.get("anchor_confirmed") or ""
    loci = ast.get("loci_draft") or []
    explore_mode = ast.get("explore_mode") or ""

    expected = gold_stems(case)

    blobs = [anchor, explore_mode, json.dumps(loci), json.dumps(ast)]
    anchor_hit = any(stem_hit(e, blobs) for e in expected) if expected else bool(anchor)

    f1_ok = phase == "explore_f1_ok" or st.get("last_action") == "f1_done"
    explicit_miss = phase in ("explore_f1_miss", "explore_f1_retrieval") or "anchor_miss_v1" in log
    schema_ok = pf.get("schema") == "problem_frame_v1" or bool(pf.get("primary_anchor"))
    no_trail = "a_trail_judge" not in log or explore_mode == "f1_anchor"
    mandatory = case.get("id") in ("17_ai_spinner_stuck", "20_cancel_ai_generation")

    row = {
        "id": case.get("id"),
        "mandatory": mandatory,
        "f1_ok": f1_ok,
        "anchor_confirmed": bool(anchor),
        "anchor_symbol_hit": anchor_hit,
        "explicit_failure": explicit_miss,
        "problem_frame_ok": schema_ok,
        "explore_mode_f1": explore_mode == "f1_anchor",
        "no_trail_in_log": no_trail,
        "phase": phase,
        "anchor": anchor,
    }
    row["pass"] = (
        f1_ok
        and anchor_hit
        and schema_ok
        and row["explore_mode_f1"]
        and no_trail
    )
    return row


def summarize(rows: list[dict]) -> dict:
    n = len(rows)
    mandatory = [r for r in rows if r.get("mandatory")]
    return {
        "n": n,
        "f1_ok": sum(bool(r.get("f1_ok")) for r in rows),
        "anchor_hit": sum(bool(r.get("anchor_symbol_hit")) for r in rows),
        "problem_frame_ok": sum(bool(r.get("problem_frame_ok")) for r in rows),
        "pass": sum(bool(r.get("pass")) for r in rows),
        "mandatory_pass": sum(bool(r.get("pass")) for r in mandatory),
        "mandatory_n": len(mandatory),
        "explicit_failures": sum(bool(r.get("explicit_failure")) for r in rows),
        "trail_violations": [r["id"] for r in rows if not r.get("no_trail_in_log")],
        "f1_miss": [r["id"] for r in rows if not r.get("f1_ok")],
    }


def gate_3b(summary: dict, rows: list[dict] | None = None) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    n = int(summary.get("n") or 0)
    if n < 5:
        reasons.append(f"incomplete battery n={n}/5")
    if int(summary.get("pass") or 0) < max(3, n - 2):
        reasons.append("pass count < 3")
    if int(summary.get("mandatory_pass") or 0) < int(summary.get("mandatory_n") or 0):
        reasons.append("mandatory 17/20 failed")
    if rows:
        for r in rows:
            if not r.get("no_trail_in_log"):
                reasons.append(f"trail in F1 log: {r.get('id')}")
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
            continue
        rows.append(score_case(case, case_dir))

    summary = summarize(rows)
    out = {"rows": rows, "summary": summary}
    gate = gate_3b(summary, rows)
    out["gate_3b"] = {"pass": gate[0], "reasons": gate[1]}
    out_path = args.round_dir / "f1_anchor_score.json"
    out_path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": summary, "gate_3b": out["gate_3b"]}, indent=2))
    if args.check_gate and not gate[0]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
