#!/usr/bin/env python3
"""Synthetic: isolate general verifier measures on ONE frozen trap (default 04).

Does NOT re-run explore. Gold: must BLOCK (refuta|dudoso).

Measures (one at a time on top of exam_hard baseline):
  M0 baseline     — exam_hard alone
  M1 job_verdicts — show notebook job verdicto in prompt (soft)
  M2 miss_blocks  — runtime: any no_encontrado/parcial → no sostienen
  M3 all_covered  — runtime: all-cubierto + miss jobs → no sostienen
  M4 skip_policy  — offline: omit-verify must not be sostienen (case 09 logic)
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from verify_lite_local import (  # noqa: E402
    blocks_exit,
    decompose_claim,
    job_miss_verdicts,
    run_verifier,
    skip_verify_verdict,
)

DEFAULT_SUMMARY = (
    ROOT
    / ".tuide/ai/l2_admin_probe/battery20_full_20260924_214909"
    / "04_deseo_console_gutter"
    / "SUMMARY.json"
)


def main() -> None:
    summary_path = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SUMMARY)
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    api = summary.get("api") or "http://192.168.64.1:8080/v1"
    model = summary.get("model") or "qwen3.6-27b-q8_0"
    prompt = summary.get("prompt") or ""
    jobs = list(summary.get("jobs") or [])
    for j in jobs:
        j.setdefault("tipo", "explore")

    old = summary.get("verify_reports") or []
    old_last = old[-1].get("veredicto") if old else None
    exam = summary.get("verify_exam") or {}
    if not exam.get("elementos") and (summary_path.parent / "verify_exam.json").is_file():
        exam = json.loads((summary_path.parent / "verify_exam.json").read_text())

    out = ROOT / ".tuide/ai/l2_admin_probe" / f"verify_measures_04_{time.strftime('%Y%m%d_%H%M%S')}"
    out.mkdir(parents=True, exist_ok=True)

    print(f"CASE {summary_path.parent.name} old_verify={old_last} gold=must_block", flush=True)
    print(f"job_misses={job_miss_verdicts(jobs)}", flush=True)
    print(f"out={out}", flush=True)

    if not exam.get("elementos"):
        print("==== decompose (no cached exam) ====", flush=True)
        exam = decompose_claim(api, model, prompt, [])
    (out / "exam.json").write_text(json.dumps(exam, ensure_ascii=False, indent=2), encoding="utf-8")

    measures = [
        {
            "id": "M0_baseline",
            "kwargs": {},
        },
        {
            "id": "M1_show_job_verdicts",
            "kwargs": {"show_job_verdicts": True},
        },
        {
            "id": "M2_block_if_job_miss",
            "kwargs": {"block_sostiene_if_job_miss": True},
        },
        {
            "id": "M3_block_all_covered_with_miss",
            "kwargs": {"block_sostiene_if_all_covered_with_miss": True},
        },
    ]

    rows = []
    for m in measures:
        mid = m["id"]
        print(f"==== {mid} ====", flush=True)
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
            exam=exam,
            variant="exam_hard",
            **m["kwargs"],
        )
        wall = round(time.time() - t0, 1)
        (out / f"{mid}.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (out / f"{mid}.log").write_text("\n".join(vlog) + "\n", encoding="utf-8")
        verd = report.get("veredicto")
        blocked = blocks_exit(str(verd or ""))
        row = {
            "measure": mid,
            "veredicto": verd,
            "veredicto_modelo": report.get("veredicto_modelo"),
            "downgraded": report.get("downgraded"),
            "downgrade_reasons": report.get("downgrade_reasons"),
            "blocked": blocked,
            "hit_gold": blocked,
            "cobertura": report.get("cobertura"),
            "why": (report.get("why") or "")[:240],
            "steps": report.get("steps"),
            "wall": wall,
        }
        rows.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)

    # M4 offline: skip policy (case 09 style)
    prior_refuta = [{"veredicto": "refuta"}, {"veredicto": "refuta"}]
    skip_old = "sostiene"  # current probe behavior
    skip_new = skip_verify_verdict(prior_refuta)
    m4 = {
        "measure": "M4_skip_omit_not_sostiene",
        "veredicto": skip_new,
        "veredicto_modelo": skip_old,
        "downgraded": skip_new != skip_old,
        "downgrade_reasons": ["omit_cap→dudoso"],
        "blocked": blocks_exit(skip_new),
        "hit_gold": blocks_exit(skip_new),
        "cobertura": None,
        "why": "política: tope verify no absuelve (antes omitía como sostienen)",
        "steps": 0,
        "wall": 0.0,
    }
    rows.append(m4)
    print(f"==== {m4['measure']} ====", flush=True)
    print(json.dumps(m4, ensure_ascii=False), flush=True)

    board = {
        "case": summary_path.parent.name,
        "source": str(summary_path),
        "old_last": old_last,
        "gold_block": True,
        "job_misses": job_miss_verdicts(jobs),
        "rows": rows,
        "helped": [r["measure"] for r in rows if r["hit_gold"] and r["measure"] != "M0_baseline"],
        "baseline_ok": next((r["hit_gold"] for r in rows if r["measure"] == "M0_baseline"), False),
    }
    (out / "COMPARE.json").write_text(json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = [
        f"# Verify measures synth ({summary_path.parent.name})",
        "",
        f"old_verify={old_last}  gold=must_block  misses={board['job_misses']}",
        "",
        "| measure | verd | modelo | blocked | gold? | reasons |",
        "|---------|------|--------|---------|-------|---------|",
    ]
    for r in rows:
        lines.append(
            f"| {r['measure']} | {r['veredicto']} | {r.get('veredicto_modelo')} | "
            f"{r['blocked']} | {'✓' if r['hit_gold'] else '✗'} | {r.get('downgrade_reasons')} |"
        )
    lines.append("")
    lines.append(f"**Ayudan (bloquean y no son baseline):** {board['helped']}")
    lines.append(f"**Baseline ya bloqueaba:** {board['baseline_ok']}")
    for r in rows:
        lines.append(f"\n## {r['measure']}\n- why: {r.get('why')}\n- cob: {r.get('cobertura')}")
    (out / "COMPARE.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("DONE", out, flush=True)
    print("helped=", board["helped"], "baseline_ok=", board["baseline_ok"], flush=True)


if __name__ == "__main__":
    main()
