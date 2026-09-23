#!/usr/bin/env python3
"""H: brecha T0/T2 por caso. T2 = scoreboards existentes o corrida local.
T0 requiere OPENAI_API_BASE + OPENAI_API_KEY (o ANTHROPIC_*); si faltan, deja H_BLOCKED."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


def load_t2_from_summary(base: Path) -> dict[str, float]:
    for name in ("S0_summary.json", "A4_summary.json", "BD_summary.json", "G_summary.json"):
        p = base / name
        if p.is_file():
            data = json.loads(p.read_text(encoding="utf-8"))
            per = data.get("per_case") or {}
            return {c: float(v.get("mean") or 0) for c, v in per.items()}
    # rebuild from scoreboards
    by: dict[str, list[int]] = {}
    for rep in sorted(base.glob("rep_*")):
        sb = rep / "scoreboard.json"
        if not sb.is_file():
            continue
        rows = json.loads(sb.read_text(encoding="utf-8")).get("rows") or []
        for r in rows:
            by.setdefault(r["case"], []).append(1 if (r.get("ok_gold") or r.get("ok")) else 0)
    return {c: (sum(v) / len(v) if v else 0.0) for c, v in by.items()}


def t0_available() -> tuple[bool, str]:
    if os.environ.get("OPENAI_API_KEY") or os.environ.get("T0_API_KEY"):
        return True, os.environ.get("OPENAI_API_BASE") or os.environ.get("T0_API_BASE") or "https://api.openai.com/v1"
    if os.environ.get("ANTHROPIC_API_KEY"):
        return True, "anthropic"
    return False, ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--t2-base", type=Path, default=Path(".tuide/ai/l2_wave/v2_night/S0"))
    ap.add_argument("--cases", nargs="+", default=["11_restore_session_state", "13_lsp_auto_restart"])
    ap.add_argument("-o", type=Path, required=True)
    args = ap.parse_args()
    t2 = load_t2_from_summary(args.t2_base)
    ok, base = t0_available()
    rows = []
    for c in args.cases:
        rows.append(
            {
                "case": c,
                "t2_ok_frac": t2.get(c),
                "t0_ok_frac": None,
                "gap": None,
                "t0_status": "ready" if ok else "blocked_no_api_key",
            }
        )
    out = {
        "protocol": "H T0/T2 gap",
        "t2_source": str(args.t2_base),
        "t0_available": ok,
        "t0_base": base or None,
        "rows": rows,
        "next": (
            "Exportar system/user de 1 turno piloto por caso y llamar T0 con el mismo JSON; "
            "puntuar con score_sense_battery gold. Env: OPENAI_API_KEY + OPENAI_API_BASE "
            "(OpenAI-compatible) o ANTHROPIC_API_KEY."
            if not ok
            else "T0 listo: falta cablear llamada chat.completions al prompt del piloto."
        ),
    }
    args.o.parent.mkdir(parents=True, exist_ok=True)
    args.o.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not ok:
        (args.o.parent / "H_BLOCKED").write_text("missing T0 API key\n", encoding="utf-8")
    print(json.dumps(out, ensure_ascii=False, indent=2))
    return 0 if ok else 0  # non-fatal: scaffold ok


if __name__ == "__main__":
    raise SystemExit(main())
