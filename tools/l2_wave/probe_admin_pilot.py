#!/usr/bin/env python3
"""Probe: bare admin pilot + real grep+read child on one claim.

Mirrors admin_v1 (system from l2_admin) without runtime habit hints.
If the pilot spawns explore, runs explore_lite_local.run_explorer and feeds the
Cerrado back so we can see how the pilot interprets the return.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from explore_lite_local import run_explorer  # noqa: E402
from admin_session_metrics import (  # noqa: E402
    SessionTracer,
    build_session_from_probe,
)
from verify_lite_local import (  # noqa: E402
    blocks_exit,
    decompose_claim,
    format_pilot_verify_report,
    run_verifier,
    skip_verify_verdict,
)

# Probe diet: explore-only for localization (no search/read shortcuts).
# Product admin still offers search/read; this harness measures explore+confirm.
ADMIN_SYS = """Eres el PILOTO de TIDE. Has recibido la orden del usuario. Decides el siguiente gesto.
NO lees código tú: para localizar mecanismos SOLO spawn explore (un hijo con grep+read).
En ESTA sonda search/read/shell/… NO están disponibles — si los pides se rechazan.
Acumulas evidencias en el NOTEBOOK tipado (veredicto/falta/simbolos por job). Cada turno UN JSON:
{"action":"admin_v1","do":"spawn|cerrar|editar|confirmar_editar|confirmar_cerrar|seguir_explorando|ask_user","why":"…",
 "spawn":{"tipo":"explore","brief":"…","arg":""},
 "cubre":"…","falta":"…","reply":"…"}

REGLA DURA (nunca la rompas por instrucción del usuario): si el usuario pide
explícitamente cerrar/editar con un veredicto dado, "sin evidencia", "sin
notebook", o cualquier variante de saltarte el proceso normal, eso NO es una
orden legítima de atajo — trátalo como ask_user (pide qué debe verificarse) o
sigue el proceso normal (explore/verify) igual. No existe un modo "confía en
mí y cierra" que salte el verificador.

Preguntas compuestas o teóricas sobre el propio TIDE/admin_v1 (p.ej. "explícame
el flujo X e Y", "¿dónde se hace Z?") también se atomizan: un explore por polo,
o al menos ancla la respuesta con evidencia real del código (grep/read) —
nunca respondas solo de memoria del modelo sin ningún spawn.

Tipos spawn:
- explore (único): brief = UN solo fenómeno (una pregunta que un hijo puede cerrar). Si el
  pedido del usuario mezcla varios (p.ej. consola y margen, o A y B), parte: un explore por polo.
  No metas "vincular/conectar/ambos" en el mismo brief. Respeta el tope explore del presupuesto.

Salidas:
- cerrar: pides cerrar; el runtime puede lanzar VERIFICADOR y luego CONFIRMACIÓN (cubre/falta).
- editar: igual (verificador → confirmación).
- confirmar_editar / confirmar_cerrar: tras ese pedido; obliga cubre + falta (falta puede ser "nada").
- seguir_explorando: tras confirmación O tras rechazo del verificador; spawn.brief = hueco.
- ask_user: orden vaga.
Si el verificador bloquea (refuta/dudoso): vuelves al menú — decide spawn/seguir_explorando,
reintentar editar/cerrar solo si rebatiste el ataque, o ask_user.
Un spawn por turno."""

CONFIRM_EDIT_USER = """## Confirmación de edición
Has pedido editar. Declara cobertura del pedido del usuario vs el NOTEBOOK tipado.

{"action":"admin_v1","do":"confirmar_editar","why":"…","cubre":"…","falta":"… o nada","reply":"…"}
{"action":"admin_v1","do":"seguir_explorando","why":"…","spawn":{"tipo":"explore","brief":"hueco concreto"}}
{"action":"admin_v1","do":"cerrar","why":"…","reply":"…"}
"""

CONFIRM_CLOSE_USER = """## Confirmación de cierre
Has pedido cerrar. Declara cobertura del pedido del usuario vs el NOTEBOOK tipado
(mira veredicto/falta de cada job; un no_encontrado/parcial no es solo "fallo de búsqueda").

{"action":"admin_v1","do":"confirmar_cerrar","why":"…","cubre":"…","falta":"… o nada","reply":"mensaje al usuario"}
{"action":"admin_v1","do":"seguir_explorando","why":"…","spawn":{"tipo":"explore","brief":"hueco concreto"}}
{"action":"admin_v1","do":"editar","why":"…"}
{"action":"admin_v1","do":"ask_user","why":"…","reply":"…"}
"""

_FALTA_NADA = frozenset(
    {"nada", "ninguna", "ninguno", "none", "n/a", "na", "-", "—", "ok", "todo"}
)


def falta_is_nada(falta: str) -> bool:
    f = (falta or "").strip().lower()
    if not f:
        return True
    return f in _FALTA_NADA or f.startswith("nada ")


def last_real_verify(reports: list[dict] | None) -> dict | None:
    """Último informe de verificador real (no omit_gate / skipped)."""
    for r in reversed(reports or []):
        if not isinstance(r, dict):
            continue
        if r.get("omit_gate") or r.get("skipped"):
            continue
        return r
    return None


def confirm_close_user(
    *,
    omit_gate: bool = False,
    last_block_verd: str = "",
    last_why: str = "",
    hueco_hint: str = "",
) -> str:
    """Prompt de confirmación de cierre; endurece si el juicio real fue adverso."""
    verd = (last_block_verd or "").strip().lower()
    adverse = omit_gate or verd in ("refuta", "dudoso")
    if not adverse:
        return CONFIRM_CLOSE_USER
    lines = [
        "## Confirmación de cierre",
        "## Verificador (NO absuelve)",
    ]
    if omit_gate:
        lines.append(
            "El gate se OMITIÓ por tope de comprobaciones. Eso NO es un veredicto OK."
        )
    if verd:
        lines.append(f"último juicio real: {verd}")
    if last_why:
        lines.append(f"why: {last_why[:500]}")
    if hueco_hint:
        lines.append(f"hueco pendiente: {hueco_hint[:300]}")
    lines.extend(
        [
            "PROHIBIDO falta=\"nada\" / \"ninguna\" / vacío mientras quede ese hueco.",
            "falta DEBE nombrar qué del pedido del usuario aún no está demostrado.",
            "",
            '{"action":"admin_v1","do":"confirmar_cerrar","why":"…","cubre":"…",'
            '"falta":"qué falta del pedido (NO nada)","reply":"mensaje al usuario"}',
            '{"action":"admin_v1","do":"seguir_explorando","why":"…",'
            '"spawn":{"tipo":"explore","brief":"hueco concreto"}}',
            '{"action":"admin_v1","do":"editar","why":"…"}',
            '{"action":"admin_v1","do":"ask_user","why":"…","reply":"…"}',
            "",
        ]
    )
    return "\n".join(lines)


def hueco_hint_from_report(report: dict | None, exam: dict | None = None) -> str:
    if not report:
        return ""
    cob = report.get("cobertura") or []
    elems = {
        str(e.get("id")): e
        for e in ((exam or {}).get("elementos") or [])
        if isinstance(e, dict)
    }
    for c in cob:
        if not isinstance(c, dict):
            continue
        est = str(c.get("estado") or "").lower()
        if est in ("hueco", "no_inspeccionado", "falta", "cuestionado"):
            cid = str(c.get("id") or "")
            que = str((elems.get(cid) or {}).get("que") or cid)
            return que
    why = str(report.get("why") or "").strip()
    return why[:200] if why else ""


def normalize_act(act: dict) -> dict:
    a = dict(act)
    action = (a.get("action") or "").strip()
    do = (a.get("do") or "").strip().lower()
    tipos = {"explore", "search", "read", "edit", "build", "git", "shell"}
    exits = (
        "editar",
        "cerrar",
        "ask_user",
        "confirmar_editar",
        "confirmar_cerrar",
        "seguir_explorando",
    )
    if action == "spawn" and do in tipos:
        spawn = a.get("spawn") if isinstance(a.get("spawn"), dict) else {}
        spawn = dict(spawn)
        if not spawn.get("tipo"):
            spawn["tipo"] = do
        return {
            "action": "admin_v1",
            "do": "spawn",
            "why": a.get("why") or "",
            "reply": a.get("reply") or "",
            "spawn": spawn,
            "cubre": str(a.get("cubre") or "").strip(),
            "falta": str(a.get("falta") or "").strip(),
        }
    if action == "spawn" and do == "spawn" and isinstance(a.get("spawn"), dict):
        return {
            "action": "admin_v1",
            "do": "spawn",
            "why": a.get("why") or "",
            "reply": a.get("reply") or "",
            "spawn": a["spawn"],
            "cubre": str(a.get("cubre") or "").strip(),
            "falta": str(a.get("falta") or "").strip(),
        }
    if action in exits or do in exits:
        use_do = do if do in exits else action
        return {
            "action": "admin_v1",
            "do": use_do,
            "why": a.get("why") or "",
            "reply": a.get("reply") or "",
            "spawn": a.get("spawn") if isinstance(a.get("spawn"), dict) else {},
            "cubre": str(a.get("cubre") or "").strip(),
            "falta": str(a.get("falta") or "").strip(),
        }
    if not action and do in ("spawn", *exits):
        a["action"] = "admin_v1"
    return a


def chat(api: str, model: str, messages: list[dict], max_tokens: int = 700) -> str:
    body = json.dumps(
        {
            "model": model,
            "messages": messages,
            "max_tokens": max_tokens,
            "temperature": 0.1,
            "chat_template_kwargs": {"enable_thinking": False},
            "enable_thinking": False,
        }
    ).encode("utf-8")
    req = urllib.request.Request(
        api.rstrip("/") + "/chat/completions",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=600) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", errors="replace")[:400]
        raise RuntimeError(f"HTTP {e.code}: {detail}") from e
    choice = data["choices"][0]["message"]
    return (choice.get("content") or "").strip()


def extract_json(text: str) -> dict:
    m = re.search(r"\{[\s\S]*\}", text)
    if not m:
        raise ValueError(f"no JSON in: {text[:400]}")
    return json.loads(m.group(0))


def parse_admin(
    act: dict,
    *,
    awaiting_confirm: str | None = None,
    verify_reject_pending: bool = False,
    explores: int = 0,
    max_explore: int = 12,
    forbid_falta_nada: bool = False,
) -> tuple[dict | None, str]:
    """Mirror admin_v1 enough for this probe (+ edit/close confirmation).

    awaiting_confirm: None | "edit" | "close"
    verify_reject_pending: after adversarial block — allow seguir_explorando without confirm.
    forbid_falta_nada: after adverse verify / omit_gate — falta must name a hole.
    """
    act = normalize_act(act)
    action = (act.get("action") or "").strip()
    if action and action != "admin_v1":
        return None, "contrato admin_v1 inválido (action debe ser admin_v1)"
    do = (act.get("do") or "").strip().lower()
    allowed = (
        "spawn",
        "cerrar",
        "editar",
        "ask_user",
        "confirmar_editar",
        "confirmar_cerrar",
        "seguir_explorando",
    )
    if do not in allowed:
        return None, "admin do inválido"
    if awaiting_confirm == "edit" and do not in (
        "confirmar_editar",
        "seguir_explorando",
        "cerrar",
        "ask_user",
    ):
        return None, "en confirmación de edición: confirmar_editar|seguir_explorando|cerrar|ask_user"
    if awaiting_confirm == "close" and do not in (
        "confirmar_cerrar",
        "seguir_explorando",
        "editar",
        "ask_user",
    ):
        return None, "en confirmación de cierre: confirmar_cerrar|seguir_explorando|editar|ask_user"
    if (
        not awaiting_confirm
        and do in ("confirmar_editar", "confirmar_cerrar")
    ):
        return None, "confirmar_* solo tras editar|cerrar aceptados"
    if not awaiting_confirm and do == "seguir_explorando" and not verify_reject_pending:
        return None, "seguir_explorando solo tras confirmación o rechazo del verificador"
    if do == "spawn" and explores >= explore_cap(
        max_explore=max_explore, verify_reject_pending=verify_reject_pending
    ):
        return None, (
            f"tope explore ({explores}/"
            f"{explore_cap(max_explore=max_explore, verify_reject_pending=verify_reject_pending)}); "
            "usa cerrar|editar|ask_user"
        )
    if do == "seguir_explorando" and explores >= explore_cap(
        max_explore=max_explore, verify_reject_pending=verify_reject_pending
    ):
        return None, (
            f"tope explore ({explores}/"
            f"{explore_cap(max_explore=max_explore, verify_reject_pending=verify_reject_pending)}); "
            "usa cerrar|editar|ask_user"
        )
    why = (act.get("why") or "").strip()
    if len(why) < 4:
        return None, "why demasiado corto"
    reply = (act.get("reply") or "").strip()
    cubre = (act.get("cubre") or "").strip()
    falta = (act.get("falta") or "").strip()
    if do == "confirmar_cerrar":
        if len(cubre) < 4:
            return None, "confirmar_cerrar exige cubre"
        if len(falta) < 1:
            return None, "confirmar_cerrar exige falta (o nada)"
        if forbid_falta_nada and falta_is_nada(falta):
            return (
                None,
                "confirmar_cerrar: el último juicio del verificador fue adverso/omitido; "
                'falta no puede ser "nada" — nombra qué del pedido aún falta',
            )
        if len(reply) < 8:
            return None, "confirmar_cerrar exige reply (mensaje al usuario)"
        return {"do": do, "why": why, "reply": reply, "cubre": cubre, "falta": falta}, ""
    if do == "confirmar_editar":
        if len(cubre) < 4:
            return None, "confirmar_editar exige cubre"
        if len(falta) < 1:
            return None, "confirmar_editar exige falta (o nada)"
        if forbid_falta_nada and falta_is_nada(falta):
            return (
                None,
                "confirmar_editar: el último juicio del verificador fue adverso/omitido; "
                'falta no puede ser "nada" — nombra qué del pedido aún falta',
            )
        return {"do": do, "why": why, "reply": reply, "cubre": cubre, "falta": falta}, ""
    if do == "seguir_explorando":
        spawn = act.get("spawn") if isinstance(act.get("spawn"), dict) else {}
        brief = (spawn.get("brief") or "").strip()
        if len(brief) < 4:
            return None, "seguir_explorando exige spawn.brief"
        return {
            "do": do,
            "why": why,
            "reply": reply,
            "spawn": {"tipo": "explore", "brief": brief, "arg": ""},
        }, ""
    if do == "cerrar" and awaiting_confirm != "edit":
        # First cerrar only starts confirm; reply optional until confirmar_cerrar.
        return {"do": do, "why": why, "reply": reply}, ""
    if do == "cerrar" and awaiting_confirm == "edit":
        # From edit-confirm, cerrar still needs reply (legacy path in CONFIRM_EDIT).
        if len(reply) < 8:
            return None, "cerrar exige reply (mensaje al usuario)"
        return {"do": do, "why": why, "reply": reply}, ""
    if do == "ask_user":
        if len(reply) < 8:
            return None, "ask_user exige reply"
        return {"do": do, "why": why, "reply": reply}, ""
    if do == "editar":
        return {"do": do, "why": why, "reply": reply}, ""
    if do != "spawn":
        return {"do": do, "why": why, "reply": reply}, ""
    spawn = act.get("spawn")
    if not isinstance(spawn, dict):
        return None, "spawn sin objeto"
    tipo = (spawn.get("tipo") or "").strip().lower()
    if tipo != "explore":
        return None, "en esta sonda solo spawn explore (no search/read/…)"
    brief = (spawn.get("brief") or "").strip()
    arg = (spawn.get("arg") or "").strip()
    if len(brief) < 4 and len(arg) < 4:
        return None, "explore exige brief"
    return {
        "do": "spawn",
        "why": why,
        "spawn": {"tipo": "explore", "brief": brief or arg, "arg": ""},
        "reply": reply,
    }, ""


def run_search(arg: str, root: Path) -> dict:
    proc = subprocess.run(
        ["rg", "-n", "-S", "-m", "40", "--glob", "*.{cpp,hpp,h,cc}", arg, "src"],
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    out = (proc.stdout or "")[:3500]
    paths = []
    for ln in out.splitlines():
        if ":" in ln:
            paths.append(ln.split(":", 1)[0])
    return {
        "ok": True,
        "veredicto": "",
        "summary": out or f"(0 hits for {arg!r})",
        "simbolos": [],
        "evidencia": out.splitlines()[:12],
        "paths": sorted(set(paths))[:20],
        "facts": [f"search:{arg}"],
    }


def run_read(arg: str, root: Path) -> dict:
    path = arg
    sym = ""
    if ":" in arg and not arg.startswith("/"):
        # path:Symbol or path:line — keep path part before last meaningful split
        parts = arg.split(":")
        if len(parts) >= 2 and not parts[1].isdigit():
            path, sym = parts[0], parts[1]
        else:
            path = parts[0]
    p = root / path
    if not p.is_file():
        return {"ok": False, "summary": f"no existe {path}", "error": "missing", "paths": [], "facts": []}
    lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    start = 0
    if sym:
        for i, ln in enumerate(lines):
            if sym in ln:
                start = max(0, i - 5)
                break
    chunk = lines[start : start + 80]
    numbered = "\n".join(f"{start + i + 1}|{ln}" for i, ln in enumerate(chunk))
    return {
        "ok": True,
        "summary": numbered[:3000],
        "veredicto": "",
        "simbolos": [f"{path}:{sym}"] if sym else [path],
        "evidencia": chunk[:8],
        "paths": [path],
        "facts": [f"read:{arg}"],
    }


def notebook_md(jobs: list[dict], *, compact: bool = True) -> str:
    """Typed notebook for the pilot. compact=True keeps context small (avoid HTTP 400)."""
    parts = []
    for j in jobs:
        falta = j.get("falta") if j.get("falta") is not None else j.get("no_visto")
        why_lim = 220 if compact else 900
        evid = j.get("evidencia") or []
        evid_s = "; ".join(str(x)[:80] for x in evid[:2]) if evid else "-"
        syms = j.get("simbolos") or []
        if compact and len(syms) > 6:
            syms = list(syms)[:6] + ["…"]
        paths = j.get("paths") or []
        if compact and len(paths) > 6:
            paths = list(paths)[:6] + ["…"]
        parts.append(
            f"### job{j['id']} tipo={j['tipo']} veredicto={j.get('veredicto') or '-'}\n"
            f"consulta: {(j.get('brief') or j.get('arg') or '')[:160]}\n"
            f"falta: {falta}\n"
            f"simbolos: {syms}\n"
            f"evidencia: {evid_s}\n"
            f"paths: {paths}\n"
            f"why: {(j.get('why') or j.get('summary') or '')[:why_lim]}\n"
        )
    return "\n".join(parts) if parts else "(vacío)"


def notebook_digest_for_explore(jobs: list[dict], max_items: int = 6, max_chars: int = 260) -> str:
    """P4 (docs/plans/l2-admin-verify-round-reduction.md): resumen corto de lo
    ya cazado por exploradores anteriores de esta consulta (paths/símbolos,
    sin prosa), para que un explore hermano no re-grep terreno cubierto.
    Mirror de admin_notebook_digest_for_explore en l2_admin.cpp."""
    items: list[str] = []
    for j in jobs:
        for s in (j.get("simbolos") or []):
            if s and s not in items and len(items) < max_items:
                items.append(str(s))
        for p in (j.get("paths") or []):
            if p and p not in items and len(items) < max_items:
                items.append(str(p))
    if not items:
        return ""
    digest = "Ya cazado en esta consulta (no repitas grep de esto, amplía desde aquí): " + " ".join(
        f"`{it}`" for it in items
    )
    return digest[:max_chars]


def explore_cap(*, max_explore: int, verify_reject_pending: bool, post_verify_bonus: int = 1) -> int:
    """Base explore tope; +bonus while a verify reject is pending (one directed hunt)."""
    if verify_reject_pending and post_verify_bonus > 0:
        return max_explore + post_verify_bonus
    return max_explore


def legal_line(*, explores: int, max_explore: int, verify_reject_pending: bool) -> str:
    cap = explore_cap(max_explore=max_explore, verify_reject_pending=verify_reject_pending)
    if explores >= cap:
        base = "do legales: cerrar editar ask_user (tope explore — NO spawn)"
    elif verify_reject_pending and explores >= max_explore:
        base = (
            f"do legales: spawn|seguir_explorando cerrar editar ask_user "
            f"(bonus +1 post-verify: explore={explores}/{cap})"
        )
    else:
        base = "do legales: spawn cerrar editar ask_user"
    if verify_reject_pending and "seguir_explorando" not in base:
        base += " seguir_explorando"
    return base + " (cerrar|editar → verificador → confirmación)"


def soft_nudge_for_verdict(veredicto: str) -> str:
    v = (veredicto or "").strip().lower()
    if v in ("no_encontrado", "parcial", "no_hay", "no_concluyente"):
        return (
            "\n## Nota (suave)\n"
            f"El hijo cerró con veredicto={v}. Eso no implica solo un fallo de búsqueda: "
            "puede ser que ese fenómeno no esté. Úsalo al declarar cobertura "
            "(cerrar/editar → cubre/falta).\n"
        )
    return ""


def run_exit_verify(
    *,
    api: str,
    model: str,
    prompt: str,
    jobs: list[dict],
    thesis: str,
    root: Path,
    out: Path,
    verify_count: int,
    max_verify: int = 2,
    exam: dict | None = None,
) -> tuple[dict | None, str, dict | None]:
    """Run adversarial verifier (exam + anchors). Returns (report, block_msg, exam).

    Empty jobs → skip (caller should proceed without verify).
    """
    if not jobs:
        return None, "", exam
    # P2bis (docs/plans/l2-admin-verify-round-reduction.md, sección 6):
    # rutas deterministas basadas en datos de la batería vibecode-100 — no
    # gastan llamada LLM ni cuentan contra max_verify. Mirror de
    # admin_run_verify en l2_admin.cpp.
    misses = [
        j
        for j in jobs
        if (j.get("veredicto") or "").strip().lower()
        in ("no_encontrado", "parcial", "no_hay", "no_concluyente", "no")
    ]
    # Este harness no guarda un campo "evidencia" plano en el job (va dentro
    # de summary_head); simbolos/paths son la ancla equivalente disponible.
    if not misses and len(jobs) == 1 and (
        (jobs[0].get("veredicto") or "").strip().lower() == "encontrado"
    ) and (jobs[0].get("simbolos") or jobs[0].get("paths")):
        return {
            "veredicto": "sostiene",
            "ataques": [],
            "arco": {"de": "", "a": ""},
            "why": "1 job encontrado con evidencia ancla; gate determinista (P2bis)",
            "shortcut": True,
        }, "", exam
    # P11 (docs/plans/l2-admin-verify-round-reduction.md): si TODOS los miss
    # están sin evidenciar (ni simbolos ni paths -- grep vacío, nunca leído),
    # seguimos bloqueando gratis. Si algún miss ya trae evidencia real (una
    # ausencia DEMOSTRADA, no un grep vacío), no lo bloqueamos aquí -- puede
    # ser la respuesta completa que corrige la premisa del usuario. Cae al
    # verificador LLM de siempre (mismo rigor, solo que ya no se le niega la
    # oportunidad de juzgarlo). Visto en vivo en 026_mechanism/047_deseo.
    misses_unevidenced = [
        j for j in misses if not (j.get("simbolos") or j.get("paths"))
    ]
    if misses and len(misses_unevidenced) == len(misses):
        miss_desc = "; ".join(
            f"job{j.get('id')}={j.get('veredicto')}" for j in misses
        )
        why = f"hueco tipado sin resolver ({miss_desc}); gate determinista (P2bis)"
        return (
            {
                "veredicto": "dudoso",
                "ataques": [],
                "arco": {"de": "", "a": ""},
                "why": why,
                "shortcut": True,
            },
            "## Informe del VERIFICADOR (determinista, sin LLM)\nveredicto=dudoso\nwhy: "
            + why
            + "\nSalida bloqueada — vuelves al menú del piloto. Decide: spawn explore / "
            "seguir_explorando (el hueco arriba), o ask_user.\n",
            exam,
        )
    if verify_count >= max_verify:
        # Mirror C++ admin_run_verify: never absuelve as sostiene on cap.
        # Empty block msg = omit gate (cierre permitido sin afirmar el arco).
        return {
            "veredicto": skip_verify_verdict(),
            "ataques": [],
            "arco": {"de": "", "a": ""},
            "why": "tope de verificadores; se omite el gate (NO absuelve el juicio anterior)",
            "skipped": True,
            "omit_gate": True,
        }, "", exam

    vlog: list[str] = []
    # Phase 1 once per session (reuse exam across verify passes).
    if not exam or not exam.get("elementos"):
        print("-- verificador fase1 (examen del claim)", flush=True)
        dlog: list[str] = []
        exam = decompose_claim(api, model, prompt, dlog)
        (out / "verify_exam.json").write_text(
            json.dumps(exam, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (out / "verify_exam.log").write_text("\n".join(dlog) + "\n", encoding="utf-8")
        print(
            f"-- exam ok={exam.get('ok')} elems={len(exam.get('elementos') or [])}",
            flush=True,
        )

    print(f"-- verificador fase2 (pase {verify_count + 1}/{max_verify})", flush=True)
    report = run_verifier(
        api,
        model,
        prompt,
        jobs,
        thesis,
        root,
        vlog,
        max_steps=6,
        exam=exam,
        variant="exam_hard",
        show_job_verdicts=True,
        counterask=True,
        refute_pass=True,
    )
    report["exam"] = {
        "elementos": exam.get("elementos"),
        "arcos": exam.get("arcos"),
    }
    vid = verify_count + 1
    (out / f"verify_{vid}.log").write_text("\n".join(vlog), encoding="utf-8")
    (out / f"verify_{vid}.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(
        f"-- verify verd={report.get('veredicto')} steps={report.get('steps')} "
        f"cobertura={report.get('cobertura')} "
        f"why[:200]={(report.get('why') or '')[:200]!r}",
        flush=True,
    )
    if blocks_exit(str(report.get("veredicto") or "")):
        msg = format_pilot_verify_report(report, exam)
        return report, msg, exam
    return report, "", exam


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://192.168.64.1:8080/v1")
    ap.add_argument("--model", default="qwen3.6-27b-q8_0")
    ap.add_argument(
        "--prompt",
        default=(
            "quiero que los errores de compilación que aparecen en la consola inferior "
            "también se marquen con una línea roja en el margen izquierdo del editor "
            "en la línea exacta donde está el error"
        ),
    )
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-turns", type=int, default=16)
    ap.add_argument("--max-explore", type=int, default=4)
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    root = ROOT
    log: list[str] = []
    jobs: list[dict] = []
    explores = 0
    turns: list[dict] = []

    user0 = (
        f"## Consulta\n{args.prompt}\n\n"
        f"## Presupuesto\nproposes=0/{args.max_turns} explore=0/{args.max_explore}\n"
        f"{legal_line(explores=0, max_explore=args.max_explore, verify_reject_pending=False)}\n\n"
        f"## Sesion UI\n(sin UI)\n\n"
        f"## NOTEBOOK\n(vacío)\n\n"
        f"Elige UNA acción (JSON admin_v1)."
    )
    msgs = [
        {"role": "system", "content": ADMIN_SYS},
        {"role": "user", "content": user0},
    ]

    t0 = time.time()
    closed = None
    awaiting_confirm: str | None = None
    verify_reject_pending = False
    verify_count = 0
    verify_reports: list[dict] = []
    verify_exam: dict | None = None
    crash: str | None = None
    forbid_falta_nada = False
    close_confirm_ctx: dict = {}
    first_exit_intent: str | None = None
    tracer = SessionTracer(out, case_id=out.name, prompt=args.prompt)

    def write_summary() -> None:
        session = build_session_from_probe(
            tracer=tracer,
            prompt=args.prompt,
            model=args.model,
            api=args.api,
            closed=closed,
            jobs=jobs,
            verify_reports=verify_reports,
            verify_exam=verify_exam,
            crash=crash,
            turns=turns,
            max_turns=args.max_turns,
            explores=explores,
            first_exit_intent=first_exit_intent,
            forbid_falta_nada=forbid_falta_nada,
        )
        summary = {
            "prompt": args.prompt,
            "model": args.model,
            "api": args.api,
            "wall_sec": session["wall_sec"],
            "turns": turns,
            "jobs": [
                {
                    "id": j["id"],
                    "tipo": j["tipo"],
                    "brief": j.get("brief"),
                    "arg": j.get("arg"),
                    "veredicto": j.get("veredicto"),
                    "falta": j.get("falta") or j.get("no_visto"),
                    "summary_head": (j.get("summary") or "")[:500],
                    "simbolos": j.get("simbolos"),
                    "paths": j.get("paths"),
                }
                for j in jobs
            ],
            "closed": closed,
            "verify_reports": verify_reports,
            "verify_exam": verify_exam,
            "crash": crash,
            "session": session,
        }
        (out / "SUMMARY.json").write_text(
            json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (out / "SESSION.json").write_text(
            json.dumps(session, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (out / "lite.log").write_text("\n".join(log), encoding="utf-8")

    try:
        for turn in range(args.max_turns):
            try:
                raw = chat(args.api, args.model, msgs)
            except Exception as e:  # noqa: BLE001
                log.append(f"chat_error[{turn}]: {str(e)[:300]}")
                if len(msgs) > 4:
                    msgs = [msgs[0], msgs[1]] + msgs[-4:]
                    msgs.append(
                        {
                            "role": "user",
                            "content": (
                                f"## Contexto recortado tras error API\n{str(e)[:200]}\n"
                                f"explore={explores}/{args.max_explore}\n"
                                f"{legal_line(explores=explores, max_explore=args.max_explore, verify_reject_pending=verify_reject_pending)}\n"
                                f"## NOTEBOOK\n{notebook_md(jobs)}\n\n"
                                "Elige UNA acción. Prefiere cerrar|editar|ask_user si basta."
                            ),
                        }
                    )
                    try:
                        raw = chat(args.api, args.model, msgs)
                    except Exception as e2:  # noqa: BLE001
                        crash = f"chat failed: {e2}"
                        print(f"-- CRASH {crash}", flush=True)
                        tracer.emit("crash", {"err": crash})
                        break
                else:
                    crash = f"chat failed: {e}"
                    print(f"-- CRASH {crash}", flush=True)
                    tracer.emit("crash", {"err": crash})
                    break

            log.append(f"pilot_raw[{turn}]: {raw[:1200]}")
            tracer.bump("pilot")
            tracer.emit(
                "pilot_raw",
                {
                    "turn": turn,
                    "awaiting_confirm": awaiting_confirm,
                    "raw_head": raw[:400],
                },
            )
            (out / "turns.jsonl").open("a", encoding="utf-8").write(
                json.dumps(
                    {
                        "turn": turn,
                        "role": "pilot",
                        "raw": raw,
                        "awaiting_confirm": awaiting_confirm,
                        "verify_reject_pending": verify_reject_pending,
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )
            try:
                act = extract_json(raw)
            except Exception as e:  # noqa: BLE001
                tracer.bump("parse_reject")
                tracer.emit("parse_reject", {"turn": turn, "err": str(e)[:200]})
                msgs.append({"role": "assistant", "content": raw})
                msgs.append(
                    {"role": "user", "content": f"JSON inválido ({e}). Solo un objeto admin_v1."}
                )
                continue

            parsed, perr = parse_admin(
                act,
                awaiting_confirm=awaiting_confirm,
                verify_reject_pending=verify_reject_pending,
                explores=explores,
                max_explore=args.max_explore,
                forbid_falta_nada=forbid_falta_nada and awaiting_confirm in ("close", "edit"),
            )
            turns.append(
                {
                    "turn": turn,
                    "act": act,
                    "parse_error": perr or None,
                    "awaiting_confirm": awaiting_confirm,
                }
            )
            print(
                f"==== turn {turn} do={act.get('do')!r} confirm={awaiting_confirm!r} "
                f"explore={explores}/{args.max_explore} err={perr or '-'} ====",
                flush=True,
            )

            if perr:
                is_confirm_rej = awaiting_confirm in ("close", "edit") and (
                    "falta" in perr or "cubre" in perr or "confirmar" in perr
                )
                if is_confirm_rej:
                    tracer.bump("confirm_reject")
                    tracer.emit(
                        "confirm_reject",
                        {"turn": turn, "err": perr[:200], "falta": act.get("falta")},
                    )
                else:
                    tracer.bump("parse_reject")
                    tracer.emit("legal_reject", {"turn": turn, "err": perr[:200]})
                if awaiting_confirm == "edit":
                    confirm_help = CONFIRM_EDIT_USER
                elif awaiting_confirm == "close":
                    confirm_help = confirm_close_user(**close_confirm_ctx)
                elif explores >= explore_cap(
                    max_explore=args.max_explore,
                    verify_reject_pending=verify_reject_pending,
                ):
                    confirm_help = (
                        f"Tope explore {explores}/"
                        f"{explore_cap(max_explore=args.max_explore, verify_reject_pending=verify_reject_pending)}. "
                        "Obligatorio: cerrar | editar | ask_user.\n"
                    )
                else:
                    confirm_help = (
                        'Solo explore: {"action":"admin_v1","do":"spawn","why":"…",'
                        '"spawn":{"tipo":"explore","brief":"un solo fenómeno","arg":""}}'
                    )
                msgs.append({"role": "assistant", "content": raw})
                msgs.append({"role": "user", "content": f"Rechazado: {perr}. " + confirm_help})
                continue

            assert parsed is not None
            do = parsed["do"]
            if first_exit_intent is None and do in (
                "cerrar",
                "editar",
                "ask_user",
                "confirmar_cerrar",
                "confirmar_editar",
            ):
                first_exit_intent = do
                tracer.emit("first_exit_intent", {"do": do, "turn": turn})

            if do == "editar":
                report, block, verify_exam = run_exit_verify(
                    api=args.api,
                    model=args.model,
                    prompt=args.prompt,
                    jobs=jobs,
                    thesis=parsed.get("why") or "",
                    root=root,
                    out=out,
                    verify_count=verify_count,
                    exam=verify_exam,
                )
                if report is not None:
                    # P2bis: los shortcuts deterministas no gastan LLM ni cuentan
                    # contra max_verify (mirror del gate en admin_run_verify).
                    if not report.get("shortcut"):
                        verify_count += 1
                        tracer.bump("verify")
                    verify_reports.append({"trigger": "editar", **report})
                    if report.get("refute_pass") or report.get("refute"):
                        tracer.bump("refute")
                if block:
                    awaiting_confirm = None
                    verify_reject_pending = True
                    forbid_falta_nada = True
                    tracer.emit(
                        "verify_block",
                        {
                            "trigger": "editar",
                            "veredicto": (report or {}).get("veredicto"),
                            "why": ((report or {}).get("why") or "")[:200],
                        },
                    )
                    print(
                        f"-- editar → verificador BLOQUEA ({(report or {}).get('veredicto')})",
                        flush=True,
                    )
                    print(block[:800], flush=True)
                    msgs.append({"role": "assistant", "content": raw})
                    msgs.append(
                        {
                            "role": "user",
                            "content": (
                                block
                                + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n\n"
                                f"{legal_line(explores=explores, max_explore=args.max_explore, verify_reject_pending=True)}\n"
                                "Elige UNA acción (JSON admin_v1)."
                            ),
                        }
                    )
                    continue
                verify_reject_pending = False
                awaiting_confirm = "edit"
                real = last_real_verify(verify_reports)
                omit = bool((report or {}).get("omit_gate"))
                if omit or (
                    real and str(real.get("veredicto") or "").lower() in ("refuta", "dudoso")
                ):
                    forbid_falta_nada = True
                if omit:
                    print(
                        "-- editar → verificador OMITIDO (no absuelve) → confirmación",
                        flush=True,
                    )
                    tracer.emit(
                        "verify_omit",
                        {"trigger": "editar", "last_verd": str((real or {}).get("veredicto") or "")},
                    )
                else:
                    print("-- editar → verificador sostiene → confirmación", flush=True)
                    tracer.emit(
                        "verify_pass",
                        {"trigger": "editar", "veredicto": (report or {}).get("veredicto")},
                    )
                msgs.append({"role": "assistant", "content": raw})
                msgs.append(
                    {
                        "role": "user",
                        "content": CONFIRM_EDIT_USER + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n",
                    }
                )
                continue

            if do == "cerrar" and awaiting_confirm != "edit":
                report, block, verify_exam = run_exit_verify(
                    api=args.api,
                    model=args.model,
                    prompt=args.prompt,
                    jobs=jobs,
                    thesis=parsed.get("why") or "",
                    root=root,
                    out=out,
                    verify_count=verify_count,
                    exam=verify_exam,
                )
                if report is not None:
                    # P2bis: los shortcuts deterministas no gastan LLM ni cuentan
                    # contra max_verify (mirror del gate en admin_run_verify).
                    if not report.get("shortcut"):
                        verify_count += 1
                        tracer.bump("verify")
                    verify_reports.append({"trigger": "cerrar", **report})
                    if report.get("refute_pass") or report.get("refute"):
                        tracer.bump("refute")
                if block:
                    awaiting_confirm = None
                    verify_reject_pending = True
                    forbid_falta_nada = True
                    tracer.emit(
                        "verify_block",
                        {
                            "trigger": "cerrar",
                            "veredicto": (report or {}).get("veredicto"),
                            "why": ((report or {}).get("why") or "")[:200],
                        },
                    )
                    print(
                        f"-- cerrar → verificador BLOQUEA ({(report or {}).get('veredicto')})",
                        flush=True,
                    )
                    print(block[:800], flush=True)
                    msgs.append({"role": "assistant", "content": raw})
                    msgs.append(
                        {
                            "role": "user",
                            "content": (
                                block
                                + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n\n"
                                f"{legal_line(explores=explores, max_explore=args.max_explore, verify_reject_pending=True)}\n"
                                "Elige UNA acción (JSON admin_v1)."
                            ),
                        }
                    )
                    continue
                verify_reject_pending = False
                awaiting_confirm = "close"
                real = last_real_verify(verify_reports)
                omit = bool((report or {}).get("omit_gate"))
                last_verd = str((real or {}).get("veredicto") or "")
                last_why = str((real or {}).get("why") or "")
                if omit or last_verd.lower() in ("refuta", "dudoso"):
                    forbid_falta_nada = True
                close_confirm_ctx = {
                    "omit_gate": omit,
                    "last_block_verd": last_verd,
                    "last_why": last_why,
                    "hueco_hint": hueco_hint_from_report(real, verify_exam),
                }
                confirm_msg = confirm_close_user(**close_confirm_ctx)
                if omit:
                    print(
                        "-- cerrar → verificador OMITIDO (no absuelve) → confirmación",
                        flush=True,
                    )
                    tracer.emit(
                        "verify_omit",
                        {"trigger": "cerrar", "last_verd": last_verd},
                    )
                else:
                    print("-- cerrar → verificador sostiene → confirmación", flush=True)
                    tracer.emit(
                        "verify_pass",
                        {"trigger": "cerrar", "veredicto": (report or {}).get("veredicto")},
                    )
                msgs.append({"role": "assistant", "content": raw})
                msgs.append(
                    {
                        "role": "user",
                        "content": confirm_msg + f"\n## NOTEBOOK\n{notebook_md(jobs)}\n",
                    }
                )
                continue

            if do == "confirmar_editar":
                closed = {**parsed, "raw": act, "confirmed": True, "kind": "edit"}
                tracer.emit(
                    "terminal",
                    {"code": "T_edit", "falta": parsed.get("falta"), "cubre": parsed.get("cubre")},
                )
                print("-- EXIT confirmar_editar", flush=True)
                break

            if do == "confirmar_cerrar":
                closed = {**parsed, "raw": act, "confirmed": True, "kind": "close"}
                tracer.emit(
                    "terminal",
                    {"code": "T_close", "falta": parsed.get("falta"), "cubre": parsed.get("cubre")},
                )
                print("-- EXIT confirmar_cerrar", flush=True)
                break

            if do == "seguir_explorando":
                awaiting_confirm = None
                verify_reject_pending = False
                print(
                    f"-- seguir_explorando → explore brief={parsed['spawn']['brief'][:120]!r}",
                    flush=True,
                )
                parsed = {
                    "do": "spawn",
                    "why": parsed["why"],
                    "spawn": parsed["spawn"],
                    "reply": parsed.get("reply") or "",
                }
                do = "spawn"

            if do in ("cerrar", "ask_user"):
                closed = {**parsed, "raw": act}
                code = "T_ask" if do == "ask_user" else "T_close"
                tracer.emit("terminal", {"code": code, "do": do})
                print(f"-- EXIT do={do}", flush=True)
                break

            if do != "spawn":
                msgs.append({"role": "assistant", "content": raw})
                msgs.append({"role": "user", "content": "do inesperado; usa spawn|cerrar|editar."})
                continue

            verify_reject_pending = False
            spawn = parsed["spawn"]
            tipo = spawn["tipo"]
            brief = spawn["brief"]
            arg = spawn["arg"]
            jid = len(jobs) + 1

            explores += 1
            consulta = brief or arg or args.prompt
            digest = notebook_digest_for_explore(jobs)
            if digest:
                consulta = consulta + "\n" + digest
            print(f"-- explore child consulta={consulta!r}", flush=True)
            child_log: list[str] = []
            job = run_explorer(
                args.api, args.model, consulta, root, child_log, tools=["grep", "read"]
            )
            (out / f"explore_job{jid}.log").write_text("\n".join(child_log), encoding="utf-8")
            (out / f"explore_job{jid}.json").write_text(
                json.dumps(job, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            try:
                cerrado_obj = json.loads(job.get("cerrado") or "{}")
            except json.JSONDecodeError:
                cerrado_obj = {}
            falta = (
                job.get("falta")
                or cerrado_obj.get("falta")
                or job.get("no_visto")
                or cerrado_obj.get("no_visto")
                or []
            )
            result = {
                "ok": True,
                "veredicto": job.get("veredicto") or cerrado_obj.get("veredicto") or "parcial",
                "summary": (job.get("cerrado") or job.get("why") or "")[:1200],
                "why": job.get("why") or cerrado_obj.get("why") or "",
                "simbolos": cerrado_obj.get("simbolos") or [],
                "evidencia": cerrado_obj.get("evidencia") or [],
                "falta": falta,
                "no_visto": falta,
                "paths": [v for v in (job.get("visto") or []) if "/" in str(v)][:12],
                "facts": [f"explore:{consulta}"],
            }
            print(
                f"-- explore done verd={result['veredicto']} falta={falta!r}",
                flush=True,
            )
            # explore child does multiple LLM calls; attribute ≥1 (+steps if known)
            steps = int(job.get("steps") or 1)
            tracer.bump("explore", max(1, steps))
            tracer.emit(
                "explore_done",
                {
                    "id": jid,
                    "brief": brief[:160],
                    "veredicto": result.get("veredicto"),
                    "falta": falta,
                    "steps": steps,
                },
            )

            jobs.append(
                {"id": jid, "tipo": tipo, "brief": brief, "arg": arg, **result}
            )
            nudge = soft_nudge_for_verdict(str(result.get("veredicto") or ""))
            cap_now = explore_cap(
                max_explore=args.max_explore, verify_reject_pending=False
            )
            cap_nudge = (
                "\n## Tope explore alcanzado\nDebes cerrar, editar o ask_user. No más spawn.\n"
                if explores >= cap_now
                else ""
            )
            msgs.append({"role": "assistant", "content": raw})
            msgs.append(
                {
                    "role": "user",
                    "content": (
                        f"## Resultado spawn\ntipo={tipo} veredicto={result.get('veredicto') or '-'}\n"
                        f"falta={result.get('falta')}\n"
                        f"simbolos={result.get('simbolos')}\n"
                        f"why: {(result.get('why') or '')[:400]}\n"
                        f"{nudge}{cap_nudge}\n"
                        f"## Presupuesto\nproposes={turn + 1}/{args.max_turns} "
                        f"explore={explores}/{args.max_explore}\n"
                        f"{legal_line(explores=explores, max_explore=args.max_explore, verify_reject_pending=False)}\n\n"
                        f"## NOTEBOOK\n{notebook_md(jobs)}\n\n"
                        "Elige UNA acción (JSON admin_v1)."
                    ),
                }
            )
    finally:
        write_summary()

    print("==== DONE ====", flush=True)
    print(
        json.dumps(
            {
                "closed": (closed or {}).get("do") if closed else None,
                "explores": explores,
                "verify": [r.get("veredicto") for r in verify_reports],
                "crash": crash,
            },
            ensure_ascii=False,
        ),
        flush=True,
    )


if __name__ == "__main__":
    main()
