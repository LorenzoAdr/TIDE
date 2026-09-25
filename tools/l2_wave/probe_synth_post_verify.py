#!/usr/bin/env python3
"""Synthetic: resume pilot AFTER verify block, with +1 explore bonus.

Reuses frozen SUMMARY (jobs + verify report + exam). Does not re-run prior explores.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from explore_lite_local import run_explorer  # noqa: E402
from probe_admin_pilot import (  # noqa: E402
    ADMIN_SYS,
    CONFIRM_CLOSE_USER,
    CONFIRM_EDIT_USER,
    chat,
    explore_cap,
    extract_json,
    legal_line,
    notebook_md,
    parse_admin,
    soft_nudge_for_verdict,
)
from verify_lite_local import format_pilot_verify_report, run_verifier  # noqa: E402

DEFAULT_SUMMARY = (
    ROOT
    / ".tuide/ai/l2_admin_probe/pilot_after_verify_04_20260924_211551"
    / "04_deseo_console_gutter"
    / "SUMMARY.json"
)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--max-explore-base", type=int, default=4)
    args = ap.parse_args()

    summary = json.loads(args.summary.read_text(encoding="utf-8"))
    api = summary.get("api") or "http://192.168.64.1:8080/v1"
    model = summary.get("model") or "qwen3.6-27b-q8_0"
    prompt = summary.get("prompt") or ""
    jobs = list(summary.get("jobs") or [])
    # restore explore-shaped fields used by notebook_md / verify
    for j in jobs:
        j.setdefault("tipo", "explore")
        j.setdefault("why", (j.get("summary_head") or "")[:400])

    exam = summary.get("verify_exam") or {}
    if not exam.get("elementos") and (args.summary.parent / "verify_exam.json").is_file():
        exam = json.loads((args.summary.parent / "verify_exam.json").read_text())

    reports = summary.get("verify_reports") or []
    if not reports and (args.summary.parent / "verify_1.json").is_file():
        reports = [json.loads((args.summary.parent / "verify_1.json").read_text())]
    report0 = reports[0] if reports else {"veredicto": "dudoso", "why": "sin report"}

    out = args.out or (
        ROOT
        / ".tuide/ai/l2_admin_probe"
        / f"synth_post_verify_{time.strftime('%Y%m%d_%H%M%S')}"
    )
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)

    explores = len([j for j in jobs if j.get("tipo") == "explore"])
    max_explore = args.max_explore_base
    verify_reject_pending = True
    cap = explore_cap(max_explore=max_explore, verify_reject_pending=True)
    block = format_pilot_verify_report(report0, exam)

    print(
        f"RESUME explores={explores}/{max_explore} cap_with_bonus={cap} "
        f"verd={report0.get('veredicto')}",
        flush=True,
    )
    print(block[:900], flush=True)

    user0 = (
        f"## Consulta\n{prompt}\n\n"
        f"## Presupuesto\nexplore={explores}/{max_explore} "
        f"(bonus post-verify → cap {cap})\n"
        f"{legal_line(explores=explores, max_explore=max_explore, verify_reject_pending=True)}\n\n"
        f"{block}\n"
        f"## NOTEBOOK\n{notebook_md(jobs)}\n\n"
        "Elige UNA acción (JSON admin_v1)."
    )
    msgs = [
        {"role": "system", "content": ADMIN_SYS},
        {"role": "user", "content": user0},
    ]
    (out / "seed_user.md").write_text(user0, encoding="utf-8")
    (out / "verify_report_to_pilot.md").write_text(block, encoding="utf-8")

    awaiting_confirm: str | None = None
    verify_count = 1  # already used one verify pass in frozen run
    verify_reports = list(reports)
    closed = None
    turns: list[dict] = []
    log: list[str] = []
    t0 = time.time()

    for turn in range(args.max_turns):
        raw = chat(api, model, msgs)
        log.append(f"pilot_raw[{turn}]: {raw[:1200]}")
        try:
            act = extract_json(raw)
        except Exception as e:  # noqa: BLE001
            msgs.append({"role": "assistant", "content": raw})
            msgs.append({"role": "user", "content": f"JSON inválido ({e})."})
            continue

        parsed, perr = parse_admin(
            act,
            awaiting_confirm=awaiting_confirm,
            verify_reject_pending=verify_reject_pending,
            explores=explores,
            max_explore=max_explore,
        )
        turns.append({"turn": turn, "act": act, "parse_error": perr})
        print(
            f"==== turn {turn} do={act.get('do')!r} explore={explores}/{max_explore} "
            f"cap={explore_cap(max_explore=max_explore, verify_reject_pending=verify_reject_pending)} "
            f"err={perr or '-'} ====",
            flush=True,
        )
        print(json.dumps(act, ensure_ascii=False)[:500], flush=True)

        if perr:
            msgs.append({"role": "assistant", "content": raw})
            msgs.append(
                {
                    "role": "user",
                    "content": (
                        f"Rechazado: {perr}. "
                        + legal_line(
                            explores=explores,
                            max_explore=max_explore,
                            verify_reject_pending=verify_reject_pending,
                        )
                    ),
                }
            )
            continue

        assert parsed is not None
        do = parsed["do"]

        if do == "seguir_explorando":
            verify_reject_pending = False
            print(
                f"-- seguir_explorando brief={parsed['spawn']['brief'][:160]!r}",
                flush=True,
            )
            parsed = {
                "do": "spawn",
                "why": parsed["why"],
                "spawn": parsed["spawn"],
                "reply": parsed.get("reply") or "",
            }
            do = "spawn"

        if do == "ask_user":
            closed = {**parsed, "kind": "ask"}
            print("-- EXIT ask_user", flush=True)
            break

        if do == "editar":
            # second verify pass allowed
            vlog: list[str] = []
            report = run_verifier(
                api,
                model,
                prompt,
                jobs,
                "",
                ROOT,
                vlog,
                max_steps=4,
                exam=exam,
                variant="exam_hard",
                counterask=True,
                refute_pass=True,
            )
            verify_count += 1
            verify_reports.append({"trigger": "editar", **report})
            (out / f"verify_{verify_count}.json").write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            from verify_lite_local import blocks_exit  # noqa: PLC0415

            if blocks_exit(str(report.get("veredicto") or "")):
                verify_reject_pending = True
                block2 = format_pilot_verify_report(report, exam)
                print(f"-- editar BLOQUEA {report.get('veredicto')}", flush=True)
                msgs.append({"role": "assistant", "content": raw})
                msgs.append(
                    {
                        "role": "user",
                        "content": block2
                        + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n"
                        + legal_line(
                            explores=explores,
                            max_explore=max_explore,
                            verify_reject_pending=True,
                        ),
                    }
                )
                continue
            awaiting_confirm = "edit"
            verify_reject_pending = False
            msgs.append({"role": "assistant", "content": raw})
            msgs.append(
                {
                    "role": "user",
                    "content": CONFIRM_EDIT_USER + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n",
                }
            )
            continue

        if do == "cerrar" and awaiting_confirm != "edit":
            vlog = []
            report = run_verifier(
                api,
                model,
                prompt,
                jobs,
                "",
                ROOT,
                vlog,
                max_steps=4,
                exam=exam,
                variant="exam_hard",
                counterask=True,
                refute_pass=True,
            )
            verify_count += 1
            verify_reports.append({"trigger": "cerrar", **report})
            (out / f"verify_{verify_count}.json").write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            from verify_lite_local import blocks_exit  # noqa: PLC0415

            if blocks_exit(str(report.get("veredicto") or "")):
                verify_reject_pending = True
                block2 = format_pilot_verify_report(report, exam)
                print(f"-- cerrar BLOQUEA {report.get('veredicto')}", flush=True)
                msgs.append({"role": "assistant", "content": raw})
                msgs.append(
                    {
                        "role": "user",
                        "content": block2
                        + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n"
                        + legal_line(
                            explores=explores,
                            max_explore=max_explore,
                            verify_reject_pending=True,
                        ),
                    }
                )
                continue
            awaiting_confirm = "close"
            verify_reject_pending = False
            msgs.append({"role": "assistant", "content": raw})
            msgs.append(
                {
                    "role": "user",
                    "content": CONFIRM_CLOSE_USER + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n",
                }
            )
            continue

        if do == "confirmar_editar":
            closed = {**parsed, "kind": "edit", "confirmed": True}
            print("-- EXIT confirmar_editar", flush=True)
            break
        if do == "confirmar_cerrar":
            closed = {**parsed, "kind": "close", "confirmed": True}
            print("-- EXIT confirmar_cerrar", flush=True)
            break
        if do == "cerrar" and awaiting_confirm == "edit":
            closed = {**parsed, "kind": "close"}
            print("-- EXIT cerrar (from edit confirm)", flush=True)
            break

        if do != "spawn":
            msgs.append({"role": "assistant", "content": raw})
            msgs.append({"role": "user", "content": "Usa spawn|cerrar|editar|ask_user."})
            continue

        # spawn explore (bonus)
        verify_reject_pending = False
        brief = parsed["spawn"]["brief"]
        explores += 1
        jid = len(jobs) + 1
        print(f"-- BONUS explore #{explores} consulta={brief!r}", flush=True)
        child_log: list[str] = []
        job = run_explorer(api, model, brief, ROOT, child_log, tools=["grep", "read"])
        (out / f"explore_job{jid}.log").write_text("\n".join(child_log), encoding="utf-8")
        (out / f"explore_job{jid}.json").write_text(
            json.dumps(job, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        try:
            cerrado_obj = json.loads(job.get("cerrado") or "{}")
        except json.JSONDecodeError:
            cerrado_obj = {}
        falta = job.get("falta") or cerrado_obj.get("falta") or []
        result = {
            "ok": True,
            "veredicto": job.get("veredicto") or cerrado_obj.get("veredicto") or "parcial",
            "summary": (job.get("cerrado") or "")[:1200],
            "why": job.get("why") or cerrado_obj.get("why") or "",
            "simbolos": cerrado_obj.get("simbolos") or [],
            "evidencia": cerrado_obj.get("evidencia") or [],
            "falta": falta,
            "paths": [v for v in (job.get("visto") or []) if "/" in str(v)][:12],
        }
        print(
            f"-- explore done verd={result['veredicto']} falta={falta!r}",
            flush=True,
        )
        jobs.append({"id": jid, "tipo": "explore", "brief": brief, "arg": "", **result})
        nudge = soft_nudge_for_verdict(str(result["veredicto"]))
        msgs.append({"role": "assistant", "content": raw})
        msgs.append(
            {
                "role": "user",
                "content": (
                    f"## Resultado spawn (bonus post-verify)\n"
                    f"veredicto={result['veredicto']} falta={result['falta']}\n"
                    f"simbolos={result['simbolos']}\n"
                    f"why: {(result['why'] or '')[:400]}\n{nudge}\n"
                    f"## Presupuesto\nexplore={explores}/{max_explore} (bonus gastado)\n"
                    f"{legal_line(explores=explores, max_explore=max_explore, verify_reject_pending=False)}\n\n"
                    f"## NOTEBOOK\n{notebook_md(jobs)}\n\n"
                    "Elige UNA acción (JSON admin_v1)."
                ),
            }
        )

    board = {
        "source": str(args.summary),
        "wall": round(time.time() - t0, 1),
        "explores_end": explores,
        "max_explore_base": max_explore,
        "closed": closed,
        "turns": turns,
        "verify_reports": [
            {"trigger": r.get("trigger"), "veredicto": r.get("veredicto"), "cobertura": r.get("cobertura")}
            for r in verify_reports
        ],
        "last_briefs": [j.get("brief") for j in jobs[-3:]],
    }
    (out / "RESUME.json").write_text(json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8")
    (out / "lite.log").write_text("\n".join(log) + "\n", encoding="utf-8")
    lines = [
        "# Synth post-verify (+1 explore)",
        f"source: `{args.summary}`",
        f"exit: {(closed or {}).get('do')}",
        f"explores_end: {explores} (base {max_explore})",
        f"verify: {board['verify_reports']}",
        "",
        "## Turns",
    ]
    for t in turns:
        act = t.get("act") or {}
        lines.append(
            f"- turn{t['turn']}: do={act.get('do')} "
            f"brief={(act.get('spawn') or {}).get('brief', '')[:100]!r} "
            f"err={t.get('parse_error')}"
        )
    (out / "RESUME.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("DONE", out, flush=True)
    print(json.dumps({"exit": (closed or {}).get("do"), "explores": explores, "turns": len(turns)}, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
