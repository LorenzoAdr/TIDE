#!/usr/bin/env python3
"""Resume A4: media/σ de scoreboard por caso sobre rep_*."""

from __future__ import annotations

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


def main() -> int:
    base = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    by_case: dict[str, list[int]] = {}
    ok_fracs: list[float] = []
    reps = 0
    for rep in sorted(base.glob("rep_*")):
        sb = rep / "scoreboard.json"
        if not sb.is_file():
            continue
        data = json.loads(sb.read_text(encoding="utf-8"))
        reps += 1
        rows = data.get("rows") or []
        oks = sum(1 for r in rows if r.get("ok"))
        ok_fracs.append(oks / len(rows) if rows else 0.0)
        for r in rows:
            by_case.setdefault(r["case"], []).append(1 if r.get("ok") else 0)
    per_case = {}
    for case, vals in by_case.items():
        m, s = mean_std([float(v) for v in vals])
        per_case[case] = {"n": len(vals), "mean": m, "std": s, "oks": vals}
    overall_m, overall_s = mean_std(ok_fracs)
    out = {
        "reps_scored": reps,
        "ok_frac_mean": overall_m,
        "ok_frac_std": overall_s,
        "per_case": per_case,
    }
    (base / "A4_summary.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(out, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
