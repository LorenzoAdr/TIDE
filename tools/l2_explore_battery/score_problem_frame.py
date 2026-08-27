#!/usr/bin/env python3
"""Score L1 ProblemFrame structural quality (primary vs secondary decomposition).

No gold-stem hit required — this is for unsupervised LLM decomposition review.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.l2_explore_battery.pf_battery_lib import (  # noqa: E402
    DEFAULT_CASES,
    load_cases,
    load_json,
    looks_like_question,
    pf_reject_noise,
    pf_search_terms,
)


def score_case(case: dict, case_dir: Path) -> dict:
    pf = load_json(case_dir / "problem_frame.json")
    meta = load_json(case_dir / "problem_frame_meta.json")

    schema_ok = pf.get("schema") == "problem_frame_v1"
    pa = pf.get("primary_anchor") or {}
    objective = str(pa.get("objective") or "")
    terms = pf_search_terms(pf)
    secs = pf.get("secondary_anchors") or []
    if not isinstance(secs, list):
        secs = []
    gaps = pf.get("mechanism_gaps") or []

    spaced_terms = [t for t in terms if " " in t]
    secondary_terms: list[str] = []
    secondary_rows = []
    for s in secs:
        if not isinstance(s, dict):
            continue
        st = [str(x) for x in (s.get("search_terms") or []) if str(x).strip()]
        secondary_terms.extend(st)
        secondary_rows.append(
            {
                "kind": s.get("kind"),
                "objective": s.get("objective"),
                "search_terms": st,
                "deferred": s.get("deferred", True),
                "why_later": s.get("why_later"),
            }
        )

    gap_ok = all(
        looks_like_question(str(g.get("question") or ""))
        for g in gaps
        if isinstance(g, dict) and g.get("question")
    ) if gaps else True

    row = {
        "id": case.get("id"),
        "schema_ok": schema_ok,
        "problem_kind": pf.get("problem_kind"),
        "problem_frame": pf.get("problem_frame"),
        "primary_kind": pa.get("kind"),
        "primary_objective": objective,
        "primary_search_terms": terms,
        "secondary_n": len(secondary_rows),
        "secondary_anchors": secondary_rows,
        "mechanism_gaps_n": len(gaps),
        "reject_noise": pf_reject_noise(pf),
        "provenance": pf.get("provenance") or meta.get("provenance") or "",
        "no_spaced_primary_terms": len(spaced_terms) == 0,
        "gaps_are_questions": gap_ok,
    }
    row["pass"] = (
        schema_ok
        and bool(objective.strip())
        and len(terms) >= 1
        and len(spaced_terms) == 0
        and gap_ok
    )
    return row


def summarize(rows: list[dict]) -> dict:
    return {
        "n": len(rows),
        "schema_ok": sum(bool(r.get("schema_ok")) for r in rows),
        "pass": sum(bool(r.get("pass")) for r in rows),
        "with_secondary": sum(int(r.get("secondary_n") or 0) > 0 for r in rows),
        "mean_primary_terms": round(
            sum(len(r.get("primary_search_terms") or []) for r in rows) / max(1, len(rows)), 2
        ),
        "mean_secondary": round(
            sum(int(r.get("secondary_n") or 0) for r in rows) / max(1, len(rows)), 2
        ),
    }


def gate_1b(summary: dict) -> tuple[bool, list[str]]:
    """Structural gate only (no gold stems)."""
    reasons: list[str] = []
    n = int(summary.get("n") or 0)
    if n < 1:
        reasons.append("empty battery")
    if int(summary.get("schema_ok") or 0) < n:
        reasons.append("schema_ok < n")
    if int(summary.get("pass") or 0) < n:
        reasons.append("structural pass < n")
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
    gate = gate_1b(summary)
    out = {"rows": rows, "summary": summary, "gate_1b": {"pass": gate[0], "reasons": gate[1]}}
    out_path = args.round_dir / "problem_frame_score.json"
    out_path.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print("=== ProblemFrame decomposition (manual review) ===\n")
    for r in rows:
        print(f"## {r.get('id')}")
        print(f"  kind={r.get('problem_kind')}  primary.kind={r.get('primary_kind')}  "
              f"prov={r.get('provenance')}  pass={r.get('pass')}")
        print(f"  frame: {r.get('problem_frame')}")
        print(f"  PRIMARY obj: {r.get('primary_objective')}")
        print(f"  PRIMARY terms: {r.get('primary_search_terms')}")
        secs = r.get("secondary_anchors") or []
        if not secs:
            print("  SECONDARY: (none)")
        for i, s in enumerate(secs, 1):
            print(f"  SECONDARY[{i}] {s.get('kind')}: {s.get('objective')}")
            print(f"             terms={s.get('search_terms')}  why={s.get('why_later')}")
        print()
    print(json.dumps({"summary": summary, "gate_1b": out["gate_1b"]}, indent=2))
    if args.check_gate and not gate[0]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
