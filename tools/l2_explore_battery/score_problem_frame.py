#!/usr/bin/env python3
"""Score L1 ProblemFrame v1 quality for anchor-hunt batteries."""
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
    looks_like_question,
    pf_reject_noise,
    pf_search_terms,
    stem_hit,
    trap_stems,
)


def score_case(case: dict, case_dir: Path) -> dict:
    pf = load_json(case_dir / "problem_frame.json")
    meta = load_json(case_dir / "problem_frame_meta.json")

    schema_ok = pf.get("schema") == "problem_frame_v1"
    pa = pf.get("primary_anchor") or {}
    objective = str(pa.get("objective") or pf.get("problem_frame") or "")
    terms = pf_search_terms(pf)
    rejects = pf_reject_noise(pf)
    gaps = pf.get("mechanism_gaps") or []

    gold = gold_stems(case)
    search_hit = any(stem_hit(g, terms) for g in gold) if gold else bool(terms)
    reject_hits_gold = any(stem_hit(g, rejects) for g in gold)

    gap_questions = 0
    gap_assertions = 0
    for g in gaps:
        q = str(g.get("question") or "")
        if looks_like_question(q):
            gap_questions += 1
        elif q:
            gap_assertions += 1

    kind = str(pf.get("problem_kind") or "")
    kind_expected = str(case.get("problem_kind_expected") or "")
    kind_ok = (not kind_expected) or kind == kind_expected

    trap = trap_stems(case)
    trap_in_terms = any(stem_hit(t, terms) for t in trap)

    causal_markers = ("porque", "debido", "causa", "provoca", " therefore ", " because ")
    frame_text = str(pf.get("problem_frame") or objective).lower()
    has_causal_hypothesis = any(m in frame_text for m in causal_markers)

    mandatory = case.get("id") in ("17_ai_spinner_stuck", "20_cancel_ai_generation")

    row = {
        "id": case.get("id"),
        "mandatory": mandatory,
        "schema_ok": schema_ok,
        "objective_nonempty": bool(objective.strip()),
        "search_terms_n": len(terms),
        "search_terms_hit_gold": search_hit,
        "reject_noise_ok": not reject_hits_gold,
        "mechanism_gaps_n": len(gaps),
        "mechanism_gaps_questions": gap_questions,
        "mechanism_gaps_assertions": gap_assertions,
        "problem_kind": kind,
        "problem_kind_ok": kind_ok,
        "trap_in_search_terms": trap_in_terms,
        "no_causal_hypothesis": not has_causal_hypothesis,
        "provenance": pf.get("provenance") or meta.get("provenance") or "",
        "search_terms": terms,
        "reject_noise": rejects,
    }
    row["pass"] = (
        schema_ok
        and row["objective_nonempty"]
        and len(terms) >= 1
        and search_hit
        and not reject_hits_gold
        and kind_ok
        and not trap_in_terms
        and not has_causal_hypothesis
        and (gap_assertions == 0)
    )
    return row


def summarize(rows: list[dict]) -> dict:
    n = len(rows)
    mandatory = [r for r in rows if r.get("mandatory")]
    return {
        "n": n,
        "schema_ok": sum(bool(r.get("schema_ok")) for r in rows),
        "search_terms_hit_gold": sum(bool(r.get("search_terms_hit_gold")) for r in rows),
        "pass": sum(bool(r.get("pass")) for r in rows),
        "mandatory_pass": sum(bool(r.get("pass")) for r in mandatory),
        "mandatory_n": len(mandatory),
        "reject_noise_fail": [r["id"] for r in rows if not r.get("reject_noise_ok")],
        "search_miss": [r["id"] for r in rows if not r.get("search_terms_hit_gold")],
        "kind_fail": [r["id"] for r in rows if not r.get("problem_kind_ok")],
    }


def gate_1b(summary: dict) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    n = int(summary.get("n") or 0)
    if n < 5:
        reasons.append(f"incomplete battery n={n}/5")
    if int(summary.get("schema_ok") or 0) < n:
        reasons.append("schema_ok < n")
    if int(summary.get("search_terms_hit_gold") or 0) < max(4, n - 1):
        reasons.append("search_terms_hit_gold < 4")
    if int(summary.get("mandatory_pass") or 0) < int(summary.get("mandatory_n") or 0):
        reasons.append("mandatory cases 17/20 failed")
    if int(summary.get("pass") or 0) < max(4, n - 1):
        reasons.append("pass count < 4")
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
    gate = gate_1b(summary)
    out["gate_1b"] = {"pass": gate[0], "reasons": gate[1]}
    out_path = args.round_dir / "problem_frame_score.json"
    out_path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": summary, "gate_1b": out["gate_1b"]}, indent=2))
    if args.check_gate and not gate[0]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
