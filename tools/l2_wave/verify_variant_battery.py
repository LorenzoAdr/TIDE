#!/usr/bin/env python3
"""Synthetic micro-battery: compare verifier variants on ONE frozen explore.

Default case: 04_deseo_console_gutter from focus5 (trap where anchors-only said sostienen).
Gold: must BLOCK (refuta|dudoso) — console→gutter bridge is not in explore anchors.

Variants:
  A anchors     — current (consulta + anclas)
  B exam        — fase1 decompose prompt → fase2 verify with exam (soft)
  C exam_hard   — like B + runtime: sostienen+hueco → dudoso
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
    run_verifier,
)

DEFAULT_SUMMARY = (
    ROOT
    / ".tuide/ai/l2_admin_probe/focus5_20260924_195052/04_deseo_console_gutter/SUMMARY.json"
)

VARIANTS = ("anchors", "exam", "exam_hard")


def main() -> None:
    summary_path = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SUMMARY)
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    api = summary.get("api") or "http://192.168.64.1:8080/v1"
    model = summary.get("model") or "qwen3.6-27b-q8_0"
    prompt = summary.get("prompt") or ""
    jobs = summary.get("jobs") or []
    old = summary.get("verify_reports") or []
    old_last = old[-1].get("veredicto") if old else None

    out = ROOT / ".tuide/ai/l2_admin_probe" / f"verify_variants_1_{time.strftime('%Y%m%d_%H%M%S')}"
    out.mkdir(parents=True, exist_ok=True)
    (out / "source.json").write_text(
        json.dumps(
            {"summary": str(summary_path), "prompt": prompt, "old_last": old_last},
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    # Gold for this trap: block exit (do NOT sostienen)
    gold_block = True

    print(f"CASE {summary_path.parent.name}", flush=True)
    print(f"old_verify={old_last}  gold=must_block={gold_block}", flush=True)
    print(f"out={out}", flush=True)

    dlog: list[str] = []
    print("==== fase1 decompose ====", flush=True)
    t0 = time.time()
    exam = decompose_claim(api, model, prompt, dlog)
    (out / "exam.json").write_text(json.dumps(exam, ensure_ascii=False, indent=2), encoding="utf-8")
    (out / "exam.log").write_text("\n".join(dlog) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "ok": exam.get("ok"),
                "n_elem": len(exam.get("elementos") or []),
                "n_arc": len(exam.get("arcos") or []),
                "elementos": exam.get("elementos"),
                "arcos": exam.get("arcos"),
                "wall": round(time.time() - t0, 1),
            },
            ensure_ascii=False,
            indent=2,
        ),
        flush=True,
    )

    rows = []
    for variant in VARIANTS:
        print(f"==== variant={variant} ====", flush=True)
        vlog: list[str] = []
        t1 = time.time()
        report = run_verifier(
            api,
            model,
            prompt,
            jobs,
            thesis="",
            root=ROOT,
            log=vlog,
            max_steps=4,
            exam=exam if variant != "anchors" else None,
            variant=variant,
        )
        wall = round(time.time() - t1, 1)
        (out / f"{variant}.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (out / f"{variant}.log").write_text("\n".join(vlog) + "\n", encoding="utf-8")
        verd = report.get("veredicto")
        blocked = blocks_exit(str(verd or ""))
        hit_gold = blocked == gold_block
        row = {
            "variant": variant,
            "veredicto": verd,
            "veredicto_modelo": report.get("veredicto_modelo"),
            "downgraded": report.get("downgraded"),
            "blocked": blocked,
            "hit_gold": hit_gold,
            "cobertura": report.get("cobertura"),
            "arco": report.get("arco"),
            "ataques": (report.get("ataques") or [])[:3],
            "why": (report.get("why") or "")[:280],
            "steps": report.get("steps"),
            "wall": wall,
        }
        rows.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)

    board = {
        "case": summary_path.parent.name,
        "source": str(summary_path),
        "old_last": old_last,
        "gold_block": gold_block,
        "exam": {
            "elementos": exam.get("elementos"),
            "arcos": exam.get("arcos"),
            "ok": exam.get("ok"),
        },
        "rows": rows,
        "winner": sorted(
            rows, key=lambda r: (not r["hit_gold"], r["veredicto"] != "refuta", r["wall"])
        )[0]["variant"]
        if rows
        else None,
    }
    (out / "COMPARE.json").write_text(
        json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    lines = [
        f"# Verify variants ×1 ({summary_path.parent.name})",
        "",
        f"Fuente: `{summary_path}`",
        f"old_verify={old_last}  gold=must_block",
        "",
        "## Examen (fase 1)",
    ]
    for el in exam.get("elementos") or []:
        lines.append(f"- [{el.get('id')}] {el.get('que')}")
    for ar in exam.get("arcos") or []:
        lines.append(f"- arco {ar.get('de')}→{ar.get('a')}: {ar.get('que')}")
    lines += ["", "| variant | verd | blocked | gold? | notes |", "|---------|------|---------|-------|-------|"]
    for r in rows:
        note = ""
        if r.get("downgraded"):
            note = f"downgrade {r.get('veredicto_modelo')}→{r.get('veredicto')}"
        lines.append(
            f"| {r['variant']} | {r['veredicto']} | {r['blocked']} | "
            f"{'✓' if r['hit_gold'] else '✗'} | {note} |"
        )
    lines.append("")
    lines.append(f"**winner (hit gold, prefer refuta):** `{board['winner']}`")
    for r in rows:
        lines.append(f"\n## {r['variant']}\n- why: {r['why']}\n- cobertura: {r['cobertura']}")
    (out / "COMPARE.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("DONE", out, flush=True)
    print("winner=", board["winner"], flush=True)


if __name__ == "__main__":
    main()
