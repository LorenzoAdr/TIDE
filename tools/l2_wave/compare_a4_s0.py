#!/usr/bin/env python3
"""Compara A4 (full) vs S0 (minimal) por caso: media ± σ y delta."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path


def mean_std(xs: list[float]) -> tuple[float, float]:
    if not xs:
        return 0.0, 0.0
    m = sum(xs) / len(xs)
    if len(xs) < 2:
        return m, 0.0
    var = sum((x - m) ** 2 for x in xs) / (len(xs) - 1)
    return m, math.sqrt(var)


def load_per_case(base: Path) -> dict:
    summary = None
    for name in (
        "A4_summary.json",
        "S0_summary.json",
        "BD_summary.json",
        "D_summary.json",
        "G_summary.json",
        "BCD_summary.json",
    ):
        cand = base / name
        if cand.is_file():
            summary = cand
            break
    if summary is not None:
        return json.loads(summary.read_text(encoding="utf-8"))
    # rebuild
    by_case: dict[str, list[int]] = {}
    ok_fracs: list[float] = []
    for rep in sorted(base.glob("rep_*")):
        sb = rep / "scoreboard.json"
        if not sb.is_file():
            continue
        data = json.loads(sb.read_text(encoding="utf-8"))
        rows = data.get("rows") or []
        oks = sum(1 for r in rows if r.get("ok"))
        ok_fracs.append(oks / len(rows) if rows else 0.0)
        for r in rows:
            by_case.setdefault(r["case"], []).append(1 if r.get("ok") else 0)
    per_case = {}
    for case, vals in by_case.items():
        m, s = mean_std([float(v) for v in vals])
        per_case[case] = {"n": len(vals), "mean": m, "std": s, "oks": vals}
    om, os_ = mean_std(ok_fracs)
    return {
        "reps_scored": len(ok_fracs),
        "ok_frac_mean": om,
        "ok_frac_std": os_,
        "per_case": per_case,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("a4")
    ap.add_argument("s0")
    ap.add_argument("-o", "--out", type=Path)
    args = ap.parse_args()
    a4 = load_per_case(Path(args.a4))
    s0 = load_per_case(Path(args.s0))
    sigma = a4.get("ok_frac_std") or 0.0
    delta = (s0.get("ok_frac_mean") or 0.0) - (a4.get("ok_frac_mean") or 0.0)
    decision = "inconclusive"
    if sigma > 0 and abs(delta) <= sigma:
        decision = "minimal_≈_actual_within_1σ → adoptar mínimo"
    elif delta < -sigma:
        decision = "minimal_peor → ablación o seguir a B–C"
    elif delta > sigma:
        decision = "minimal_mejor → adoptar mínimo"
    else:
        decision = "sigma≈0; mirar por caso"

    rows = []
    cases = sorted(
        set(a4.get("per_case", {})) | set(s0.get("per_case", {}))
    )
    for c in cases:
        ca = a4.get("per_case", {}).get(c, {})
        cs = s0.get("per_case", {}).get(c, {})
        rows.append(
            {
                "case": c,
                "a4_mean": ca.get("mean"),
                "a4_std": ca.get("std"),
                "s0_mean": cs.get("mean"),
                "s0_std": cs.get("std"),
                "delta": (cs.get("mean") or 0) - (ca.get("mean") or 0),
            }
        )
    out = {
        "a4": {"ok_frac_mean": a4.get("ok_frac_mean"), "ok_frac_std": a4.get("ok_frac_std"),
               "reps": a4.get("reps_scored")},
        "s0": {"ok_frac_mean": s0.get("ok_frac_mean"), "ok_frac_std": s0.get("ok_frac_std"),
               "reps": s0.get("reps_scored")},
        "delta_ok_frac": delta,
        "decision_hint": decision,
        "per_case": rows,
    }
    text = json.dumps(out, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
