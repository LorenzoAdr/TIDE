#!/usr/bin/env python3
"""Adversarial verifier: post-explore, pre-accept edit/close.

N-turn LLM with tightly scoped tools (entre + inbody/read on notebook paths only).
Mission: refute whether notebook *anchors* prove the user claim (esp. A→B arcs).
Does NOT receive explorer/pilot narrative — only consulta + inflated symbols.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

from explore_lite_local import (  # noqa: E402
    chat,
    extract_json,
    tool_entre,
    tool_inbody,
    tool_read,
)

VERIFY_SYS = """Eres VERIFICADOR. No explores el repo en busca de features nuevas.
Te dan la consulta del usuario y una lista de ANCLAS (símbolos/paths con snippet).
Tu misión es REFUTAR o hallar que esas anclas NO demuestran el arco de la consulta
(sobre todo A→B: que un símbolo/mecanismo implique otro). Co-ocurrencia léxica ≠ puente.
Si solo co-ocurren o el puente no está en las anclas: veredicto=refuta o dudoso.
Si las anclas demuestran el arco del pedido: sostiene.
Si no puedes decidir con las tools: dudoso. Máximo N olas. Al cerrar:
{"do":"cerrar","veredicto":"sostiene|refuta|dudoso","ataques":["…"],
 "arco":{"de":"…","a":"…"},"why":"…"}

En la ÚLTIMA ola disponible DEBES cerrar (no más entre/inbody/read).
Si tras tools el puente sigue sin verse: cerrar con refuta o dudoso — no alargar.
Tools (UNA por turno):
{"do":"entre","from":"symbol_a","to":"symbol_b","why":"…"}
{"do":"inbody","path":"src/…","pattern":"needle","why":"…"}  // path ya en anclas
{"do":"read","path":"src/…","offset":1,"why":"…"}  // path ya en anclas; clip corto
{"do":"cerrar","veredicto":"sostiene|refuta|dudoso","ataques":[],"arco":{"de":"","a":""},"why":"…"}

entre = co-ocurrencia débil entre dos símbolos (sin camino también es evidencia).
PROHIBIDO: grep global, cerca, atlas, follow, inventar paths fuera de las anclas.
NO reescribas la historia del explorador: ataca el arco de la CONSULTA contra las anclas."""

MISSION = (
    "Ataca si estas anclas demuestran el arco de la consulta; "
    "si solo co-ocurren, refuta/dudoso."
)

DECOMPOSE_SYS = """Eres ANALISTA de cobertura (fase 1). NO ves código ni anclas.
Solo la consulta del usuario. Descompón qué tendrías que poder EXPLICAR
para dar por cubierto el pedido (elementos + arcos causales A→B).
Sin paths. Sin inventar APIs. Máximo 5 elementos y 3 arcos.
Responde UN JSON:
{"elementos":[{"id":"e1","que":"…"},…],
 "arcos":[{"de":"e1","a":"e2","que":"…"},…],
 "why":"…"}"""

DECOMPOSE_SYS_LITERAL = """Eres ANALISTA de cobertura (fase 1). NO ves código ni anclas.
Solo la consulta del usuario. Descompón qué tendrías que poder EXPLICAR
para dar por cubierto el pedido (elementos + arcos causales A→B).
Sin paths. Sin inventar APIs. Máximo 5 elementos y 3 arcos.

IMPORTANTE: en cada "que", usa términos del pedido del usuario (sus palabras),
no abstracciones genéricas. Si el usuario nombra un origen/destino concretos,
los elementos deben nombrarlos; no los sustituyas por un mecanismo "parecido".
Un falso amigo (otro pipeline que hace algo similar) NO debe poder marcarse
cubierto solo porque exista algo vecino.

Responde UN JSON:
{"elementos":[{"id":"e1","que":"…"},…],
 "arcos":[{"de":"e1","a":"e2","que":"…"},…],
 "why":"…"}"""

VERIFY_SYS_EXAM = """Eres VERIFICADOR (fase 2). No explores features nuevas.
Te dan: (1) EXAMEN de cobertura (elementos/arcos que hay que poder explicar),
(2) ANCLAS del explorador (símbolos+snippets).
Califica el examen contra las anclas. Co-ocurrencia ≠ puente.
Si algún elemento/arco del examen no está demostrado por anclas: refuta o dudoso.
PROHIBIDO sostener si queda algún hueco del examen sin cubrir.
Máximo N olas. Al cerrar:
{"do":"cerrar","veredicto":"sostiene|refuta|dudoso",
 "cobertura":[{"id":"e1","estado":"cubierto|hueco|no_inspeccionado"},…],
 "ataques":["…"],"arco":{"de":"…","a":"…"},"why":"…"}

En la ÚLTIMA ola DEBES cerrar con cobertura[]. Si un elemento del examen
no está en las anclas: estado=hueco y veredicto=refuta|dudoso.
Tools (UNA por turno):
{"do":"entre","from":"symbol_a","to":"symbol_b","why":"…"}
{"do":"inbody","path":"src/…","pattern":"needle","why":"…"}
{"do":"read","path":"src/…","offset":1,"why":"…"}
{"do":"cerrar","veredicto":"…","cobertura":[],"ataques":[],"arco":{"de":"","a":""},"why":"…"}

PROHIBIDO: grep global, inventar paths fuera de anclas."""

REFUTE_SYS = """Eres REFUTADOR adversarial. NO explores el repo.
Te dan: consulta del usuario, examen, cobertura/veredicto de un verificador, y anclas.
Tu ÚNICA misión: tumbar cada estado=cubierto si confunde un mecanismo VECINO
con el pedido LITERAL del usuario. Co-ocurrencia ≠ el pedido.
Si tumba al menos un cubierto: veredicto=refuta (o dudoso si no estás seguro).
Si no puedes tumbar ninguno con argumento concreto: veredicto=sostiene.
Responde UN JSON (sin tools):
{"veredicto":"sostiene|refuta|dudoso",
 "ataques":[{"id":"e1","tumbado":true|false,"why":"…"},…],
 "why":"…"}"""


def notebook_paths(jobs: list[dict]) -> set[str]:
    paths: set[str] = set()
    for j in jobs:
        for p in j.get("paths") or []:
            s = str(p).strip()
            if s and ("/" in s or s.endswith((".cpp", ".hpp", ".h", ".cc"))):
                paths.add(s.split(":")[0])
        for ev in j.get("evidencia") or []:
            s = str(ev)
            if ":" in s and "/" in s:
                paths.add(s.split(":")[0])
        for sym in j.get("simbolos") or []:
            s = str(sym)
            if ":" in s and "/" in s.split(":")[0]:
                paths.add(s.split(":")[0])
    return paths


def _parse_anchor(raw: str) -> tuple[str, str | None, int | None]:
    """Return (path, symbol_or_None, line_or_None) from path:sym or path:N."""
    s = (raw or "").strip()
    if not s or "/" not in s:
        return "", None, None
    if ":" not in s:
        return s, None, None
    path, rest = s.split(":", 1)
    path = path.strip()
    rest = rest.strip()
    if not path:
        return "", None, None
    if re.fullmatch(r"\d+", rest):
        return path, None, int(rest)
    if re.fullmatch(r"\d+-\d+", rest):
        return path, None, int(rest.split("-")[0])
    # symbol may contain spaces / parens from explorer noise — take first token-ish
    sym = rest.split()[0] if rest else None
    return path, sym, None


def _inflate_one(anchor: str, root: Path, ctx: int = 2) -> str:
    path, sym, line = _parse_anchor(anchor)
    if not path:
        return f"- {anchor!r} (inválido)"
    full = (root / path).resolve()
    try:
        if not str(full).startswith(str(root.resolve())) or not full.is_file():
            return f"- `{path}`" + (f":{sym}" if sym else "") + " (no legible)"
    except OSError:
        return f"- `{path}` (error)"
    lines = full.read_text(encoding="utf-8", errors="replace").splitlines()
    hit = line
    if hit is None and sym:
        short = sym.split("::")[-1]
        short = re.sub(r"[^A-Za-z0-9_]", "", short) or short
        for i, ln in enumerate(lines, start=1):
            if short and short in ln:
                hit = i
                break
    if hit is None:
        return f"- `{path}`" + (f":`{sym}`" if sym else "") + " (sin línea)"
    lo = max(1, hit - ctx)
    hi = min(len(lines), hit + ctx)
    body = "\n".join(f"{i}|{lines[i - 1]}" for i in range(lo, hi + 1))
    label = f"`{path}:{sym or hit}`"
    return f"- {label}\n```\n{body}\n```"


def inflate_anchors(jobs: list[dict], root: Path, max_anchors: int = 16) -> str:
    """Symbols (+ paths) with short code snippets; no explorer prose."""
    seen: set[str] = set()
    ordered: list[str] = []
    for j in jobs:
        for sym in j.get("simbolos") or []:
            s = str(sym).strip()
            if not s or s in seen:
                continue
            seen.add(s)
            ordered.append(s)
        for p in j.get("paths") or []:
            s = str(p).strip().split(":")[0]
            if not s or "/" not in s or s in seen:
                continue
            # only add bare path if we have room and no symbol already covers it
            if any(x.startswith(s + ":") for x in seen):
                continue
            seen.add(s)
            ordered.append(s)
        if len(ordered) >= max_anchors:
            break
    ordered = ordered[:max_anchors]
    if not ordered:
        return "(sin anclas)"
    parts = [_inflate_one(a, root) for a in ordered]
    return "\n".join(parts)


def path_allowed(path: str, allowed: set[str]) -> bool:
    p = (path or "").strip().split(":")[0]
    if not p:
        return False
    if p in allowed:
        return True
    for a in allowed:
        if p.endswith(a) or a.endswith(p) or p == a:
            return True
    return False


def decompose_claim(
    api: str,
    model: str,
    consulta: str,
    log: list[str] | None = None,
    *,
    literal: bool = False,
) -> dict:
    """Phase 1: prompt-only exam (elementos + arcos). No tools."""
    log = log if log is not None else []
    sys = DECOMPOSE_SYS_LITERAL if literal else DECOMPOSE_SYS
    messages = [
        {"role": "system", "content": sys},
        {
            "role": "user",
            "content": f"## Consulta\n{consulta}\n\nDescompón el examen (JSON).",
        },
    ]
    raw = chat(api, model, messages, max_tokens=500)
    log.append(f"decompose_raw: {raw[:1000]}")
    try:
        exam = extract_json(raw)
    except Exception as e:  # noqa: BLE001
        return {
            "elementos": [],
            "arcos": [],
            "why": f"decompose parse fail: {e}",
            "raw": raw[:800],
            "ok": False,
            "literal": literal,
        }
    elems = exam.get("elementos") or []
    if not isinstance(elems, list):
        elems = []
    clean_e = []
    for i, el in enumerate(elems[:5]):
        if isinstance(el, dict):
            clean_e.append(
                {
                    "id": str(el.get("id") or f"e{i+1}")[:32],
                    "que": str(el.get("que") or "")[:200],
                }
            )
        elif str(el).strip():
            clean_e.append({"id": f"e{i+1}", "que": str(el)[:200]})
    arcs = exam.get("arcos") or []
    if not isinstance(arcs, list):
        arcs = []
    clean_a = []
    for ar in arcs[:3]:
        if not isinstance(ar, dict):
            continue
        clean_a.append(
            {
                "de": str(ar.get("de") or "")[:32],
                "a": str(ar.get("a") or "")[:32],
                "que": str(ar.get("que") or "")[:200],
            }
        )
    return {
        "elementos": clean_e,
        "arcos": clean_a,
        "why": str(exam.get("why") or "")[:400],
        "raw": exam,
        "ok": bool(clean_e),
        "literal": literal,
    }


def format_exam(exam: dict) -> str:
    lines = ["## EXAMEN de cobertura (fase 1 — qué hay que poder explicar)"]
    for el in exam.get("elementos") or []:
        lines.append(f"- [{el.get('id')}] {el.get('que')}")
    for ar in exam.get("arcos") or []:
        lines.append(f"- arco {ar.get('de')}→{ar.get('a')}: {ar.get('que')}")
    if exam.get("why"):
        lines.append(f"why examen: {exam.get('why')}")
    return "\n".join(lines) if len(lines) > 1 else "## EXAMEN\n(vacío)"


def run_refute_pass(
    api: str,
    model: str,
    consulta: str,
    exam: dict | None,
    cobertura: list,
    anchors: str,
    prior_why: str,
    log: list[str] | None = None,
) -> dict:
    """Adversarial LLM pass: try to tumble each cubierto. No runtime rewrite logic."""
    log = log if log is not None else []
    cob_txt = json.dumps(cobertura, ensure_ascii=False)[:1200]
    exam_txt = format_exam(exam) if exam and exam.get("elementos") else "(sin examen)"
    messages = [
        {"role": "system", "content": REFUTE_SYS},
        {
            "role": "user",
            "content": (
                f"## Consulta del usuario\n{consulta}\n\n"
                f"{exam_txt}\n\n"
                f"## Cobertura del verificador\n{cob_txt}\n"
                f"why verificador: {(prior_why or '')[:400]}\n\n"
                f"## Anclas\n{anchors[:4500]}\n\n"
                "Intenta tumbar cubiertos. UN JSON."
            ),
        },
    ]
    raw = chat(api, model, messages, max_tokens=600)
    log.append(f"refute_raw: {raw[:1000]}")
    try:
        obj = extract_json(raw)
    except Exception as e:  # noqa: BLE001
        return {
            "veredicto": "dudoso",
            "ataques": [],
            "why": f"refute parse fail: {e}",
            "raw": raw[:600],
            "ok": False,
        }
    verd = str(obj.get("veredicto") or "dudoso").strip().lower()
    if verd not in ("sostiene", "refuta", "dudoso"):
        verd = "dudoso"
    ataques = obj.get("ataques") or []
    if not isinstance(ataques, list):
        ataques = []
    clean_a = []
    for a in ataques[:8]:
        if not isinstance(a, dict):
            continue
        clean_a.append(
            {
                "id": str(a.get("id") or "")[:32],
                "tumbado": bool(a.get("tumbado")),
                "why": str(a.get("why") or "")[:200],
            }
        )
    # If any cubierto tumbled, prefer refuta/dudoso over accidental sostenido
    if any(a.get("tumbado") for a in clean_a) and verd == "sostiene":
        verd = "refuta"
    return {
        "veredicto": verd,
        "ataques": clean_a,
        "why": str(obj.get("why") or "")[:800],
        "raw": obj,
        "ok": True,
    }


def _downgrade_sostiene_if_gaps(verd: str, cobertura: list) -> str:
    """Hard rule: cannot sostienen if any exam slot is hueco/no_inspeccionado."""
    if verd != "sostiene" or not cobertura:
        return verd
    for c in cobertura:
        if not isinstance(c, dict):
            continue
        st = str(c.get("estado") or "").strip().lower()
        if st in ("hueco", "no_inspeccionado", "falta", "vacio", "vacío"):
            return "dudoso"
    return verd


def job_miss_verdicts(jobs: list[dict]) -> list[dict]:
    """Jobs whose structured verdicto is a miss/partial (general, not domain)."""
    miss_labels = {
        "no_encontrado",
        "parcial",
        "no_hay",
        "no_concluyente",
        "no",
    }
    out = []
    for j in jobs:
        v = str(j.get("veredicto") or "").strip().lower()
        if v in miss_labels:
            out.append(
                {
                    "id": j.get("id"),
                    "veredicto": v,
                    "brief": str(j.get("brief") or j.get("arg") or "")[:160],
                }
            )
    return out


def format_job_verdicts(jobs: list[dict]) -> str:
    """Factual verify context (mirrors admin_verify_context_prompt in C++)."""
    lines = [
        "## Veredictos del notebook (hechos tipados; no narrativa del piloto)",
        "Un no_encontrado/parcial en un polo NO se borra porque otro job haya "
        "encontrado otra cosa. Incluye explore/read/search anclados.",
    ]
    if not jobs:
        lines.append("(sin jobs)")
    evid_lines: list[str] = []
    for j in jobs:
        tipo = (j.get("tipo") or "explore") or "explore"
        v = j.get("veredicto") or "-"
        # Prefer cerrado why / summary; fall back to brief. Cap high (C++ uses ≤4k).
        sum_txt = (
            str(j.get("summary") or j.get("why") or j.get("brief") or j.get("arg") or "")
        )[:4000]
        lines.append(f"- job{j.get('id')} ({tipo}): veredicto={v} | {sum_txt}")
        evs = [str(e) for e in (j.get("evidencia") or []) if e][:12]
        if evs:
            lines.append("  evidencia: " + " ".join(f"`{e}`" for e in evs))
            for e in evs:
                evid_lines.append(f"- job{j.get('id')}: `{e}`")
        tail = str(j.get("log_tail") or "")
        if tail and tipo in ("read", "search"):
            lines.append("  extracto:\n" + tail[:2500])
    lines.append("")
    lines.append("## Evidencia tipada (path:línea / símbolos; no prosa)")
    if evid_lines:
        lines.extend(evid_lines[:64])
    else:
        lines.append("(sin evidencia tipada en jobs)")
    return "\n".join(lines)


def _downgrade_sostiene_if_job_miss(verd: str, jobs: list[dict]) -> tuple[str, str | None]:
    """General: sostienen incompatible with any miss/partial job in notebook."""
    if verd != "sostiene":
        return verd, None
    misses = job_miss_verdicts(jobs)
    if not misses:
        return verd, None
    return "dudoso", f"notebook_miss:{[m['veredicto'] for m in misses]}"


def _downgrade_sostiene_if_all_covered_with_miss(
    verd: str, cobertura: list, jobs: list[dict]
) -> tuple[str, str | None]:
    """General: all-cubierto + sostienen contradicted by miss jobs → dudoso."""
    if verd != "sostiene" or not cobertura:
        return verd, None
    if not job_miss_verdicts(jobs):
        return verd, None
    states = [
        str(c.get("estado") or "").strip().lower()
        for c in cobertura
        if isinstance(c, dict)
    ]
    if states and all(s == "cubierto" for s in states):
        return "dudoso", "all_covered_but_notebook_miss"
    return verd, None


def _finalize_verdicto(
    verd: str,
    cobertura: list,
    jobs: list[dict],
    *,
    hard: bool,
    block_sostiene_if_job_miss: bool,
    block_sostiene_if_all_covered_with_miss: bool,
) -> tuple[str, list[str]]:
    """Apply stacked general downgrades. Returns (verd_final, reasons)."""
    reasons: list[str] = []
    v = verd
    if hard:
        v2 = _downgrade_sostiene_if_gaps(v, cobertura)
        if v2 != v:
            reasons.append("cobertura_gap")
            v = v2
    if block_sostiene_if_job_miss:
        v2, why = _downgrade_sostiene_if_job_miss(v, jobs)
        if why:
            reasons.append(why)
            v = v2
    if block_sostiene_if_all_covered_with_miss:
        v2, why = _downgrade_sostiene_if_all_covered_with_miss(v, cobertura, jobs)
        if why:
            reasons.append(why)
            v = v2
    return v, reasons


def skip_verify_verdict(prior_reports: list[dict] | None = None) -> str:
    """When verify pass cap hit: never absuelve as sostienen."""
    del prior_reports  # any prior outcome: omit → dudoso (blocks)
    return "dudoso"


def run_verifier(
    api: str,
    model: str,
    consulta: str,
    jobs: list[dict],
    thesis: str,
    root: Path,
    log: list[str] | None = None,
    max_steps: int = 4,
    max_entre: int = 3,
    max_inbody: int = 2,
    max_read: int = 2,
    exam: dict | None = None,
    variant: str = "anchors",
    *,
    show_job_verdicts: bool = False,
    counterask: bool = False,
    counterask_consulta: bool = False,
    refute_pass: bool = False,
    block_sostiene_if_job_miss: bool = False,
    block_sostiene_if_all_covered_with_miss: bool = False,
) -> dict:
    """Return {veredicto, ataques, arco, why, raw, steps, …}.

    variant:
      - anchors: current (consulta + anclas)
      - exam: two-phase style — include exam; soft coverage in prompt
      - exam_hard: exam + runtime blocks sostienen if cobertura has gaps

    Optional general measures (LLM-first; no domain lists):
      - show_job_verdicts: inject job verdicto list into the prompt
      - counterask: if model closes sostienen while notebook has miss/parcial,
        one re-ask (LLM re-closes; no runtime rewrite of veredicto)
      - counterask_consulta: if model closes sostienen, one re-ask contrasting
        pedido literal vs mecanismo vecino (works without miss jobs)
      - refute_pass: after sostienen, adversarial LLM pass; use its veredicto
      - block_sostiene_if_job_miss: runtime downgrade if any miss/partial job
      - block_sostiene_if_all_covered_with_miss: downgrade if all-cubierto contradicts miss jobs
    """
    del thesis
    log = log if log is not None else []
    allowed = notebook_paths(jobs)
    anchors = inflate_anchors(jobs, root)
    use_exam = variant in ("exam", "exam_hard") and exam and exam.get("elementos")
    hard = variant == "exam_hard"
    if counterask:
        show_job_verdicts = True
    verdicts_blob = format_job_verdicts(jobs) if show_job_verdicts else ""
    misses = job_miss_verdicts(jobs)
    counterask_used = False
    counterask_consulta_used = False

    if use_exam:
        sys = VERIFY_SYS_EXAM.replace("N olas", f"{max_steps} olas")
        if show_job_verdicts:
            sys += (
                "\nSi el notebook tiene job(s) no_encontrado/parcial sobre un polo del examen, "
                "ese polo no puede marcarse cubierto solo porque otro job encontró otra cosa."
            )
        user0 = (
            f"## Consulta del usuario\n{consulta}\n\n"
            f"{format_exam(exam)}\n\n"
            + (f"{verdicts_blob}\n\n" if verdicts_blob else "")
            + f"## Anclas (sin narrativa del explorador)\n{anchors}\n\n"
            f"## Paths legibles\n{sorted(allowed)[:40]}\n\n"
            f"N={max_steps}. Califica el EXAMEN. Elige UNA acción JSON."
        )
    else:
        sys = VERIFY_SYS.replace("N olas", f"{max_steps} olas")
        user0 = (
            f"## Consulta del usuario\n{consulta}\n\n"
            + (f"{verdicts_blob}\n\n" if verdicts_blob else "")
            + f"## Anclas (símbolos inflados; sin narrativa del explorador)\n{anchors}\n\n"
            f"## Paths legibles\n{sorted(allowed)[:40]}\n\n"
            f"N={max_steps}. {MISSION}\nElige UNA acción JSON."
        )

    messages = [
        {"role": "system", "content": sys},
        {"role": "user", "content": user0},
    ]
    entres = inbodies = reads = 0
    last: dict = {
        "veredicto": "dudoso",
        "ataques": ["tope sin cerrar"],
        "arco": {"de": "", "a": ""},
        "why": "tope de pasos sin cerrar",
        "raw": {},
        "steps": 0,
        "cobertura": [],
        "variant": variant,
        "counterask": False,
        "counterask_consulta": False,
        "refute_pass": False,
    }

    if not jobs:
        return {
            "veredicto": "dudoso",
            "ataques": ["notebook vacío"],
            "arco": {"de": "", "a": ""},
            "why": "sin evidencia que verificar",
            "raw": {},
            "steps": 0,
            "cobertura": [],
            "variant": variant,
            "counterask": False,
            "counterask_consulta": False,
            "refute_pass": False,
        }

    # +1 ola por cada tipo de contra-pregunta posible
    extra = (1 if counterask else 0) + (1 if counterask_consulta else 0)
    budget = max_steps + extra

    def _finish_close(
        verd: str,
        cobertura: list,
        ataques: list,
        arco: dict,
        why: str,
        ola: dict,
        step: int,
        *,
        forced_close: bool = False,
    ) -> dict:
        verd_final, down_reasons = _finalize_verdicto(
            verd,
            cobertura,
            jobs,
            hard=hard,
            block_sostiene_if_job_miss=block_sostiene_if_job_miss,
            block_sostiene_if_all_covered_with_miss=block_sostiene_if_all_covered_with_miss,
        )
        out = {
            "veredicto": verd_final,
            "veredicto_modelo": verd,
            "ataques": ataques,
            "arco": {
                "de": str(arco.get("de") or "")[:120],
                "a": str(arco.get("a") or "")[:120],
            },
            "why": why[:800],
            "raw": ola,
            "steps": step + 1,
            "cobertura": cobertura,
            "variant": variant,
            "downgraded": verd_final != verd,
            "downgrade_reasons": down_reasons,
            "show_job_verdicts": show_job_verdicts,
            "counterask": counterask_used,
            "counterask_consulta": counterask_consulta_used,
            "refute_pass": False,
            "block_sostiene_if_job_miss": block_sostiene_if_job_miss,
            "block_sostiene_if_all_covered_with_miss": block_sostiene_if_all_covered_with_miss,
        }
        if forced_close:
            out["forced_close"] = True
        # F: adversarial LLM pass when still sostenido — use refutador's verdicto
        if refute_pass and verd_final == "sostiene":
            ref = run_refute_pass(
                api,
                model,
                consulta,
                exam if use_exam else None,
                cobertura,
                anchors,
                why,
                log,
            )
            out["refute_pass"] = True
            out["refute"] = {
                "veredicto": ref.get("veredicto"),
                "ataques": ref.get("ataques"),
                "why": ref.get("why"),
                "ok": ref.get("ok"),
            }
            rv = str(ref.get("veredicto") or "dudoso")
            if rv in ("refuta", "dudoso"):
                out["veredicto"] = rv
                out["veredicto_refutador"] = rv
                out["why"] = (
                    f"[refutador] {(ref.get('why') or '')[:500]}"
                    if ref.get("why")
                    else out["why"]
                )
            elif rv == "sostiene":
                out["veredicto_refutador"] = "sostiene"
        return out
    for step in range(budget):
        last_wave = step >= budget - 1
        if last_wave:
            # Soft pressure: remind before the model speaks on the final wave.
            messages.append(
                {
                    "role": "user",
                    "content": (
                        "## ÚLTIMA OLA — cierra YA\n"
                        'Emite solo {"do":"cerrar",...}. '
                        + (
                            "Incluye cobertura[] por cada elemento del examen. "
                            if use_exam
                            else ""
                        )
                        + "Si falta puente/elemento: refuta o dudoso."
                    ),
                }
            )

        raw = chat(api, model, messages, max_tokens=700)
        log.append(f"verify_raw[{step}]: {raw[:800]}")
        try:
            ola = extract_json(raw)
        except Exception as e:  # noqa: BLE001
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": f"JSON inválido ({e}). Solo un objeto: entre|inbody|read|cerrar.",
                }
            )
            continue

        do = str(ola.get("do") or "").strip().lower()
        last["steps"] = step + 1
        last["raw"] = ola

        if do == "cerrar":
            verd = str(ola.get("veredicto") or "dudoso").strip().lower()
            if verd not in ("sostiene", "refuta", "dudoso"):
                verd = "dudoso"
            arco = ola.get("arco") if isinstance(ola.get("arco"), dict) else {}
            ataques = ola.get("ataques") or []
            if isinstance(ataques, str):
                ataques = [ataques] if ataques.strip() else []
            ataques = [str(x)[:160] for x in ataques if str(x).strip()][:8]
            cobertura = ola.get("cobertura") or []
            if not isinstance(cobertura, list):
                cobertura = []
            cobertura = [
                {
                    "id": str(c.get("id") or "")[:32],
                    "estado": str(c.get("estado") or "")[:40],
                }
                for c in cobertura
                if isinstance(c, dict)
            ][:8]
            why = str(ola.get("why") or "").strip()
            if len(why) < 4:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": "cerrar exige why (≥4). Reemite cerrar con veredicto y why.",
                    }
                )
                continue
            if use_exam and not cobertura and exam and not last_wave:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": (
                            "cerrar con examen exige cobertura[] "
                            "(cubierto|hueco|no_inspeccionado por cada elemento). Reemite."
                        ),
                    }
                )
                continue
            if use_exam and not cobertura and exam and last_wave:
                # synthesize hueco coverage so exam_hard can downgrade
                cobertura = [
                    {"id": str(el.get("id") or f"e{i}"), "estado": "no_inspeccionado"}
                    for i, el in enumerate(exam.get("elementos") or [], 1)
                ]
            # B: re-ask once if sostienen contradicts notebook miss (LLM decides again)
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
                log.append(f"counterask: {miss_txt[:200]}")
                continue
            # B′: re-ask once — pedido literal vs mecanismo vecino (no exige miss)
            if (
                counterask_consulta
                and verd == "sostiene"
                and not counterask_consulta_used
            ):
                counterask_consulta_used = True
                last["counterask_consulta"] = True
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": (
                            "## Contra-pregunta (pedido del usuario)\n"
                            f"Consulta: {consulta}\n"
                            "¿Las anclas demuestran el pedido LITERAL del usuario, "
                            "o un mecanismo VECINO/parecido?\n"
                            "Si es vecino: rebaja a hueco/dudoso/refuta.\n"
                            "Si es el pedido: en why cita palabras concretas de la "
                            "consulta que las anclas demuestran.\n"
                            "Reemite SOLO un cerrar."
                        ),
                    }
                )
                log.append("counterask_consulta")
                continue
            return _finish_close(verd, cobertura, ataques, arco, why, ola, step)

        # Tools not allowed on last wave — force close.
        if last_wave and do != "cerrar":
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": (
                        "ÚLTIMA OLA: tools prohibidas. Cierra ahora con do=cerrar, "
                        "veredicto y why"
                        + (" y cobertura[]." if use_exam else ".")
                    ),
                }
            )
            # one forced extra chat after tools rejected on last wave
            raw2 = chat(api, model, messages, max_tokens=700)
            log.append(f"verify_force_close: {raw2[:800]}")
            try:
                ola2 = extract_json(raw2)
            except Exception:  # noqa: BLE001
                return last
            if str(ola2.get("do") or "").strip().lower() != "cerrar":
                return last
            # reuse cerrar path via recursive-ish inline
            ola = ola2
            verd = str(ola.get("veredicto") or "dudoso").strip().lower()
            if verd not in ("sostiene", "refuta", "dudoso"):
                verd = "dudoso"
            arco = ola.get("arco") if isinstance(ola.get("arco"), dict) else {}
            ataques = ola.get("ataques") or []
            if isinstance(ataques, str):
                ataques = [ataques] if ataques.strip() else []
            ataques = [str(x)[:160] for x in ataques if str(x).strip()][:8]
            cobertura = ola.get("cobertura") or []
            if not isinstance(cobertura, list):
                cobertura = []
            cobertura = [
                {
                    "id": str(c.get("id") or "")[:32],
                    "estado": str(c.get("estado") or "")[:40],
                }
                for c in cobertura
                if isinstance(c, dict)
            ][:8]
            why = str(ola.get("why") or "").strip()
            if len(why) < 4:
                why = "cierre forzado en última ola"
            if use_exam and not cobertura and exam:
                cobertura = [
                    {"id": str(el.get("id") or f"e{i}"), "estado": "hueco"}
                    for i, el in enumerate(exam.get("elementos") or [], 1)
                ]
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
                messages.append({"role": "assistant", "content": raw2})
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
                log.append(f"counterask_forced: {miss_txt[:200]}")
                continue
            if (
                counterask_consulta
                and verd == "sostiene"
                and not counterask_consulta_used
            ):
                counterask_consulta_used = True
                last["counterask_consulta"] = True
                messages.append({"role": "assistant", "content": raw2})
                messages.append(
                    {
                        "role": "user",
                        "content": (
                            "## Contra-pregunta (pedido del usuario)\n"
                            f"Consulta: {consulta}\n"
                            "¿Las anclas demuestran el pedido LITERAL del usuario, "
                            "o un mecanismo VECINO/parecido?\n"
                            "Si es vecino: rebaja a hueco/dudoso/refuta.\n"
                            "Si es el pedido: en why cita palabras concretas de la "
                            "consulta que las anclas demuestran.\n"
                            "Reemite SOLO un cerrar."
                        ),
                    }
                )
                log.append("counterask_consulta_forced")
                continue
            return _finish_close(
                verd, cobertura, ataques, arco, why, ola, step, forced_close=True
            )

        if do == "entre":
            if entres >= max_entre:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope de entre. Cierra ahora."})
                continue
            entres += 1
            result = tool_entre(str(ola.get("from") or ""), str(ola.get("to") or ""), root)
            messages.append({"role": "assistant", "content": raw})
            remain = max_steps - step - 1
            tip = (
                "\n\nQueda 1 ola: después DEBES cerrar."
                if remain <= 1
                else f"\n\nOlas restantes≈{remain}."
            )
            messages.append(
                {
                    "role": "user",
                    "content": f"## Resultado entre\n```\n{result[:3500]}\n```\n\nSiguiente ola o cerrar.{tip}",
                }
            )
            continue

        if do == "inbody":
            if inbodies >= max_inbody:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope de inbody. Cierra o entre/read."})
                continue
            path = str(ola.get("path") or "").strip()
            if not path_allowed(path, allowed):
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": f"inbody rechazado: path no anclado ({path!r}). Solo: {sorted(allowed)[:20]}",
                    }
                )
                continue
            inbodies += 1
            result = tool_inbody(path.split(":")[0], str(ola.get("pattern") or ""), root)
            messages.append({"role": "assistant", "content": raw})
            remain = max_steps - step - 1
            tip = (
                "\n\nQueda 1 ola: después DEBES cerrar."
                if remain <= 1
                else f"\n\nOlas restantes≈{remain}."
            )
            messages.append(
                {
                    "role": "user",
                    "content": f"## Resultado inbody\n```\n{result[:3000]}\n```\n\nSiguiente ola o cerrar.{tip}",
                }
            )
            continue

        if do == "read":
            if reads >= max_read:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope de read. Cierra o entre."})
                continue
            path = str(ola.get("path") or "").strip()
            if not path_allowed(path, allowed):
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": f"read rechazado: path no anclado ({path!r}). Solo: {sorted(allowed)[:20]}",
                    }
                )
                continue
            reads += 1
            off = int(ola.get("offset") or 1)
            result = tool_read(path.split(":")[0], off, 40, root, allowed=allowed or None)
            messages.append({"role": "assistant", "content": raw})
            remain = max_steps - step - 1
            tip = (
                "\n\nQueda 1 ola: después DEBES cerrar."
                if remain <= 1
                else f"\n\nOlas restantes≈{remain}."
            )
            messages.append(
                {
                    "role": "user",
                    "content": f"## Resultado read\n```\n{result[:3000]}\n```\n\nSiguiente ola o cerrar.{tip}",
                }
            )
            continue

        messages.append({"role": "assistant", "content": raw})
        messages.append(
            {
                "role": "user",
                "content": 'do inválido. Usa entre|inbody|read|cerrar. Ejemplo: {"do":"cerrar","veredicto":"refuta",…}',
            }
        )

    return last


def blocks_exit(veredicto: str) -> bool:
    """Plan default: dudoso blocks like refuta."""
    return (veredicto or "").strip().lower() in ("refuta", "dudoso")


def format_pilot_verify_report(report: dict, exam: dict | None = None) -> str:
    """Texto que vuelve al piloto tras un bloqueo: huecos accionables + menú."""
    exam = exam or {}
    elems = {str(e.get("id")): e for e in (exam.get("elementos") or []) if isinstance(e, dict)}
    cobertura = report.get("cobertura") or []
    verd = str(report.get("veredicto") or "dudoso")
    arco = report.get("arco") if isinstance(report.get("arco"), dict) else {}
    ataques = report.get("ataques") or []
    why = str(report.get("why") or "")[:900]

    lines = [
        "## Informe del VERIFICADOR (salida bloqueada — vuelves al menú)",
        f"veredicto={verd}",
        f"arco_critico: {arco.get('de') or '?'} → {arco.get('a') or '?'}",
        "",
        "### Cobertura del examen",
    ]
    huecos: list[dict] = []
    if cobertura:
        for c in cobertura:
            if not isinstance(c, dict):
                continue
            cid = str(c.get("id") or "")
            estado = str(c.get("estado") or "").lower()
            que = str((elems.get(cid) or {}).get("que") or "")
            mark = {"cubierto": "✓", "hueco": "✗", "no_inspeccionado": "?"}.get(estado, "·")
            lines.append(f"- [{cid}] {mark} {estado}" + (f" — {que}" if que else ""))
            if estado in ("hueco", "no_inspeccionado", "falta"):
                huecos.append({"id": cid, "que": que or cid, "estado": estado})
    else:
        lines.append("(sin cobertura estructurada)")

    if not huecos and exam.get("elementos"):
        # fallback: if no cobertura but blocked, surface all exam elems as suspects
        for e in exam.get("elementos") or []:
            if isinstance(e, dict):
                huecos.append(
                    {"id": str(e.get("id") or ""), "que": str(e.get("que") or ""), "estado": "sospecha"}
                )

    lines.append("")
    lines.append("### Huecos a cazar (briefs sugeridos)")
    if huecos:
        for h in huecos:
            brief = h["que"].strip() or h["id"]
            # keep as a clear explore question
            if not brief.endswith("?"):
                brief_q = f"¿Cómo se implementa o conecta: {brief}?"
            else:
                brief_q = brief
            lines.append(f"- {h['id']}: {brief_q}")
    else:
        lines.append("- (ningún hueco explícito; relee ataques/why)")

    if ataques:
        lines.append("")
        lines.append("### Ataques")
        for a in ataques[:5]:
            lines.append(f"- {a}")

    lines.append("")
    lines.append(f"### Why\n{why}")
    lines.append("")
    lines.append("### Decide UNA acción")
    if huecos:
        h0 = huecos[0]["que"].strip() or huecos[0]["id"]
        brief0 = h0 if h0.endswith("?") else f"¿Cómo se conecta o implementa: {h0}?"
        brief_json = json.dumps(brief0[:200], ensure_ascii=False)
        lines.append(
            '{"action":"admin_v1","do":"seguir_explorando","why":"cubrir hueco del verificador",'
            '"spawn":{"tipo":"explore","brief":' + brief_json + "}}"
        )
    lines.append(
        '{"action":"admin_v1","do":"spawn","why":"…","spawn":{"tipo":"explore","brief":"hueco concreto"}}'
    )
    lines.append('{"action":"admin_v1","do":"editar","why":"…"}  // solo si rebatiste el ataque')
    lines.append('{"action":"admin_v1","do":"cerrar","why":"…"}  // solo si rebatiste el ataque')
    lines.append('{"action":"admin_v1","do":"ask_user","why":"…","reply":"…"}')
    return "\n".join(lines) + "\n"
