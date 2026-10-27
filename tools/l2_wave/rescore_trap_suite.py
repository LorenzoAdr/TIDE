#!/usr/bin/env python3
"""Vuelve a puntuar una tirada de `trap_suite` sin repetir ni un run.

Las filas de `runs.jsonl` llevan el veredicto del scorer del momento. Cuando el
scorer cambia -- y cambió: medía el proceso, no la respuesta -- la tirada sigue
siendo válida porque cada run guardó su `summary.json` entero. Esto los relee y
reescribe las filas.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from admin_session_metrics import score_session  # noqa: E402
from trap_suite import partial_summary  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--cases", type=Path,
                    default=ROOT / "tools/l2_wave/admin_vibecode_100.json")
    ap.add_argument("--write", action="store_true",
                    help="reescribe runs.jsonl (si no, solo compara)")
    args = ap.parse_args()

    raw = json.loads(args.cases.read_text(encoding="utf-8"))
    by_id = {c["id"]: c for c in (raw if isinstance(raw, list) else raw.get("cases", []))}
    jsonl = args.out / "runs.jsonl"
    rows = [json.loads(l) for l in jsonl.read_text(encoding="utf-8").splitlines() if l.strip()]

    fixed, changed = [], 0
    for old in rows:
        run_dir = args.out / "runs" / (old.get("run_dir") or "")
        case = by_id.get(old["id"])
        sp = run_dir / "summary.json"
        summary = None
        if sp.exists():
            try:
                summary = json.loads(sp.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                summary = None
        if summary is None:
            summary = partial_summary(run_dir, old.get("err"))
        if case is None or summary is None:
            fixed.append(old)  # sin materia prima no se repuntúa
            continue
        new = score_session(case=case, summary=summary,
                            session=summary.get("session"), err=old.get("err"))
        # Lo medido por el runner (tiempo, llamadas, brazo) no lo toca el scorer.
        for k in ("arm", "rep", "wall_sec_measured", "exit", "llm_total", "run_dir",
                  "n_parse_reject", "verify_n", "verify_verdicts", "partial"):
            if k in old:
                new[k] = old[k]
        new["lie"] = bool((new.get("scores") or {}).get("lie", new.get("lie")))
        o, n = old.get("scores") or {}, new.get("scores") or {}
        if o.get("trap_handled") != n.get("trap_handled") or old.get("lie") != new.get("lie"):
            changed += 1
            print(f"  {old['id']:<14} {old.get('arm'):<4} "
                  f"trampa {o.get('trap_handled')}→{n.get('trap_handled')}  "
                  f"miente {old.get('lie')}→{new.get('lie')}  "
                  f"premisa={new.get('premise_verdict')}")
        fixed.append(new)

    print(f"\n{len(fixed)} filas, {changed} cambian de veredicto")
    if args.write:
        bak = jsonl.with_suffix(".jsonl.pre_rescore")
        if not bak.exists():
            bak.write_text(jsonl.read_text(encoding="utf-8"), encoding="utf-8")
        jsonl.write_text("".join(json.dumps(r, ensure_ascii=False) + "\n" for r in fixed),
                         encoding="utf-8")
        print(f"escrito {jsonl} (original en {bak.name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
