#!/usr/bin/env python3
"""Decisión A4 (1σ) de un baseline X frente a S0. Escribe DECISION.json."""

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


def load_summary(base: Path) -> dict:
    for name in (
        "BD_summary.json",
        "D_summary.json",
        "G_summary.json",
        "BCD_summary.json",
        "S0_summary.json",
        "A4_summary.json",
    ):
        p = base / name
        if p.is_file():
            return json.loads(p.read_text(encoding="utf-8"))
    by_case: dict[str, list[int]] = {}
    ok_fracs: list[float] = []
    for rep in sorted(base.glob("rep_*")):
        sb = rep / "scoreboard.json"
        if not sb.is_file():
            continue
        data = json.loads(sb.read_text(encoding="utf-8"))
        rows = data.get("rows") or []
        oks = sum(1 for r in rows if r.get("ok_gold") or r.get("ok"))
        ok_fracs.append(oks / len(rows) if rows else 0.0)
        for r in rows:
            by_case.setdefault(r["case"], []).append(
                1 if (r.get("ok_gold") or r.get("ok")) else 0
            )
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


def decide(s0: dict, x: dict, label: str) -> dict:
    s0_m = float(s0.get("ok_frac_mean") or 0.0)
    s0_s = float(s0.get("ok_frac_std") or 0.0)
    x_m = float(x.get("ok_frac_mean") or 0.0)
    x_s = float(x.get("ok_frac_std") or 0.0)
    sigma = s0_s if s0_s > 0 else x_s
    delta = x_m - s0_m
    if sigma > 0 and abs(delta) <= sigma:
        action = "keep_s0"
        hint = f"{label}_≈_S0_within_1σ → quedarse en S0 (no adoptar complejidad)"
    elif delta > sigma:
        action = "adopt_bd" if label == "BD" else "adopt_x"
        hint = f"{label}_≳_S0 → adoptar {label}"
    elif delta < -sigma:
        action = "ablate_b"
        hint = f"{label}_peor_que_S0 → ablación B (correr S0+D)"
    else:
        action = "keep_s0"
        hint = "sigma≈0; por defecto S0"
    return {
        "label": label,
        "s0_ok_frac_mean": s0_m,
        "s0_ok_frac_std": s0_s,
        "x_ok_frac_mean": x_m,
        "x_ok_frac_std": x_s,
        "delta_ok_frac": delta,
        "sigma_ref": sigma,
        "action": action,
        "hint": hint,
        "s0_reps": s0.get("reps_scored"),
        "x_reps": x.get("reps_scored"),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("s0", type=Path)
    ap.add_argument("x", type=Path)
    ap.add_argument("--label", default="BD")
    ap.add_argument("-o", "--out", type=Path, required=True)
    args = ap.parse_args()
    out = decide(load_summary(args.s0), load_summary(args.x), args.label)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(out, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
