#!/usr/bin/env python3
"""Score post-PF entityness link reports (chain links → explore_mode)."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.l2_explore_battery.pf_battery_lib import DEFAULT_CASES, load_cases, load_json  # noqa: E402


def score_case(case: dict, case_dir: Path) -> dict:
    ej = load_json(case_dir / "entityness.json")
    # Prefer new links format; fall back to legacy candidates.
    links = ej.get("links")
    if isinstance(links, list) and links:
        best = max(links, key=lambda x: float((x or {}).get("entityness") or 0))
        bad_score = False
        has_ev = False
        for L in links:
            if not isinstance(L, dict):
                continue
            try:
                s = float(L.get("entityness") or 0)
            except (TypeError, ValueError):
                bad_score = True
                s = -1
            if s < 0 or s > 1.0001:
                bad_score = True
            for eid in L.get("evidence_ids") or []:
                es = str(eid)
                if es.startswith("latch:") or es.startswith("fn:"):
                    has_ev = True
        mode = str(ej.get("explore_mode") or "")
        row = {
            "id": case.get("id"),
            "format": "links",
            "n_links": len(links),
            "explore_mode": mode,
            "best_role": ej.get("best_role"),
            "best_entityness": ej.get("best_entityness"),
            "threshold": ej.get("threshold"),
            "top_terms": best.get("search_terms") if isinstance(best, dict) else [],
            "top_owner_stem": best.get("owner_stem") if isinstance(best, dict) else None,
            "has_evidence_ids": has_ev,
            "scores_in_unit": not bad_score,
            "links": [
                {
                    "role": L.get("role"),
                    "entityness": L.get("entityness"),
                    "concentration": L.get("concentration"),
                    "hit_count": L.get("hit_count"),
                    "hit_score": L.get("hit_score"),
                    "owner_stem": L.get("owner_stem"),
                    "search_terms": L.get("search_terms"),
                }
                for L in links
                if isinstance(L, dict)
            ],
        }
        row["pass"] = (
            bool(ej)
            and len(links) >= 1
            and not bad_score
            and mode in ("f1_anchor", "classic_scan")
        )
        # best_role may be primary, secondary_N, or hyp_N
        role = str(ej.get("best_role") or "")
        row["best_role_ok"] = (
            not role
            or role == "primary"
            or role.startswith("secondary_")
            or role.startswith("hyp_")
        )
        if row["pass"] and role:
            row["pass"] = bool(row["best_role_ok"])
        return row

    # Legacy prompt-token format
    cands = ej.get("candidates") or []
    if not isinstance(cands, list):
        cands = []
    top = cands[0] if cands else {}
    row = {
        "id": case.get("id"),
        "format": "legacy_candidates",
        "n_candidates": len(cands),
        "top_term": top.get("term"),
        "top_entityness": top.get("entityness"),
        "top_owner_stem": top.get("owner_stem"),
        "pass": False,
        "error": "legacy_format_use_problem_frame_links",
    }
    return row


def summarize(rows: list[dict]) -> dict:
    return {
        "n": len(rows),
        "pass": sum(bool(r.get("pass")) for r in rows),
        "f1_anchor": sum(r.get("explore_mode") == "f1_anchor" for r in rows),
        "classic_scan": sum(r.get("explore_mode") == "classic_scan" for r in rows),
        "with_evidence": sum(bool(r.get("has_evidence_ids")) for r in rows),
    }


def gate(summary: dict) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    n = int(summary.get("n") or 0)
    if n < 1:
        reasons.append("empty battery")
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
            continue  # partial --only rounds
        rows.append(score_case(case, case_dir))

    if not rows:
        print("no case dirs in", args.round_dir, file=sys.stderr)
        return 1

    summary = summarize(rows)
    g = gate(summary)
    out = {"rows": rows, "summary": summary, "gate": {"pass": g[0], "reasons": g[1]}}
    out_path = args.round_dir / "entityness_score.json"
    out_path.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print("=== Entityness links (post-PF) ===\n")
    for r in rows:
        print(f"## {r.get('id')}")
        print(
            f"  mode={r.get('explore_mode')} best_role={r.get('best_role')} "
            f"ent={r.get('best_entityness')} stem={r.get('top_owner_stem')} pass={r.get('pass')}"
        )
        for L in r.get("links") or []:
            print(
                f"    - [{L.get('role')}] {L.get('entityness')} "
                f"stem={L.get('owner_stem')} terms={L.get('search_terms')}"
            )
        print()
    print(json.dumps({"summary": summary, "gate": out["gate"]}, indent=2))
    if args.check_gate and not g[0]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
