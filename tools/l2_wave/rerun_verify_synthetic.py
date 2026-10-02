#!/usr/bin/env python3
"""Re-run verifier only on existing probe SUMMARYs (no explore/pilot).

Default: traps 04/06/09 from battery20 with B (miss counterask) + F (refutador).
Gold: must block (refuta|dudoso).
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from verify_lite_local import blocks_exit, job_miss_verdicts, run_verifier  # noqa: E402

DEFAULT_MEASURE = (
    ROOT / ".tuide/ai/l2_admin_probe/battery20_full_20260924_214909"
)
DEFAULT_CASES = [
    "04_deseo_console_gutter",
    "06_typo_compialcion",
    "09_multipolo_console_and_style",
]


def main() -> None:
    measure = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_MEASURE)
    out = (
        ROOT
        / ".tuide/ai/l2_admin_probe"
        / f"verify_bf_synth_{time.strftime('%Y%m%d_%H%M%S')}"
    )
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for cid in DEFAULT_CASES:
        cdir = measure / cid
        summary_path = cdir / "SUMMARY.json"
        if not summary_path.is_file():
            print(f"SKIP {cid}: no SUMMARY", flush=True)
            continue
        summary = json.loads(summary_path.read_text())
        api = summary.get("api") or "http://192.168.64.1:8080/v1"
        model = summary.get("model") or "qwen3.6-27b-q8_0"
        prompt = summary.get("prompt") or ""
        jobs = list(summary.get("jobs") or [])
        for j in jobs:
            j.setdefault("tipo", "explore")
        exam = summary.get("verify_exam") or {}
        if not exam.get("elementos") and (cdir / "verify_exam.json").is_file():
            exam = json.loads((cdir / "verify_exam.json").read_text())

        old = summary.get("verify_reports") or []
        old_last = old[-1].get("veredicto") if old else None
        old_real = [
            r.get("veredicto")
            for r in old
            if not r.get("skipped") and "omite" not in str(r.get("why") or "").lower()
        ]
        old_any_block = any(blocks_exit(str(v or "")) for v in old_real)

        c_out = out / cid
        c_out.mkdir(parents=True, exist_ok=True)
        misses = job_miss_verdicts(jobs)

        print(
            f"==== {cid} old_last={old_last} old_block={old_any_block} "
            f"misses={[(m['id'], m['veredicto']) for m in misses]} ====",
            flush=True,
        )
        vlog: list[str] = []
        t0 = time.time()
        report = run_verifier(
            api,
            model,
            prompt,
            jobs,
            thesis="",
            root=ROOT,
            log=vlog,
            max_steps=4,
            exam=exam if exam.get("elementos") else None,
            variant="exam_hard" if exam.get("elementos") else "anchors",
            counterask=True,
            refute_pass=True,
        )
        wall = round(time.time() - t0, 1)
        (c_out / "verify_bf.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (c_out / "verify_bf.log").write_text("\n".join(vlog) + "\n", encoding="utf-8")

        verd = report.get("veredicto")
        blocked = blocks_exit(str(verd or ""))
        row = {
            "id": cid,
            "gold_block": True,
            "misses": misses,
            "old_last": old_last,
            "old_verdicts": [r.get("veredicto") for r in old],
            "old_any_block": old_any_block,
            "new": verd,
            "new_modelo": report.get("veredicto_modelo"),
            "new_refutador": report.get("veredicto_refutador"),
            "blocked": blocked,
            "hit_gold": blocked,
            "counterask": report.get("counterask"),
            "refute_pass": report.get("refute_pass"),
            "refute": report.get("refute"),
            "downgraded": report.get("downgraded"),
            "cobertura": report.get("cobertura"),
            "new_why": (report.get("why") or "")[:280],
            "steps": report.get("steps"),
            "wall": wall,
        }
        rows.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)

    n_ok = sum(1 for r in rows if r.get("hit_gold"))
    board = {
        "measure": str(measure),
        "out": str(out),
        "lever": "B_counterask+F_refute",
        "rows": rows,
        "ok_frac": (n_ok / len(rows)) if rows else 0.0,
        "helped_vs_old": [
            r["id"]
            for r in rows
            if r.get("hit_gold") and not r.get("old_any_block")
        ],
    }
    (out / "COMPARE.json").write_text(
        json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    lines = [
        "# Verify B+F synth — traps 04/06/09",
        "",
        f"Fuente: `{measure}`",
        f"ok_frac={board['ok_frac']:.2f}  helped_vs_old={board['helped_vs_old']}",
        "",
        "| caso | misses | old | new | modelo | refutador | B | F | gold |",
        "|------|--------|-----|-----|--------|-----------|---|---|------|",
    ]
    for r in rows:
        miss_s = (
            ",".join(f"{m['id']}:{m['veredicto']}" for m in (r.get("misses") or []))
            or "—"
        )
        lines.append(
            f"| {r['id']} | {miss_s} | {r['old_last']} | {r['new']} | "
            f"{r.get('new_modelo')} | {r.get('new_refutador')} | "
            f"{r.get('counterask')} | {r.get('refute_pass')} | "
            f"{'✓' if r.get('hit_gold') else '✗'} |"
        )
    lines.append("")
    for r in rows:
        lines.append(f"## {r['id']}")
        lines.append(f"- why: {r.get('new_why')}")
        lines.append(f"- cob: {r.get('cobertura')}")
        if r.get("refute"):
            lines.append(f"- refute: {r.get('refute')}")
        lines.append("")
    (out / "COMPARE.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("DONE", out, flush=True)
    print("ok_frac=", board["ok_frac"], "helped=", board["helped_vs_old"], flush=True)


if __name__ == "__main__":
    main()
