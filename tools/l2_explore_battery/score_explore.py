#!/usr/bin/env python3
"""Score L2 explore-only battery cases from state.json + pack.md + session.md (+ a_state)."""
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
        "plan_outside_loci",
    )
    return sum(log.count(k) for k in keys)


def load_a_state(case_dir: Path) -> dict:
    for name in ("a_state.json",):
        p = case_dir / name
        if p.exists():
            try:
                return json.loads(p.read_text(encoding="utf-8"))
            except Exception:
                return {}
    return {}


def count_premature_multi_stem_plans(log: str) -> int:
    """Plans with ≥3 distinct path stems before a_done (should be 0 in Phase A)."""
    if "a_done" not in log and "explore_a" not in log:
        return 0
    premature = 0
    in_a = False
    for line in log.splitlines():
        if "fase=explore_a" in line or "phase: explore_a" in line:
            in_a = True
        if "a_done" in line or "fase=explore_b" in line:
            in_a = False
        if not in_a:
            continue
        if "plan targets=" in line or "acción=plan" in line:
            premature += 1
    return premature


def score_phase_a(case: dict, case_dir: Path, log: str, pack: str) -> dict:
    """Metrics for L2_EXPLORE_PHASE_A runs (absent → empty)."""
    ast = load_a_state(case_dir)
    if not ast and "explore_a" not in log and "a_judge" not in log:
        return {}

    peeks = int(ast.get("peeks_used") or 0)
    turns = int(ast.get("turns") or 0)
    expansions = int(ast.get("expansions") or 0)
    loci = ast.get("loci_draft") or []
    expected = case.get("expected_stems") or []
    loci_stems = {str(x.get("stem") or "") for x in loci if isinstance(x, dict)}
    loci_hit = True
    if expected:
        loci_hit = any(e in loci_stems or e in pack for e in expected)

    # Gold outside top-40 recovered if expansions>0 and stem later in pack/loci
    rank_miss_recovered = expansions > 0 and (loci_hit if expected else bool(loci))

    premature = count_premature_multi_stem_plans(log)

    return {
        "A_peeks": peeks,
        "A_turns": turns,
        "A_expansions": expansions,
        "loci_count": len(loci),
        "loci_hit": loci_hit,
        "rank_miss_recovered": rank_miss_recovered,
        "premature_multi_stem_plans": premature,
        "A_peeks_ok": peeks <= 32,
        "A_turns_ok": turns <= 8,
        "no_premature_plan": premature == 0,
    }


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
    explore_ok = (run_row or {}).get("explore_ok") or (
        (run_row or {}).get("exit") == 0 and (run_row or {}).get("phase") == "explore_ok"
    )

    facets: list[tuple[str, bool]] = []
    facets.append(("has_pack", has_pack))
    facets.append(("pack_complete", has_pack and not pack_incomplete))
    facets.append(("fragments_ok", plan["fragments_ok"] > 0))
    facets.append(
        (
            "fragments_all",
            plan["fragments_total"] > 0 and plan["fragments_ok"] == plan["fragments_total"],
        )
    )
    facets.append(
        ("ready_to_edit", last_action == "ready_to_edit" or phase == "edit" or explore_ok)
    )
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

    phase_a = score_phase_a(case, case_dir, log, pack)
    if phase_a:
        facets.append(("A_peeks_ok", phase_a["A_peeks_ok"]))
        facets.append(("A_turns_ok", phase_a["A_turns_ok"]))
        facets.append(("no_premature_plan", phase_a["no_premature_plan"]))
        if expected_stems:
            facets.append(("loci_hit", phase_a["loci_hit"]))

    hits = sum(1 for _, ok in facets if ok)
    total = len(facets)
    core = ["has_pack", "pack_complete", "fragments_ok", "ready_to_edit", "no_clarify"]
    core_ok = all(dict(facets).get(k, False) for k in core)
    out = {
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
    if phase_a:
        out["phase_a"] = phase_a
    return out


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
    phase_a_n = sum(1 for s in scored if s.get("phase_a"))
    premature = sum(
        int((s.get("phase_a") or {}).get("premature_multi_stem_plans") or 0) for s in scored
    )
    summary = {
        "cases": len(scored),
        "explore_success": ok_n,
        "explore_success_rate": ok_n / max(len(scored), 1),
        "phase_a_cases": phase_a_n,
        "premature_multi_stem_plans_total": premature,
        "scored": scored,
    }
    out = args.round_dir / "explore_score.json"
    out.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "explore_success": ok_n,
                "cases": len(scored),
                "phase_a_cases": phase_a_n,
                "premature_plans": premature,
                "wrote": str(out),
            },
            ensure_ascii=False,
        )
    )
    print(f"\n{'id':<32} ok  frag   steps  phase  A_peeks")
    for s in scored:
        mark = "OK" if s["explore_success"] else "!!"
        pa = s.get("phase_a") or {}
        apeeks = pa.get("A_peeks", "-")
        print(
            f"{s['id']:<32} {mark:2}  {s['fragments_ok']}/{s['fragments_total']:>2}  "
            f"{s['steps']:>3}  {s.get('phase') or '?':12}  {apeeks}"
        )


if __name__ == "__main__":
    main()
