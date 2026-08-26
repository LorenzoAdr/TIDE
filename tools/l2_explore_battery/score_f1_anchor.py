#!/usr/bin/env python3
"""Score F1 anchor-hunt battery (explore_f1_ok / anchor_miss_v1)."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

OPERATIONAL_EXTRA: dict[str, list[str]] = {
    "17_ai_spinner_stuck": ["busy_strip", "set_busy", "clear_busy", "agent_busy"],
    "20_cancel_ai_generation": ["busy_strip"],
}


def load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def stem_hit(needle: str, haystacks: list[str]) -> bool:
    n = needle.lower()
    for h in haystacks:
        hl = h.lower()
        if n == hl or n in hl or hl in n:
            return True
    return False


def score_case(case: dict, case_dir: Path) -> dict:
    st = load_json(case_dir / "state.json")
    ast = load_json(case_dir / "a_state.json")
    log = (case_dir / "run.log").read_text(errors="replace") if (case_dir / "run.log").exists() else ""
    pf = load_json(case_dir / "problem_frame.json")

    phase = st.get("phase") or ""
    anchor = ast.get("anchor_confirmed") or ""
    loci = ast.get("loci_draft") or []
    explore_mode = ast.get("explore_mode") or ""

    expected = list(case.get("expected_stems") or [])
    expected += OPERATIONAL_EXTRA.get(case.get("id", ""), [])

    blobs = [anchor, explore_mode, json.dumps(loci), json.dumps(ast)]
    anchor_hit = any(stem_hit(e, blobs) for e in expected) if expected else bool(anchor)

    f1_ok = phase == "explore_f1_ok" or st.get("last_action") == "f1_done"
    explicit_miss = phase in ("explore_f1_miss", "explore_f1_retrieval") or "anchor_miss_v1" in log
    schema_ok = pf.get("schema") == "problem_frame_v1" or bool(pf.get("primary_anchor"))
    no_trail = "a_trail_judge" not in log or explore_mode == "f1_anchor"

    return {
        "id": case.get("id"),
        "f1_ok": f1_ok,
        "anchor_confirmed": bool(anchor),
        "anchor_symbol_hit": anchor_hit,
        "explicit_failure": explicit_miss,
        "problem_frame_ok": schema_ok,
        "explore_mode_f1": explore_mode == "f1_anchor",
        "no_trail_in_log": no_trail,
        "phase": phase,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--round-dir", type=Path, required=True)
    ap.add_argument("--cases", type=Path, default=Path("tests/fixtures/stem_boost_battery/prompts_nl_human.json"))
    args = ap.parse_args()

    cases = json.loads(args.cases.read_text(encoding="utf-8"))
    rows = []
    for case in cases:
        cid = case.get("id")
        if not cid:
            continue
        case_dir = args.round_dir / cid
        if not case_dir.exists():
            continue
        rows.append(score_case(case, case_dir))

    summary = {
        "n": len(rows),
        "f1_ok": sum(bool(r.get("f1_ok")) for r in rows),
        "anchor_hit": sum(bool(r.get("anchor_symbol_hit")) for r in rows),
        "problem_frame_ok": sum(bool(r.get("problem_frame_ok")) for r in rows),
        "explicit_failures": sum(bool(r.get("explicit_failure")) for r in rows),
    }
    out = {"rows": rows, "summary": summary}
    out_path = args.round_dir / "f1_anchor_score.json"
    out_path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
