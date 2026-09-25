#!/usr/bin/env python3
"""LLM-first verifier matrix (plan: verify_llm_first_test_plan.md).

Runtime may reject form / re-ask / preseed tools — must NOT rewrite veredicto
except in CONTROL_* rows (M2/M3/M4).

Default case: 04 frozen from battery20. Gold: must block.
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from explore_lite_local import chat, extract_json, tool_entre, tool_inbody, tool_read  # noqa: E402
from verify_lite_local import (  # noqa: E402
    VERIFY_SYS_EXAM,
    blocks_exit,
    format_exam,
    format_job_verdicts,
    inflate_anchors,
    job_miss_verdicts,
    notebook_paths,
    path_allowed,
    run_verifier,
    skip_verify_verdict,
)

DEFAULT_SUMMARY = (
    ROOT
    / ".tuide/ai/l2_admin_probe/battery20_full_20260924_214909"
    / "04_deseo_console_gutter"
    / "SUMMARY.json"
)

MISS = {"no_encontrado", "parcial", "no_hay", "no_concluyente", "no"}


def _sym_or_path(job: dict) -> str:
    syms = job.get("simbolos") or []
    if syms:
        return str(syms[0])
    paths = job.get("paths") or []
    for p in paths:
        s = str(p).strip()
        if s and "/" in s:
            return s.split(":")[0]
    return ""


def _job_by_id(jobs: list[dict], jid) -> dict | None:
    for j in jobs:
        if j.get("id") == jid or str(j.get("id")) == str(jid):
            return j
    return None


def _cubierto_job_ids_illegal(cobertura: list, jobs: list[dict]) -> str | None:
    """Return error string if any cubierto lacks non-miss job_ids."""
    for c in cobertura:
        if not isinstance(c, dict):
            continue
        if str(c.get("estado") or "").lower() != "cubierto":
            continue
        raw_ids = c.get("job_ids")
        if raw_ids is None:
            raw_ids = []
        if isinstance(raw_ids, (str, int)):
            raw_ids = [raw_ids]
        if not isinstance(raw_ids, list) or not raw_ids:
            return f"cubierto {c.get('id')}: exige job_ids de jobs no-miss"
        ok = False
        for jid in raw_ids:
            j = _job_by_id(jobs, jid)
            if j is None:
                continue
            v = str(j.get("veredicto") or "").lower()
            if v and v not in MISS:
                ok = True
                break
        if not ok:
            return (
                f"cubierto {c.get('id')}: job_ids={raw_ids} no citan ningún job "
                f"encontrado (solo miss/ausentes)"
            )
    return None


def _filter_jobs(jobs: list[dict], *, only_miss: bool) -> list[dict]:
    out = []
    for j in jobs:
        v = str(j.get("veredicto") or "").lower()
        is_miss = v in MISS
        if only_miss and is_miss:
            out.append(j)
        elif not only_miss and not is_miss:
            out.append(j)
    return out


def run_llm_first(
    api: str,
    model: str,
    consulta: str,
    jobs: list[dict],
    exam: dict,
    root: Path,
    log: list[str],
    *,
    max_steps: int = 4,
    require_job_ids: bool = False,
    counterask: bool = False,
    show_verdicts: bool = False,
    cite_obligation: bool = False,
    preseed_entre: bool = False,
    two_pass: bool = False,
) -> dict:
    """Verifier loop: LLM decides; runtime only form-reject / counterask / tools."""
    allowed = notebook_paths(jobs)
    use_jobs = _filter_jobs(jobs, only_miss=True) if two_pass else jobs
    if two_pass and not use_jobs:
        use_jobs = jobs
    anchors = inflate_anchors(use_jobs, root)
    misses = job_miss_verdicts(jobs)

    sys = VERIFY_SYS_EXAM.replace("N olas", f"{max_steps} olas")
    sys += (
        "\nAl cerrar, cobertura[] por cada elemento del examen. "
        "Co-ocurrencia ≠ puente. Si un polo del examen no está demostrado: hueco."
    )
    if cite_obligation or require_job_ids:
        sys += (
            "\nSi estado=cubierto, incluye job_ids:[…] de jobs del notebook con "
            "veredicto encontrado (no no_encontrado/parcial) y anclas:[path:sym]."
        )
    if show_verdicts or cite_obligation:
        sys += (
            "\nLos veredictos tipados del notebook son hechos: un no_encontrado/"
            "parcial no se borra porque otro job encontró otra cosa."
        )

    parts = [
        f"## Consulta del usuario\n{consulta}\n",
        format_exam(exam),
    ]
    if show_verdicts or cite_obligation or counterask:
        parts.append(format_job_verdicts(jobs))
    if two_pass:
        parts.append(
            "## Anclas (pase A: solo jobs miss/parcial)\n" + anchors
            if _filter_jobs(jobs, only_miss=True)
            else "## Anclas\n" + anchors
        )
    else:
        parts.append("## Anclas (sin narrativa)\n" + anchors)
    parts.append(f"## Paths legibles\n{sorted(allowed)[:40]}")
    parts.append(f"N={max_steps}. Califica el EXAMEN. UNA acción JSON.")
    user0 = "\n\n".join(parts)

    messages: list[dict] = [
        {"role": "system", "content": sys},
        {"role": "user", "content": user0},
    ]

    if preseed_entre:
        miss_jobs = job_miss_verdicts(jobs)
        hit_jobs = [j for j in jobs if str(j.get("veredicto") or "").lower() not in MISS]
        if miss_jobs and hit_jobs:
            jm = _job_by_id(jobs, miss_jobs[0]["id"]) or {}
            jh = hit_jobs[0]
            a, b = _sym_or_path(jm), _sym_or_path(jh)
            if a and b:
                res = tool_entre(a, b, root)
                log.append(f"preseed_entre: {a} → {b}")
                messages.append(
                    {
                        "role": "user",
                        "content": (
                            f"## Resultado entre (miss job{jm.get('id')} ↔ hit job{jh.get('id')})\n"
                            f"from=`{a}` to=`{b}`\n```\n{res[:3000]}\n```\n"
                            "Úsalo al calificar arcos del examen."
                        ),
                    }
                )

    form_retry_used = False
    counterask_used = False
    two_pass_injected = False
    entres = inbodies = reads = 0
    last = {
        "veredicto": "dudoso",
        "ataques": ["tope sin cerrar"],
        "arco": {"de": "", "a": ""},
        "why": "tope de pasos sin cerrar",
        "cobertura": [],
        "veredicto_modelo": "dudoso",
        "form_retries": 0,
        "counterask": False,
        "steps": 0,
    }

    # Allow a couple of extra turns for form/counterask without eating tool budget badly
    extra = (1 if require_job_ids else 0) + (1 if counterask else 0) + (1 if two_pass else 0)
    for step in range(max_steps + extra):
        last_wave = step >= max_steps + extra - 1
        if two_pass and not two_pass_injected and step >= max(1, max_steps // 2):
            hit_jobs = _filter_jobs(jobs, only_miss=False)
            if hit_jobs:
                messages.append(
                    {
                        "role": "user",
                        "content": (
                            "## Anclas adicionales (pase B: jobs encontrado)\n"
                            + inflate_anchors(hit_jobs, root)
                            + "\nCalifica de nuevo el examen completo; no absuelvas un miss "
                            "solo porque aparezcan anclas de otro polo."
                        ),
                    }
                )
                two_pass_injected = True
                log.append("two_pass: injected found anchors")

        if last_wave:
            messages.append(
                {
                    "role": "user",
                    "content": (
                        "## ÚLTIMA OLA — cierra YA con do=cerrar, veredicto, "
                        "cobertura[]"
                        + (", job_ids en cada cubierto" if require_job_ids else "")
                        + "."
                    ),
                }
            )

        raw = chat(api, model, messages, max_tokens=700)
        log.append(f"raw[{step}]: {raw[:800]}")
        try:
            ola = extract_json(raw)
        except Exception as e:  # noqa: BLE001
            messages.append({"role": "assistant", "content": raw})
            messages.append({"role": "user", "content": f"JSON inválido ({e}). Reemite."})
            continue

        do = str(ola.get("do") or "").strip().lower()
        last["steps"] = step + 1
        last["raw"] = ola

        if do == "cerrar":
            verd = str(ola.get("veredicto") or "dudoso").strip().lower()
            if verd not in ("sostiene", "refuta", "dudoso"):
                verd = "dudoso"
            cobertura = ola.get("cobertura") or []
            if not isinstance(cobertura, list):
                cobertura = []
            # normalize
            cob_n = []
            for c in cobertura:
                if not isinstance(c, dict):
                    continue
                item = {
                    "id": str(c.get("id") or "")[:32],
                    "estado": str(c.get("estado") or "")[:40],
                }
                if "job_ids" in c:
                    item["job_ids"] = c.get("job_ids")
                if "anclas" in c:
                    item["anclas"] = c.get("anclas")
                cob_n.append(item)
            cobertura = cob_n
            why = str(ola.get("why") or "").strip()
            if len(why) < 4:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "cerrar exige why≥4. Reemite."})
                continue
            if not cobertura and exam.get("elementos"):
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": "cerrar exige cobertura[] por cada elemento. Reemite.",
                    }
                )
                continue

            if require_job_ids:
                err = _cubierto_job_ids_illegal(cobertura, jobs)
                if err and not form_retry_used:
                    form_retry_used = True
                    last["form_retries"] = 1
                    messages.append({"role": "assistant", "content": raw})
                    messages.append(
                        {
                            "role": "user",
                            "content": (
                                f"Cierre rechazado (forma): {err}. "
                                "Reemite cerrar: o marca hueco, o cita job_ids de jobs "
                                "encontrado. No inventes jobs."
                            ),
                        }
                    )
                    continue

            # Counterask: model said sostienen but notebook has misses — LLM must respond
            if (
                counterask
                and verd == "sostiene"
                and misses
                and not counterask_used
            ):
                counterask_used = True
                last["counterask"] = True
                miss_txt = "; ".join(
                    f"job{m['id']}={m['veredicto']} ({m['brief'][:80]})" for m in misses
                )
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": (
                            "## Contra-pregunta (hechos del notebook)\n"
                            f"Hay miss/parcial: {miss_txt}\n"
                            "O bien rebajas cobertura/veredicto (hueco/dudoso/refuta), "
                            "o explicas en why por qué ese miss NO toca ningún elemento "
                            "del examen. Reemite SOLO un cerrar."
                        ),
                    }
                )
                continue

            arco = ola.get("arco") if isinstance(ola.get("arco"), dict) else {}
            ataques = ola.get("ataques") or []
            if isinstance(ataques, str):
                ataques = [ataques]
            return {
                "veredicto": verd,  # LLM decides — no runtime rewrite
                "veredicto_modelo": verd,
                "ataques": [str(x)[:160] for x in ataques if str(x).strip()][:8],
                "arco": {
                    "de": str(arco.get("de") or "")[:120],
                    "a": str(arco.get("a") or "")[:120],
                },
                "why": why[:800],
                "cobertura": cobertura,
                "raw": ola,
                "steps": step + 1,
                "form_retries": last["form_retries"],
                "counterask": last["counterask"],
                "downgraded": False,
            }

        if last_wave and do != "cerrar":
            messages.append({"role": "assistant", "content": raw})
            messages.append({"role": "user", "content": "ÚLTIMA OLA: solo cerrar."})
            continue

        if do == "entre":
            if entres >= 3:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope entre. Cierra."})
                continue
            entres += 1
            result = tool_entre(str(ola.get("from") or ""), str(ola.get("to") or ""), root)
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {"role": "user", "content": f"## entre\n```\n{result[:3500]}\n```\nSiguiente o cerrar."}
            )
            continue
        if do == "inbody":
            path = str(ola.get("path") or "").strip()
            if not path_allowed(path, allowed) or inbodies >= 2:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "inbody rechazado/tope. Cierra o otra tool."})
                continue
            inbodies += 1
            result = tool_inbody(path.split(":")[0], str(ola.get("pattern") or ""), root)
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {"role": "user", "content": f"## inbody\n```\n{result[:3000]}\n```\nSiguiente o cerrar."}
            )
            continue
        if do == "read":
            path = str(ola.get("path") or "").strip()
            if not path_allowed(path, allowed) or reads >= 2:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "read rechazado/tope. Cierra."})
                continue
            reads += 1
            result = tool_read(
                path.split(":")[0], int(ola.get("offset") or 1), 40, root, allowed=allowed or None
            )
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {"role": "user", "content": f"## read\n```\n{result[:3000]}\n```\nSiguiente o cerrar."}
            )
            continue

        messages.append({"role": "assistant", "content": raw})
        messages.append({"role": "user", "content": "do inválido. entre|inbody|read|cerrar."})

    return last


RUNS = [
    {"id": "R0_baseline", "kwargs": {}},
    {"id": "R1_A_job_ids", "kwargs": {"require_job_ids": True}},
    {"id": "R2_B_counterask", "kwargs": {"counterask": True, "show_verdicts": True}},
    {
        "id": "R3_C_cite_verdicts",
        "kwargs": {"show_verdicts": True, "cite_obligation": True},
    },
    {"id": "R4_D_entre_preseed", "kwargs": {"preseed_entre": True}},
    {"id": "R5_E_two_pass", "kwargs": {"two_pass": True, "show_verdicts": True}},
    {
        "id": "R6_A_B",
        "kwargs": {"require_job_ids": True, "counterask": True, "show_verdicts": True},
    },
    {
        "id": "R7_A_D",
        "kwargs": {"require_job_ids": True, "preseed_entre": True},
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
    exam = summary.get("verify_exam") or {}
    if not exam.get("elementos") and (summary_path.parent / "verify_exam.json").is_file():
        exam = json.loads((summary_path.parent / "verify_exam.json").read_text())

    out = (
        ROOT
        / ".tuide/ai/l2_admin_probe"
        / f"verify_llm_first_{time.strftime('%Y%m%d_%H%M%S')}"
    )
    out.mkdir(parents=True, exist_ok=True)
    (out / "exam.json").write_text(json.dumps(exam, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"CASE {summary_path.parent.name} gold=must_block misses={job_miss_verdicts(jobs)}", flush=True)
    print(f"out={out}", flush=True)

    rows = []
    for run in RUNS:
        rid = run["id"]
        print(f"==== {rid} ====", flush=True)
        vlog: list[str] = []
        t0 = time.time()
        report = run_llm_first(
            api, model, prompt, jobs, exam, ROOT, vlog, max_steps=4, **run["kwargs"]
        )
        wall = round(time.time() - t0, 1)
        (out / f"{rid}.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        (out / f"{rid}.log").write_text("\n".join(vlog) + "\n", encoding="utf-8")
        verd = report.get("veredicto")
        row = {
            "id": rid,
            "kind": "llm_first",
            "veredicto": verd,
            "veredicto_modelo": report.get("veredicto_modelo"),
            "blocked": blocks_exit(str(verd or "")),
            "hit_gold": blocks_exit(str(verd or "")),
            "form_retries": report.get("form_retries"),
            "counterask": report.get("counterask"),
            "cobertura": report.get("cobertura"),
            "why": (report.get("why") or "")[:220],
            "wall": wall,
            "steps": report.get("steps"),
        }
        rows.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)

    # CONTROLs — runtime rewrite (for comparison only)
    for cid, kw in [
        ("C1_CONTROL_M2_miss", {"block_sostiene_if_job_miss": True}),
        ("C2_CONTROL_M3_all_covered", {"block_sostiene_if_all_covered_with_miss": True}),
    ]:
        print(f"==== {cid} ====", flush=True)
        vlog = []
        t0 = time.time()
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
            show_job_verdicts=True,
            **kw,
        )
        (out / f"{cid}.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        verd = report.get("veredicto")
        row = {
            "id": cid,
            "kind": "control_runtime",
            "veredicto": verd,
            "veredicto_modelo": report.get("veredicto_modelo"),
            "blocked": blocks_exit(str(verd or "")),
            "hit_gold": blocks_exit(str(verd or "")),
            "downgrade_reasons": report.get("downgrade_reasons"),
            "why": (report.get("why") or "")[:220],
            "wall": round(time.time() - t0, 1),
            "steps": report.get("steps"),
        }
        rows.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)

    c3 = {
        "id": "C3_CONTROL_M4_skip",
        "kind": "control_policy",
        "veredicto": skip_verify_verdict([{"veredicto": "refuta"}]),
        "veredicto_modelo": "sostiene",
        "blocked": True,
        "hit_gold": True,
        "why": "omit cap → dudoso (no absuelve)",
        "wall": 0.0,
        "steps": 0,
    }
    rows.append(c3)
    print(f"==== {c3['id']} ====", flush=True)
    print(json.dumps(c3, ensure_ascii=False), flush=True)

    helped = [
        r["id"]
        for r in rows
        if r.get("hit_gold") and r["id"] != "R0_baseline" and r.get("kind") == "llm_first"
    ]
    board = {
        "case": summary_path.parent.name,
        "source": str(summary_path),
        "gold_block": True,
        "misses": job_miss_verdicts(jobs),
        "rows": rows,
        "llm_first_helped": helped,
        "baseline_ok": next(r["hit_gold"] for r in rows if r["id"] == "R0_baseline"),
    }
    (out / "COMPARE.json").write_text(json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = [
        f"# LLM-first matrix ({summary_path.parent.name})",
        "",
        f"misses={board['misses']}",
        "",
        "| id | kind | verd | modelo | block | gold | notes |",
        "|----|------|------|--------|-------|------|-------|",
    ]
    for r in rows:
        notes = []
        if r.get("form_retries"):
            notes.append(f"form_retry={r['form_retries']}")
        if r.get("counterask"):
            notes.append("counterask")
        if r.get("downgrade_reasons"):
            notes.append(str(r["downgrade_reasons"]))
        lines.append(
            f"| {r['id']} | {r.get('kind')} | {r.get('veredicto')} | {r.get('veredicto_modelo')} | "
            f"{r.get('blocked')} | {'✓' if r.get('hit_gold') else '✗'} | {', '.join(notes)} |"
        )
    lines += [
        "",
        f"**Baseline bloqueaba:** {board['baseline_ok']}",
        f"**LLM-first que ayudan:** {helped}",
        "",
        "Preferencia plan: A≥B≥D≥E≥C≥F≥CONTROL",
    ]
    for r in rows:
        lines.append(f"\n## {r['id']}\n- why: {r.get('why')}\n- cob: {r.get('cobertura')}")
    (out / "COMPARE.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("DONE", out, flush=True)
    print("llm_first_helped=", helped, "baseline_ok=", board["baseline_ok"], flush=True)


if __name__ == "__main__":
    main()
