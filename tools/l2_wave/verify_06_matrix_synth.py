#!/usr/bin/env python3
"""Mini-matriz caso 06: B′ / F / B′+F / fase1-literal (LLM-first).

Gold: must block. Notebook congelado battery20 (todo encontrado → B miss inerte).
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
)

DEFAULT_SUMMARY = (
    ROOT
    / ".tuide/ai/l2_admin_probe/battery20_full_20260924_214909"
    / "06_typo_compialcion"
    / "SUMMARY.json"
)

RUNS = [
    {"id": "R0_baseline", "kwargs": {}, "redecompose": False},
    {
        "id": "R1_Bp_consulta",
        "kwargs": {"counterask_consulta": True},
        "redecompose": False,
    },
    {"id": "R2_F_refute", "kwargs": {"refute_pass": True}, "redecompose": False},
    {
        "id": "R3_Bp_F",
        "kwargs": {"counterask_consulta": True, "refute_pass": True},
        "redecompose": False,
    },
    {
        "id": "R4_fase1_literal",
        "kwargs": {},
        "redecompose": True,
    },
    {
        "id": "R5_literal_Bp_F",
        "kwargs": {"counterask_consulta": True, "refute_pass": True},
        "redecompose": True,
    },
]


def main() -> None:
    summary_path = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SUMMARY)
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    api = summary.get("api") or "http://192.168.64.1:8080/v1"
    model = summary.get("model") or "qwen3.6-27b-q8_0"
    prompt = summary.get("prompt") or ""
    jobs = list(summary.get("jobs") or [])
    for j in jobs:
        j.setdefault("tipo", "explore")

    exam0 = summary.get("verify_exam") or {}
    if not exam0.get("elementos") and (summary_path.parent / "verify_exam.json").is_file():
        exam0 = json.loads((summary_path.parent / "verify_exam.json").read_text())

    out = (
        ROOT
        / ".tuide/ai/l2_admin_probe"
        / f"verify_06_matrix_{time.strftime('%Y%m%d_%H%M%S')}"
    )
    out.mkdir(parents=True, exist_ok=True)
    misses = job_miss_verdicts(jobs)
    print(
        f"CASE {summary_path.parent.name} gold=must_block misses={misses}",
        flush=True,
    )
    print(f"out={out}", flush=True)

    rows = []
    for run in RUNS:
        rid = run["id"]
        print(f"==== {rid} ====", flush=True)
        exam = exam0
        if run.get("redecompose"):
            dlog: list[str] = []
            exam = decompose_claim(api, model, prompt, dlog, literal=True)
            (out / f"{rid}_exam.json").write_text(
                json.dumps(exam, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            (out / f"{rid}_exam.log").write_text("\n".join(dlog) + "\n", encoding="utf-8")
            print(
                f"-- exam_literal ok={exam.get('ok')} "
                f"elems={[e.get('que','')[:60] for e in (exam.get('elementos') or [])]}",
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
            **run["kwargs"],
        )
        wall = round(time.time() - t0, 1)
        (out / f"{rid}.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (out / f"{rid}.log").write_text("\n".join(vlog) + "\n", encoding="utf-8")

        verd = report.get("veredicto")
        row = {
            "id": rid,
            "veredicto": verd,
            "veredicto_modelo": report.get("veredicto_modelo"),
            "veredicto_refutador": report.get("veredicto_refutador"),
            "blocked": blocks_exit(str(verd or "")),
            "hit_gold": blocks_exit(str(verd or "")),
            "counterask_consulta": report.get("counterask_consulta"),
            "refute_pass": report.get("refute_pass"),
            "refute": report.get("refute"),
            "cobertura": report.get("cobertura"),
            "redecompose": bool(run.get("redecompose")),
            "exam_elems": [
                e.get("que") for e in (exam.get("elementos") or [])
            ][:5],
            "why": (report.get("why") or "")[:280],
            "wall": wall,
            "steps": report.get("steps"),
        }
        rows.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)

    helped = [r["id"] for r in rows if r.get("hit_gold") and r["id"] != "R0_baseline"]
    board = {
        "case": summary_path.parent.name,
        "source": str(summary_path),
        "gold_block": True,
        "misses": misses,
        "rows": rows,
        "llm_first_helped": helped,
        "baseline_ok": next(
            (r["hit_gold"] for r in rows if r["id"] == "R0_baseline"), False
        ),
    }
    (out / "COMPARE.json").write_text(
        json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    lines = [
        f"# Mini-matriz 06 (B′ / F / literal)",
        "",
        f"misses={misses}",
        "",
        "| id | verd | modelo | refutador | Bp | F | gold | notes |",
        "|----|------|--------|-----------|----|---|------|-------|",
    ]
    for r in rows:
        notes = []
        if r.get("redecompose"):
            notes.append("fase1_literal")
        if r.get("counterask_consulta"):
            notes.append("Bp_fired")
        lines.append(
            f"| {r['id']} | {r.get('veredicto')} | {r.get('veredicto_modelo')} | "
            f"{r.get('veredicto_refutador')} | {r.get('counterask_consulta')} | "
            f"{r.get('refute_pass')} | {'✓' if r.get('hit_gold') else '✗'} | "
            f"{', '.join(notes)} |"
        )
    lines += [
        "",
        f"**Baseline bloqueaba:** {board['baseline_ok']}",
        f"**Ayudan:** {helped}",
        "",
        "Preferencia: B′ ≥ F ≥ literal (todo LLM; sin rewrite runtime).",
    ]
    for r in rows:
        lines.append(f"\n## {r['id']}")
        lines.append(f"- exam: {r.get('exam_elems')}")
        lines.append(f"- why: {r.get('why')}")
        if r.get("refute"):
            lines.append(f"- refute: {r.get('refute')}")
    (out / "COMPARE.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("DONE", out, flush=True)
    print("helped=", helped, "baseline_ok=", board["baseline_ok"], flush=True)


if __name__ == "__main__":
    main()
