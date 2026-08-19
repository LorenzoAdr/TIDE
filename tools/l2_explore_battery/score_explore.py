#!/usr/bin/env python3
"""Score L2 explore-only battery cases from state.json + pack.md + session.md."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def load_cases(path: Path) -> list[dict]:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_plan_stats(session: str) -> dict:
    out = {"fragments_ok": 0, "fragments_total": 0, "pack_chars": 0}
    m = re.search(
        r"fragments_ok:\s*(\d+)/(\d+).*pack_chars:\s*(\d+)",
        session,
        re.DOTALL,
    )
    if m:
        out["fragments_ok"] = int(m.group(1))
        out["fragments_total"] = int(m.group(2))
        out["pack_chars"] = int(m.group(3))
    return out


def count_pushbacks(log: str) -> int:
    keys = (
        "pack_incomplete_pushback",
        "post_pack_tool_pushback",
        "repeated_plan_pushback",
        "clarify_pushback",
    )
    return sum(log.count(k) for k in keys)


def score_case(case: dict, case_dir: Path, run_row: dict | None) -> dict:
    st = {}
    state_path = case_dir / "state.json"
    if state_path.exists():
        try:
            st = json.loads(state_path.read_text(encoding="utf-8"))
        except Exception:
            st = {}
    session = (case_dir / "session.md").read_text(errors="replace") if (case_dir / "session.md").exists() else ""
    pack = (case_dir / "pack.md").read_text(errors="replace") if (case_dir / "pack.md").exists() else ""
    log = (case_dir / "run.log").read_text(errors="replace") if (case_dir / "run.log").exists() else ""
    plan = parse_plan_stats(session)

    has_pack = bool(st.get("has_pack"))
    pack_incomplete = bool(st.get("pack_incomplete"))
    phase = st.get("phase") or (run_row or {}).get("phase")
    last_action = st.get("last_action") or (run_row or {}).get("last_action")
    explore_ok = (run_row or {}).get("explore_ok") or (run_row or {}).get("exit") == 0 and (run_row or {}).get("phase") == "explore_ok"

    facets: list[tuple[str, bool]] = []
    facets.append(("has_pack", has_pack))
    facets.append(("pack_complete", has_pack and not pack_incomplete))
    facets.append(("fragments_ok", plan["fragments_ok"] > 0))
    facets.append(("fragments_all", plan["fragments_total"] > 0 and plan["fragments_ok"] == plan["fragments_total"]))
    facets.append(("ready_to_edit", last_action == "ready_to_edit" or phase == "edit" or explore_ok))
    facets.append(("no_clarify", phase != "clarify"))
    facets.append(("no_timeout", phase != "timeout"))
    pushbacks = count_pushbacks(log)
    facets.append(("no_pushback_loop", pushbacks <= 2))
    facets.append(("pack_nonempty", len(pack.strip()) > 80))

    expected_stems = case.get("expected_stems") or []
    if expected_stems:
        hit = any(s in pack for s in expected_stems)
        facets.append(("expected_stem_in_pack", hit))

    steps = (run_row or {}).get("steps") or st.get("turn") or 0
    facets.append(("steps_bounded", int(steps) <= 16))

    hits = sum(1 for _, ok in facets if ok)
    total = len(facets)
    core = ["has_pack", "pack_complete", "fragments_ok", "ready_to_edit", "no_clarify"]
    core_ok = all(dict(facets).get(k, False) for k in core)
    return {
        "id": case["id"],
        "complexity": case.get("complexity"),
        "explore_success": core_ok,
        "facet_hits": hits,
        "facet_total": total,
        "facet_recall": hits / max(total, 1),
        "facets": [{"name": n, "ok": ok} for n, ok in facets],
        "phase": phase,
        "last_action": last_action,
        "steps": steps,
        "pushbacks": pushbacks,
        "fragments_ok": plan["fragments_ok"],
        "fragments_total": plan["fragments_total"],
        "pack_chars": plan["pack_chars"] or len(pack),
        "watchlist": st.get("watchlist") or [],
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=Path, required=True)
    ap.add_argument("--round-dir", type=Path, required=True)
    args = ap.parse_args()

    cases = load_cases(args.cases)
    rows_by_id = {}
    results = args.round_dir / "results.jsonl"
    if results.exists():
        for line in results.read_text(errors="replace").splitlines():
            if not line.strip():
                continue
            try:
                r = json.loads(line)
                rows_by_id[r["id"]] = r
            except Exception:
                pass

    scored = []
    for c in cases:
        cid = c["id"]
        case_dir = args.round_dir / cid
        scored.append(score_case(c, case_dir, rows_by_id.get(cid)))

    ok_n = sum(1 for s in scored if s["explore_success"])
    summary = {
        "cases": len(scored),
        "explore_success": ok_n,
        "explore_success_rate": ok_n / max(len(scored), 1),
        "scored": scored,
    }
    out = args.round_dir / "explore_score.json"
    out.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"explore_success": ok_n, "cases": len(scored), "wrote": str(out)}, ensure_ascii=False))
    print(f"\n{'id':<32} ok  frag   steps  phase")
    for s in scored:
        mark = "OK" if s["explore_success"] else "!!"
        print(
            f"{s['id']:<32} {mark:2}  {s['fragments_ok']}/{s['fragments_total']:>2}  "
            f"{s['steps']:>3}  {s.get('phase') or '?'}"
        )


if __name__ == "__main__":
    main()
