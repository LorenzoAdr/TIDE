#!/usr/bin/env python3
"""Score L2 facet-sweep rounds from cases.json expect + per-case artifacts."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def load_cases(path: Path) -> list[dict]:
    return json.loads(path.read_text(encoding="utf-8"))


def score_case(case: dict, case_dir: Path, row: dict | None) -> dict:
    expect = case.get("expect") or {}
    facets: list[tuple[str, bool]] = []
    patch = ""
    if (case_dir / "diff.patch").exists():
        patch = (case_dir / "diff.patch").read_text(errors="replace")
    st = {}
    if (case_dir / "state.json").exists():
        try:
            st = json.loads((case_dir / "state.json").read_text())
        except Exception:
            st = {}
    row = row or {}
    phase = st.get("phase") or row.get("phase")
    last = st.get("last_action") or row.get("last_action")
    run_ok = row.get("run_ok_line") or ""

    paths = expect.get("paths_touched") or []
    for p in paths:
        facets.append((f"path:{p}", p in patch or f"a/{p}" in patch or f"b/{p}" in patch))

    for s in expect.get("diff_must_contain") or []:
        facets.append((f"contains:{s[:40]}", s in patch))

    for s in expect.get("diff_must_not_contain") or []:
        facets.append((f"forbid:{s[:40]}", s not in patch))

    if expect.get("compile_ok"):
        ok = (row.get("n_compile_ok") or 0) > 0 and "FAIL compile" not in run_ok
        # also accept done after compile without fail line
        if phase == "done" and last == "done" and (row.get("n_compile_fail") or 0) == 0:
            ok = True
        facets.append(("compile_ok", bool(ok)))

    if expect.get("forbid_false_done"):
        false_done = last == "compile_fail_rollback" or (
            phase == "done" and "FAIL compile" in run_ok
        )
        facets.append(("no_false_done", not false_done))

    if expect.get("must_clarify"):
        clarified = phase == "clarify" or "clarify" in str(last) or "need_clarification" in str(
            last
        )
        facets.append(("must_clarify", clarified))

    if expect.get("must_edit"):
        edited = bool(patch.strip()) or (row.get("n_edit") or 0) > 0
        not_clarify = phase != "clarify"
        facets.append(("must_edit", edited and not_clarify))

    if expect.get("min_edits"):
        n = row.get("n_edit") or 0
        # count edit actions in log if present
        log = ""
        if (case_dir / "run.log").exists():
            log = (case_dir / "run.log").read_text(errors="replace")
        n = max(n, len(re.findall(r"acción=edit", log)))
        facets.append((f"min_edits:{expect['min_edits']}", n >= int(expect["min_edits"])))

    if expect.get("phase_done"):
        facets.append(("phase_done", phase == "done" and bool(st.get("done", row.get("done")))))

    hits = sum(1 for _, ok in facets if ok)
    total = max(len(facets), 1)
    all_pass = hits == total and total > 0
    return {
        "id": case["id"],
        "facet_hits": hits,
        "facet_total": total,
        "facet_recall": hits / total,
        "all_pass": all_pass,
        "facets": [{"name": n, "ok": ok} for n, ok in facets],
        "phase": phase,
        "last_action": last,
    }


def score_round(cases_path: Path, round_dir: Path) -> dict:
    cases = load_cases(cases_path)
    rows_by_id = {}
    results = round_dir / "results.jsonl"
    if results.exists():
        for line in results.read_text(errors="replace").splitlines():
            if not line.strip():
                continue
            try:
                r = json.loads(line)
                rows_by_id[r.get("id")] = r
            except Exception:
                pass
    expected_ids = [c["id"] for c in cases]
    missing = [cid for cid in expected_ids if cid not in rows_by_id]
    incomplete = len(missing) > 0
    scored = []
    for c in cases:
        cid = c["id"]
        if cid not in rows_by_id:
            scored.append(
                {
                    "id": cid,
                    "facet_hits": 0,
                    "facet_total": 1,
                    "facet_recall": 0.0,
                    "all_pass": False,
                    "facets": [{"name": "ran", "ok": False}],
                    "phase": None,
                    "last_action": "missing_result",
                }
            )
            continue
        scored.append(score_case(c, round_dir / cid, rows_by_id.get(cid)))
    n = len(scored) or 1
    recall = sum(s["facet_recall"] for s in scored) / n
    all_pass = sum(1 for s in scored if s["all_pass"])
    metrics = {
        "facet_recall": 0.0 if incomplete else recall,
        "all_pass": -1 if incomplete else all_pass,
        "all_pass_rate": 0.0 if incomplete else all_pass / n,
        "n_cases": len(scored),
        "n_results": len(rows_by_id),
        "incomplete": incomplete,
        "missing_ids": missing,
        "cases": scored,
    }
    (round_dir / "metrics.json").write_text(json.dumps(metrics, indent=2, ensure_ascii=False) + "\n")
    return metrics


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", required=True)
    ap.add_argument("--round-dir", required=True)
    args = ap.parse_args()
    m = score_round(Path(args.cases), Path(args.round_dir))
    print(json.dumps({k: m[k] for k in ("facet_recall", "all_pass", "all_pass_rate", "n_cases", "incomplete", "missing_ids") if k in m}, indent=2))


if __name__ == "__main__":
    main()
