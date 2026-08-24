#!/usr/bin/env python3
"""Score Phase A-only battery: a_done, loci, map rank, A0 expand, coverage errors."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

# Operational gold stems when fixture expected_stems miss the edit/control site.
OPERATIONAL_EXTRA: dict[str, list[str]] = {
    "17_ai_spinner_stuck": ["busy_strip"],
    "20_cancel_ai_generation": ["busy_strip"],
}


def load_cases(path: Path) -> list[dict]:
    return json.loads(path.read_text(encoding="utf-8"))


def load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def map_entry_stems(map_text: str, top_n: int = 15) -> list[tuple[int, str, str]]:
    """Return [(rank, path, stem), ...] from map_last.md ranked entries."""
    out: list[tuple[int, str, str]] = []
    for m in re.finditer(
        r"^(\d+)\.\s+(\S+?)(?::\d+)?\s+",
        map_text,
        re.MULTILINE,
    ):
        rank = int(m.group(1))
        path = m.group(2)
        stem = Path(path).stem
        out.append((rank, path, stem))
        if len(out) >= top_n:
            break
    return out


def stem_hit(needle: str, haystacks: list[str]) -> bool:
    n = needle.lower()
    for h in haystacks:
        hl = h.lower()
        if n == hl or n in hl or hl in n:
            return True
    return False


def collect_blob_stems(ast: dict, map_text: str, notes_md: str) -> dict[str, list[str]]:
    loci = ast.get("loci_draft") or []
    notes = ast.get("notes") or []
    queue = ast.get("queue") or []
    a1q = ast.get("a1_queue") or []
    shown = ast.get("a0_shown_targets") or []

    def stems_from_items(items: list) -> list[str]:
        out: list[str] = []
        for it in items:
            if not isinstance(it, dict):
                out.append(str(it))
                continue
            for k in ("stem", "path", "target", "anchor", "symbol"):
                v = it.get(k)
                if v:
                    out.append(str(v))
                    if k == "path":
                        out.append(Path(str(v)).stem)
        return out

    map_top = map_entry_stems(map_text, 40)
    return {
        "loci": stems_from_items(loci) + [str(x.get("anchor") or "") for x in loci if isinstance(x, dict)],
        "notes": stems_from_items(notes),
        "notes_useful": stems_from_items(
            [n for n in notes if isinstance(n, dict) and n.get("verdict") == "useful"]
        ),
        "queue": stems_from_items(queue),
        "a1_queue": stems_from_items(a1q) + [str(x) for x in shown],
        "map_top15": [s for _, _, s in map_top[:15]] + [p for _, p, _ in map_top[:15]],
        "map_top40": [s for _, _, s in map_top] + [p for _, p, _ in map_top],
        "notes_md": [notes_md],
        "all": [],
    }


def any_stem_in(stems: list[str], blobs: list[str]) -> list[str]:
    return [s for s in stems if stem_hit(s, blobs)]


def premature_plans(log: str) -> int:
    premature = 0
    in_a = False
    for line in log.splitlines():
        if "fase=explore_a" in line or "phase: explore_a" in line or "run-explore-a" in line:
            in_a = True
        if "a_done" in line or "fase=explore_b" in line or "Phase A OK" in line:
            in_a = False
        if not in_a:
            continue
        if "plan targets=" in line or "acción=plan" in line:
            premature += 1
    return premature


def score_case(case: dict, case_dir: Path, run_row: dict | None) -> dict:
    ast = load_json(case_dir / "a_state.json")
    st = load_json(case_dir / "state.json")
    log = (case_dir / "run.log").read_text(errors="replace") if (case_dir / "run.log").exists() else ""
    map_text = (
        (case_dir / "map_last.md").read_text(errors="replace") if (case_dir / "map_last.md").exists() else ""
    )
    notes_md = (
        (case_dir / "a_notes.md").read_text(errors="replace") if (case_dir / "a_notes.md").exists() else ""
    )
    blobs = collect_blob_stems(ast, map_text, notes_md)
    blobs["all"] = (
        blobs["loci"]
        + blobs["notes"]
        + blobs["queue"]
        + blobs["a1_queue"]
        + blobs["map_top40"]
        + blobs["notes_md"]
    )

    expected = list(case.get("expected_stems") or [])
    operational = list(dict.fromkeys(expected + OPERATIONAL_EXTRA.get(case["id"], [])))

    loci_hits = any_stem_in(expected, blobs["loci"])
    op_loci_hits = any_stem_in(operational, blobs["loci"])
    map15_hits = any_stem_in(operational, blobs["map_top15"])
    queue_hits = any_stem_in(operational, blobs["queue"])
    expand_hits = any_stem_in(
        operational,
        blobs["notes_useful"] + blobs["a1_queue"] + blobs["notes_md"],
    )
    # Expanded if A0/A1 touched gold OR classic expansions>0 with useful note on stem
    gold_expanded = bool(expand_hits) or (
        int(ast.get("expansions") or 0) > 0 and bool(any_stem_in(operational, blobs["notes"]))
    )

    a_done_ok = bool(
        (run_row or {}).get("a_done_ok")
        or ("Phase A OK" in log)
        or ("explore_a_ok" in log)
        or ast.get("done")
    )
    coverage_fail = int(
        (run_row or {}).get("a0_coverage_fail")
        if run_row and "a0_coverage_fail" in (run_row or {})
        else log.count("faltan veredictos")
    )
    premature = premature_plans(log)
    peeks = int(ast.get("peeks_used") or 0)
    turns = int(ast.get("turns") or 0)
    expansions = int(ast.get("expansions") or 0)
    a0_turns = int(ast.get("a0_turns") or 0)

    facets = [
        ("a_done_ok", a_done_ok),
        ("loci_hit", bool(loci_hits) if expected else bool(ast.get("loci_draft"))),
        ("op_loci_hit", bool(op_loci_hits) if operational else False),
        ("gold_in_map_top15", bool(map15_hits)),
        ("gold_in_queue", bool(queue_hits)),
        ("gold_expanded", gold_expanded),
        ("no_a0_coverage_fail", coverage_fail == 0),
        ("no_premature_plan", premature == 0),
        ("A_peeks_ok", peeks <= 32),
        ("A_turns_ok", turns <= 12),
        ("no_clarify", (st.get("phase") or (run_row or {}).get("phase")) != "clarify"),
    ]
    hits = sum(1 for _, ok in facets if ok)
    core = ["a_done_ok", "op_loci_hit", "no_premature_plan", "no_a0_coverage_fail"]
    core_ok = all(dict(facets).get(k, False) for k in core)

    return {
        "id": case["id"],
        "complexity": case.get("complexity"),
        "a_success": core_ok,
        "facet_hits": hits,
        "facet_total": len(facets),
        "facet_recall": hits / max(len(facets), 1),
        "facets": [{"name": n, "ok": ok} for n, ok in facets],
        "a_done_ok": a_done_ok,
        "loci_hit": bool(loci_hits),
        "op_loci_hit": bool(op_loci_hits),
        "loci_hits": loci_hits,
        "op_loci_hits": op_loci_hits,
        "gold_in_map_top15": bool(map15_hits),
        "map15_hits": map15_hits,
        "gold_in_queue": bool(queue_hits),
        "gold_expanded": gold_expanded,
        "expand_hits": expand_hits,
        "a0_coverage_fail": coverage_fail,
        "premature_plans": premature,
        "peeks": peeks,
        "turns": turns,
        "expansions": expansions,
        "a0_turns": a0_turns,
        "cards_used": ast.get("cards_used"),
        "a_subphase": ast.get("a_subphase"),
        "phase": st.get("phase") or (run_row or {}).get("phase"),
        "loci_n": len(ast.get("loci_draft") or []),
        "expected": expected,
        "operational": operational,
        "exit": (run_row or {}).get("exit"),
        "error": (run_row or {}).get("error"),
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=Path, required=True)
    ap.add_argument("--round-dir", type=Path, required=True)
    args = ap.parse_args()

    cases = load_cases(args.cases)
    rows_by_id: dict[str, dict] = {}
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

    # Only score cases that have a result row or case dir artifacts
    scored = []
    for c in cases:
        cid = c["id"]
        case_dir = args.round_dir / cid
        if cid not in rows_by_id and not case_dir.exists():
            continue
        scored.append(score_case(c, case_dir, rows_by_id.get(cid)))

    n = len(scored)
    a_ok = sum(1 for s in scored if s["a_success"])
    a_done = sum(1 for s in scored if s["a_done_ok"])
    loci = sum(1 for s in scored if s["loci_hit"])
    op_loci = sum(1 for s in scored if s["op_loci_hit"])
    map15 = sum(1 for s in scored if s["gold_in_map_top15"])
    expanded = sum(1 for s in scored if s["gold_expanded"])
    cov_fail = sum(int(s["a0_coverage_fail"] or 0) for s in scored)
    premature = sum(int(s["premature_plans"] or 0) for s in scored)

    summary = {
        "cases": n,
        "a_success": a_ok,
        "a_success_rate": a_ok / max(n, 1),
        "a_done_ok": a_done,
        "a_done_rate": a_done / max(n, 1),
        "loci_hit": loci,
        "loci_hit_rate": loci / max(n, 1),
        "op_loci_hit": op_loci,
        "op_loci_hit_rate": op_loci / max(n, 1),
        "gold_in_map_top15": map15,
        "gold_in_map_top15_rate": map15 / max(n, 1),
        "gold_expanded": expanded,
        "gold_expanded_rate": expanded / max(n, 1),
        "a0_coverage_fail_total": cov_fail,
        "premature_plans_total": premature,
        "scored": scored,
    }
    out = args.round_dir / "phase_a_score.json"
    out.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(
        json.dumps(
            {
                "a_success": a_ok,
                "a_done_ok": a_done,
                "loci_hit": loci,
                "op_loci_hit": op_loci,
                "gold_in_map_top15": map15,
                "gold_expanded": expanded,
                "cases": n,
                "a0_coverage_fails": cov_fail,
                "premature_plans": premature,
                "wrote": str(out),
            },
            ensure_ascii=False,
        )
    )
    print(
        f"\n{'id':<32} ok done loci opl map15 exp  a0f peek turn phase"
    )
    for s in scored:
        mark = "OK" if s["a_success"] else "!!"
        print(
            f"{s['id']:<32} {mark:2} {int(s['a_done_ok'])}    "
            f"{int(s['loci_hit'])}    {int(s['op_loci_hit'])}   "
            f"{int(s['gold_in_map_top15'])}     {int(s['gold_expanded'])}    "
            f"{s['a0_coverage_fail']:>3} {s['peeks']:>4} {s['turns']:>4}  "
            f"{s.get('phase') or '?'}"
        )


if __name__ == "__main__":
    main()
